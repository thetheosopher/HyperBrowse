#pragma once

#include <windows.h>

#include <functional>

namespace hyperbrowse::services
{
    enum class BatchConvertFormat;
}

namespace hyperbrowse::ui
{
    class FileCommandController final
    {
    public:
        using CommandHandler = std::function<void()>;
        using BoolHandler = std::function<void(bool)>;
        using IntHandler = std::function<void(int)>;
        using BatchConvertHandler = std::function<void(bool, services::BatchConvertFormat)>;

        struct Handlers
        {
            CommandHandler onOpenFolder;
            CommandHandler onNavigateBackFolder;
            CommandHandler onNavigateForwardFolder;
            CommandHandler onToggleCurrentFolderFavorite;
            CommandHandler onClearFavoriteDestinations;
            CommandHandler onClearRecentFolders;
            CommandHandler onClearRecentDestinations;
            CommandHandler onExit;
            CommandHandler onEscape;
            CommandHandler onMinimize;
            CommandHandler onToggleCloseMainWindowOnEscape;
            CommandHandler onRefreshTree;
            CommandHandler onOpenSelected;
            CommandHandler onCompareSelected;
            CommandHandler onViewOnSecondaryMonitor;
            CommandHandler onImageInformation;
            CommandHandler onQuickSendMove;
            CommandHandler onQuickSendCopy;
            CommandHandler onCopySelection;
            CommandHandler onRenameSelected;
            CommandHandler onBatchRenameSelection;
            CommandHandler onMoveSelection;
            CommandHandler onMoveSelectionToNewChildFolder;
            CommandHandler onTogglePairedRawJpeg;
            BoolHandler onDeleteSelection;
            CommandHandler onRevealInExplorer;
            CommandHandler onOpenContainingFolder;
            CommandHandler onCopyPath;
            CommandHandler onCopyFiles;
            CommandHandler onCut;
            CommandHandler onCopyImagePixels;
            CommandHandler onPasteFiles;
            CommandHandler onUndo;
            CommandHandler onRedo;
            CommandHandler onDuplicateSelection;
            CommandHandler onSelectAll;
            CommandHandler onProperties;
            CommandHandler onEditTags;
            IntHandler onRotateJpeg;
            BatchConvertHandler onBatchConvert;
            CommandHandler onCancelBatchConvert;
        };

        FileCommandController() = default;
        FileCommandController(const FileCommandController&) = delete;
        FileCommandController& operator=(const FileCommandController&) = delete;

        void Configure(Handlers handlers);
        bool Handle(UINT commandId) const;

    private:
        Handlers handlers_;
    };
}