#include "ui/FileCommandController.h"

#include <utility>

#include "services/BatchConvertService.h"
#include "ui/CommandIds.h"

namespace hyperbrowse::ui
{
    namespace
    {
        template <typename Handler, typename... Arguments>
        bool Invoke(const Handler& handler, Arguments&&... arguments)
        {
            if (handler)
            {
                handler(std::forward<Arguments>(arguments)...);
            }
            return true;
        }
    }

    using namespace command_ids;

    void FileCommandController::Configure(Handlers handlers)
    {
        handlers_ = std::move(handlers);
    }

    bool FileCommandController::Handle(UINT commandId) const
    {
        if (commandId >= ID_FILE_OPEN_RECENT_FOLDER_BASE && commandId <= ID_FILE_OPEN_RECENT_FOLDER_LAST)
        {
            return Invoke(handlers_.onOpenRecentFolder,
                          static_cast<std::size_t>(commandId - ID_FILE_OPEN_RECENT_FOLDER_BASE));
        }

        if (commandId >= ID_FILE_COPY_SELECTION_FAVORITE_BASE && commandId <= ID_FILE_COPY_SELECTION_FAVORITE_LAST)
        {
            return Invoke(handlers_.onCopySelectionToFavorite,
                          static_cast<std::size_t>(commandId - ID_FILE_COPY_SELECTION_FAVORITE_BASE));
        }

        if (commandId >= ID_FILE_COPY_SELECTION_RECENT_BASE && commandId <= ID_FILE_COPY_SELECTION_RECENT_LAST)
        {
            return Invoke(handlers_.onCopySelectionToRecent,
                          static_cast<std::size_t>(commandId - ID_FILE_COPY_SELECTION_RECENT_BASE));
        }

        if (commandId >= ID_FILE_MOVE_SELECTION_FAVORITE_BASE && commandId <= ID_FILE_MOVE_SELECTION_FAVORITE_LAST)
        {
            return Invoke(handlers_.onMoveSelectionToFavorite,
                          static_cast<std::size_t>(commandId - ID_FILE_MOVE_SELECTION_FAVORITE_BASE));
        }

        if (commandId >= ID_FILE_MOVE_SELECTION_RECENT_BASE && commandId <= ID_FILE_MOVE_SELECTION_RECENT_LAST)
        {
            return Invoke(handlers_.onMoveSelectionToRecent,
                          static_cast<std::size_t>(commandId - ID_FILE_MOVE_SELECTION_RECENT_BASE));
        }

        switch (commandId)
        {
        case ID_FILE_OPEN_FOLDER:
            return Invoke(handlers_.onOpenFolder);
        case ID_VIEW_NAVIGATE_BACK_FOLDER:
            return Invoke(handlers_.onNavigateBackFolder);
        case ID_VIEW_NAVIGATE_FORWARD_FOLDER:
            return Invoke(handlers_.onNavigateForwardFolder);
        case ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION:
            return Invoke(handlers_.onToggleCurrentFolderFavorite);
        case ID_FILE_CLEAR_FAVORITE_DESTINATIONS:
            return Invoke(handlers_.onClearFavoriteDestinations);
        case ID_FILE_CLEAR_RECENT_FOLDERS:
            return Invoke(handlers_.onClearRecentFolders);
        case ID_FILE_CLEAR_RECENT_DESTINATIONS:
            return Invoke(handlers_.onClearRecentDestinations);
        case ID_FILE_EXIT:
            return Invoke(handlers_.onExit);
        case ID_FILE_ESCAPE:
            return Invoke(handlers_.onEscape);
        case ID_FILE_MINIMIZE:
            return Invoke(handlers_.onMinimize);
        case ID_EDIT_CLOSE_MAIN_WINDOW_ON_ESCAPE:
            return Invoke(handlers_.onToggleCloseMainWindowOnEscape);
        case ID_FILE_REFRESH_TREE:
            return Invoke(handlers_.onRefreshTree);
        case ID_FILE_OPEN_SELECTED:
            return Invoke(handlers_.onOpenSelected);
        case ID_FILE_COMPARE_SELECTED:
            return Invoke(handlers_.onCompareSelected);
        case ID_FILE_VIEW_ON_SECONDARY_MONITOR:
            return Invoke(handlers_.onViewOnSecondaryMonitor);
        case ID_FILE_IMAGE_INFORMATION:
            return Invoke(handlers_.onImageInformation);
        case ID_FILE_QUICK_SEND_MOVE:
            return Invoke(handlers_.onQuickSendMove);
        case ID_FILE_QUICK_SEND_COPY:
            return Invoke(handlers_.onQuickSendCopy);
        case ID_FILE_COPY_SELECTION:
        case ID_FILE_COPY_SELECTION_BROWSE:
            return Invoke(handlers_.onCopySelection);
        case ID_FILE_RENAME_SELECTED:
            return Invoke(handlers_.onRenameSelected);
        case ID_FILE_BATCH_RENAME_SELECTION:
            return Invoke(handlers_.onBatchRenameSelection);
        case ID_FILE_MOVE_SELECTION:
        case ID_FILE_MOVE_SELECTION_BROWSE:
            return Invoke(handlers_.onMoveSelection);
        case ID_FILE_MOVE_SELECTION_TO_NEW_CHILD_FOLDER:
            return Invoke(handlers_.onMoveSelectionToNewChildFolder);
        case ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS:
            return Invoke(handlers_.onTogglePairedRawJpeg);
        case ID_FILE_DELETE_SELECTION:
            return Invoke(handlers_.onDeleteSelection, false);
        case ID_FILE_DELETE_SELECTION_PERMANENT:
            return Invoke(handlers_.onDeleteSelection, true);
        case ID_FILE_REVEAL_IN_EXPLORER:
            return Invoke(handlers_.onRevealInExplorer);
        case ID_FILE_OPEN_CONTAINING_FOLDER:
            return Invoke(handlers_.onOpenContainingFolder);
        case ID_FILE_COPY_PATH:
            return Invoke(handlers_.onCopyPath);
        case ID_FILE_COPY_FILES_TO_CLIPBOARD:
            return Invoke(handlers_.onCopyFiles);
        case ID_EDIT_CUT:
            return Invoke(handlers_.onCut);
        case ID_FILE_COPY_IMAGE_PIXELS:
            return Invoke(handlers_.onCopyImagePixels);
        case ID_FILE_PASTE_FILES:
            return Invoke(handlers_.onPasteFiles);
        case ID_EDIT_UNDO:
            return Invoke(handlers_.onUndo);
        case ID_EDIT_REDO:
            return Invoke(handlers_.onRedo);
        case ID_FILE_DUPLICATE_SELECTION:
            return Invoke(handlers_.onDuplicateSelection);
        case ID_FILE_SELECT_ALL:
            return Invoke(handlers_.onSelectAll);
        case ID_FILE_PROPERTIES:
            return Invoke(handlers_.onProperties);
        case ID_FILE_EDIT_TAGS:
            return Invoke(handlers_.onEditTags);
        case ID_FILE_ROTATE_JPEG_LEFT:
            return Invoke(handlers_.onRotateJpeg, -1);
        case ID_FILE_ROTATE_JPEG_RIGHT:
            return Invoke(handlers_.onRotateJpeg, +1);
        case ID_FILE_BATCH_CONVERT_SELECTION_JPEG:
            return Invoke(handlers_.onBatchConvert, true, services::BatchConvertFormat::Jpeg);
        case ID_FILE_BATCH_CONVERT_SELECTION_PNG:
            return Invoke(handlers_.onBatchConvert, true, services::BatchConvertFormat::Png);
        case ID_FILE_BATCH_CONVERT_SELECTION_TIFF:
            return Invoke(handlers_.onBatchConvert, true, services::BatchConvertFormat::Tiff);
        case ID_FILE_BATCH_CONVERT_FOLDER_JPEG:
            return Invoke(handlers_.onBatchConvert, false, services::BatchConvertFormat::Jpeg);
        case ID_FILE_BATCH_CONVERT_FOLDER_PNG:
            return Invoke(handlers_.onBatchConvert, false, services::BatchConvertFormat::Png);
        case ID_FILE_BATCH_CONVERT_FOLDER_TIFF:
            return Invoke(handlers_.onBatchConvert, false, services::BatchConvertFormat::Tiff);
        case ID_FILE_BATCH_CONVERT_CANCEL:
            return Invoke(handlers_.onCancelBatchConvert);
        default:
            return false;
        }
    }
}
