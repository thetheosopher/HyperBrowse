#include "services/FolderTreeEnumerationService.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <future>
#include <system_error>

#include "util/Diagnostics.h"
#include "util/Log.h"

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
                        std::vector<std::wstring> childFolders)
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

    void EnumerateChildDirectories(const EnumerationSharedStateView& stateView,
                                   const std::wstring& folderPath)
    {
        try
        {
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

            std::vector<std::wstring> childFolders;
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

                std::error_code statusError;
                if (!iterator->is_directory(statusError) || statusError)
                {
                    continue;
                }

                childFolders.push_back(iterator->path().wstring());
            }

            std::sort(childFolders.begin(), childFolders.end(), [](const std::wstring& lhs, const std::wstring& rhs)
            {
                return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
            });

            PostCompletion(stateView, folderPath, std::move(childFolders));
        }
        catch (const std::exception& exception)
        {
            const std::string message = exception.what();
            PostFailure(stateView,
                        folderPath,
                        L"Folder tree enumeration failed: " + std::wstring(message.begin(), message.end()));
        }
    }
}

namespace hyperbrowse::services
{
    FolderTreeEnumerationService::FolderTreeEnumerationService()
        : sharedState_(std::make_shared<FolderTreeEnumerationSharedState>())
    {
    }

    FolderTreeEnumerationService::~FolderTreeEnumerationService()
    {
        sharedState_->shutdown.store(true, std::memory_order_release);
        CancelAll();
        WaitForWorkers();
    }

    std::uint64_t FolderTreeEnumerationService::EnumerateChildDirectoriesAsync(HWND targetWindow, std::wstring folderPath)
    {
        ReapCompletedWorkers();

        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::uint64_t generation = sharedState_->generation.load(std::memory_order_acquire);
        EnumerationSharedStateView stateView{sharedState_, targetWindow, requestId, generation};
        util::LogInfo(L"Starting async folder-tree enumeration for " + folderPath);

        workers_.push_back(std::async(std::launch::async, [stateView, folderPath = std::move(folderPath)]() mutable
        {
            util::Stopwatch stopwatch;
            EnumerateChildDirectories(stateView, folderPath);
            util::RecordTiming(L"folder.tree.enumeration", stopwatch.ElapsedMilliseconds());
        }));

        return requestId;
    }

    void FolderTreeEnumerationService::CancelAll()
    {
        sharedState_->generation.fetch_add(1, std::memory_order_acq_rel);
        ReapCompletedWorkers();
    }

    void FolderTreeEnumerationService::ReapCompletedWorkers()
    {
        workers_.erase(std::remove_if(workers_.begin(), workers_.end(), [](std::future<void>& worker)
        {
            return !worker.valid() || worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        }), workers_.end());
    }

    void FolderTreeEnumerationService::WaitForWorkers()
    {
        for (std::future<void>& worker : workers_)
        {
            if (worker.valid())
            {
                worker.wait();
            }
        }
        workers_.clear();
    }
}