#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderWatchService.h"
#include "ui/FolderEnumerationCoordinator.h"
#include "ui/FolderHistory.h"

namespace hyperbrowse::ui
{
    class FolderLoadCoordinator
    {
    public:
        static constexpr UINT kEnumerationMessageId = services::FolderEnumerationService::kMessageId;
        static constexpr UINT kWatchMessageId = services::FolderWatchService::kMessageId;
        static constexpr UINT_PTR kPresentationTimerId = FolderEnumerationCoordinator::kPresentationTimerId;

        using LoadStartedHandler = std::function<void(const std::wstring&)>;
        using LoadQueuedHandler = std::function<void(const std::wstring&)>;
        using BatchHandler = FolderEnumerationCoordinator::BatchHandler;
        using CompletionHandler = FolderEnumerationCoordinator::CompletionHandler;
        using FailureHandler = FolderEnumerationCoordinator::FailureHandler;
        using PresentationHandler = FolderEnumerationCoordinator::PresentationHandler;
        using WatchUpdateHandler = std::function<void(const services::FolderWatchUpdate&)>;
        using RestoreSelectionHandler = std::function<bool(const std::vector<std::wstring>&,
                                                           const std::wstring&,
                                                           bool,
                                                           bool)>;

        struct PendingViewerResult
        {
            bool consumed{};
            bool opened{};
        };

        using OpenViewerHandler = std::function<PendingViewerResult(const std::wstring&, bool)>;
        using ViewerSettledHandler = std::function<void()>;

        struct Handlers
        {
            LoadStartedHandler onLoadStarted;
            LoadQueuedHandler onLoadQueued;
            BatchHandler onBatch;
            CompletionHandler onCompleted;
            FailureHandler onFailed;
            PresentationHandler onPresentation;
            WatchUpdateHandler onWatchUpdate;
            RestoreSelectionHandler onRestoreSelection;
            OpenViewerHandler onOpenViewer;
            ViewerSettledHandler onViewerSettled;
        };

        struct DeferredWatchEffects
        {
            std::wstring reloadPath;
            bool treeRefresh{};
        };

        FolderLoadCoordinator() = default;
        FolderLoadCoordinator(const FolderLoadCoordinator&) = delete;
        FolderLoadCoordinator& operator=(const FolderLoadCoordinator&) = delete;

        void Configure(HWND targetWindow, Handlers handlers);
        void Start(std::wstring folderPath,
                   bool historyNavigation,
                   bool recursive,
                   bool includeSubfolders);
        void Cancel();
        bool IsEnumerationActive() const noexcept;

        bool CanNavigateBack() const noexcept;
        bool CanNavigateForward() const noexcept;
        bool HasPendingNavigation() const noexcept;
        bool NavigateBack(std::wstring currentFolderPath,
                          bool recursive,
                          bool includeSubfolders);
        bool NavigateForward(std::wstring currentFolderPath,
                             bool recursive,
                             bool includeSubfolders);

        void HandleEnumerationMessage(LPARAM lParam);
        void HandlePresentationTimer();
        LRESULT HandleWatchMessage(LPARAM lParam);

        void SetPendingStartupSelectionPath(std::wstring path);
        void SetPendingStartupViewerPath(std::wstring path);
        void ClearPendingStartupPaths();
        void ClearPendingStartupViewerPath();
        bool HasPendingStartupViewerPath() const noexcept;
        void SetPendingFolderReloadSelection(std::vector<std::wstring> selectionPaths,
                              std::wstring focusedPath);
        void RestorePendingPresentation(bool clearStartupPathsIfNotFound);

        void QueueWatchReload(std::wstring folderPath);
        void MarkWatchTreeRefresh();
        DeferredWatchEffects TakeDeferredWatchEffects();

    private:
        void StopFolderWatch();
        void StartFolderWatch(const std::wstring& folderPath);
        FolderEnumerationCoordinator::Handlers EnumerationHandlers();

        HWND targetWindow_{};
        Handlers handlers_;
        FolderHistory history_;
        FolderEnumerationCoordinator enumerationCoordinator_;
        services::FolderWatchService folderWatchService_;
        std::uint64_t activeFolderWatchRequestId_{};
        bool recursiveBrowsingEnabled_{};
        std::wstring pendingWatchReloadPath_;
        bool pendingWatchTreeRefresh_{};
        std::wstring pendingStartupSelectionPath_;
        std::wstring pendingStartupViewerPath_;
        bool viewerOpenedBeforeFolderEnumerationCompleted_{};
        std::vector<std::wstring> pendingFolderReloadSelectionPaths_;
        std::wstring pendingFolderReloadFocusedPath_;
    };
}
