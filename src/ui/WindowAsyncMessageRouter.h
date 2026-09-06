#pragma once

#include <windows.h>

#include <functional>
#include <optional>

namespace hyperbrowse::ui
{
    class WindowAsyncMessageRouter final
    {
    public:
        using NoArgumentHandler = std::function<LRESULT()>;
        using WParamHandler = std::function<LRESULT(WPARAM)>;
        using LParamHandler = std::function<LRESULT(LPARAM)>;
        using WParamLParamHandler = std::function<LRESULT(WPARAM, LPARAM)>;

        struct MessageIds
        {
            UINT externalLaunch{};
            UINT memoryPressureSampled{};
            UINT persistentThumbnailCacheMaintenance{};
            UINT deferredMenuState{};
        };

        struct Handlers
        {
            LParamHandler onFolderEnumeration;
            LParamHandler onFolderTreeEnumeration;
            LParamHandler onFolderWatch;
            WParamLParamHandler onBrowserPaneState;
            WParamLParamHandler onBrowserPaneOpenItem;
            WParamLParamHandler onBrowserPaneContextMenu;
            WParamLParamHandler onBrowserPaneQuickSendDrag;
            LParamHandler onBatchConvert;
            LParamHandler onFileOperation;
            LParamHandler onFileOperationProgress;
            LParamHandler onDetailsPanelThumbnail;
            LParamHandler onViewerZoom;
            LParamHandler onViewerActivity;
            WParamHandler onViewerCurrentItemChanged;
            WParamHandler onViewerDeleteRequested;
            WParamLParamHandler onViewerQuickSendRequest;
            WParamHandler onViewerStartFolderSlideshow;
            WParamHandler onViewerContextMenuCommand;
            LParamHandler onViewerDroppedFile;
            NoArgumentHandler onViewerClosed;
            LParamHandler onExternalLaunch;
            LParamHandler onMemoryPressureSampled;
            WParamHandler onPersistentThumbnailCacheMaintenance;
            NoArgumentHandler onDeferredMenuState;
        };

        WindowAsyncMessageRouter() = default;
        WindowAsyncMessageRouter(const WindowAsyncMessageRouter&) = delete;
        WindowAsyncMessageRouter& operator=(const WindowAsyncMessageRouter&) = delete;

        void Configure(Handlers handlers);
        void Configure(MessageIds messageIds, Handlers handlers);
        std::optional<LRESULT> Handle(UINT message, WPARAM wParam, LPARAM lParam) const;

    private:
        MessageIds messageIds_;
        Handlers handlers_;
    };
}
