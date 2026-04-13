#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "browser/BrowserModel.h"

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
    enum class FileOperationType : int;
    struct FileOperationUpdate;
    class BatchConvertService;
    class FileOperationService;
    class FolderEnumerationService;
    class FolderTreeEnumerationService;
    class FolderWatchService;
}

namespace hyperbrowse::viewer
{
    class ViewerWindow;
}

namespace hyperbrowse::ui
{
    class DiagnosticsWindow;

    class MainWindow
    {
    public:
        explicit MainWindow(HINSTANCE instance);
        ~MainWindow();

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

        enum class ThemeMode
        {
            Light = 0,
            Dark = 1
        };

        enum class DragMode
        {
            None,
            Splitter
        };

        struct ThemePalette
        {
            COLORREF windowBackground;
            COLORREF paneBackground;
            COLORREF text;
            COLORREF treeLine;
            COLORREF splitter;
            COLORREF actionStripBackground;
            COLORREF actionStripBorder;
            COLORREF actionFieldBackground;
            COLORREF accent;
            COLORREF accentFill;
            COLORREF accentText;
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
        FolderTreeNodeData* GetFolderTreeNodeData(HTREEITEM item) const;
        std::wstring GetSelectedFolderTreePath() const;
        void LayoutChildren();
        void UpdateStatusText() const;
        void UpdateMenuState() const;
        void UpdateActionStripState() const;
        void UpdateWindowTitle() const;
        void ApplyTheme();
        void LoadWindowState();
        void SaveWindowState() const;
        bool HandleCommand(UINT commandId);
        void OpenFolder();
        void LoadFolderAsync(std::wstring folderPath);
        void RefreshBrowserPane();
        void OpenItemInViewer(int modelIndex, bool preferSecondaryMonitor = false);
        void OpenItemsInViewer(std::vector<browser::BrowserItem> items,
                       int selectedIndex,
                       bool startSlideshow,
                       bool preferSecondaryMonitor = false);
        std::vector<browser::BrowserItem> CollectItemsForScope(bool selectionScope) const;
        bool ChooseFolder(std::wstring* folderPath) const;
        bool HasSelectedJpegItems() const;
        void ShowBrowserContextMenu(POINT screenPoint);
        void ShowDiagnosticsSnapshot();
        void ResetDiagnosticsState();
        void ShowImageInformation();
        void StartCopySelection();
        void StartMoveSelection();
        void StartDeleteSelection(bool permanent);
        void StartFileOperation(services::FileOperationType type,
                    std::vector<std::wstring> sourcePaths,
                    std::wstring destinationFolder);
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
        LRESULT OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBatchConvertMessage(LPARAM lParam);
        LRESULT OnFileOperationMessage(LPARAM lParam);
        LRESULT OnViewerZoomMessage(LPARAM lParam);
        LRESULT OnViewerActivityMessage(LPARAM lParam);
        LRESULT OnViewerClosedMessage();

        void SetBrowserMode(BrowserMode mode);
        void ToggleRecursiveBrowsing();
        void ApplyThumbnailDisplaySettings();
        void SetThemeMode(ThemeMode themeMode);
        void ShowSortActionMenu();
        void ShowThumbnailSizeActionMenu();
        void ShowThemeActionMenu();
        void UpdateDetailsStripText();
        ThemePalette GetThemePalette() const;
        std::wstring BuildShellStateText() const;
        std::wstring GetCurrentFolderDisplayName() const;
        bool DrawActionButton(const DRAWITEMSTRUCT& drawItem) const;

        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        void OnSize();
        void OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const;
        void OnLButtonDown(int x, int y);
        void OnLButtonUp();
        void OnLButtonDoubleClick(int x, int y);
        void OnMouseMove(int x, int y);
        bool IsOverSplitter(int x) const;

        HINSTANCE instance_{};
        HWND hwnd_{};
        HWND openFolderButton_{};
        HWND recursiveButton_{};
        HWND thumbnailModeButton_{};
        HWND detailsModeButton_{};
        HWND sortMenuButton_{};
        HWND sizeMenuButton_{};
        HWND themeMenuButton_{};
        HWND filterEdit_{};
        HWND copyButton_{};
        HWND moveButton_{};
        HWND deleteButton_{};
        HWND treePane_{};
        HWND browserPane_{};
        HWND statusBar_{};
        HWND detailsStrip_{};
        HIMAGELIST treeImageList_{};
        HMENU menu_{};
        HACCEL accelerators_{};
        int leftPaneWidth_{kDefaultLeftPaneWidth};
        BrowserMode browserMode_{BrowserMode::Thumbnails};
        ThemeMode themeMode_{ThemeMode::Light};
        bool recursiveBrowsingEnabled_{false};
        bool suppressTreeSelectionChange_{};
        DragMode dragMode_{DragMode::None};
        HBRUSH backgroundBrush_{};
        HBRUSH actionFieldBrush_{};
        std::wstring startupFolderPath_;
        std::vector<std::unique_ptr<FolderTreeNodeData>> folderTreeNodes_;
        std::unique_ptr<browser::BrowserModel> browserModel_;
        std::unique_ptr<browser::BrowserPane> browserPaneController_;
        std::unique_ptr<services::BatchConvertService> batchConvertService_;
        std::unique_ptr<services::FileOperationService> fileOperationService_;
        std::unique_ptr<services::FolderEnumerationService> folderEnumerationService_;
        std::unique_ptr<services::FolderTreeEnumerationService> folderTreeEnumerationService_;
        std::unique_ptr<services::FolderWatchService> folderWatchService_;
        std::unique_ptr<DiagnosticsWindow> diagnosticsWindow_;
        std::unique_ptr<viewer::ViewerWindow> viewerWindow_;
        std::unordered_map<std::uint64_t, HTREEITEM> pendingFolderTreeEnumerationItems_;
        std::wstring pendingTreeSelectionPath_;
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
        std::wstring activeFileOperationLabel_;
        int viewerZoomPercent_{};
        bool viewerWindowActive_{};
        bool nvJpegEnabled_{};
        bool libRawOutOfProcessEnabled_{true};
        browser::ThumbnailSizePreset thumbnailSizePreset_{static_cast<browser::ThumbnailSizePreset>(192)};
        browser::BrowserSortMode sortMode_{static_cast<browser::BrowserSortMode>(0)};
        bool sortAscending_{true};
        bool compactThumbnailLayout_{};
        bool thumbnailDetailsVisible_{true};
        UINT slideshowIntervalMs_{3000};
        bool detailsStripVisible_{true};
    };
}
