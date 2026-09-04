#include "services/FolderEnumerationService.h"

#include <filesystem>
#include <system_error>

#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/StringConvert.h"

namespace fs = std::filesystem;

namespace hyperbrowse::services
{
    struct FolderEnumerationSharedState
    {
        std::atomic_uint64_t activeRequestId{0};
        std::atomic_bool shutdown{false};
    };
}

namespace
{
    constexpr std::size_t kInitialBatchSize = 16;
    constexpr std::size_t kInitialBatchItemLimit = kInitialBatchSize * 2;
    constexpr std::size_t kBatchSize = 64;
    constexpr std::size_t kWorkerCount = 2;
    constexpr std::size_t kMaxPendingTaskCount = 2;
    constexpr DWORD kInitialBatchPauseMilliseconds = 8;
    constexpr DWORD kBatchPauseMilliseconds = 1;

    struct EnumerationSharedStateView
    {
        std::shared_ptr<hyperbrowse::services::FolderEnumerationSharedState> state;
        HWND targetWindow{};
        std::uint64_t requestId{};
    };

    bool ShouldStop(const EnumerationSharedStateView& stateView)
    {
        return stateView.state->shutdown.load(std::memory_order_acquire)
            || stateView.state->activeRequestId.load(std::memory_order_acquire) != stateView.requestId;
    }

