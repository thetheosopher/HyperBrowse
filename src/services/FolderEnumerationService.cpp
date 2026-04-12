#include "services/FolderEnumerationService.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <system_error>

#include "util/Diagnostics.h"
#include "util/Log.h"

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
    constexpr std::size_t kBatchSize = 64;

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
                     std::vector<hyperbrowse::browser::BrowserItem>* batch,
                     std::uint64_t* totalCount,
                     std::uint64_t* totalBytes)
    {
        std::error_code statusError;
        if (!entry.is_regular_file(statusError) || statusError)
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

        if (batch->size() >= kBatchSize)
        {
            FlushBatch(stateView, folderPath, batch, *totalCount, *totalBytes);
        }
    }

    void EnumerateFolder(const EnumerationSharedStateView& stateView,
                         const std::wstring& folderPath,
                         bool recursive)
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

                    HandleEntry(stateView, folderPath, *iterator, &batch, &totalCount, &totalBytes);
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

                    HandleEntry(stateView, folderPath, *iterator, &batch, &totalCount, &totalBytes);
                }
            }

            FlushBatch(stateView, folderPath, &batch, totalCount, totalBytes);
            PostCompletion(stateView, folderPath, totalCount, totalBytes);
        }
        catch (const std::exception& exception)
        {
            const std::string message = exception.what();
            PostFailure(stateView,
                        folderPath,
                        L"Folder enumeration failed: " + std::wstring(message.begin(), message.end()));
        }
    }
}

namespace hyperbrowse::services
{
    FolderEnumerationService::FolderEnumerationService()
        : sharedState_(std::make_shared<FolderEnumerationSharedState>())
    {
    }

    FolderEnumerationService::~FolderEnumerationService()
    {
        sharedState_->shutdown.store(true, std::memory_order_release);
        Cancel();
        WaitForWorkers();
    }

    std::uint64_t FolderEnumerationService::EnumerateFolderAsync(HWND targetWindow, std::wstring folderPath, bool recursive)
    {
        ReapCompletedWorkers();

        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        sharedState_->activeRequestId.store(requestId, std::memory_order_release);

        EnumerationSharedStateView stateView{sharedState_, targetWindow, requestId};
        util::LogInfo(L"Starting async folder enumeration for " + folderPath);

        workers_.push_back(std::async(std::launch::async, [stateView, folderPath = std::move(folderPath), recursive]() mutable
        {
            util::Stopwatch stopwatch;
            EnumerateFolder(stateView, folderPath, recursive);
            util::RecordTiming(L"folder.enumeration", stopwatch.ElapsedMilliseconds());
        }));

        return requestId;
    }

    void FolderEnumerationService::Cancel()
    {
        sharedState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
        ReapCompletedWorkers();
    }

    void FolderEnumerationService::ReapCompletedWorkers()
    {
        workers_.erase(std::remove_if(workers_.begin(), workers_.end(), [](std::future<void>& worker)
        {
            return !worker.valid() || worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        }), workers_.end());
    }

    void FolderEnumerationService::WaitForWorkers()
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