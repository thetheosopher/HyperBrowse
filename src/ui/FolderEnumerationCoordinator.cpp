#include "ui/FolderEnumerationCoordinator.h"

#include <memory>
#include <utility>

namespace hyperbrowse::ui
{
    std::uint64_t FolderEnumerationCoordinator::Start(HWND targetWindow,
                                                       std::wstring folderPath,
                                                       bool recursive,
                                                       bool includeSubfolders)
    {
        StopPresentationTimer();
        targetWindow_ = targetWindow;
        presentationPending_ = false;
        firstBatchPresented_ = false;
        activeRequestId_ = service_.EnumerateFolderAsync(
            targetWindow,
            std::move(folderPath),
            recursive,
            includeSubfolders);
        active_ = true;
        return activeRequestId_;
    }

    void FolderEnumerationCoordinator::Cancel()
    {
        StopPresentationTimer();
        service_.Cancel();
        active_ = false;
    }

    bool FolderEnumerationCoordinator::IsActive() const noexcept
    {
        return active_;
    }

    void FolderEnumerationCoordinator::SchedulePresentation()
    {
        presentationPending_ = true;
        if (presentationTimerId_ == 0 && targetWindow_)
        {
            presentationTimerId_ = SetTimer(
                targetWindow_,
                kPresentationTimerId,
                kPresentationIntervalMs,
                nullptr);
        }
    }

    void FolderEnumerationCoordinator::FlushPresentation(bool clearStartupPathsIfNotFound,
                                                         const PresentationHandler& handler)
    {
        StopPresentationTimer();
        if (!presentationPending_ && !clearStartupPathsIfNotFound)
        {
            return;
        }

        presentationPending_ = false;
        if (!clearStartupPathsIfNotFound && handler && !handler(false))
        {
            SchedulePresentation();
        }
        else if (clearStartupPathsIfNotFound && handler)
        {
            handler(true);
        }
    }

    void FolderEnumerationCoordinator::StopPresentationTimer()
    {
        if (presentationTimerId_ != 0 && targetWindow_)
        {
            KillTimer(targetWindow_, presentationTimerId_);
            presentationTimerId_ = 0;
        }
    }

    void FolderEnumerationCoordinator::HandlePresentationTimer(const PresentationHandler& handler)
    {
        if (presentationTimerId_ != 0)
        {
            FlushPresentation(false, handler);
        }
    }

    void FolderEnumerationCoordinator::HandleMessage(LPARAM lParam, const Handlers& handlers)
    {
        std::unique_ptr<services::FolderEnumerationUpdate> update(
            reinterpret_cast<services::FolderEnumerationUpdate*>(lParam));
        if (!update || update->requestId != activeRequestId_)
        {
            return;
        }

        switch (update->kind)
        {
        case services::FolderEnumerationUpdateKind::Batch:
        {
            const bool isFirstBatch = !firstBatchPresented_;
            firstBatchPresented_ = true;
            if (handlers.onBatch)
            {
                handlers.onBatch(std::move(update->items),
                                 update->totalCount,
                                 update->totalBytes);
            }
            presentationPending_ = true;
            if (isFirstBatch)
            {
                FlushPresentation(false, handlers.onPresentation);
            }
            else
            {
                SchedulePresentation();
            }
            return;
        }
        case services::FolderEnumerationUpdateKind::Completed:
            active_ = false;
            if (handlers.onCompleted)
            {
                handlers.onCompleted(std::move(update->folderPath),
                                     update->totalCount,
                                     update->totalBytes);
            }
            FlushPresentation(true, handlers.onPresentation);
            if (handlers.onSettled)
            {
                handlers.onSettled(true);
            }
            return;
        case services::FolderEnumerationUpdateKind::Failed:
            active_ = false;
            if (handlers.onFailed)
            {
                handlers.onFailed(std::move(update->folderPath), std::move(update->message));
            }
            FlushPresentation(true, handlers.onPresentation);
            if (handlers.onSettled)
            {
                handlers.onSettled(false);
            }
            return;
        default:
            return;
        }
    }
}