    void PostUpdate(HWND targetWindow, std::unique_ptr<hyperbrowse::services::FolderEnumerationUpdate> update)
    {
        if (!targetWindow)
        {
            return;
        }

        if (!PostMessageW(targetWindow,
                          hyperbrowse::services::FolderEnumerationService::kMessageId,
                          0,
                          reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }

    void FlushBatch(const EnumerationSharedStateView& stateView,
                    const std::wstring& folderPath,
                    std::vector<hyperbrowse::browser::BrowserItem>* batch,
                    std::uint64_t totalCount,
                    std::uint64_t totalBytes)
    {
        if (batch->empty() || ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderEnumerationUpdateKind::Batch;
        update->folderPath = folderPath;
        update->items = std::move(*batch);
        update->totalCount = totalCount;
        update->totalBytes = totalBytes;
        PostUpdate(stateView.targetWindow, std::move(update));
        batch->clear();
        Sleep(totalCount <= kInitialBatchItemLimit
                  ? kInitialBatchPauseMilliseconds
                  : kBatchPauseMilliseconds);
    }

    void PostFailure(const EnumerationSharedStateView& stateView,
                     const std::wstring& folderPath,
                     std::wstring message)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderEnumerationUpdateKind::Failed;
        update->folderPath = folderPath;
        update->message = std::move(message);
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    void PostCompletion(const EnumerationSharedStateView& stateView,
                        const std::wstring& folderPath,
                        std::uint64_t totalCount,
                        std::uint64_t totalBytes)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderEnumerationUpdateKind::Completed;
        update->folderPath = folderPath;
        update->totalCount = totalCount;
        update->totalBytes = totalBytes;
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    void HandleEntry(const EnumerationSharedStateView& stateView,
                     const std::wstring& folderPath,
                     const fs::directory_entry& entry,
                     bool includeSubfolder,
                     std::vector<hyperbrowse::browser::BrowserItem>* batch,
                     std::uint64_t* totalCount,
                     std::uint64_t* totalBytes)
    {
        std::error_code statusError;
        if (entry.is_directory(statusError))
        {
            if (statusError || !includeSubfolder)
            {
                return;
            }

            hyperbrowse::browser::BrowserItem item = hyperbrowse::browser::BuildBrowserItemFromPath(entry.path());
            item.isDirectory = true;
            item.fileType = L"Folder";
            ++(*totalCount);
            batch->push_back(std::move(item));
        }
        else
        {
            if (statusError || !entry.is_regular_file(statusError))
            {
                return;
            }

            if (!hyperbrowse::browser::IsSupportedImageExtension(entry.path().extension().wstring()))
            {
                return;
            }

            hyperbrowse::browser::BrowserItem item = hyperbrowse::browser::BuildBrowserItemFromPath(entry.path());
            *totalBytes += item.fileSizeBytes;
            ++(*totalCount);
            batch->push_back(std::move(item));
        }

        const std::size_t batchSize = *totalCount <= kInitialBatchItemLimit ? kInitialBatchSize : kBatchSize;
        if (batch->size() >= batchSize)
        {
            FlushBatch(stateView, folderPath, batch, *totalCount, *totalBytes);
        }
    }

    void EnumerateFolder(const EnumerationSharedStateView& stateView,
                         const std::wstring& folderPath,
                         bool recursive,
                         bool includeSubfolders)
    {
        try
        {
            if (ShouldStop(stateView))
            {
                return;
            }

            const fs::path basePath(folderPath);
            std::error_code existsError;
            if (!fs::exists(basePath, existsError) || existsError)
            {
                PostFailure(stateView, folderPath, L"The selected folder no longer exists.");
                return;
            }

            std::vector<hyperbrowse::browser::BrowserItem> batch;
            batch.reserve(kBatchSize);
            std::uint64_t totalCount = 0;
            std::uint64_t totalBytes = 0;
            const fs::directory_options options = fs::directory_options::skip_permission_denied;

            if (recursive)
            {
                std::error_code iteratorError;
                for (fs::recursive_directory_iterator iterator(basePath, options, iteratorError), end;
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

                    HandleEntry(stateView,
                                folderPath,
                                *iterator,
                                includeSubfolders && iterator.depth() == 0,
                                &batch,
                                &totalCount,
                                &totalBytes);
                }
            }
            else
            {
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

                    HandleEntry(stateView,
                                folderPath,
                                *iterator,
                                includeSubfolders,
                                &batch,
                                &totalCount,
                                &totalBytes);
                }
            }

            FlushBatch(stateView, folderPath, &batch, totalCount, totalBytes);
            PostCompletion(stateView, folderPath, totalCount, totalBytes);
        }
        catch (const std::exception& exception)
        {
            PostFailure(stateView,
                        folderPath,
                        L"Folder enumeration failed: " + hyperbrowse::util::WidenExceptionMessage(exception.what()));
        }
    }
}

namespace hyperbrowse::services
{
    FolderEnumerationService::FolderEnumerationService()
        : sharedState_(std::make_shared<FolderEnumerationSharedState>())
        , executor_(kWorkerCount, kMaxPendingTaskCount)
    {
    }

    FolderEnumerationService::~FolderEnumerationService()
    {
        sharedState_->shutdown.store(true, std::memory_order_release);
        Cancel();
    }

    std::uint64_t FolderEnumerationService::EnumerateFolderAsync(HWND targetWindow,
                                                                 std::wstring folderPath,
                                                                 bool recursive,
                                                                 bool includeSubfolders)
    {
        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        sharedState_->activeRequestId.store(requestId, std::memory_order_release);

        EnumerationSharedStateView stateView{sharedState_, targetWindow, requestId};
        util::LogInfo(L"Starting async folder enumeration for " + folderPath);
        const std::wstring requestedFolderPath = folderPath;

        const bool accepted = executor_.Post([stateView,
                                              folderPath = std::move(folderPath),
                                              recursive,
                                              includeSubfolders]() mutable
        {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
            util::Stopwatch stopwatch;
            EnumerateFolder(stateView, folderPath, recursive, includeSubfolders);
            util::RecordTiming(L"folder.enumeration", stopwatch.ElapsedMilliseconds());
        });
        if (!accepted)
        {
            util::IncrementCounter(L"service.folder_enumeration.queue_rejected");
            PostFailure(stateView, requestedFolderPath, L"Folder enumeration could not be queued.");
        }

        return requestId;
    }

    void FolderEnumerationService::Cancel()
    {
        sharedState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
    }
}
