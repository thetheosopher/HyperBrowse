#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "browser/BrowserModel.h"
#include "render/D2DRenderer.h"
#include "util/ResourceSizing.h"
#include "util/UiTextSize.h"
#include "ui/FileOperationJournal.h"
#include "ui/FileOperationReconciler.h"
#include "ui/FolderTreeDragController.h"
#include "ui/FolderLoadCoordinator.h"
#include "ui/FolderTreeController.h"
#include "ui/FileCommandController.h"
#include "ui/CommandBarController.h"
#include "ui/CommandBarPainter.h"
#include "ui/DetailsPanelChromePainter.h"
#include "ui/DetailsPanelHistogramPainter.h"
#include "ui/DetailsPanelTextPainter.h"
#include "ui/DisplaySurfaceRecoveryPolicy.h"
#include "ui/MenuPainter.h"
#include "ui/MenuMessageHandling.h"
#include "ui/QuickAccessMenuBuilder.h"
#include "ui/QuickAccessLayout.h"
#include "ui/QuickAccessPainter.h"
#include "ui/QuickSend.h"
#include "ui/ViewerPendingOperationState.h"
#include "ui/ViewCommandController.h"
#include "ui/WindowAsyncMessageRouter.h"
#include "ui/WindowTimerRouter.h"

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
    enum class EscapeKeyBehavior : int;
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
    class ExternalDropTarget;
    class FolderEnumerationCoordinator;
    class FolderWatchChangeCoordinator;
    class ToolbarIconLibrary;

    class MainWindow
    {
    public:
        explicit MainWindow(HINSTANCE instance);
        ~MainWindow();

        void SetStartupLaunchPath(std::wstring path);
        void OpenViewerAtPath(const std::wstring& filePath);
        void HandleExternalLaunchPath(const std::wstring& path);
        bool Create();
        void Show(int nCmdShow) const;
        bool TranslateAcceleratorMessage(MSG* message);
        HWND Hwnd() const noexcept { return hwnd_; }
        hyperbrowse::util::AppTextSize AppTextSize() const noexcept { return appTextSize_; }
        hyperbrowse::viewer::EscapeKeyBehavior ViewerEscapeKeyBehavior() const noexcept { return viewerEscapeKeyBehavior_; }

        static constexpr UINT kExternalLaunchMessage = WM_APP + 71;

    private:
        struct FileOperationCompletionContext;
        using FolderTreeNodeData = FolderTreeController::NodeData;

        static constexpr const wchar_t* kWindowClassName = L"HyperBrowseMainWindow";
        static constexpr UINT kDeferredMenuStateMessage = WM_APP + 73;
        static constexpr UINT_PTR kDisplaySurfaceRecoveryTimerId = 9103;
        static constexpr UINT_PTR kFileOperationShutdownTimerId = 9104;
        static constexpr UINT kFileOperationShutdownIntervalMs = 1000;
        static constexpr UINT kDisplaySurfaceRecoveryIntervalMs = 400;
        static constexpr int kDisplaySurfaceRecoveryRetryLimit = 8;
        static constexpr int kActionStripHeight = 44;
        static constexpr int kMinLeftPaneWidth = 250;
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

        using ToolbarItemKind = CommandBarController::ToolbarItemKind;
        using ToolbarAlignment = CommandBarController::ToolbarAlignment;
        using ToolbarItem = CommandBarController::ToolbarItem;
        using CommandBarMenuButton = CommandBarController::CommandBarMenuButton;

        using QuickAccessDestinationRow = QuickAccessLayout::Row;

        bool RegisterWindowClass() const;
        bool CreateAccelerators();
        bool CreateMenuBar();
        bool CreateChildWindows();
        void RefreshFolderTree();
        void InvalidateFolderTreeChildPresence(std::wstring_view folderPath);
        HTREEITEM FindFolderTreeItemByPath(const std::wstring& folderPath) const;
        void InsertFolderTreeFolderIfParentLoaded(const std::wstring& folderPath);
        FolderTreeNodeData* GetFolderTreeNodeData(HTREEITEM item) const;
        std::wstring GetSelectedFolderTreePath() const;
        void LayoutChildren();
        void UpdateStatusText();
        void UpdateMenuState();
        void UpdateWindowTitle() const;
        void ApplyViewerMouseWheelSetting();
        void ApplyViewerEscapeKeyBehavior();
        void ApplyViewerTransitionSettings();
        void ApplyThumbnailMemoryPressureState();
        void ApplyResourceProfileSetting();
        void ApplyPersistentThumbnailCacheSetting();
        void ApplyRawJpegPairingSettings();
        void ApplyAppTextSize();
        void RebuildAppTextFonts();
        void ApplyTheme();
        void QueueMemoryPressureSample();
        void LoadWindowState();
        void ApplyStartupLaunchPathOverride();
        void SaveWindowState() const;
        bool HandleCommand(UINT commandId);
        void OpenFolder();
        void LoadFolderAsync(std::wstring folderPath, bool historyNavigation = false);
        void RefreshBrowserPane();
        void OpenItemInViewer(int modelIndex, bool preferSecondaryMonitor = false);
        bool OpenItemsInViewer(std::vector<browser::BrowserItem> items,
                       int selectedIndex,
                       bool startSlideshow,
                       bool preferSecondaryMonitor = false,
                       bool resolvePairedRawJpegItems = true);
        bool ShouldDefaultViewerToSecondaryMonitor() const;
        std::vector<browser::BrowserItem> ResolvePairedRawJpegViewerItems(
            std::vector<browser::BrowserItem> items,
            bool startSlideshow) const;
        bool SyncViewerToBrowserModel(std::wstring_view preferredPath = {});
        void RebuildQuickAccessDestinationRows(int innerLeft, int innerRight, int top);
        void UpdateQuickAccessSortTooltip();
        void UpdateQuickAccessShortcutEditControls();
        void HideQuickAccessShortcutEditControls();
        void MutateQuickSendState(const std::function<void()>& mutation);
        void LoadQuickSendStateFromRegistry();
        void SaveQuickSendStateToRegistry() const;
        void SortFavoriteDestinationsByShortcutInMemory();
        bool IsQuickAccessShortcutEdit(HWND control) const;
        bool IsQuickAccessDestinationCurrentFolder(std::wstring_view folderPath) const;
        bool CanNavigateToQuickAccessDestination(std::wstring_view folderPath) const;
        bool CanUseQuickAccessDestinationActions(std::wstring_view folderPath) const;
        void SelectRightPaneTab(RightPaneTab tab);
        void ToggleDetailsPanelVisibility();
        int HitTestDetailsPanelTab(int x, int y) const;
        int HitTestDetailsPanelCloseButton(int x, int y) const;
        int HitTestQuickAccessSortButton(int x, int y) const;
        int HitTestQuickAccessDestinationRow(int x, int y) const;
        int HitTestQuickAccessDestinationButton(int x, int y, services::FileOperationType* type = nullptr) const;
        std::vector<browser::BrowserItem> CollectItemsForScope(bool selectionScope) const;
        std::vector<std::wstring> ExpandRawJpegPairedPaths(const std::vector<std::wstring>& paths,
                                   std::size_t* pairedCompanionCount = nullptr) const;
        std::vector<std::wstring> SelectedFileOperationPathsSnapshot(std::size_t* pairedCompanionCount = nullptr) const;
        bool ChooseFolder(std::wstring* folderPath, HWND ownerWindow = nullptr) const;
        bool HasSelectedJpegItems() const;
        void ShowBrowserContextMenu(POINT screenPoint);
        bool ShowShellContextMenuForSelection(POINT screenPoint);
        void ShowFolderTreeContextMenu(POINT screenPoint, HTREEITEM item);
        void ShowUserGuide() const;
        void ShowShortcutReference() const;
        void ShowAboutDialog() const;
        void ShowFileAssociationsDialog();
        void ShowSlideshowSettingsDialog();
        void ShowConsolidatedSettingsDialog();
        void ShowPerformanceSettingsDialog();
        void ShowPersistentThumbnailCacheDialog();
        void ShowPersistentThumbnailCacheDialogContents(std::wstring content, std::wstring expandedInformation);
        void StartPersistentThumbnailCacheStatistics();
        void StartPersistentThumbnailCacheMaintenance(bool purge);
        void ShowDiagnosticsSnapshot();
        void ExportRedactedDiagnosticsSnapshot();
        void ResetDiagnosticsState();
        void ShowImageInformation(HWND ownerWindow = nullptr);
        void StartRenameSelectedImage();
        void StartBatchRenameSelection();
        void StartCompareSelected();
        void SetSelectionRating(int rating);
        void EditSelectionTags();
        void StartCopySelection();
        void StartMoveSelection();
        void StartMoveSelectionToNewChildFolder();
        void StartDeleteSelection(bool permanent);
        void StartFolderTreeRename(std::wstring folderPath);
        bool BeginFolderTreeInlineRename(const std::wstring& folderPath);
        void StartFolderTreeDelete(std::wstring folderPath, bool permanent);
        void StartFolderTreeMoveToDestination(std::wstring folderPath, std::wstring destinationFolder);
        void StartFolderTreeCreateNewFolder(std::wstring parentPath);
        void StartSelectionFileOperationToDestination(services::FileOperationType type, std::wstring destinationFolder);
        bool ChooseQuickSendDestination(services::FileOperationType operationType,
                         POINT popupPoint,
                         HWND dialogOwner,
                         std::wstring* destinationFolder);
        void StartQuickSendForSelection(services::FileOperationType type);
        bool StartViewerQuickSendOperation(services::FileOperationType type, std::wstring destinationFolder);
        bool StartFileOperation(services::FileOperationType type,
                    std::vector<std::wstring> sourcePaths,
                    std::wstring destinationFolder,
                    services::FileConflictPolicy conflictPolicy,
                    std::vector<std::wstring> targetLeafNames = {},
                    HWND ownerWindow = nullptr);
        void UpdateTaskbarProgress(ULONGLONG completed, ULONGLONG total);
        void ClearTaskbarProgress();
        void NotifyLongOperationComplete(const std::wstring& title, const std::wstring& message);
        void EnsureTrayIcon();
        void RemoveTrayIcon();
        void RecordUndoableOperation(const services::FileOperationUpdate& update);
        FileOperationCompletionContext CaptureFileOperationCompletionContext();
        void ApplyBrowserFileOperationEffects(
            const services::FileOperationUpdate& update,
            const FileOperationCompletionContext& completionContext,
            const FileOperationTreeEffects& treeEffects,
            bool reloadCurrentFolder,
            bool viewerDeleteOperation,
            bool viewerQuickSendOperation,
            const std::wstring& fallbackFolderPath);
        bool ApplyViewerFileOperationEffects(const services::FileOperationUpdate& update,
                              const FileOperationCompletionContext& completionContext,
                              bool viewerDeleteOperation,
                              bool viewerQuickSendOperation);
        void PerformUndo();
        void PerformRedo();
        void UpdateUndoRedoMenuState();
        void RevealSelectedInExplorer() const;
        void OpenSelectedContainingFolder() const;
        void CopySelectedPathsToClipboard() const;
        void CopySelectionFilesToClipboard(bool movePreferred = false) const;
        void PasteClipboardFilesIntoCurrentFolder();
        void ShowSelectedFileProperties() const;
        void SetDesktopWallpaperFromImageFile(const std::wstring& imagePath);
        void ShowImageInformationForPath(const std::wstring& filePath, HWND ownerWindow = nullptr);
        void CopySelectedImagePixelsToClipboard(std::wstring_view preferredPath = {});
        void StartDuplicateSelection();
        void StartSlideshow(bool selectionScope);
        void StartFolderSlideshow(std::wstring_view preferredPath = {});
        void StartBatchConvert(bool selectionScope, services::BatchConvertFormat format);
        void AdjustSelectedJpegOrientation(int quarterTurnsDelta);
        void ApplyCompletedFileOperation(const services::FileOperationUpdate& update);
        bool IsPathInCurrentScope(std::wstring_view path) const;
        void ApplyFolderWatchChanges(const services::FolderWatchUpdate& update);
        bool FlushFolderEnumerationPresentation(bool clearStartupPathsIfNotFound);
        LRESULT OnFolderTreeNotify(LPARAM lParam);
        void RelayFolderTreeTooltipEvent(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK FolderTreeTooltipSubclassProc(HWND hwnd,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam,
                                       UINT_PTR subclassId,
                                       DWORD_PTR refData);
        static LRESULT CALLBACK QuickAccessShortcutEditSubclassProc(HWND hwnd,
                           UINT message,
                           WPARAM wParam,
                           LPARAM lParam,
                           UINT_PTR subclassId,
                           DWORD_PTR refData);
        static LRESULT CALLBACK CommandBarMenuFilterProc(int code, WPARAM wParam, LPARAM lParam);
        LRESULT OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView);
        LRESULT OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView);
        LRESULT OnFolderTreeBeginDrag(const NMTREEVIEWW& treeView);
        LRESULT OnFolderTreeBeginLabelEdit(const NMTVDISPINFOW& dispInfo);
        LRESULT OnFolderTreeEndLabelEdit(const NMTVDISPINFOW& dispInfo);
        LRESULT OnFolderTreeRightClick();
        void UpdateFolderTreeDrag(POINT windowPoint);
        void FinishFolderTreeDrag(bool commitDrop);
        void UpdateInternalSelectionDrag(POINT windowPoint);
        void FinishInternalSelectionDrag(bool commitDrop);
        void StartExternalSelectionDrag();
        LRESULT OnDropFiles(HDROP dropHandle);
        std::vector<std::wstring> ShellPathsFromDataObject(IDataObject* dataObject) const;
        DWORD DropEffectForKeyState(DWORD keyState, const std::wstring& destinationFolder, const std::vector<std::wstring>& sourcePaths) const;
        DWORD UpdateExternalDropFeedback(IDataObject* dataObject, DWORD keyState, POINT clientPoint);
        std::wstring ResolveExternalDropTarget(POINT clientPoint, HTREEITEM* treeItemOut) const;
        DWORD HandleExternalDrop(IDataObject* dataObject, DWORD keyState, POINT clientPoint);
        void ClearExternalDropVisuals();
        LRESULT OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneQuickSendDragMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBatchConvertMessage(LPARAM lParam);
        LRESULT OnFileOperationMessage(LPARAM lParam);
        LRESULT OnFileOperationProgressMessage(LPARAM lParam);
        LRESULT OnDetailsPanelThumbnailMessage(LPARAM lParam);
        LRESULT OnViewerZoomMessage(LPARAM lParam);
        LRESULT OnViewerActivityMessage(LPARAM lParam);
        LRESULT OnViewerCurrentItemChangedMessage(WPARAM wParam);
        LRESULT OnViewerDeleteRequested(WPARAM wParam);
        LRESULT OnViewerQuickSendRequest(WPARAM wParam, LPARAM lParam);
        LRESULT OnViewerStartFolderSlideshowMessage(WPARAM wParam);
        LRESULT OnViewerContextMenuCommand(WPARAM wParam);
        LRESULT OnViewerDroppedFileMessage(LPARAM lParam);
        LRESULT OnViewerClosedMessage();
        LRESULT OnMemoryPressureSampleMessage(LPARAM lParam);
        LRESULT OnPersistentThumbnailCacheMaintenanceMessage(WPARAM wParam);

        void SetBrowserMode(BrowserMode mode);
        void StepThumbnailSize(int direction);
        void ToggleRecursiveBrowsing();
        void ToggleShowSubfoldersInBrowser();
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
        void SortFavoriteDestinationsByShortcut();
        void RecordRecentFolder(std::wstring folderPath);
        void RecordRecentDestination(std::wstring folderPath);
        void SyncQuickSendModel();
        bool NavigateBackToLastOpenedFolder();
        bool NavigateForwardToLastOpenedFolder();
        void RefreshQuickAccessMenus();
        void RefreshPersistentMenuOwnerDraw();
        void PrepareMenuForOwnerDraw(HMENU menu,
                         std::vector<std::unique_ptr<MenuDrawItemData>>& storage,
                         bool ownerDrawCurrentLevel) const;
        void UpdateDetailsPanel();
        void ApplyDetailsPanelText(std::wstring title, std::wstring summary, std::wstring body);
        void RefreshDetailsPanelBodyPresentation();
        void RecreateDetailsPanelThumbnailScheduler();
        void ApplyCacheCapacityOverrideSettings();
        void ResetDetailsPanelHistogram();
        void RequestDetailsPanelHistogram(const browser::BrowserItem& item, int modelIndex);
        void ApplyDetailsPanelHistogram(const cache::CachedThumbnail& thumbnail);
        DetailsPanelChromePainter::State BuildDetailsPanelChromePainterState() const;
        DetailsPanelChromePainter::Palette BuildDetailsPanelChromePainterPalette(const ThemePalette& palette) const;
        DetailsPanelHistogramPainter::State BuildDetailsPanelHistogramPainterState() const;
        DetailsPanelHistogramPainter::Palette BuildDetailsPanelHistogramPainterPalette(const ThemePalette& palette) const;
        DetailsPanelTextPainter::Palette BuildDetailsPanelTextPainterPalette(const ThemePalette& palette) const;
        QuickAccessPainter::State BuildQuickAccessPainterState(
            const QuickAccessLayout::Metrics& metrics,
            std::vector<QuickAccessPainter::RowState>& rowStates) const;
        QuickAccessPainter::Palette BuildQuickAccessPainterPalette(const ThemePalette& palette) const;
        bool PaintDetailsPanelD2D(HDC hdc, const RECT& clientRect) const;
        void PaintDetailsPanel(HDC hdc, const RECT& clientRect) const;
        void DrawStatusStrip(const DRAWITEMSTRUCT& drawItem) const;
        void MeasureOwnerDrawMenuItem(MEASUREITEMSTRUCT* measureItem) const;
        void DrawOwnerDrawMenuItem(const DRAWITEMSTRUCT& drawItem) const;
        int CommandBarMenuHitTest(int x, int y) const;
        void ActivateCommandBarKeyboardMode(int index);
        void DeactivateCommandBarKeyboardMode(bool restoreFocus);
        bool HandleCommandBarKeyboardInput(UINT message, WPARAM wParam, LPARAM lParam);
        void OpenCommandBarMenu(int index);
        ThemePalette GetThemePalette() const;
        void InitToolbarItems();
        void LayoutToolbar();
        void PaintToolbar(HDC hdc, const RECT& stripRect);
        bool EnsureD2DResources();
        void ResetD2DResources();
        void PaintToolbarD2D(ID2D1RenderTarget* renderTarget, const RECT& stripRect);
        int ToolbarHitTest(int x, int y) const;
        void ToolbarHandleClick(int itemIndex);
        void ShowDropdownForItem(int itemIndex);
        void UpdateToolbarItemStates();
        void InvalidateToolbarStrip();
        void HandleDisplaySurfaceChange();
        void RecoverDisplaySurfaces(bool relayout);
        void ScheduleDisplaySurfaceRecoveryRetries();
        void StopDisplaySurfaceRecoveryRetries();
        LRESULT HandlePaintMessage();
        std::optional<LRESULT> HandleControlColorMessage(UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleNotifyMessage(LPARAM lParam);
        std::optional<LRESULT> HandleMouseInputMessage(UINT message, WPARAM wParam, LPARAM lParam);
        std::optional<LRESULT> HandleCommandMessage(WPARAM wParam, LPARAM lParam);

        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        void OnSize();
        void OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const;
        void OnLButtonDown(int x, int y);
        void OnLButtonUp();
        void OnLButtonDoubleClick(int x, int y);
        void OnMouseMove(int x, int y);
        bool OnQuickAccessMouseWheel(WPARAM wParam, LPARAM lParam);
        void OnQuickAccessScroll(WPARAM wParam);
        bool IsOverSplitter(int x, int y) const;
        bool IsOverDetailsPanelSplitter(int x, int y) const;

        HINSTANCE instance_{};
        HWND hwnd_{};
        HWND filterEdit_{};
        HWND treePane_{};
        HWND browserPane_{};
        HWND statusBar_{};
        HWND detailsPanelText_{};
        HWND quickAccessScrollBar_{};
        HWND tooltipControl_{};
        std::wstring treeFolderTooltipText_;
        std::wstring treeTooltipPath_;
        HMODULE detailsPanelRichEditModule_{};
        HIMAGELIST treeImageList_{};
        ExternalDropTarget* externalDropTarget_{};
        HTREEITEM externalDropTreeHoverItem_{};
        ITaskbarList3* taskbarList_{};
        bool taskbarProgressActive_{};
        bool trayIconAdded_{};
        UINT trayIconMessageId_{};

        FileOperationJournal fileOperationJournal_;
        bool applyingUndoRedo_{};
        CommandBarController commandBarController_;
        CommandBarPainter commandBarPainter_;
        MenuPainter menuPainter_;
        QuickAccessMenuBuilder quickAccessMenuBuilder_;
        HMENU menu_{};
        HMENU fileMenu_{};
        HMENU editMenu_{};
        HMENU viewMenu_{};
        HMENU helpMenu_{};
        HMENU openRecentFolderMenu_{};
        HMENU copySelectionToMenu_{};
        HMENU moveSelectionToMenu_{};
        HACCEL accelerators_{};
        int leftPaneWidth_{kDefaultLeftPaneWidth};
        int detailsPanelWidth_{340};
        std::array<CommandBarMenuButton, 4>& commandBarMenuButtons_;
        int commandBarHotIndex_{-1};
        int commandBarPressedIndex_{-1};
        int commandBarMenuNavigationIndex_{-1};
        HWND commandBarPreviousFocus_{};
        bool commandBarKeyboardActive_{};
        bool menuLoopActive_{};
        bool menuStateRefreshPending_{};
        bool menuStateRefreshPosted_{};
        int toolbarHotIndex_{-1};
        int toolbarPressedIndex_{-1};
        bool toolbarMouseTracking_{};
        std::vector<ToolbarItem>& toolbarItems_;
        std::unique_ptr<ToolbarIconLibrary> toolbarIconLibrary_;
        BrowserMode browserMode_{BrowserMode::Thumbnails};
        RightPaneTab activeRightPaneTab_{RightPaneTab::FileDetails};
        ThemeMode themeMode_{ThemeMode::Light};
        hyperbrowse::util::AppTextSize appTextSize_{hyperbrowse::util::kDefaultAppTextSize};
        bool closeMainWindowOnEscape_{};
        bool recursiveBrowsingEnabled_{false};
        bool showSubfoldersInBrowser_{false};
        bool rawJpegPairedOperationsEnabled_{false};
        browser::RawJpegDisplayPreference pairedRawJpegViewerPreference_{browser::RawJpegDisplayPreference::Raw};
        bool persistentThumbnailCacheEnabled_{true};
        bool defaultViewerToSecondaryMonitor_{false};
        bool suppressTreeSelectionChange_{};
        bool hasPersistedWindowBounds_{};
        DragMode dragMode_{DragMode::None};
        HBRUSH backgroundBrush_{};
        HBRUSH actionFieldBrush_{};
        HBRUSH detailsPanelBrush_{};
        HBRUSH menuBackgroundBrush_{};
        HFONT appTextUiFont_{};
        HFONT detailsPanelTitleFont_{};
        HFONT detailsPanelSummaryFont_{};
        HFONT detailsPanelBodyFont_{};
        hyperbrowse::render::ComPtr<ID2D1HwndRenderTarget> d2dRenderTarget_;
        hyperbrowse::render::ComPtr<IDWriteTextFormat> d2dToolbarTextFormat_;
        std::wstring startupLaunchPathOverride_;
        std::wstring startupFolderPath_;
        std::wstring startupSelectedImagePath_;
        std::vector<std::wstring> recentFolders_;
        std::vector<std::wstring> recentDestinationFolders_;
        std::vector<std::wstring> favoriteDestinationFolders_;
        std::wstring lastQuickSendDestination_;
        QuickSendModel quickSendModel_;
        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems_;
        std::unique_ptr<browser::BrowserModel> browserModel_;
        std::unique_ptr<browser::BrowserPane> browserPaneController_;
        std::unique_ptr<services::BatchConvertService> batchConvertService_;
        std::unique_ptr<services::FileOperationService> fileOperationService_;
        std::unique_ptr<FolderLoadCoordinator> folderLoadCoordinator_;
        std::unique_ptr<FolderWatchChangeCoordinator> folderWatchChangeCoordinator_;
        std::unique_ptr<FolderTreeController> folderTreeController_;
        FolderTreeDragController folderTreeDragController_;
        WindowAsyncMessageRouter asyncMessageRouter_;
        WindowTimerRouter timerRouter_;
        FileCommandController fileCommandController_;
        ViewCommandController viewCommandController_;
        std::unique_ptr<services::ThumbnailScheduler> detailsPanelThumbnailScheduler_;
        std::unique_ptr<services::UserMetadataStore> userMetadataStore_;
        std::unique_ptr<DiagnosticsWindow> diagnosticsWindow_;
        std::unique_ptr<viewer::ViewerWindow> viewerWindow_;
        std::unique_ptr<util::BackgroundExecutor> memoryPressureExecutor_;
        std::unique_ptr<util::BackgroundExecutor> cacheMaintenanceExecutor_;
        std::shared_ptr<struct PersistentThumbnailCacheMaintenanceState> cacheMaintenanceState_;
        mutable HWND shortcutReferenceWindow_{};
        std::wstring pendingTreeMouseSelectionPath_;
        HTREEITEM internalSelectionTreeDropItem_{};
        std::wstring internalSelectionTreeDropPath_;
        RECT detailsPanelRect_{};
        RECT persistedWindowBounds_{};
        RECT detailsPanelTabStripRect_{};
        std::array<RECT, 2> detailsPanelTabRects_{};
        std::wstring statusPrimaryText_;
        std::wstring statusSecondaryText_;
        RECT detailsPanelContentRect_{};
        RECT detailsPanelHistogramRect_{};
        RECT detailsPanelCloseButtonRect_{};
        RECT quickAccessDestinationPanelRect_{};
        RECT quickAccessDestinationViewportRect_{};
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
        int detailsPanelHotTabIndex_{-1};
        int detailsPanelPressedTabIndex_{-1};
        bool detailsPanelCloseButtonHot_{};
        bool detailsPanelCloseButtonPressed_{};
        bool detailsPanelHistogramTooltipAdded_{};
        RECT quickAccessSortButtonRect_{};
        bool quickAccessSortButtonHot_{};
        bool quickAccessSortButtonPressed_{};
        bool quickAccessSortTooltipAdded_{};
        int quickAccessHotRowIndex_{-1};
        int quickAccessHotButtonIndex_{-1};
        int quickAccessPressedRowIndex_{-1};
        int quickAccessPressedButtonIndex_{-1};
        int quickAccessScrollOffset_{};
        std::vector<HWND> quickAccessShortcutEdits_;
        bool updatingQuickAccessShortcutEdits_{};
        std::array<std::uint32_t, 64> detailsPanelHistogramRed_{};
        std::array<std::uint32_t, 64> detailsPanelHistogramGreen_{};
        std::array<std::uint32_t, 64> detailsPanelHistogramBlue_{};
        std::uint32_t detailsPanelHistogramPeak_{};
        std::uint64_t activeBatchConvertRequestId_{};
        std::uint64_t activeFileOperationRequestId_{};
        HWND foregroundWindowAtFileOperationStart_{};
        HWND focusWindowAtFileOperationStart_{};
        bool batchConvertActive_{};
        bool fileOperationActive_{};
        bool cacheMaintenanceActive_{};
        bool closePending_{};
        ULONGLONG closePendingSinceTick_{};
        bool closeWaitNoticeShown_{};
        std::size_t batchConvertCompleted_{};
        std::size_t batchConvertTotal_{};
        std::size_t batchConvertFailed_{};
        std::wstring batchConvertOutputFolder_;
        std::wstring batchConvertCurrentFile_;
        std::wstring activeTreeFolderOperationPath_;
        std::wstring activeTreeFolderRenamePath_;
        std::wstring pendingInlineRenameOriginalPath_;
        using PendingViewerDelete = ViewerPendingOperationState::DeleteRequest;
        using PendingViewerQuickSend = ViewerPendingOperationState::QuickSendRequest;
        struct FileOperationActivationContext
        {
            HWND activationRestoreWindow{};
            HWND focusRestoreWindow{};
        };
        struct FileOperationUndoRedoContext
        {
            UndoRedoOperation completedOperation{UndoRedoOperation::None};
        };
        struct FileOperationViewerContext
        {
            std::wstring viewerDeleteSourcePath;
            std::vector<std::wstring> viewerDeleteSourcePaths;
            std::wstring viewerDeletePreferredFocusPath;
            PendingViewerQuickSend viewerQuickSend;
        };
        struct FileOperationDeferredWatchContext
        {
            std::wstring deferredFolderWatchReloadPath;
            bool deferredFolderWatchTreeRefresh{};
        };
        struct FileOperationTreeContext
        {
            std::wstring treeFolderOperationPath;
            std::wstring treeFolderRenamePath;
            std::wstring treeFolderMoveSourcePath;
            std::wstring treeFolderMoveDestinationFolder;
        };
        struct FileOperationCompletionContext
        {
            FileOperationActivationContext activation;
            FileOperationUndoRedoContext undoRedo;
            FileOperationViewerContext viewer;
            FileOperationDeferredWatchContext deferredWatch;
            FileOperationTreeContext tree;
        };
        ViewerPendingOperationState viewerPendingOperations_;
        bool quickSendPopupActive_{};
        std::size_t quickSendPopupInitialDownCount_{};
        std::wstring activeFileOperationLabel_;
        std::wstring activeTreeFolderMoveSourcePath_;
        std::wstring activeTreeFolderMoveDestinationFolder_;
        int viewerZoomPercent_{};
        bool viewerWindowActive_{};
        bool nvJpegEnabled_{};
        bool libRawOutOfProcessEnabled_{true};
        bool thumbnailMemoryPressureActive_{};
        bool showPressureStateInStatusBar_{};
        bool memoryPressureSampleQueued_{};
        unsigned int memoryPressureRecoveryClearSampleCount_{};
        util::ResourceProfile resourceProfile_{util::ResourceProfile::Balanced};
        int prefetchDepthOverride_{util::kAutomaticPrefetchDepth};
        std::size_t thumbnailCacheCapacityOverrideBytes_{};
        std::size_t metadataCacheCapacityOverrideEntries_{};
        browser::ThumbnailSizePreset thumbnailSizePreset_{static_cast<browser::ThumbnailSizePreset>(192)};
        browser::BrowserSortMode sortMode_{static_cast<browser::BrowserSortMode>(0)};
        bool sortAscending_{true};
        bool compactThumbnailLayout_{true};
        bool thumbnailDetailsVisible_{true};
        UINT slideshowIntervalMs_{3000};
        viewer::TransitionStyle slideshowTransitionStyle_{};
        UINT slideshowTransitionDurationMs_{350};
        bool useSlideshowTransition_{};
        bool detailsStripVisible_{true};
        viewer::MouseWheelBehavior viewerMouseWheelBehavior_{};
        bool invertKeyboardPanning_{};
        viewer::EscapeKeyBehavior viewerEscapeKeyBehavior_{};
        UINT_PTR memoryPressureTimerId_{};
        UINT_PTR displaySurfaceRecoveryTimerId_{};
        DisplaySurfaceRecoveryPolicy displaySurfaceRecoveryPolicy_;
        bool sessionNotificationRegistered_{};
        HPOWERNOTIFY consoleDisplayNotify_{};
        HPOWERNOTIFY monitorPowerNotify_{};

    };
}
