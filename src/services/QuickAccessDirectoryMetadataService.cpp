#include "services/QuickAccessDirectoryMetadataService.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>

#include "browser/BrowserModel.h"
#include "util/Diagnostics.h"
#include "util/Log.h"

namespace fs = std::filesystem;

namespace hyperbrowse::services
{
    struct QuickAccessDirectoryMetadataSharedState
    {
        std::atomic_bool shuttingDown{};
        std::atomic_uint64_t generation{};
        std::mutex postMutex;
    };
}

namespace
{
    using hyperbrowse::services::QuickAccessDirectoryImageCount;
    using hyperbrowse::services::QuickAccessDirectoryMetadataUpdate;
    using hyperbrowse::services::QuickAccessDirectoryMetadataSharedState;
    using hyperbrowse::services::QuickAccessDirectoryMetadataService;

    constexpr std::size_t kWorkerCount = 1;
    constexpr std::size_t kMaxPendingTaskCount = 4;

    struct RequestStateView
    {
        std::shared_ptr<QuickAccessDirectoryMetadataSharedState> state;
        HWND targetWindow{};
        std::uint64_t requestId{};
        std::uint64_t generation{};
    };

    bool ShouldStop(const RequestStateView& request)
    {
        return request.state->shuttingDown.load(std::memory_order_acquire)
            || request.state->generation.load(std::memory_order_acquire) != request.generation;
    }

    std::optional<QuickAccessDirectoryImageCount> CountSupportedImages(
        const RequestStateView& request,
        const std::wstring& folderPath)
    {
        if (ShouldStop(request))
        {
            return std::nullopt;
        }

        QuickAccessDirectoryImageCount result;
        result.folderPath = folderPath;

        std::error_code error;
        const fs::path directoryPath(folderPath);
        if (!fs::is_directory(directoryPath, error) || error)
        {
            result.errorCode = error ? static_cast<DWORD>(error.value()) : ERROR_DIRECTORY;
            return result;
        }

        fs::directory_iterator iterator(directoryPath, error);
        if (error)
        {
            result.errorCode = static_cast<DWORD>(error.value());
            return result;
        }

        const fs::directory_iterator end;
        while (iterator != end)
        {
            if (ShouldStop(request))
            {
                return std::nullopt;
            }

            if (error)
            {
                result.errorCode = static_cast<DWORD>(error.value());
                return result;
            }

            std::error_code statusError;
            const bool regularFile = iterator->is_regular_file(statusError);
            if (statusError)
            {
                result.errorCode = static_cast<DWORD>(statusError.value());
                return result;
            }
            if (regularFile
                && hyperbrowse::browser::IsSupportedImageExtension(iterator->path().extension().wstring()))
            {
                ++result.imageCount;
            }

            iterator.increment(error);
        }

        if (error)
        {
            result.imageCount = 0;
            result.errorCode = static_cast<DWORD>(error.value());
            return result;
        }

        result.available = true;
        return result;
    }

    void PostUpdate(const RequestStateView& request,
                    std::unique_ptr<QuickAccessDirectoryMetadataUpdate> update)
    {
        if (!request.targetWindow || !update)
        {
            return;
        }

        bool posted = false;
        {
            std::scoped_lock lock(request.state->postMutex);
            if (!ShouldStop(request))
            {
                posted = PostMessageW(request.targetWindow,
                                      QuickAccessDirectoryMetadataService::kMessageId,
                                      0,
                                      reinterpret_cast<LPARAM>(update.get())) != FALSE;
                if (posted)
                {
                    update.release();
                }
            }
        }

        if (!posted && !ShouldStop(request))
        {
            hyperbrowse::util::LogLastError(L"PostMessageW(QuickAccessDirectoryMetadata)");
        }
    }

    void CountFolderImages(const RequestStateView& request, std::vector<std::wstring> folderPaths)
    {
        auto update = std::make_unique<QuickAccessDirectoryMetadataUpdate>();
        update->requestId = request.requestId;
        update->imageCounts.reserve(folderPaths.size());

        for (const std::wstring& folderPath : folderPaths)
        {
            const std::optional<QuickAccessDirectoryImageCount> result =
                CountSupportedImages(request, folderPath);
            if (!result)
            {
                return;
            }
            if (!result->available)
            {
                hyperbrowse::util::LogInfo(L"Quick Actions image count unavailable for "
                                           + folderPath + L" (error "
                                           + std::to_wstring(result->errorCode) + L")");
            }
            update->imageCounts.push_back(*result);
        }

        PostUpdate(request, std::move(update));
    }
}

namespace hyperbrowse::services
{
    QuickAccessDirectoryMetadataService::QuickAccessDirectoryMetadataService()
        : sharedState_(std::make_shared<QuickAccessDirectoryMetadataSharedState>())
        , executor_(kWorkerCount, kMaxPendingTaskCount)
    {
    }

    QuickAccessDirectoryMetadataService::~QuickAccessDirectoryMetadataService()
    {
        {
            std::scoped_lock lock(sharedState_->postMutex);
            sharedState_->shuttingDown.store(true, std::memory_order_release);
            sharedState_->generation.fetch_add(1, std::memory_order_acq_rel);
        }
        executor_.Shutdown();
    }

    std::uint64_t QuickAccessDirectoryMetadataService::CountSupportedImagesAsync(
        HWND targetWindow,
        std::vector<std::wstring> folderPaths)
    {
        if (!targetWindow || folderPaths.empty())
        {
            return 0;
        }

        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(sharedState_->postMutex);
            if (sharedState_->shuttingDown.load(std::memory_order_acquire))
            {
                return 0;
            }
            generation = sharedState_->generation.load(std::memory_order_acquire);
        }

        const RequestStateView request{sharedState_, targetWindow, requestId, generation};
        util::LogInfo(L"Starting async Quick Actions image counts for "
                      + std::to_wstring(folderPaths.size()) + L" folders");
        if (!executor_.Post([request, folderPaths = std::move(folderPaths)]() mutable
            {
                CountFolderImages(request, std::move(folderPaths));
            }))
        {
            hyperbrowse::util::IncrementCounter(L"service.quick_access_metadata.queue_rejected");
            return 0;
        }

        return requestId;
    }

    void QuickAccessDirectoryMetadataService::CancelAll()
    {
        std::scoped_lock lock(sharedState_->postMutex);
        sharedState_->generation.fetch_add(1, std::memory_order_acq_rel);
    }
}
