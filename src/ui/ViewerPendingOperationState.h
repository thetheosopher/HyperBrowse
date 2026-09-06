#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace hyperbrowse::services
{
    enum class FileOperationType : int;
}

namespace hyperbrowse::ui
{
    class ViewerPendingOperationState final
    {
    public:
        struct DeleteRequest
        {
            std::wstring sourcePath;
            std::vector<std::wstring> sourcePaths;
            std::wstring preferredFocusPath;
            bool permanent{};
        };

        struct QuickSendRequest
        {
            services::FileOperationType type{static_cast<services::FileOperationType>(0)};
            std::wstring sourcePath;
            std::vector<std::wstring> sourcePaths;
            std::wstring destinationFolder;
            bool viewerAdvanced{};
            bool active{};
        };

        void SetActiveDelete(DeleteRequest request);
        std::optional<DeleteRequest> TakeActiveDelete();
        DeleteRequest* ActiveDelete() noexcept;
        bool HasActiveDelete() const noexcept;

        void QueueDelete(DeleteRequest request);
        std::optional<DeleteRequest> TakeNextDelete();
        bool HasQueuedDeletes() const noexcept;
        void ClearQueuedDeletes() noexcept;

        void SetQuickSend(QuickSendRequest request);
        std::optional<QuickSendRequest> TakeQuickSend();
        QuickSendRequest* ActiveQuickSend() noexcept;
        bool HasActiveQuickSend() const noexcept;
        void ClearQuickSend() noexcept;

        void Clear() noexcept;

    private:
        std::optional<DeleteRequest> activeDelete_;
        std::deque<DeleteRequest> queuedDeletes_;
        std::optional<QuickSendRequest> quickSend_;
    };
}
