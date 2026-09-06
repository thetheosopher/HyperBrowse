#include "ui/ViewerPendingOperationState.h"

#include <utility>

namespace hyperbrowse::ui
{
    void ViewerPendingOperationState::SetActiveDelete(DeleteRequest request)
    {
        activeDelete_ = std::move(request);
    }

    std::optional<ViewerPendingOperationState::DeleteRequest> ViewerPendingOperationState::TakeActiveDelete()
    {
        std::optional<DeleteRequest> result = std::move(activeDelete_);
        activeDelete_.reset();
        return result;
    }

    ViewerPendingOperationState::DeleteRequest* ViewerPendingOperationState::ActiveDelete() noexcept
    {
        return activeDelete_ ? &*activeDelete_ : nullptr;
    }

    bool ViewerPendingOperationState::HasActiveDelete() const noexcept
    {
        return activeDelete_.has_value();
    }

    void ViewerPendingOperationState::QueueDelete(DeleteRequest request)
    {
        queuedDeletes_.push_back(std::move(request));
    }

    std::optional<ViewerPendingOperationState::DeleteRequest> ViewerPendingOperationState::TakeNextDelete()
    {
        if (queuedDeletes_.empty())
        {
            return std::nullopt;
        }

        DeleteRequest result = std::move(queuedDeletes_.front());
        queuedDeletes_.pop_front();
        return result;
    }

    bool ViewerPendingOperationState::HasQueuedDeletes() const noexcept
    {
        return !queuedDeletes_.empty();
    }

    void ViewerPendingOperationState::ClearQueuedDeletes() noexcept
    {
        queuedDeletes_.clear();
    }

    void ViewerPendingOperationState::SetQuickSend(QuickSendRequest request)
    {
        request.active = true;
        quickSend_ = std::move(request);
    }

    std::optional<ViewerPendingOperationState::QuickSendRequest> ViewerPendingOperationState::TakeQuickSend()
    {
        std::optional<QuickSendRequest> result = std::move(quickSend_);
        quickSend_.reset();
        return result;
    }

    ViewerPendingOperationState::QuickSendRequest* ViewerPendingOperationState::ActiveQuickSend() noexcept
    {
        return quickSend_ ? &*quickSend_ : nullptr;
    }

    bool ViewerPendingOperationState::HasActiveQuickSend() const noexcept
    {
        return quickSend_.has_value();
    }

    void ViewerPendingOperationState::ClearQuickSend() noexcept
    {
        quickSend_.reset();
    }

    void ViewerPendingOperationState::Clear() noexcept
    {
        activeDelete_.reset();
        queuedDeletes_.clear();
        quickSend_.reset();
    }
}
