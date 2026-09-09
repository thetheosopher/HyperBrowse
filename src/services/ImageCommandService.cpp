#include "services/ImageCommandService.h"

#include <algorithm>
#include <utility>

#include "cache/ThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "services/ImageMetadataService.h"
#include "services/JpegTransformService.h"

namespace
{
    void PostUpdate(
        HWND targetWindow,
        std::unique_ptr<hyperbrowse::services::ImageCommandUpdate> update,
        const std::atomic_bool* shutdown)
    {
        if (!targetWindow || (shutdown && shutdown->load(std::memory_order_acquire)))
        {
            return;
        }
        if (PostMessageW(
                targetWindow,
                hyperbrowse::services::ImageCommandService::kMessageId,
                0,
                reinterpret_cast<LPARAM>(update.get())))
        {
            update.release();
        }
    }

    bool IsJpeg(const hyperbrowse::browser::BrowserItem& item)
    {
        return _wcsicmp(item.fileType.c_str(), L"JPG") == 0
            || _wcsicmp(item.fileType.c_str(), L"JPEG") == 0;
    }
}

namespace hyperbrowse::services
{
    ImageCommandService::ImageCommandService()
        : sharedState_(std::make_shared<SharedState>())
        , executor_(1, 1)
    {
    }

    ImageCommandService::~ImageCommandService()
    {
        Shutdown();
    }

    std::uint64_t ImageCommandService::BeginRequest() noexcept
    {
        Cancel();
        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        sharedState_->activeRequestId.store(requestId, std::memory_order_release);
        return requestId;
    }

    bool ImageCommandService::PostOrReportFailure(
        HWND targetWindow,
        std::uint64_t requestId,
        ImageCommandKind kind,
        HWND ownerWindow,
        std::function<void()> task)
    {
        if (executor_.Post(std::move(task)))
        {
            return true;
        }

        auto update = std::make_unique<ImageCommandUpdate>();
        update->requestId = requestId;
        update->kind = kind;
        update->ownerWindow = ownerWindow;
        update->finished = true;
        update->failedCount = 1;
        update->completedCount = 1;
        update->totalCount = 1;
        update->message = L"The image command could not be queued.";
        PostUpdate(targetWindow, std::move(update), &sharedState_->shutdown);
        return false;
    }

    std::uint64_t ImageCommandService::StartInformation(
        HWND targetWindow,
        HWND ownerWindow,
        browser::BrowserItem item)
    {
        const std::uint64_t requestId = BeginRequest();
        const auto sharedState = sharedState_;
        PostOrReportFailure(
            targetWindow,
            requestId,
            ImageCommandKind::Information,
            ownerWindow,
            [sharedState, targetWindow, ownerWindow, item = std::move(item), requestId]() mutable
            {
                auto update = std::make_unique<ImageCommandUpdate>();
                update->requestId = requestId;
                update->kind = ImageCommandKind::Information;
                update->ownerWindow = ownerWindow;
                update->item = item;
                update->completedCount = 1;
                update->totalCount = 1;
                update->finished = true;
                if (sharedState->activeRequestId.load(std::memory_order_acquire) != requestId)
                {
                    update->cancelled = true;
                    PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
                    return;
                }

                std::wstring errorMessage;
                try
                {
                    update->metadata = ExtractImageMetadata(item, &errorMessage);
                }
                catch (...)
                {
                    errorMessage = L"Image metadata extraction failed unexpectedly.";
                }
                if (sharedState->activeRequestId.load(std::memory_order_acquire) != requestId)
                {
                    update->metadata.reset();
                    update->cancelled = true;
                }
                else if (update->metadata)
                {
                    update->succeededCount = 1;
                }
                else
                {
                    update->failedCount = 1;
                    update->message = std::move(errorMessage);
                }
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
            });
        return requestId;
    }

