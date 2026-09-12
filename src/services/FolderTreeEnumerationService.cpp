#include "services/FolderTreeEnumerationService.h"

#include <shlobj.h>
#include <winioctl.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <future>
#include <system_error>

#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/StringConvert.h"

namespace fs = std::filesystem;

namespace hyperbrowse::services
{
    struct FolderTreeEnumerationSharedState
    {
        std::atomic_bool shutdown{false};
        std::atomic_uint64_t generation{0};
    };
}

namespace
{
    constexpr std::size_t kWorkerCount = 2;
    constexpr std::size_t kMaxPendingTaskCount = 8;
    constexpr std::size_t kMinimumParallelProbeCount = 8;

    struct EnumerationSharedStateView
    {
        std::shared_ptr<hyperbrowse::services::FolderTreeEnumerationSharedState> state;
        HWND targetWindow{};
        std::uint64_t requestId{};
        std::uint64_t generation{};
    };

    bool ShouldStop(const EnumerationSharedStateView& stateView)
    {
        return stateView.state->shutdown.load(std::memory_order_acquire)
            || stateView.state->generation.load(std::memory_order_acquire) != stateView.generation;
    }

    void PostUpdate(HWND targetWindow, std::unique_ptr<hyperbrowse::services::FolderTreeEnumerationUpdate> update)
    {
        if (!targetWindow)
        {
            return;
        }

        if (!PostMessageW(targetWindow,
                          hyperbrowse::services::FolderTreeEnumerationService::kMessageId,
                          0,
                          reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }

    void PostFailure(const EnumerationSharedStateView& stateView,
                     const std::wstring& folderPath,
                     std::wstring message)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderTreeEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderTreeEnumerationUpdateKind::Failed;
        update->folderPath = folderPath;
        update->message = std::move(message);
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    void PostCompletion(const EnumerationSharedStateView& stateView,
                        const std::wstring& folderPath,
                        std::vector<hyperbrowse::services::FolderTreeChild> childFolders)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderTreeEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderTreeEnumerationUpdateKind::Completed;
        update->folderPath = folderPath;
        update->childFolders = std::move(childFolders);
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    void PostChildPresenceCompletion(const EnumerationSharedStateView& stateView,
                                     std::vector<hyperbrowse::services::FolderTreeChild> childPresenceResults)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderTreeEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderTreeEnumerationUpdateKind::ChildPresenceCompleted;
        update->childPresenceResults = std::move(childPresenceResults);
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    bool IsVisibleChildFolder(const fs::directory_entry& entry, bool showHiddenFolders)
    {
        std::error_code statusError;
        if (!entry.is_directory(statusError) || statusError)
        {
            return false;
        }

        const DWORD attributes = GetFileAttributesW(entry.path().c_str());
        return attributes == INVALID_FILE_ATTRIBUTES
            || (attributes & FILE_ATTRIBUTE_HIDDEN) == 0
            || showHiddenFolders;
    }

    bool HasVisibleChildDirectory(const EnumerationSharedStateView& stateView,
                                  const fs::path& folderPath,
                                  bool showHiddenFolders)
    {
        std::wstring searchPattern = folderPath.wstring();
        if (!searchPattern.empty() && searchPattern.back() != L'\\' && searchPattern.back() != L'/')
        {
            searchPattern.push_back(L'\\');
        }
        searchPattern.push_back(L'*');

        WIN32_FIND_DATAW findData{};
        const HANDLE searchHandle = FindFirstFileExW(searchPattern.c_str(),
                                                     FindExInfoBasic,
                                                     &findData,
                                                     FindExSearchLimitToDirectories,
                                                     nullptr,
                                                     FIND_FIRST_EX_LARGE_FETCH);
        if (searchHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        bool hasVisibleChild = false;
        do
        {
            if (ShouldStop(stateView))
            {
                break;
            }

            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                && wcscmp(findData.cFileName, L".") != 0
                && wcscmp(findData.cFileName, L"..") != 0
                && (showHiddenFolders
                    || (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == 0))
            {
                hasVisibleChild = true;
                break;
            }
        } while (FindNextFileW(searchHandle, &findData) != FALSE);

        FindClose(searchHandle);
        return hasVisibleChild;
    }

    bool IsLowSeekPenaltyFixedVolume(const std::wstring& folderPath)
    {
        wchar_t volumePath[MAX_PATH]{};
        if (GetVolumePathNameW(folderPath.c_str(), volumePath, static_cast<DWORD>(std::size(volumePath))) == FALSE
            || GetDriveTypeW(volumePath) != DRIVE_FIXED
            || volumePath[0] == L'\0'
            || volumePath[1] != L':'
            || volumePath[2] != L'\\'
            || volumePath[3] != L'\0')
        {
            return false;
        }

        std::wstring devicePath = L"\\\\.\\";
        devicePath.append(volumePath, 2);
        const HANDLE volumeHandle = CreateFileW(devicePath.c_str(),
                                                0,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                nullptr,
                                                OPEN_EXISTING,
                                                0,
                                                nullptr);
        if (volumeHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
        DWORD bytesReturned{};
        const BOOL queried = DeviceIoControl(volumeHandle,
                                              IOCTL_STORAGE_QUERY_PROPERTY,
                                              &query,
                                              sizeof(query),
                                              &descriptor,
                                              sizeof(descriptor),
                                              &bytesReturned,
                                              nullptr);
        CloseHandle(volumeHandle);
        return queried != FALSE && descriptor.IncursSeekPenalty == FALSE;
    }

    struct ChildPresenceProbeBatch
    {
        std::vector<hyperbrowse::services::FolderTreeChild> results;
        bool cancelled{};
    };

    ChildPresenceProbeBatch ProbeChildDirectoryPresenceRange(
        const EnumerationSharedStateView& stateView,
        const std::vector<std::wstring>& folderPaths,
        std::size_t begin,
        std::size_t end,
        bool showHiddenFolders)
    {
        ChildPresenceProbeBatch batch;
        batch.results.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index)
        {
            if (ShouldStop(stateView))
            {
                batch.cancelled = true;
                return batch;
            }

            const fs::path basePath(folderPaths[index]);
            hyperbrowse::services::FolderTreeChild childPresence;
            childPresence.path = folderPaths[index];
            childPresence.hasChildren = HasVisibleChildDirectory(stateView, basePath, showHiddenFolders);
            batch.results.push_back(std::move(childPresence));
        }

        return batch;
    }

    void EnumerateChildDirectories(const EnumerationSharedStateView& stateView,
                                   const std::wstring& folderPath)
    {
        try
        {
            if (ShouldStop(stateView))
            {
                return;
            }

            SHELLFLAGSTATE shellState{};
            SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
            const bool showHiddenFolders = shellState.fShowAllObjects != FALSE;
            const fs::path basePath(folderPath);
            std::error_code existsError;
            if (!fs::exists(basePath, existsError) || existsError)
            {
                PostFailure(stateView, folderPath, L"The selected folder no longer exists.");
                return;
            }

            std::error_code directoryError;
            if (!fs::is_directory(basePath, directoryError) || directoryError)
            {
                PostFailure(stateView, folderPath, L"The selected tree node is not a readable folder.");
                return;
            }

            std::vector<hyperbrowse::services::FolderTreeChild> childFolders;
            const fs::directory_options options = fs::directory_options::skip_permission_denied;
            std::error_code iteratorError;
            for (fs::directory_iterator iterator(basePath, options, iteratorError), end;
                 iterator != end;
                 iterator.increment(iteratorError))
            {
                if (ShouldStop(stateView))
                {
                    return;
                }

                if (iteratorError)
                {
                    iteratorError.clear();
                    continue;
                }

                if (!IsVisibleChildFolder(*iterator, showHiddenFolders))
                {
                    continue;
                }

                hyperbrowse::services::FolderTreeChild childFolder;
                childFolder.path = iterator->path().wstring();
                childFolders.push_back(std::move(childFolder));
            }

            std::sort(childFolders.begin(), childFolders.end(), [](const auto& lhs, const auto& rhs)
            {
                return _wcsicmp(lhs.path.c_str(), rhs.path.c_str()) < 0;
            });

            PostCompletion(stateView, folderPath, std::move(childFolders));
        }
        catch (const std::exception& exception)
        {
            PostFailure(stateView,
                        folderPath,
                        L"Folder tree enumeration failed: " + hyperbrowse::util::WidenExceptionMessage(exception.what()));
        }
    }

    void QueryChildDirectoryPresence(const EnumerationSharedStateView& stateView,
                                     const std::vector<std::wstring>& folderPaths)
    {
        try
        {
            if (ShouldStop(stateView))
            {
                return;
            }

            SHELLFLAGSTATE shellState{};
            SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
            const bool showHiddenFolders = shellState.fShowAllObjects != FALSE;
            if (folderPaths.size() < kMinimumParallelProbeCount
                || !IsLowSeekPenaltyFixedVolume(folderPaths.front()))
            {
                ChildPresenceProbeBatch batch = ProbeChildDirectoryPresenceRange(
                    stateView,
                    folderPaths,
                    0,
                    folderPaths.size(),
                    showHiddenFolders);
                if (batch.cancelled)
                {
                    return;
                }

                PostChildPresenceCompletion(stateView, std::move(batch.results));
                return;
            }

            const std::size_t splitIndex = (folderPaths.size() + 1) / 2;
            auto firstBatch = std::async(std::launch::async,
                                         [&stateView, &folderPaths, splitIndex, showHiddenFolders]()
                                         {
                                             return ProbeChildDirectoryPresenceRange(
                                                 stateView,
                                                 folderPaths,
                                                 0,
                                                 splitIndex,
                                                 showHiddenFolders);
                                         });
            auto secondBatch = std::async(std::launch::async,
                                          [&stateView, &folderPaths, splitIndex, showHiddenFolders]()
                                          {
                                              return ProbeChildDirectoryPresenceRange(
                                                  stateView,
                                                  folderPaths,
                                                  splitIndex,
                                                  folderPaths.size(),
                                                  showHiddenFolders);
                                          });
            ChildPresenceProbeBatch firstResults = firstBatch.get();
            ChildPresenceProbeBatch secondResults = secondBatch.get();
            if (firstResults.cancelled || secondResults.cancelled)
            {
                return;
            }

            std::vector<hyperbrowse::services::FolderTreeChild> childPresenceResults;
            childPresenceResults.reserve(folderPaths.size());
            childPresenceResults.insert(childPresenceResults.end(),
                                        std::make_move_iterator(firstResults.results.begin()),
                                        std::make_move_iterator(firstResults.results.end()));
            childPresenceResults.insert(childPresenceResults.end(),
                                        std::make_move_iterator(secondResults.results.begin()),
                                        std::make_move_iterator(secondResults.results.end()));
            PostChildPresenceCompletion(stateView, std::move(childPresenceResults));
        }
        catch (const std::exception& exception)
        {
            PostFailure(stateView,
                        folderPaths.empty() ? std::wstring{} : folderPaths.front(),
                        L"Folder tree child-presence query failed: "
                            + hyperbrowse::util::WidenExceptionMessage(exception.what()));
        }
    }
}

namespace hyperbrowse::services
{
    FolderTreeEnumerationService::FolderTreeEnumerationService()
        : sharedState_(std::make_shared<FolderTreeEnumerationSharedState>())
        , executor_(kWorkerCount, kMaxPendingTaskCount)
    {
    }

    FolderTreeEnumerationService::~FolderTreeEnumerationService()
    {
        sharedState_->shutdown.store(true, std::memory_order_release);
        CancelAll();
    }

    std::uint64_t FolderTreeEnumerationService::EnumerateChildDirectoriesAsync(HWND targetWindow, std::wstring folderPath)
    {
        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::uint64_t generation = sharedState_->generation.load(std::memory_order_acquire);
        EnumerationSharedStateView stateView{sharedState_, targetWindow, requestId, generation};
        util::LogInfo(L"Starting async folder-tree enumeration for " + folderPath);
        const std::wstring requestedFolderPath = folderPath;

        const bool accepted = executor_.Post([stateView, folderPath = std::move(folderPath)]() mutable
        {
            util::Stopwatch stopwatch;
            EnumerateChildDirectories(stateView, folderPath);
            util::RecordTiming(L"folder.tree.enumeration", stopwatch.ElapsedMilliseconds());
        });
        if (!accepted)
        {
            util::IncrementCounter(L"service.folder_tree.queue_rejected");
            PostFailure(stateView, requestedFolderPath, L"Folder tree enumeration could not be queued.");
        }

        util::RecordMaximum(L"service.folder_tree.queue_depth_peak",
                            executor_.PeakPendingTaskCount());

        return requestId;
    }

    std::uint64_t FolderTreeEnumerationService::QueryChildDirectoryPresenceAsync(HWND targetWindow,
                                                                                   std::wstring folderPath)
    {
        std::vector<std::wstring> folderPaths;
        folderPaths.push_back(std::move(folderPath));
        return QueryChildDirectoryPresenceAsync(targetWindow, std::move(folderPaths));
    }

    std::uint64_t FolderTreeEnumerationService::QueryChildDirectoryPresenceAsync(
        HWND targetWindow,
        std::vector<std::wstring> folderPaths)
    {
        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::uint64_t generation = sharedState_->generation.load(std::memory_order_acquire);
        EnumerationSharedStateView stateView{sharedState_, targetWindow, requestId, generation};
        util::LogInfo(L"Starting async folder-tree child-presence query for "
                      + std::to_wstring(folderPaths.size()) + L" folders");
        const std::wstring requestedFolderPath = folderPaths.empty() ? std::wstring{} : folderPaths.front();

        const bool accepted = executor_.Post([stateView, folderPaths = std::move(folderPaths)]() mutable
        {
            util::Stopwatch stopwatch;
            QueryChildDirectoryPresence(stateView, folderPaths);
            util::RecordTiming(L"folder.tree.child.presence", stopwatch.ElapsedMilliseconds());
        });
        if (!accepted)
        {
            util::IncrementCounter(L"service.folder_tree_presence.queue_rejected");
            PostFailure(stateView, requestedFolderPath, L"Folder tree child-presence query could not be queued.");
        }

        util::RecordMaximum(L"service.folder_tree_presence.queue_depth_peak",
                            executor_.PeakPendingTaskCount());

        return requestId;
    }

    void FolderTreeEnumerationService::CancelAll()
    {
        sharedState_->generation.fetch_add(1, std::memory_order_acq_rel);
        cancellationCount_.fetch_add(1, std::memory_order_relaxed);
        util::IncrementCounter(L"service.folder_tree.cancelled");
    }
}
