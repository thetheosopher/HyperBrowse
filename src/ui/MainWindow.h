#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hyperbrowse::browser
{
    class BrowserModel;
    class BrowserPane;
}

namespace hyperbrowse::services
{
    class FolderEnumerationService;
}

namespace hyperbrowse::viewer
{
    class ViewerWindow;
}

namespace hyperbrowse::ui
{
    class MainWindow
    {
    public:
        explicit MainWindow(HINSTANCE instance);
        ~MainWindow();

        bool Create();
        void Show(int nCmdShow) const;

    private:
        struct FolderTreeNodeData
        {
            std::wstring path;
            bool childrenLoaded{};
        };

        static constexpr const wchar_t* kWindowClassName = L"HyperBrowseMainWindow";
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
        };

        bool RegisterWindowClass() const;
        bool CreateMenuBar();
        bool CreateChildWindows();
        void InitializeFolderTree();
        void PopulateSpecialFolderRoots();
        void PopulateDriveRoots();
        void RefreshFolderTree();
        HTREEITEM InsertFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath);
        void AddFolderTreePlaceholder(HTREEITEM parentItem);
        void EnsureFolderTreeChildren(HTREEITEM item);
        void ShowSelectedFolderInTree();
        void SelectFolderInTree(const std::wstring& folderPath);
        HTREEITEM FindChildFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath) const;
        FolderTreeNodeData* GetFolderTreeNodeData(HTREEITEM item) const;
        std::wstring GetSelectedFolderTreePath() const;
        void LayoutChildren();
        void UpdateStatusText() const;
        void UpdateMenuState() const;
        void UpdateWindowTitle() const;
        void ApplyTheme();
        void LoadWindowState();
        void SaveWindowState() const;
        bool HandleCommand(UINT commandId);
        void LoadFolderAsync(std::wstring folderPath);
        void RefreshBrowserPane();
        void OpenItemInViewer(int modelIndex);
        LRESULT OnFolderEnumerationMessage(LPARAM lParam);
        LRESULT OnFolderTreeNotify(LPARAM lParam);
        LRESULT OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView);
        LRESULT OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView);
        LRESULT OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam);
        LRESULT OnViewerZoomMessage(LPARAM lParam);
        LRESULT OnViewerActivityMessage(LPARAM lParam);
        LRESULT OnViewerClosedMessage();

        void SetBrowserMode(BrowserMode mode);
        void ToggleRecursiveBrowsing();
        void SetThemeMode(ThemeMode themeMode);
        ThemePalette GetThemePalette() const;
        std::wstring BuildShellStateText() const;
        std::wstring GetCurrentFolderDisplayName() const;

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
        HWND treePane_{};
        HWND browserPane_{};
        HWND statusBar_{};
        HIMAGELIST treeImageList_{};
        HMENU menu_{};
        int leftPaneWidth_{kDefaultLeftPaneWidth};
        BrowserMode browserMode_{BrowserMode::Thumbnails};
        ThemeMode themeMode_{ThemeMode::Light};
        bool recursiveBrowsingEnabled_{false};
        bool suppressTreeSelectionChange_{};
        DragMode dragMode_{DragMode::None};
        HBRUSH backgroundBrush_{};
        std::wstring startupFolderPath_;
        std::vector<std::unique_ptr<FolderTreeNodeData>> folderTreeNodes_;
        std::unique_ptr<browser::BrowserModel> browserModel_;
        std::unique_ptr<browser::BrowserPane> browserPaneController_;
        std::unique_ptr<services::FolderEnumerationService> folderEnumerationService_;
        std::unique_ptr<viewer::ViewerWindow> viewerWindow_;
        std::uint64_t activeEnumerationRequestId_{};
        int viewerZoomPercent_{};
        bool viewerWindowActive_{};
    };
}
