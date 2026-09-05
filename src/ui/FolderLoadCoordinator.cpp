#include "ui/FolderLoadCoordinator.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace hyperbrowse::ui
{
    namespace
    {
        std::wstring NormalizeFolderPath(std::wstring path)
        {
            std::replace(path.begin(), path.end(), L'/', L'\\');
            while (path.size() > 3 && !path.empty() && path.back() == L'\\')
            {
                path.pop_back();
            }
            return path;
        }

        bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
        {
            return _wcsicmp(std::wstring(lhs).c_str(), std::wstring(rhs).c_str()) == 0;
        }

        std::wstring FindExistingFolderAncestor(fs::path candidate)
        {
            std::error_code error;
            while (!candidate.empty())
            {
                if (fs::is_directory(candidate, error) && !error)
                {
                    return NormalizeFolderPath(candidate.wstring());
                }

                error.clear();
                const fs::path parent = candidate.parent_path();
                if (parent == candidate)
                {
                    break;
                }

                candidate = parent;
            }

            return {};
        }
    }

    void FolderLoadCoordinator::Configure(HWND targetWindow, Handlers handlers)
    {
        targetWindow_ = targetWindow;
        handlers_ = std::move(handlers);
    }

    void FolderLoadCoordinator::Start(std::wstring folderPath,
                                      bool historyNavigation,
                                      bool recursive,
                                      bool includeSubfolders)
    {
        if (!targetWindow_ || folderPath.empty())
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        if (!historyNavigation)
        {
            history_.CancelPendingNavigation();
        }

        StopFolderWatch();
        pendingFolderReloadSelectionPaths_.clear();
        pendingFolderReloadFocusedPath_.clear();
        viewerOpenedBeforeFolderEnumerationCompleted_ = false;
        recursiveBrowsingEnabled_ = recursive;
        if (handlers_.onLoadStarted)
        {
            handlers_.onLoadStarted(folderPath);
        }

        enumerationCoordinator_.Start(
            targetWindow_,
            folderPath,
            recursive,
            includeSubfolders);
        if (handlers_.onLoadQueued)
        {
            handlers_.onLoadQueued(folderPath);
        }
    }

    void FolderLoadCoordinator::Cancel()
    {
        enumerationCoordinator_.Cancel();
        StopFolderWatch();
    }

    bool FolderLoadCoordinator::IsEnumerationActive() const noexcept
    {
        return enumerationCoordinator_.IsActive();
    }
    void FolderLoadCoordinator::SetPendingStartupSelectionPath(std::wstring path)
    {
        pendingStartupSelectionPath_ = std::move(path);
    }

    void FolderLoadCoordinator::SetPendingStartupViewerPath(std::wstring path)
    {
        pendingStartupViewerPath_ = std::move(path);
    }

    void FolderLoadCoordinator::ClearPendingStartupPaths()
    {
        pendingStartupViewerPath_.clear();
        pendingStartupSelectionPath_.clear();
    }

    void FolderLoadCoordinator::ClearPendingStartupViewerPath()
    {
        pendingStartupViewerPath_.clear();
    }

    bool FolderLoadCoordinator::HasPendingStartupViewerPath() const noexcept
    {
        return !pendingStartupViewerPath_.empty();
    }

    void FolderLoadCoordinator::SetPendingFolderReloadSelection(
        std::vector<std::wstring> selectionPaths,
        std::wstring focusedPath)
    {
        pendingFolderReloadSelectionPaths_ = std::move(selectionPaths);
        pendingFolderReloadFocusedPath_ = std::move(focusedPath);
    }

    void FolderLoadCoordinator::RestorePendingPresentation(bool clearStartupPathsIfNotFound)
    {
        if (!pendingStartupSelectionPath_.empty()
            && pendingStartupViewerPath_.empty()
            && handlers_.onRestoreSelection)
        {
            const std::vector<std::wstring> selectionPaths{pendingStartupSelectionPath_};
            if (handlers_.onRestoreSelection(
                selectionPaths,
                pendingStartupSelectionPath_,
                clearStartupPathsIfNotFound,
                true))
            {
                pendingStartupSelectionPath_.clear();
            }
        }

        if (!pendingFolderReloadSelectionPaths_.empty()
            && pendingStartupViewerPath_.empty()
            && handlers_.onRestoreSelection
            && handlers_.onRestoreSelection(
                pendingFolderReloadSelectionPaths_,
                pendingFolderReloadFocusedPath_,
                clearStartupPathsIfNotFound,
                false))
        {
            pendingFolderReloadSelectionPaths_.clear();
            pendingFolderReloadFocusedPath_.clear();
        }

        if (!pendingStartupViewerPath_.empty() && handlers_.onOpenViewer)
        {
            const PendingViewerResult result = handlers_.onOpenViewer(
                pendingStartupViewerPath_,
                clearStartupPathsIfNotFound);
            if (result.consumed)
            {
                pendingStartupViewerPath_.clear();
            }
            viewerOpenedBeforeFolderEnumerationCompleted_ = result.opened;
        }
    }

    bool FolderLoadCoordinator::CanNavigateBack() const noexcept
    {
        return history_.CanNavigateBack();
    }

    bool FolderLoadCoordinator::CanNavigateForward() const noexcept
    {
        return history_.CanNavigateForward();
    }

    bool FolderLoadCoordinator::HasPendingNavigation() const noexcept
    {
        return history_.HasPendingNavigation();
    }

    bool FolderLoadCoordinator::NavigateBack(std::wstring currentFolderPath,
                                              bool recursive,
                                              bool includeSubfolders)
    {
        const auto navigation = history_.FindBack(
            NormalizeFolderPath(std::move(currentFolderPath)),
            [](std::wstring_view path) { return FindExistingFolderAncestor(fs::path(path)); },
            [](std::wstring_view lhs, std::wstring_view rhs) { return FolderPathsEqual(lhs, rhs); });
        if (!navigation)
        {
            return false;
        }

        history_.BeginNavigation(navigation->direction, navigation->targetIndex);
        Start(navigation->folderPath, true, recursive, includeSubfolders);
        return true;
    }

    bool FolderLoadCoordinator::NavigateForward(std::wstring currentFolderPath,
                                                 bool recursive,
                                                 bool includeSubfolders)
    {
        const auto navigation = history_.FindForward(
            NormalizeFolderPath(std::move(currentFolderPath)),
            [](std::wstring_view path) { return FindExistingFolderAncestor(fs::path(path)); },
            [](std::wstring_view lhs, std::wstring_view rhs) { return FolderPathsEqual(lhs, rhs); });
        if (!navigation)
        {
            return false;
        }

        history_.BeginNavigation(navigation->direction, navigation->targetIndex);
        Start(navigation->folderPath, true, recursive, includeSubfolders);
        return true;
    }

    FolderEnumerationCoordinator::Handlers FolderLoadCoordinator::EnumerationHandlers()
    {
        FolderEnumerationCoordinator::Handlers handlers;
        handlers.onBatch = handlers_.onBatch;
        handlers.onCompleted = [this](std::wstring folderPath,
                                       std::uint64_t totalCount,
                                       std::uint64_t totalBytes)
        {
            history_.RecordOpenedFolder(NormalizeFolderPath(folderPath));
            const std::wstring watchedFolderPath = folderPath;
            if (handlers_.onCompleted)
            {
                handlers_.onCompleted(std::move(folderPath), totalCount, totalBytes);
            }
            StartFolderWatch(watchedFolderPath);
        };
        handlers.onFailed = [this](std::wstring folderPath, std::wstring message)
        {
            history_.CancelPendingNavigation();
            if (handlers_.onFailed)
            {
                handlers_.onFailed(std::move(folderPath), std::move(message));
            }
        };
        handlers.onPresentation = handlers_.onPresentation;
        handlers.onSettled = [this](bool completed)
        {
            if (completed && viewerOpenedBeforeFolderEnumerationCompleted_)
            {
                if (handlers_.onViewerSettled)
                {
                    handlers_.onViewerSettled();
                }
                viewerOpenedBeforeFolderEnumerationCompleted_ = false;
            }
        };
        return handlers;
    }

    void FolderLoadCoordinator::HandleEnumerationMessage(LPARAM lParam)
    {
        enumerationCoordinator_.HandleMessage(lParam, EnumerationHandlers());
    }

    void FolderLoadCoordinator::HandlePresentationTimer()
    {
        enumerationCoordinator_.HandlePresentationTimer(handlers_.onPresentation);
    }

    LRESULT FolderLoadCoordinator::HandleWatchMessage(LPARAM lParam)
    {
        (void)lParam;
        std::unique_ptr<services::FolderWatchUpdate> update = folderWatchService_.TakePendingUpdate();
        if (!update || update->requestId != activeFolderWatchRequestId_)
        {
            return 0;
        }

        if (handlers_.onWatchUpdate)
        {
            handlers_.onWatchUpdate(*update);
        }
        return 0;
    }

    void FolderLoadCoordinator::QueueWatchReload(std::wstring folderPath)
    {
        pendingWatchReloadPath_ = std::move(folderPath);
    }

    void FolderLoadCoordinator::MarkWatchTreeRefresh()
    {
        pendingWatchTreeRefresh_ = true;
    }

    FolderLoadCoordinator::DeferredWatchEffects FolderLoadCoordinator::TakeDeferredWatchEffects()
    {
        DeferredWatchEffects effects;
        effects.reloadPath = std::move(pendingWatchReloadPath_);
        effects.treeRefresh = pendingWatchTreeRefresh_;
        pendingWatchReloadPath_.clear();
        pendingWatchTreeRefresh_ = false;
        return effects;
    }

    void FolderLoadCoordinator::StopFolderWatch()
    {
        folderWatchService_.Stop();
        activeFolderWatchRequestId_ = 0;
    }

    void FolderLoadCoordinator::StartFolderWatch(const std::wstring& folderPath)
    {
        activeFolderWatchRequestId_ = folderWatchService_.StartWatching(
            targetWindow_,
            folderPath,
            recursiveBrowsingEnabled_);
    }
}
