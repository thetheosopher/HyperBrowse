#pragma once

#include <windows.h>

#include <functional>

namespace hyperbrowse::ui
{
    class ViewCommandController final
    {
    public:
        using CommandHandler = std::function<void()>;
        using CommandIdHandler = std::function<void(UINT)>;

        struct Handlers
        {
            CommandIdHandler onViewerMouseWheelBehavior;
            CommandIdHandler onAppTextSize;
            CommandIdHandler onViewerOverlayTextSize;
            CommandIdHandler onRating;
            CommandHandler onThumbnails;
            CommandHandler onDetails;
            CommandHandler onSortMenu;
            CommandHandler onThumbnailSizeMenu;
            CommandHandler onThemeMenu;
            CommandHandler onRecursive;
            CommandHandler onShowSubfolders;
            CommandHandler onSettings;
            CommandHandler onAssociations;
            CommandHandler onNvJpeg;
            CommandHandler onLibRaw;
            CommandHandler onPersistentThumbnailCache;
            CommandHandler onPersistentThumbnailCacheManager;
            CommandHandler onSingleInstance;
            CommandHandler onPreferJpeg;
            CommandHandler onPreferRaw;
            CommandHandler onDefaultViewerSecondaryMonitor;
            CommandHandler onUseSlideshowTransition;
            CommandIdHandler onThumbnailSizePreset;
            CommandHandler onThumbnailSizeIncrease;
            CommandHandler onThumbnailSizeDecrease;
            CommandHandler onThumbnailDetails;
            CommandHandler onThumbnailLayoutCompact;
            CommandHandler onDetailsStrip;
            CommandIdHandler onSortMode;
            CommandHandler onSortDirection;
            CommandHandler onThemeLight;
            CommandHandler onThemeDark;
            CommandHandler onViewerDetailOverlays;
            CommandHandler onViewerFullMetadata;
            CommandHandler onPressureStateStatus;
            CommandHandler onSlideshowSelection;
            CommandHandler onSlideshowFolder;
            CommandHandler onUserGuide;
            CommandHandler onKeyboardShortcuts;
            CommandHandler onAbout;
            CommandHandler onPerformanceSettings;
            CommandIdHandler onPerformanceProfile;
            CommandHandler onDiagnosticsSnapshot;
            CommandHandler onDiagnosticsExport;
            CommandHandler onDiagnosticsReset;
        };

        ViewCommandController() = default;
        ViewCommandController(const ViewCommandController&) = delete;
        ViewCommandController& operator=(const ViewCommandController&) = delete;

        void Configure(Handlers handlers);
        bool Handle(UINT commandId) const;

    private:
        Handlers handlers_;
    };
}
