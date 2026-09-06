#include "ui/ViewCommandController.h"

#include <utility>

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

    void ViewCommandController::Configure(Handlers handlers)
    {
        handlers_ = std::move(handlers);
    }

    bool ViewCommandController::Handle(UINT commandId) const
    {
        if (commandId >= ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM && commandId <= ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE)
        {
            return Invoke(handlers_.onViewerMouseWheelBehavior, commandId);
        }

        if (commandId >= ID_VIEW_APP_TEXT_SIZE_SMALL && commandId <= ID_VIEW_APP_TEXT_SIZE_LARGE)
        {
            return Invoke(handlers_.onAppTextSize, commandId);
        }

        if (commandId >= ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL && commandId <= ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE)
        {
            return Invoke(handlers_.onViewerOverlayTextSize, commandId);
        }

        if (commandId >= ID_FILE_SET_RATING_0 && commandId <= ID_FILE_SET_RATING_5)
        {
            return Invoke(handlers_.onRating, commandId);
        }

        switch (commandId)
        {
        case ID_VIEW_THUMBNAILS:
            return Invoke(handlers_.onThumbnails);
        case ID_VIEW_DETAILS:
            return Invoke(handlers_.onDetails);
        case ID_ACTION_SORT_MENU:
            return Invoke(handlers_.onSortMenu);
        case ID_ACTION_THUMBNAIL_SIZE_MENU:
            return Invoke(handlers_.onThumbnailSizeMenu);
        case ID_ACTION_THEME_MENU:
            return Invoke(handlers_.onThemeMenu);
        case ID_VIEW_RECURSIVE:
            return Invoke(handlers_.onRecursive);
        case ID_VIEW_SHOW_SUBFOLDERS:
            return Invoke(handlers_.onShowSubfolders);
        case ID_VIEW_SETTINGS:
            return Invoke(handlers_.onSettings);
        case ID_FILE_ASSOCIATIONS:
            return Invoke(handlers_.onAssociations);
        case ID_VIEW_NVJPEG_ACCELERATION:
            return Invoke(handlers_.onNvJpeg);
        case ID_VIEW_LIBRAW_OUT_OF_PROCESS:
            return Invoke(handlers_.onLibRaw);
        case ID_VIEW_PERSISTENT_THUMBNAIL_CACHE:
            return Invoke(handlers_.onPersistentThumbnailCache);
        case ID_VIEW_PERSISTENT_THUMBNAIL_CACHE_MANAGER:
            return Invoke(handlers_.onPersistentThumbnailCacheManager);
        case ID_VIEW_SINGLE_INSTANCE:
            return Invoke(handlers_.onSingleInstance);
        case ID_VIEW_PAIRED_RAW_JPEG_PREFER_JPEG:
            return Invoke(handlers_.onPreferJpeg);
        case ID_VIEW_PAIRED_RAW_JPEG_PREFER_RAW:
            return Invoke(handlers_.onPreferRaw);
        case ID_VIEW_DEFAULT_VIEWER_SECONDARY_MONITOR:
            return Invoke(handlers_.onDefaultViewerSecondaryMonitor);
        case ID_VIEW_USE_SLIDESHOW_TRANSITION:
            return Invoke(handlers_.onUseSlideshowTransition);
        case ID_VIEW_THUMBNAIL_SIZE_96:
        case ID_VIEW_THUMBNAIL_SIZE_128:
        case ID_VIEW_THUMBNAIL_SIZE_160:
        case ID_VIEW_THUMBNAIL_SIZE_192:
        case ID_VIEW_THUMBNAIL_SIZE_256:
        case ID_VIEW_THUMBNAIL_SIZE_320:
        case ID_VIEW_THUMBNAIL_SIZE_360:
        case ID_VIEW_THUMBNAIL_SIZE_420:
        case ID_VIEW_THUMBNAIL_SIZE_480:
        case ID_VIEW_THUMBNAIL_SIZE_560:
        case ID_VIEW_THUMBNAIL_SIZE_640:
            return Invoke(handlers_.onThumbnailSizePreset, commandId);
        case ID_VIEW_THUMBNAIL_SIZE_INCREASE:
            return Invoke(handlers_.onThumbnailSizeIncrease);
        case ID_VIEW_THUMBNAIL_SIZE_DECREASE:
            return Invoke(handlers_.onThumbnailSizeDecrease);
        case ID_VIEW_THUMBNAIL_DETAILS:
            return Invoke(handlers_.onThumbnailDetails);
        case ID_VIEW_THUMBNAIL_LAYOUT_COMPACT:
            return Invoke(handlers_.onThumbnailLayoutCompact);
        case ID_VIEW_DETAILS_STRIP:
            return Invoke(handlers_.onDetailsStrip);
        case ID_VIEW_SORT_FILENAME:
        case ID_VIEW_SORT_MODIFIED:
        case ID_VIEW_SORT_SIZE:
        case ID_VIEW_SORT_DIMENSIONS:
        case ID_VIEW_SORT_TYPE:
        case ID_VIEW_SORT_DATETAKEN:
        case ID_VIEW_SORT_RATING:
        case ID_VIEW_SORT_TAGS:
        case ID_VIEW_SORT_RANDOM:
            return Invoke(handlers_.onSortMode, commandId);
        case ID_VIEW_SORT_DIRECTION:
            return Invoke(handlers_.onSortDirection);
        case ID_VIEW_THEME_LIGHT:
            return Invoke(handlers_.onThemeLight);
        case ID_VIEW_THEME_DARK:
            return Invoke(handlers_.onThemeDark);
        case ID_VIEW_VIEWER_DETAIL_OVERLAYS:
            return Invoke(handlers_.onViewerDetailOverlays);
        case ID_VIEW_VIEWER_FULL_METADATA:
            return Invoke(handlers_.onViewerFullMetadata);
        case ID_VIEW_PRESSURE_STATE_STATUS:
            return Invoke(handlers_.onPressureStateStatus);
        case ID_VIEW_SLIDESHOW_SELECTION:
            return Invoke(handlers_.onSlideshowSelection);
        case ID_VIEW_SLIDESHOW_FOLDER:
            return Invoke(handlers_.onSlideshowFolder);
        case ID_HELP_USER_GUIDE:
            return Invoke(handlers_.onUserGuide);
        case ID_HELP_KEYBOARD_SHORTCUTS:
            return Invoke(handlers_.onKeyboardShortcuts);
        case ID_HELP_ABOUT:
            return Invoke(handlers_.onAbout);
        case ID_HELP_PERFORMANCE_SETTINGS:
            return Invoke(handlers_.onPerformanceSettings);
        case ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE:
        case ID_HELP_PERFORMANCE_PROFILE_BALANCED:
        case ID_HELP_PERFORMANCE_PROFILE_PERFORMANCE:
        case ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE:
            return Invoke(handlers_.onPerformanceProfile, commandId);
        case ID_HELP_DIAGNOSTICS_SNAPSHOT:
            return Invoke(handlers_.onDiagnosticsSnapshot);
        case ID_HELP_DIAGNOSTICS_EXPORT:
            return Invoke(handlers_.onDiagnosticsExport);
        case ID_HELP_DIAGNOSTICS_RESET:
            return Invoke(handlers_.onDiagnosticsReset);
        default:
            return false;
        }
    }
}