    std::uint64_t ImageCommandService::StartCopyPixels(
        HWND targetWindow,
        HWND ownerWindow,
        browser::BrowserItem item)
    {
        const std::uint64_t requestId = BeginRequest();
        const auto sharedState = sharedState_;
        PostOrReportFailure(
            targetWindow,
            requestId,
            ImageCommandKind::CopyPixels,
            ownerWindow,
            [sharedState, targetWindow, ownerWindow, item = std::move(item), requestId]() mutable
            {
                auto update = std::make_unique<ImageCommandUpdate>();
                update->requestId = requestId;
                update->kind = ImageCommandKind::CopyPixels;
                update->ownerWindow = ownerWindow;
                update->item = item;
                update->completedCount = 1;
                update->totalCount = 1;
                update->finished = true;
                if (sharedState->activeRequestId.load(std::memory_order_acquire) != requestId)
                {
                    update->cancelled = true;
                    PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
                    return;
                }

                std::wstring errorMessage;
                try
                {
                    update->image = decode::DecodeFullImage(item, &errorMessage);
                }
                catch (...)
                {
                    errorMessage = L"Image decoding failed unexpectedly.";
                }
                if (sharedState->activeRequestId.load(std::memory_order_acquire) != requestId)
                {
                    update->image.reset();
                    update->cancelled = true;
                }
                else if (update->image && update->image->Bitmap())
                {
                    update->succeededCount = 1;
                }
                else
                {
                    update->failedCount = 1;
                    update->message = std::move(errorMessage);
                }
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
            });
        return requestId;
    }

    std::uint64_t ImageCommandService::StartJpegOrientation(
        HWND targetWindow,
        HWND ownerWindow,
        std::vector<browser::BrowserItem> items,
        int quarterTurnsDelta)
    {
        items.erase(std::remove_if(items.begin(), items.end(), [](const browser::BrowserItem& item)
        {
            return !IsJpeg(item);
        }), items.end());

        const std::uint64_t requestId = BeginRequest();
        const auto sharedState = sharedState_;
        const std::size_t totalCount = items.size();
        PostOrReportFailure(
            targetWindow,
            requestId,
            ImageCommandKind::AdjustJpegOrientation,
            ownerWindow,
            [sharedState,
             targetWindow,
             ownerWindow,
             items = std::move(items),
             totalCount,
             quarterTurnsDelta,
             requestId]() mutable
            {
                auto update = std::make_unique<ImageCommandUpdate>();
                update->requestId = requestId;
                update->kind = ImageCommandKind::AdjustJpegOrientation;
                update->ownerWindow = ownerWindow;
                update->totalCount = totalCount;
                for (const browser::BrowserItem& item : items)
                {
                    if (sharedState->activeRequestId.load(std::memory_order_acquire) != requestId)
                    {
                        update->cancelled = true;
                        break;
                    }

                    std::wstring errorMessage;
                    bool succeeded = false;
                    try
                    {
                        succeeded = AdjustJpegOrientation(item.filePath, quarterTurnsDelta, &errorMessage);
                    }
                    catch (...)
                    {
                        errorMessage = L"JPEG orientation adjustment failed unexpectedly.";
                    }
                    ++update->completedCount;
                    if (succeeded)
                    {
                        ++update->succeededCount;
                        update->updatedPaths.push_back(item.filePath);
                    }
                    else
                    {
                        ++update->failedCount;
                        if (update->message.empty())
                        {
                            update->message = std::move(errorMessage);
                        }
                    }

                    auto progress = std::make_unique<ImageCommandUpdate>();
                    progress->requestId = requestId;
                    progress->kind = ImageCommandKind::AdjustJpegOrientation;
                    progress->ownerWindow = ownerWindow;
                    progress->item = item;
                    progress->completedCount = update->completedCount;
                    progress->totalCount = update->totalCount;
                    progress->succeededCount = update->succeededCount;
                    progress->failedCount = update->failedCount;
                    PostUpdate(targetWindow, std::move(progress), &sharedState->shutdown);
                }
                update->finished = true;
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
            });
        return requestId;
    }

    void ImageCommandService::Cancel() noexcept
    {
        sharedState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
    }

    void ImageCommandService::Shutdown() noexcept
    {
        if (!sharedState_->shutdown.exchange(true, std::memory_order_acq_rel))
        {
            Cancel();
            executor_.Shutdown();
        }
    }
}
