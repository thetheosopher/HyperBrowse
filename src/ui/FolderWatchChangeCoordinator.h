#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "services/FolderWatchService.h"

namespace hyperbrowse::ui
{
    class FolderWatchChangeCoordinator
    {
    public:
        using ClearTreePresenceHandler = std::function<void()>;
        using InvalidateTreePresenceHandler = std::function<void(std::wstring_view)>;
        using HasTreeItemHandler = std::function<bool(std::wstring_view)>;
        using RefreshTreeHandler = std::function<void()>;
        using InsertTreeFolderHandler = std::function<void(std::wstring_view)>;
        using ReloadFolderHandler = std::function<void(std::wstring)>;
        using QueueWatchReloadHandler = std::function<void(std::wstring)>;
        using MarkWatchTreeRefreshHandler = std::function<void()>;
        using RefreshBrowserHandler = std::function<void()>;
        using RestoreSelectionHandler = std::function<void(const std::vector<std::wstring>&,
                                                           const std::wstring&)>;
        using UpdatePresentationHandler = std::function<void()>;

        struct Handlers
        {
            ClearTreePresenceHandler onClearTreePresence;
            InvalidateTreePresenceHandler onInvalidateTreePresence;
            HasTreeItemHandler onHasTreeItem;
            RefreshTreeHandler onRefreshTree;
            InsertTreeFolderHandler onInsertTreeFolder;
            ReloadFolderHandler onReloadFolder;
            QueueWatchReloadHandler onQueueWatchReload;
            MarkWatchTreeRefreshHandler onMarkWatchTreeRefresh;
            RefreshBrowserHandler onRefreshBrowser;
            RestoreSelectionHandler onRestoreSelection;
            UpdatePresentationHandler onUpdatePresentation;
        };

        FolderWatchChangeCoordinator() = default;
        FolderWatchChangeCoordinator(const FolderWatchChangeCoordinator&) = delete;
        FolderWatchChangeCoordinator& operator=(const FolderWatchChangeCoordinator&) = delete;

        void Configure(Handlers handlers);
        void Apply(browser::BrowserModel& browserModel,
                   browser::BrowserPane& browserPane,
                   const services::FolderWatchUpdate& update,
                   bool fileOperationActive,
                   bool recursiveBrowsingEnabled,
                   bool showSubfoldersInBrowser);

    private:
        static bool IsExistingDirectory(std::wstring_view path);

        Handlers handlers_;
    };
}
