#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "browser/BrowserModel.h"
#include "util/ResourceSizing.h"

namespace hyperbrowse::browser
{
    enum class BrowserSortMode : int;
    enum class ThumbnailSizePreset : int;
    class BrowserPane;
}

namespace hyperbrowse::services
{
    struct FolderWatchUpdate;
    enum class BatchConvertFormat : int;
    enum class FileConflictPolicy : int;
    enum class FileOperationType : int;
    struct FileConflictPlan;
    struct FileOperationUpdate;
    class BatchConvertService;
    class FileOperationService;
    class FolderEnumerationService;
    class FolderTreeEnumerationService;
    class FolderWatchService;
    class ThumbnailScheduler;
    class UserMetadataStore;
}

namespace hyperbrowse::cache
{
    class CachedThumbnail;
}

namespace hyperbrowse::viewer
{
    enum class CompareDirection : int;
    enum class MouseWheelBehavior : int;
    enum class TransitionStyle : int;
    class ViewerWindow;
}

namespace hyperbrowse::util
{
    class BackgroundExecutor;
}

namespace hyperbrowse::ui
{
    class DiagnosticsWindow;
    class ToolbarIconLibrary;

    class MainWindow
    {
    public:
        explicit MainWindow(HINSTANCE instance);
        ~MainWindow();

        void SetStartupLaunchPath(std::wstring path);
        bool Create();
        void Show(int nCmdShow) const;
        bool TranslateAcceleratorMessage(MSG* message) const;

    private:
        struct FolderTreeNodeData
        {
            std::wstring path;
            bool childrenLoaded{};
            bool childrenLoading{};
            std::uint64_t childEnumerationRequestId{};
        };

        static constexpr const wchar_t* kWindowClassName = L"HyperBrowseMainWindow";
        static constexpr int kActionStripHeight = 44;
        static constexpr int kMinLeftPaneWidth = 180;
        static constexpr int kMinRightPaneWidth = 240;
        static constexpr int kSplitterWidth = 6;
        static constexpr int kDefaultLeftPaneWidth = 280;
        static constexpr int kMinWindowWidth = 960;
        static constexpr int kMinWindowHeight = 640;

        enum class BrowserMode
        {
            Thumbnails = 0,
            Details = 1
        };

        enum class RightPaneTab
        {
            FileDetails = 0,
            QuickSend = 1,
        };

        enum class ThemeMode
        {
            Light = 0,
            Dark = 1
        };

        enum class DragMode
        {
            None,
            LeftSplitter,
            DetailsSplitter,
            QuickAccessInternal
        };

        struct ThemePalette
        {
            COLORREF windowBackground;
            COLORREF paneBackground;
            COLORREF text;
            COLORREF mutedText;
            COLORREF treeLine;
            COLORREF splitter;
            COLORREF actionStripBackground;
            COLORREF actionStripBorder;
            COLORREF actionFieldBackground;
            COLORREF accent;
            COLORREF accentFill;
            COLORREF accentText;
        };

        enum class ToolbarItemKind
        {
            IconButton,
            IconToggle,
            IconDropdown,
            Separator,
            FilterEdit,
        };

        enum class ToolbarAlignment
        {
            Left,
            Right,
        };

        struct ToolbarItem
        {
            UINT commandId{};
            std::string iconName;
            std::wstring tooltip;
            ToolbarItemKind kind{ToolbarItemKind::IconButton};
            ToolbarAlignment alignment{ToolbarAlignment::Left};
            bool enabled{true};
            bool checked{};
            RECT rect{};
        };

        struct QuickAccessDestinationRow
        {
            std::wstring destinationPath;
            std::wstring displayLabel;
            std::wstring metadataLabel;
            bool favorite{};
            RECT rowRect{};
            RECT copyRect{};
            RECT moveRect{};
            RECT removeRect{};
        };

