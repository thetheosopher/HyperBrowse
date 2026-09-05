#include "ui/WindowAsyncMessageRouter.h"

#include <utility>

#include "browser/BrowserPane.h"
#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "services/ThumbnailScheduler.h"
#include "ui/FolderLoadCoordinator.h"
#include "ui/FolderTreeController.h"
#include "viewer/ViewerWindow.h"

namespace hyperbrowse::ui
{
    namespace
    {
        template <typename Handler, typename... Arguments>
        std::optional<LRESULT> Invoke(const Handler& handler, Arguments&&... arguments)
        {
            if (!handler)
            {
                return std::nullopt;
            }

            return handler(std::forward<Arguments>(arguments)...);
        }
    }

    void WindowAsyncMessageRouter::Configure(Handlers handlers)
    {
        handlers_ = std::move(handlers);
    }

    std::optional<LRESULT> WindowAsyncMessageRouter::Handle(
        UINT message,
        WPARAM wParam,
        LPARAM lParam) const
    {
        switch (message)
        {
        case FolderLoadCoordinator::kEnumerationMessageId:
            return Invoke(handlers_.onFolderEnumeration, lParam);
        case FolderTreeController::kEnumerationMessageId:
            return Invoke(handlers_.onFolderTreeEnumeration, lParam);
        case FolderLoadCoordinator::kWatchMessageId:
            return Invoke(handlers_.onFolderWatch, lParam);
        case browser::BrowserPane::kStateChangedMessage:
            return Invoke(handlers_.onBrowserPaneState, wParam, lParam);
        case browser::BrowserPane::kOpenItemMessage:
            return Invoke(handlers_.onBrowserPaneOpenItem, wParam, lParam);
        case browser::BrowserPane::kContextMenuMessage:
            return Invoke(handlers_.onBrowserPaneContextMenu, wParam, lParam);
        case browser::BrowserPane::kQuickSendDragMessage:
            return Invoke(handlers_.onBrowserPaneQuickSendDrag, wParam, lParam);
        case services::BatchConvertService::kMessageId:
            return Invoke(handlers_.onBatchConvert, lParam);
        case services::FileOperationService::kMessageId:
            return Invoke(handlers_.onFileOperation, lParam);
        case services::FileOperationService::kProgressMessageId:
            return Invoke(handlers_.onFileOperationProgress, lParam);
        case services::ThumbnailScheduler::kMessageId:
            return Invoke(handlers_.onDetailsPanelThumbnail, lParam);
        case viewer::ViewerWindow::kZoomChangedMessage:
            return Invoke(handlers_.onViewerZoom, lParam);
        case viewer::ViewerWindow::kActivityChangedMessage:
            return Invoke(handlers_.onViewerActivity, lParam);
        case viewer::ViewerWindow::kCurrentItemChangedMessage:
            return Invoke(handlers_.onViewerCurrentItemChanged, wParam);
        case viewer::ViewerWindow::kDeleteRequestedMessage:
            return Invoke(handlers_.onViewerDeleteRequested, wParam);
        case viewer::ViewerWindow::kQuickSendRequestedMessage:
            return Invoke(handlers_.onViewerQuickSendRequest, wParam, lParam);
        case viewer::ViewerWindow::kStartFolderSlideshowMessage:
            return Invoke(handlers_.onViewerStartFolderSlideshow, wParam);
        case viewer::ViewerWindow::kContextMenuCommandMessage:
            return Invoke(handlers_.onViewerContextMenuCommand, wParam);
        case viewer::ViewerWindow::kDroppedFileMessage:
            return Invoke(handlers_.onViewerDroppedFile, lParam);
        case viewer::ViewerWindow::kClosedMessage:
            return Invoke(handlers_.onViewerClosed);
        default:
            return std::nullopt;
        }
    }
}
