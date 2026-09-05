#include "ui/FolderWatchChangeCoordinator.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::size_t kIncrementalFolderWatchEventLimit = 64;

        bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
        {
            return _wcsicmp(std::wstring(lhs).c_str(), std::wstring(rhs).c_str()) == 0;
        }
    }

    void FolderWatchChangeCoordinator::Configure(Handlers handlers)
    {
        handlers_ = std::move(handlers);
    }

    void FolderWatchChangeCoordinator::Apply(browser::BrowserModel& browserModel,
                                              browser::BrowserPane& browserPane,
                                              const services::FolderWatchUpdate& update,
                                              bool fileOperationActive,
                                              bool recursiveBrowsingEnabled,
                                              bool showSubfoldersInBrowser)
    {
        if (update.requiresFullReload)
        {
            if (handlers_.onClearTreePresence)
            {
                handlers_.onClearTreePresence();
            }
        }
        for (const services::FolderWatchEvent& event : update.events)
        {
            if (handlers_.onInvalidateTreePresence)
            {
                handlers_.onInvalidateTreePresence(event.path);
                handlers_.onInvalidateTreePresence(event.oldPath);
            }
        }

        const auto reloadFolderPreservingSelection = [&](std::wstring folderPath)
        {
            if (handlers_.onReloadFolder)
            {
                handlers_.onReloadFolder(std::move(folderPath));
            }
        };

        if (fileOperationActive)
        {
            // Watch notifications raised while our own file operation is running are
            // normally just echoes of that operation. Defer only changes too large
            // or ambiguous to reconstruct from the operation result.
            if (update.requiresFullReload || update.events.size() >= kIncrementalFolderWatchEventLimit)
            {
                if (handlers_.onQueueWatchReload)
                {
                    handlers_.onQueueWatchReload(
                        update.folderPath.empty() ? browserModel.FolderPath() : update.folderPath);
                }
            }

            const auto treeRefreshNeededForPath = [&](std::wstring_view path)
            {
                return !path.empty()
                    && handlers_.onHasTreeItem
                    && handlers_.onHasTreeItem(path);
            };
            const bool treeRefreshNeeded = update.requiresFullReload
                || std::any_of(update.events.begin(), update.events.end(), [&](const services::FolderWatchEvent& event)
                {
                    switch (event.kind)
                    {
                    case services::FolderWatchEventKind::Added:
                        return IsExistingDirectory(event.path);
                    case services::FolderWatchEventKind::Removed:
                        return treeRefreshNeededForPath(event.path);
                    case services::FolderWatchEventKind::Renamed:
                        return treeRefreshNeededForPath(event.oldPath)
                            || IsExistingDirectory(event.path);
                    case services::FolderWatchEventKind::Modified:
                    default:
                        return false;
                    }
                });
            if (treeRefreshNeeded && handlers_.onMarkWatchTreeRefresh)
            {
                handlers_.onMarkWatchTreeRefresh();
            }
            return;
        }

        if (update.requiresFullReload)
        {
            if (handlers_.onRefreshTree)
            {
                handlers_.onRefreshTree();
            }
            reloadFolderPreservingSelection(
                update.folderPath.empty() ? browserModel.FolderPath() : update.folderPath);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPane.SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPane.FocusedFilePathSnapshot();
        std::vector<std::wstring> invalidatedPaths;
        std::vector<std::wstring> foldersToInsertIntoTree;
        bool changed = false;
        bool refreshFolderTree = false;
        bool preferAsyncReload = update.events.size() >= kIncrementalFolderWatchEventLimit;

        auto upsertFromPath = [&](const std::wstring& path)
        {
            std::error_code error;
            const fs::path watchedPath(path);
            if (fs::is_regular_file(watchedPath, error) && !error)
            {
                if (browser::IsSupportedImageExtension(watchedPath.extension().wstring()))
                {
                    changed = browserModel.UpsertItem(browser::BuildBrowserItemFromPath(watchedPath)) || changed;
                    invalidatedPaths.push_back(path);
                }
                return;
            }

            if (!fs::is_directory(watchedPath, error) || error)
            {
                return;
            }

            if (showSubfoldersInBrowser
                && FolderPathsEqual(watchedPath.parent_path().wstring(), browserModel.FolderPath()))
            {
                changed = browserModel.UpsertItem(browser::BuildBrowserItemFromPath(watchedPath)) || changed;
            }

            if (recursiveBrowsingEnabled)
            {
                preferAsyncReload = true;
            }
        };

        for (const services::FolderWatchEvent& event : update.events)
        {
            if (event.kind == services::FolderWatchEventKind::Added && IsExistingDirectory(event.path))
            {
                foldersToInsertIntoTree.push_back(event.path);
                if (recursiveBrowsingEnabled)
                {
                    preferAsyncReload = true;
                }
            }

            if (event.kind == services::FolderWatchEventKind::Removed
                && handlers_.onHasTreeItem
                && handlers_.onHasTreeItem(event.path))
            {
                refreshFolderTree = true;
            }

            if (event.kind == services::FolderWatchEventKind::Renamed)
            {
                if (!event.oldPath.empty()
                    && handlers_.onHasTreeItem
                    && handlers_.onHasTreeItem(event.oldPath))
                {
                    refreshFolderTree = true;
                }

                if (IsExistingDirectory(event.path))
                {
                    foldersToInsertIntoTree.push_back(event.path);
                    if (recursiveBrowsingEnabled)
                    {
                        preferAsyncReload = true;
                    }
                }
            }

            switch (event.kind)
            {
            case services::FolderWatchEventKind::Added:
            case services::FolderWatchEventKind::Modified:
                upsertFromPath(event.path);
                break;
            case services::FolderWatchEventKind::Removed:
                changed = browserModel.RemoveItemByPath(event.path) || changed;
                changed = browserModel.RemoveItemsByPathPrefix(event.path) || changed;
                invalidatedPaths.push_back(event.path);
                break;
            case services::FolderWatchEventKind::Renamed:
            {
                const bool renamed = browserModel.ReplacePathPrefix(event.oldPath, event.path);
                changed = renamed || changed;
                invalidatedPaths.push_back(event.oldPath);
                invalidatedPaths.push_back(event.path);
                if (!renamed)
                {
                    changed = browserModel.RemoveItemByPath(event.oldPath) || changed;
                    changed = browserModel.RemoveItemsByPathPrefix(event.oldPath) || changed;
                    upsertFromPath(event.path);
                }
                break;
            }
            default:
                break;
            }
        }

        const auto updateTree = [&]
        {
            if (refreshFolderTree)
            {
                if (handlers_.onRefreshTree)
                {
                    handlers_.onRefreshTree();
                }
            }
            else if (handlers_.onInsertTreeFolder)
            {
                for (const std::wstring& folderPath : foldersToInsertIntoTree)
                {
                    handlers_.onInsertTreeFolder(folderPath);
                }
            }
        };

        if (preferAsyncReload)
        {
            updateTree();
            reloadFolderPreservingSelection(browserModel.FolderPath());
            return;
        }

        if (!changed && invalidatedPaths.empty())
        {
            updateTree();
            return;
        }

        browserPane.InvalidateMediaCacheForPaths(invalidatedPaths);
        if (handlers_.onRefreshBrowser)
        {
            handlers_.onRefreshBrowser();
        }
        if (handlers_.onRestoreSelection)
        {
            handlers_.onRestoreSelection(selectedPaths, focusedPath);
        }
        updateTree();
        if (handlers_.onUpdatePresentation)
        {
            handlers_.onUpdatePresentation();
        }
    }

    bool FolderWatchChangeCoordinator::IsExistingDirectory(std::wstring_view path)
    {
        if (path.empty())
        {
            return false;
        }

        std::error_code error;
        return fs::is_directory(fs::path(path), error) && !error;
    }
}