        bool RegisterWindowClass() const;
        bool CreateAccelerators();
        bool CreateMenuBar();
        bool CreateChildWindows();
        void InitializeFolderTree();
        void PopulateSpecialFolderRoots();
        void PopulateDriveRoots();
        void RefreshFolderTree();
        HTREEITEM InsertFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath);
        void AddFolderTreePlaceholder(HTREEITEM parentItem);
        void RequestFolderTreeChildren(HTREEITEM item);
        void ApplyFolderTreeChildren(HTREEITEM item, std::vector<std::wstring> childFolderPaths);
        void ShowSelectedFolderInTree();
        void SelectFolderInTree(const std::wstring& folderPath);
        void ContinueSelectingFolderInTree();
        HTREEITEM FindChildFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath) const;
        HTREEITEM FindFolderTreeItemByPath(const std::wstring& folderPath) const;
        void InsertFolderTreeFolderIfParentLoaded(const std::wstring& folderPath);
        FolderTreeNodeData* GetFolderTreeNodeData(HTREEITEM item) const;
        std::wstring GetSelectedFolderTreePath() const;
        void LayoutChildren();
        void UpdateStatusText() const;
        void UpdateMenuState();
        void UpdateWindowTitle() const;
        void ApplyViewerMouseWheelSetting();
        void ApplyViewerTransitionSettings();
        void ApplyThumbnailMemoryPressureState();
        void ApplyResourceProfileSetting();
        void ApplyPersistentThumbnailCacheSetting();
        void ApplyTheme();
        void QueueMemoryPressureSample();
        void LoadWindowState();
        void ApplyStartupLaunchPathOverride();
        void SaveWindowState() const;
        bool HandleCommand(UINT commandId);
        void OpenFolder();
        void LoadFolderAsync(std::wstring folderPath);
        void RefreshBrowserPane();
        void OpenItemInViewer(int modelIndex, bool preferSecondaryMonitor = false);
        bool OpenItemsInViewer(std::vector<browser::BrowserItem> items,
                       int selectedIndex,
                       bool startSlideshow,
                       bool preferSecondaryMonitor = false);
        bool SyncViewerToBrowserModel(std::wstring_view preferredPath = {});
        void RebuildQuickAccessDestinationRows(int innerLeft, int innerRight, int top);
        bool IsQuickAccessDestinationCurrentFolder(std::wstring_view folderPath) const;
        bool CanNavigateToQuickAccessDestination(std::wstring_view folderPath) const;
        bool CanUseQuickAccessDestinationActions(std::wstring_view folderPath) const;
        int HitTestQuickAccessDestinationRow(int x, int y) const;
        int HitTestQuickAccessDestinationButton(int x, int y, services::FileOperationType* type = nullptr) const;
        std::vector<browser::BrowserItem> CollectItemsForScope(bool selectionScope) const;
        std::vector<std::wstring> SelectedFileOperationPathsSnapshot(std::size_t* pairedCompanionCount = nullptr) const;
        bool ChooseFolder(std::wstring* folderPath) const;
        bool HasSelectedJpegItems() const;
        void ShowBrowserContextMenu(POINT screenPoint);
        void ShowFolderTreeContextMenu(POINT screenPoint, HTREEITEM item);
        void ShowAboutDialog() const;
        void ShowPerformanceSettingsDialog();
        void ShowDiagnosticsSnapshot();
        void ResetDiagnosticsState();
        void ShowImageInformation();
        void StartRenameSelectedImage();
        void StartBatchRenameSelection();
        void StartCompareSelected();
        void SetSelectionRating(int rating);
        void EditSelectionTags();
        void StartCopySelection();
        void StartMoveSelection();
        void StartDeleteSelection(bool permanent);
        void StartFolderTreeRename(std::wstring folderPath);
        void StartFolderTreeDelete(std::wstring folderPath, bool permanent);
        void StartSelectionFileOperationToDestination(services::FileOperationType type, std::wstring destinationFolder);
        void StartFileOperation(services::FileOperationType type,
                    std::vector<std::wstring> sourcePaths,
                    std::wstring destinationFolder,
                    services::FileConflictPolicy conflictPolicy,
                    std::vector<std::wstring> targetLeafNames = {});
        void RevealSelectedInExplorer() const;
        void OpenSelectedContainingFolder() const;
        void CopySelectedPathsToClipboard() const;
        void ShowSelectedFileProperties() const;
        void StartSlideshow(bool selectionScope);
        void StartBatchConvert(bool selectionScope, services::BatchConvertFormat format);
        void AdjustSelectedJpegOrientation(int quarterTurnsDelta);
        void ApplyCompletedFileOperation(const services::FileOperationUpdate& update);
        bool IsPathInCurrentScope(std::wstring_view path) const;
        void ApplyFolderWatchChanges(const services::FolderWatchUpdate& update);
        LRESULT OnFolderEnumerationMessage(LPARAM lParam);
        LRESULT OnFolderTreeEnumerationMessage(LPARAM lParam);
        LRESULT OnFolderWatchMessage(LPARAM lParam);
        LRESULT OnFolderTreeNotify(LPARAM lParam);
        LRESULT OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView);
        LRESULT OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView);
        LRESULT OnFolderTreeRightClick();
        LRESULT OnDropFiles(HDROP dropHandle);
        LRESULT OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneQuickSendDragMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBatchConvertMessage(LPARAM lParam);
        LRESULT OnFileOperationMessage(LPARAM lParam);
        LRESULT OnDetailsPanelThumbnailMessage(LPARAM lParam);
        LRESULT OnViewerZoomMessage(LPARAM lParam);
        LRESULT OnViewerActivityMessage(LPARAM lParam);
        LRESULT OnViewerDeleteRequested(WPARAM wParam);
        LRESULT OnViewerClosedMessage();
        LRESULT OnMemoryPressureSampleMessage(LPARAM lParam);
        void TryOpenPendingStartupViewerPath(bool clearIfNotFound);

        void SetBrowserMode(BrowserMode mode);
        void ToggleRecursiveBrowsing();
        void ToggleCurrentFolderFavoriteDestination();
        int CommonSelectionRating() const;
        void ApplyThumbnailDisplaySettings();
        void SetThemeMode(ThemeMode themeMode);
        bool IsFavoriteDestination(std::wstring_view folderPath) const;
        std::vector<std::wstring> RecentDestinationShortcutPaths() const;
        void RemoveFavoriteDestination(std::wstring_view folderPath);
        void RemoveRecentDestination(std::wstring_view folderPath);
        void ClearFavoriteDestinations();
        void ClearRecentFolders();
        void ClearRecentDestinations();
        void RecordRecentFolder(std::wstring folderPath);
        void RecordRecentDestination(std::wstring folderPath);
        void RefreshQuickAccessMenus();
        void UpdateDetailsPanel();
        void ApplyDetailsPanelText(std::wstring title, std::wstring summary, std::wstring body);
        void RefreshDetailsPanelBodyPresentation();
        void RecreateDetailsPanelThumbnailScheduler();
        void ApplyCacheCapacityOverrideSettings();
        void ResetDetailsPanelHistogram();
        void RequestDetailsPanelHistogram(const browser::BrowserItem& item, int modelIndex);
        void ApplyDetailsPanelHistogram(const cache::CachedThumbnail& thumbnail);
        void PaintDetailsPanel(HDC hdc, const RECT& clientRect) const;
        ThemePalette GetThemePalette() const;
        void InitToolbarItems();
        void LayoutToolbar();
        void PaintToolbar(HDC hdc, const RECT& stripRect);
        int ToolbarHitTest(int x, int y) const;
        void ToolbarHandleClick(int itemIndex);
        void ShowDropdownForItem(int itemIndex);
        void UpdateToolbarItemStates();
        void InvalidateToolbarStrip();

        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        void OnSize();
        void OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const;
        void OnLButtonDown(int x, int y);
        void OnLButtonUp();
        void OnLButtonDoubleClick(int x, int y);
        void OnMouseMove(int x, int y);
        bool IsOverSplitter(int x, int y) const;
        bool IsOverDetailsPanelSplitter(int x, int y) const;

        HINSTANCE instance_{};
        HWND hwnd_{};
        HWND filterEdit_{};
        HWND treePane_{};
        HWND browserPane_{};
        HWND statusBar_{};
        HWND detailsPanelTabs_{};
        HWND detailsPanelText_{};
        HWND tooltipControl_{};
        HMODULE detailsPanelRichEditModule_{};
        HIMAGELIST treeImageList_{};
        HMENU menu_{};
        HMENU fileMenu_{};
        HMENU openRecentFolderMenu_{};
        HMENU copySelectionToMenu_{};
        HMENU moveSelectionToMenu_{};
        HACCEL accelerators_{};
        int leftPaneWidth_{kDefaultLeftPaneWidth};
        int detailsPanelWidth_{340};
        int toolbarHotIndex_{-1};
        int toolbarPressedIndex_{-1};
        bool toolbarMouseTracking_{};
        std::vector<ToolbarItem> toolbarItems_;
        std::unique_ptr<ToolbarIconLibrary> toolbarIconLibrary_;
        BrowserMode browserMode_{BrowserMode::Thumbnails};
        RightPaneTab activeRightPaneTab_{RightPaneTab::FileDetails};
        ThemeMode themeMode_{ThemeMode::Light};
        bool recursiveBrowsingEnabled_{false};
        bool rawJpegPairedOperationsEnabled_{false};
        bool persistentThumbnailCacheEnabled_{true};
        bool suppressTreeSelectionChange_{};
        DragMode dragMode_{DragMode::None};
        HBRUSH backgroundBrush_{};
        HBRUSH actionFieldBrush_{};
        HBRUSH detailsPanelBrush_{};
        HFONT detailsPanelTitleFont_{};
        HFONT detailsPanelSummaryFont_{};
        HFONT detailsPanelBodyFont_{};
        std::wstring startupLaunchPathOverride_;
        std::wstring startupFolderPath_;
        std::wstring pendingStartupViewerPath_;
        std::vector<std::wstring> recentFolders_;
        std::vector<std::wstring> recentDestinationFolders_;
        std::vector<std::wstring> favoriteDestinationFolders_;
        std::vector<std::unique_ptr<FolderTreeNodeData>> folderTreeNodes_;
        std::unique_ptr<browser::BrowserModel> browserModel_;
        std::unique_ptr<browser::BrowserPane> browserPaneController_;
        std::unique_ptr<services::BatchConvertService> batchConvertService_;
        std::unique_ptr<services::FileOperationService> fileOperationService_;
        std::unique_ptr<services::FolderEnumerationService> folderEnumerationService_;
        std::unique_ptr<services::FolderTreeEnumerationService> folderTreeEnumerationService_;
        std::unique_ptr<services::FolderWatchService> folderWatchService_;
        std::unique_ptr<services::ThumbnailScheduler> detailsPanelThumbnailScheduler_;
        std::unique_ptr<services::UserMetadataStore> userMetadataStore_;
        std::unique_ptr<DiagnosticsWindow> diagnosticsWindow_;
        std::unique_ptr<viewer::ViewerWindow> viewerWindow_;
        std::unique_ptr<util::BackgroundExecutor> memoryPressureExecutor_;
        std::unordered_map<std::uint64_t, HTREEITEM> pendingFolderTreeEnumerationItems_;
        std::wstring pendingTreeSelectionPath_;
        RECT detailsPanelRect_{};
        RECT detailsPanelContentRect_{};
        RECT detailsPanelHistogramRect_{};
        RECT quickAccessDestinationPanelRect_{};
        std::wstring detailsPanelTitleText_;
        std::wstring detailsPanelSummaryText_;
        std::wstring detailsPanelBodyText_;
        std::wstring detailsPanelHistogramPath_;
        std::vector<QuickAccessDestinationRow> quickAccessDestinationRows_;
        std::uint64_t detailsPanelHistogramModifiedTimestampUtc_{};
        std::uint64_t detailsPanelThumbnailSessionId_{1};
        std::uint64_t detailsPanelThumbnailRequestEpoch_{};
        int detailsPanelHistogramModelIndex_{-1};
        bool detailsPanelHistogramVisible_{};
        bool detailsPanelHistogramLoading_{};
        int quickAccessHotRowIndex_{-1};
        int quickAccessHotButtonIndex_{-1};
        int quickAccessPressedRowIndex_{-1};
        int quickAccessPressedButtonIndex_{-1};
        std::array<std::uint32_t, 64> detailsPanelHistogramRed_{};
        std::array<std::uint32_t, 64> detailsPanelHistogramGreen_{};
        std::array<std::uint32_t, 64> detailsPanelHistogramBlue_{};
        std::uint32_t detailsPanelHistogramPeak_{};
        std::uint64_t activeEnumerationRequestId_{};
        std::uint64_t activeFolderWatchRequestId_{};
        std::uint64_t activeBatchConvertRequestId_{};
        std::uint64_t activeFileOperationRequestId_{};
        bool batchConvertActive_{};
        bool fileOperationActive_{};
        std::size_t batchConvertCompleted_{};
        std::size_t batchConvertTotal_{};
        std::size_t batchConvertFailed_{};
        std::wstring batchConvertOutputFolder_;
        std::wstring batchConvertCurrentFile_;
        std::wstring activeTreeFolderOperationPath_;
        std::wstring activeTreeFolderRenamePath_;
        std::wstring pendingViewerDeleteSourcePath_;
        std::wstring pendingViewerDeletePreferredFocusPath_;
        std::wstring pendingFolderWatchReloadPath_;
        bool pendingFolderWatchTreeRefresh_{};
        std::wstring activeFileOperationLabel_;
        int viewerZoomPercent_{};
        bool viewerWindowActive_{};
        bool nvJpegEnabled_{};
        bool libRawOutOfProcessEnabled_{true};
        bool thumbnailMemoryPressureActive_{};
        bool showPressureStateInStatusBar_{};
        bool memoryPressureSampleQueued_{};
        unsigned int memoryPressureRecoveryClearSampleCount_{};
        util::ResourceProfile resourceProfile_{util::ResourceProfile::Balanced};
        std::size_t thumbnailCacheCapacityOverrideBytes_{};
        std::size_t metadataCacheCapacityOverrideEntries_{};
        browser::ThumbnailSizePreset thumbnailSizePreset_{static_cast<browser::ThumbnailSizePreset>(192)};
        browser::BrowserSortMode sortMode_{static_cast<browser::BrowserSortMode>(0)};
        bool sortAscending_{true};
        bool compactThumbnailLayout_{};
        bool thumbnailDetailsVisible_{true};
        UINT slideshowIntervalMs_{3000};
        viewer::TransitionStyle slideshowTransitionStyle_{};
        UINT slideshowTransitionDurationMs_{350};
        bool detailsStripVisible_{true};
        viewer::MouseWheelBehavior viewerMouseWheelBehavior_{};
        UINT_PTR memoryPressureTimerId_{};
    };
}
