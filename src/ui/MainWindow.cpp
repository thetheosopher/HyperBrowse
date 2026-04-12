#include "ui/MainWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "services/FolderEnumerationService.h"
#include "util/Log.h"
#include "util/Timing.h"
#include "viewer/ViewerWindow.h"

namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueLeftPaneWidth[] = L"LeftPaneWidth";
    constexpr wchar_t kRegistryValueBrowserMode[] = L"BrowserMode";
    constexpr wchar_t kRegistryValueRecursiveBrowsing[] = L"RecursiveBrowsing";
    constexpr wchar_t kRegistryValueThemeMode[] = L"ThemeMode";
    constexpr wchar_t kRegistryValueSelectedFolderPath[] = L"SelectedFolderPath";

    constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacyAttribute = 19;

    constexpr UINT ID_FILE_REFRESH_TREE = 1001;
    constexpr UINT ID_FILE_EXIT = 1002;
    constexpr UINT ID_VIEW_THUMBNAILS = 2001;
    constexpr UINT ID_VIEW_DETAILS = 2002;
    constexpr UINT ID_VIEW_RECURSIVE = 2003;
    constexpr UINT ID_VIEW_THEME_LIGHT = 2101;
    constexpr UINT ID_VIEW_THEME_DARK = 2102;
    constexpr UINT ID_VIEW_SORT_FILENAME = 2201;
    constexpr UINT ID_VIEW_SORT_MODIFIED = 2202;
    constexpr UINT ID_VIEW_SORT_SIZE = 2203;
    constexpr UINT ID_VIEW_SORT_DIMENSIONS = 2204;
    constexpr UINT ID_VIEW_SORT_TYPE = 2205;
    constexpr UINT ID_VIEW_SORT_RANDOM = 2206;
    constexpr UINT ID_HELP_ABOUT = 9001;

    bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    bool TryReadStringValue(HKEY key, const wchar_t* valueName, std::wstring* value)
    {
        value->clear();

        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS
            || (type != REG_SZ && type != REG_EXPAND_SZ)
            || size < sizeof(wchar_t))
        {
            return false;
        }

        std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1, L'\0');
        if (RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &size) != ERROR_SUCCESS)
        {
            return false;
        }

        *value = buffer.data();
        return true;
    }

    void WriteStringValue(HKEY key, const wchar_t* valueName, std::wstring_view value)
    {
        const DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.data()), size);
    }

    void ApplyWindowFrameTheme(HWND hwnd, bool useDarkMode)
    {
        const BOOL enabled = useDarkMode ? TRUE : FALSE;
        const HRESULT result = DwmSetWindowAttribute(
            hwnd,
            kDwmUseImmersiveDarkModeAttribute,
            &enabled,
            sizeof(enabled));

        if (FAILED(result))
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmUseImmersiveDarkModeLegacyAttribute,
                &enabled,
                sizeof(enabled));
        }
    }

    std::wstring GetFolderDisplayName(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return L"No Folder";
        }

        const fs::path path(folderPath);
        const std::wstring leaf = path.filename().wstring();
        return leaf.empty() ? std::wstring(folderPath) : leaf;
    }

    std::wstring TrimForStatus(std::wstring_view value, std::size_t maxLength)
    {
        if (value.size() <= maxLength)
        {
            return std::wstring(value);
        }

        return std::wstring(value.substr(0, maxLength > 3 ? maxLength - 3 : maxLength)) + L"...";
    }

    std::wstring NormalizeFolderPath(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        while (path.size() > 3 && !path.empty() && path.back() == L'\\')
        {
            path.pop_back();
        }

        if (path.size() == 2 && path[1] == L':')
        {
            path.push_back(L'\\');
        }

        return path;
    }

    bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
    {
        const std::wstring normalizedLeft = NormalizeFolderPath(std::wstring(lhs));
        const std::wstring normalizedRight = NormalizeFolderPath(std::wstring(rhs));
        return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
    }

    struct ShellTreeItemInfo
    {
        std::wstring displayName;
        int iconIndex{};
        int openIconIndex{};
    };

    std::wstring FormatDriveDisplayName(const std::wstring& rootPath)
    {
        wchar_t volumeName[MAX_PATH]{};
        if (GetVolumeInformationW(
            rootPath.c_str(),
            volumeName,
            static_cast<DWORD>(std::size(volumeName)),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            0) != FALSE
            && volumeName[0] != L'\0')
        {
            std::wstring label = volumeName;
            label.append(L" (");
            label.append(rootPath.substr(0, 2));
            label.append(L")");
            return label;
        }

        return rootPath;
    }

    std::wstring TryGetKnownFolderPath(REFKNOWNFOLDERID folderId)
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return NormalizeFolderPath(std::move(path));
    }

    ShellTreeItemInfo QueryShellTreeItemInfo(const std::wstring& folderPath)
    {
        ShellTreeItemInfo info;

        SHFILEINFOW shellInfo{};
        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
        {
            info.displayName = shellInfo.szDisplayName;
            info.iconIndex = shellInfo.iIcon;
        }

        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_OPENICON | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
        {
            info.openIconIndex = shellInfo.iIcon;
        }
        else
        {
            info.openIconIndex = info.iconIndex;
        }

        if (info.displayName.empty())
        {
            const fs::path path(folderPath);
            const std::wstring normalizedRoot = NormalizeFolderPath(path.root_path().wstring());
            info.displayName = FolderPathsEqual(normalizedRoot, folderPath)
                ? FormatDriveDisplayName(folderPath)
                : GetFolderDisplayName(folderPath);
        }

        return info;
    }

    bool FolderHasChildDirectories(const std::wstring& folderPath)
    {
        std::error_code error;
        const fs::directory_options options = fs::directory_options::skip_permission_denied;
        for (fs::directory_iterator iterator(fs::path(folderPath), options, error), end;
             iterator != end;
             iterator.increment(error))
        {
            if (error)
            {
                error.clear();
                continue;
            }

            std::error_code statusError;
            if (iterator->is_directory(statusError) && !statusError)
            {
                return true;
            }
        }

        return false;
    }

    std::vector<std::wstring> EnumerateChildDirectories(const std::wstring& folderPath)
    {
        std::vector<std::wstring> childFolders;
        std::error_code error;
        const fs::directory_options options = fs::directory_options::skip_permission_denied;
        for (fs::directory_iterator iterator(fs::path(folderPath), options, error), end;
             iterator != end;
             iterator.increment(error))
        {
            if (error)
            {
                error.clear();
                continue;
            }

            std::error_code statusError;
            if (!iterator->is_directory(statusError) || statusError)
            {
                continue;
            }

            childFolders.push_back(NormalizeFolderPath(iterator->path().wstring()));
        }

        std::sort(childFolders.begin(), childFolders.end(), [](const std::wstring& lhs, const std::wstring& rhs)
        {
            return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
        });

        return childFolders;
    }

    hyperbrowse::browser::BrowserSortMode SortModeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_SORT_FILENAME:
            return hyperbrowse::browser::BrowserSortMode::FileName;
        case ID_VIEW_SORT_MODIFIED:
            return hyperbrowse::browser::BrowserSortMode::ModifiedDate;
        case ID_VIEW_SORT_SIZE:
            return hyperbrowse::browser::BrowserSortMode::FileSize;
        case ID_VIEW_SORT_DIMENSIONS:
            return hyperbrowse::browser::BrowserSortMode::Dimensions;
        case ID_VIEW_SORT_TYPE:
            return hyperbrowse::browser::BrowserSortMode::FileType;
        case ID_VIEW_SORT_RANDOM:
        default:
            return hyperbrowse::browser::BrowserSortMode::Random;
        }
    }

    UINT CommandIdFromSortMode(hyperbrowse::browser::BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case hyperbrowse::browser::BrowserSortMode::FileName:
            return ID_VIEW_SORT_FILENAME;
        case hyperbrowse::browser::BrowserSortMode::ModifiedDate:
            return ID_VIEW_SORT_MODIFIED;
        case hyperbrowse::browser::BrowserSortMode::FileSize:
            return ID_VIEW_SORT_SIZE;
        case hyperbrowse::browser::BrowserSortMode::Dimensions:
            return ID_VIEW_SORT_DIMENSIONS;
        case hyperbrowse::browser::BrowserSortMode::FileType:
            return ID_VIEW_SORT_TYPE;
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return ID_VIEW_SORT_RANDOM;
        }
    }
}

namespace hyperbrowse::ui
{
    MainWindow::MainWindow(HINSTANCE instance)
        : instance_(instance)
        , browserModel_(std::make_unique<browser::BrowserModel>())
        , browserPaneController_(std::make_unique<browser::BrowserPane>(instance))
        , folderEnumerationService_(std::make_unique<services::FolderEnumerationService>())
        , viewerWindow_(std::make_unique<viewer::ViewerWindow>(instance))
    {
    }

    MainWindow::~MainWindow()
    {
        if (folderEnumerationService_)
        {
            folderEnumerationService_->Cancel();
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }
    }

    bool MainWindow::Create()
    {
        util::ScopedTimer timer{L"MainWindow::Create"};

        if (!RegisterWindowClass())
        {
            return false;
        }

        LoadWindowState();

        hwnd_ = CreateWindowExW(
            0,
            kWindowClassName,
            L"HyperBrowse",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1400,
            900,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_)
        {
            util::LogLastError(L"CreateWindowExW(MainWindow)");
            return false;
        }

        if (!CreateMenuBar() || !CreateChildWindows())
        {
            return false;
        }

        ApplyTheme();
        UpdateMenuState();
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        return true;
    }

    void MainWindow::Show(int nCmdShow) const
    {
        ShowWindow(hwnd_, nCmdShow);
        UpdateWindow(hwnd_);
    }

    bool MainWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MainWindow::WindowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

        return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool MainWindow::CreateMenuBar()
    {
        menu_ = CreateMenu();
        HMENU fileMenu = CreatePopupMenu();
        HMENU viewMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        HMENU themeMenu = CreatePopupMenu();
        HMENU helpMenu = CreatePopupMenu();

        if (!menu_ || !fileMenu || !viewMenu || !sortMenu || !themeMenu || !helpMenu)
        {
            return false;
        }

        AppendMenuW(fileMenu, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_EXIT, L"E&xit");

        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_RECURSIVE, L"&Recursive Browsing");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(themeMenu, MF_STRING, ID_VIEW_THEME_LIGHT, L"&Light");
        AppendMenuW(themeMenu, MF_STRING, ID_VIEW_THEME_DARK, L"&Dark");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"&Theme");

        AppendMenuW(helpMenu, MF_STRING, ID_HELP_ABOUT, L"&About");

        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

        return SetMenu(hwnd_, menu_) != FALSE;
    }

    bool MainWindow::CreateChildWindows()
    {
        treePane_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_TREEVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        statusBar_ = CreateWindowExW(
            0,
            STATUSCLASSNAMEW,
            nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!treePane_ || !statusBar_)
        {
            util::LogLastError(L"CreateChildWindows");
            return false;
        }

        if (!browserPaneController_ || !browserPaneController_->Create(hwnd_))
        {
            util::LogError(L"Failed to create the browser pane control");
            return false;
        }

        browserPane_ = browserPaneController_->Hwnd();

        const HFONT defaultGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(treePane_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);

        SHFILEINFOW shellInfo{};
        treeImageList_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            L"C:\\",
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
        if (treeImageList_)
        {
            TreeView_SetImageList(treePane_, treeImageList_, TVSIL_NORMAL);
        }

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);

        InitializeFolderTree();
        if (!startupFolderPath_.empty())
        {
            std::error_code error;
            if (fs::is_directory(fs::path(startupFolderPath_), error) && !error)
            {
                LoadFolderAsync(startupFolderPath_);
            }
        }
        RefreshBrowserPane();
        UpdateStatusText();
        LayoutChildren();
        return true;
    }

    void MainWindow::InitializeFolderTree()
    {
        if (!treePane_)
        {
            return;
        }

        suppressTreeSelectionChange_ = true;
        TreeView_DeleteAllItems(treePane_);
        folderTreeNodes_.clear();
        PopulateSpecialFolderRoots();
        PopulateDriveRoots();
        suppressTreeSelectionChange_ = false;
        ShowSelectedFolderInTree();
    }

    void MainWindow::PopulateSpecialFolderRoots()
    {
        const KNOWNFOLDERID specialFolderIds[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Pictures,
        };

        for (const KNOWNFOLDERID& specialFolderId : specialFolderIds)
        {
            const std::wstring folderPath = TryGetKnownFolderPath(specialFolderId);
            if (folderPath.empty())
            {
                continue;
            }

            std::error_code error;
            if (!fs::is_directory(fs::path(folderPath), error) || error)
            {
                continue;
            }

            if (!FindChildFolderTreeItem(nullptr, folderPath))
            {
                InsertFolderTreeItem(TVI_ROOT, folderPath);
            }
        }
    }

    void MainWindow::PopulateDriveRoots()
    {
        const DWORD driveMask = GetLogicalDrives();
        for (wchar_t driveLetter = L'A'; driveLetter <= L'Z'; ++driveLetter)
        {
            const DWORD driveBit = 1UL << (driveLetter - L'A');
            if ((driveMask & driveBit) == 0)
            {
                continue;
            }

            std::wstring drivePath;
            drivePath.push_back(driveLetter);
            drivePath.append(L":\\");

            const UINT driveType = GetDriveTypeW(drivePath.c_str());
            if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN)
            {
                continue;
            }

            if (!FindChildFolderTreeItem(nullptr, drivePath))
            {
                InsertFolderTreeItem(TVI_ROOT, drivePath);
            }
        }
    }

    void MainWindow::RefreshFolderTree()
    {
        InitializeFolderTree();
    }

    HTREEITEM MainWindow::InsertFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath)
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        const ShellTreeItemInfo shellInfo = QueryShellTreeItemInfo(normalizedPath);

        auto nodeData = std::make_unique<FolderTreeNodeData>();
        nodeData->path = normalizedPath;
        nodeData->childrenLoaded = false;
        FolderTreeNodeData* rawNodeData = nodeData.get();
        folderTreeNodes_.push_back(std::move(nodeData));

        TVINSERTSTRUCTW item{};
        item.hParent = parentItem;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
        item.item.pszText = const_cast<LPWSTR>(shellInfo.displayName.c_str());
        item.item.iImage = shellInfo.iconIndex;
        item.item.iSelectedImage = shellInfo.openIconIndex;
        item.item.lParam = reinterpret_cast<LPARAM>(rawNodeData);

        const HTREEITEM insertedItem = TreeView_InsertItem(treePane_, &item);
        if (insertedItem && FolderHasChildDirectories(normalizedPath))
        {
            AddFolderTreePlaceholder(insertedItem);
        }

        return insertedItem;
    }

    void MainWindow::AddFolderTreePlaceholder(HTREEITEM parentItem)
    {
        TVINSERTSTRUCTW placeholder{};
        placeholder.hParent = parentItem;
        placeholder.hInsertAfter = TVI_LAST;
        placeholder.item.mask = TVIF_TEXT;
        placeholder.item.pszText = const_cast<LPWSTR>(L"");
        TreeView_InsertItem(treePane_, &placeholder);
    }

    void MainWindow::EnsureFolderTreeChildren(HTREEITEM item)
    {
        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->childrenLoaded)
        {
            return;
        }

        nodeData->childrenLoaded = true;

        HTREEITEM childItem = TreeView_GetChild(treePane_, item);
        while (childItem)
        {
            HTREEITEM nextSibling = TreeView_GetNextSibling(treePane_, childItem);
            if (!GetFolderTreeNodeData(childItem))
            {
                TreeView_DeleteItem(treePane_, childItem);
            }
            childItem = nextSibling;
        }

        for (const std::wstring& childFolderPath : EnumerateChildDirectories(nodeData->path))
        {
            InsertFolderTreeItem(item, childFolderPath);
        }
    }

    void MainWindow::ShowSelectedFolderInTree()
    {
        if (!treePane_ || !browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        SelectFolderInTree(browserModel_->FolderPath());
    }

    void MainWindow::SelectFolderInTree(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty())
        {
            return;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        HTREEITEM currentItem = FindChildFolderTreeItem(nullptr, normalizedPath);
        if (currentItem)
        {
            suppressTreeSelectionChange_ = true;
            TreeView_SelectItem(treePane_, currentItem);
            TreeView_EnsureVisible(treePane_, currentItem);
            suppressTreeSelectionChange_ = false;
            return;
        }

        const fs::path targetPath(normalizedPath);
        const std::wstring rootPath = NormalizeFolderPath(targetPath.root_path().wstring());
        if (rootPath.empty())
        {
            return;
        }

        currentItem = FindChildFolderTreeItem(nullptr, rootPath);
        if (!currentItem)
        {
            return;
        }

        if (!FolderPathsEqual(normalizedPath, rootPath))
        {
            fs::path currentPath(rootPath);
            for (const auto& segment : targetPath.relative_path())
            {
                if (segment.empty())
                {
                    continue;
                }

                currentPath /= segment;
                TreeView_Expand(treePane_, currentItem, TVE_EXPAND);
                EnsureFolderTreeChildren(currentItem);

                currentItem = FindChildFolderTreeItem(currentItem, currentPath.wstring());
                if (!currentItem)
                {
                    return;
                }
            }
        }

        suppressTreeSelectionChange_ = true;
        TreeView_SelectItem(treePane_, currentItem);
        TreeView_EnsureVisible(treePane_, currentItem);
        suppressTreeSelectionChange_ = false;
    }

    HTREEITEM MainWindow::FindChildFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath) const
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        HTREEITEM currentItem = parentItem
            ? TreeView_GetChild(treePane_, parentItem)
            : TreeView_GetRoot(treePane_);
        while (currentItem)
        {
            FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
            if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
            {
                return currentItem;
            }

            currentItem = TreeView_GetNextSibling(treePane_, currentItem);
        }

        return nullptr;
    }

    MainWindow::FolderTreeNodeData* MainWindow::GetFolderTreeNodeData(HTREEITEM item) const
    {
        if (!treePane_ || !item)
        {
            return nullptr;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_PARAM;
        treeItem.hItem = item;
        if (TreeView_GetItem(treePane_, &treeItem) == FALSE)
        {
            return nullptr;
        }

        return reinterpret_cast<FolderTreeNodeData*>(treeItem.lParam);
    }

    std::wstring MainWindow::GetSelectedFolderTreePath() const
    {
        if (!treePane_)
        {
            return {};
        }

        const HTREEITEM selectedItem = TreeView_GetSelection(treePane_);
        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(selectedItem);
        return nodeData ? nodeData->path : std::wstring{};
    }

    LRESULT MainWindow::OnFolderTreeNotify(LPARAM lParam)
    {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (!header || header->hwndFrom != treePane_)
        {
            return 0;
        }

        switch (header->code)
        {
        case TVN_ITEMEXPANDINGW:
            return OnFolderTreeItemExpanding(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_SELCHANGEDW:
            return OnFolderTreeSelectionChanged(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        default:
            return 0;
        }
    }

    LRESULT MainWindow::OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView)
    {
        if (suppressTreeSelectionChange_)
        {
            return 0;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(treeView.itemNew.hItem);
        if (!nodeData)
        {
            return 0;
        }

        if (!browserModel_ || !FolderPathsEqual(browserModel_->FolderPath(), nodeData->path))
        {
            LoadFolderAsync(nodeData->path);
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView)
    {
        if ((treeView.action & TVE_EXPAND) != 0)
        {
            EnsureFolderTreeChildren(treeView.itemNew.hItem);
        }

        return 0;
    }

    void MainWindow::LayoutChildren()
    {
        if (!hwnd_ || !treePane_ || !browserPane_ || !statusBar_)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);

        SendMessageW(statusBar_, WM_SIZE, 0, 0);

        RECT statusRect{};
        GetWindowRect(statusBar_, &statusRect);
        const int statusHeight = statusRect.bottom - statusRect.top;

        const int clientWidth = client.right - client.left;
        const int clientHeight = std::max(0, static_cast<int>(client.bottom - client.top) - statusHeight);

        const int maxLeft = std::max(kMinLeftPaneWidth, clientWidth - kMinRightPaneWidth - kSplitterWidth);
        leftPaneWidth_ = std::clamp(leftPaneWidth_, kMinLeftPaneWidth, maxLeft);

        MoveWindow(treePane_, 0, 0, leftPaneWidth_, clientHeight, TRUE);
        MoveWindow(browserPane_, leftPaneWidth_ + kSplitterWidth, 0,
                   clientWidth - leftPaneWidth_ - kSplitterWidth, clientHeight, TRUE);

        InvalidateRect(hwnd_, nullptr, TRUE);
        UpdateStatusText();
    }

    void MainWindow::UpdateStatusText() const
    {
        if (!statusBar_)
        {
            return;
        }

        int parts[] = {240, 650, 930, -1};
        SendMessageW(statusBar_, SB_SETPARTS, static_cast<WPARAM>(std::size(parts)), reinterpret_cast<LPARAM>(parts));

        const std::wstring folderText = browserModel_ && !browserModel_->FolderPath().empty()
            ? L"Folder: " + TrimForStatus(GetCurrentFolderDisplayName(), 28)
            : L"Folder: none selected";

        std::wstring progressText = L"Folder totals: n/a";
        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            if (browserModel_->HasError())
            {
                progressText = L"Load failed: " + TrimForStatus(browserModel_->ErrorMessage(), 60);
            }
            else
            {
                progressText = L"Images: " + std::to_wstring(browserModel_->TotalCount())
                    + L" | Size: " + browser::FormatByteSize(browserModel_->TotalBytes())
                    + L" | " + std::wstring(browserModel_->IsEnumerating() ? L"Loading..." : L"Loaded");
            }
        }

        const std::uint64_t selectedCount = browserPaneController_ ? browserPaneController_->SelectedCount() : 0;
        const std::uint64_t selectedBytes = browserPaneController_ ? browserPaneController_->SelectedBytes() : 0;
        const std::wstring selectionText = L"Selection: " + std::to_wstring(selectedCount)
            + L" images | " + browser::FormatByteSize(selectedBytes);

        const std::wstring shellStateText = BuildShellStateText();

        SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(folderText.c_str()));
        SendMessageW(statusBar_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(progressText.c_str()));
        SendMessageW(statusBar_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(selectionText.c_str()));
        SendMessageW(statusBar_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(shellStateText.c_str()));
    }

    void MainWindow::RefreshBrowserPane()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->RefreshFromModel();
    }

    void MainWindow::OpenItemInViewer(int modelIndex)
    {
        if (!browserModel_ || !browserPaneController_ || !viewerWindow_)
        {
            return;
        }

        const auto& modelItems = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
        {
            return;
        }

        std::vector<int> orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        std::vector<browser::BrowserItem> viewerItems;
        viewerItems.reserve(orderedModelIndices.size());
        int selectedViewerIndex = -1;
        for (int orderedModelIndex : orderedModelIndices)
        {
            if (orderedModelIndex < 0 || orderedModelIndex >= static_cast<int>(modelItems.size()))
            {
                continue;
            }

            viewerItems.push_back(modelItems[static_cast<std::size_t>(orderedModelIndex)]);
            if (orderedModelIndex == modelIndex)
            {
                selectedViewerIndex = static_cast<int>(viewerItems.size()) - 1;
            }
        }

        if (selectedViewerIndex < 0)
        {
            return;
        }

        if (viewerWindow_->Open(hwnd_, std::move(viewerItems), selectedViewerIndex, themeMode_ == ThemeMode::Dark))
        {
            viewerWindowActive_ = true;
            UpdateStatusText();
        }
    }

    void MainWindow::UpdateMenuState() const
    {
        if (!menu_)
        {
            return;
        }

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAILS,
            ID_VIEW_DETAILS,
            browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
            MF_BYCOMMAND);

        CheckMenuItem(
            menu_,
            ID_VIEW_RECURSIVE,
            MF_BYCOMMAND | (recursiveBrowsingEnabled_ ? MF_CHECKED : MF_UNCHECKED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THEME_LIGHT,
            ID_VIEW_THEME_DARK,
            themeMode_ == ThemeMode::Light ? ID_VIEW_THEME_LIGHT : ID_VIEW_THEME_DARK,
            MF_BYCOMMAND);

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_RANDOM,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);

        if (hwnd_)
        {
            DrawMenuBar(hwnd_);
        }
    }

    void MainWindow::UpdateWindowTitle() const
    {
        if (!hwnd_)
        {
            return;
        }

        std::wstring title = L"HyperBrowse";
        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            title.append(L" - ");
            title.append(GetCurrentFolderDisplayName());
        }

        title.append(L" - ");
        title.append(browserMode_ == BrowserMode::Thumbnails ? L"Thumbnail Shell" : L"Details Shell");
        if (recursiveBrowsingEnabled_)
        {
            title.append(L" - Recursive");
        }

        title.append(L" - ");
        title.append(themeMode_ == ThemeMode::Dark ? L"Dark" : L"Light");
        SetWindowTextW(hwnd_, title.c_str());
    }

    void MainWindow::ApplyTheme()
    {
        const ThemePalette palette = GetThemePalette();

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
            backgroundBrush_ = nullptr;
        }

        backgroundBrush_ = CreateSolidBrush(palette.windowBackground);

        if (hwnd_)
        {
            ApplyWindowFrameTheme(hwnd_, themeMode_ == ThemeMode::Dark);
        }

        if (treePane_)
        {
            TreeView_SetBkColor(treePane_, palette.paneBackground);
            TreeView_SetTextColor(treePane_, palette.text);
            TreeView_SetLineColor(treePane_, palette.treeLine);
            InvalidateRect(treePane_, nullptr, TRUE);
        }

        if (browserPaneController_)
        {
            browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (viewerWindow_)
        {
            viewerWindow_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (hwnd_)
        {
            SetWindowPos(
                hwnd_,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        }
    }

    void MainWindow::LoadWindowState()
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            DWORD value = 0;

            if (TryReadDwordValue(key, kRegistryValueLeftPaneWidth, &value))
            {
                leftPaneWidth_ = static_cast<int>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueBrowserMode, &value) && value <= static_cast<DWORD>(BrowserMode::Details))
            {
                browserMode_ = static_cast<BrowserMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueThemeMode, &value) && value <= static_cast<DWORD>(ThemeMode::Dark))
            {
                themeMode_ = static_cast<ThemeMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueRecursiveBrowsing, &value))
            {
                recursiveBrowsingEnabled_ = value != 0;
            }

            TryReadStringValue(key, kRegistryValueSelectedFolderPath, &startupFolderPath_);

            RegCloseKey(key);
        }

        leftPaneWidth_ = std::max(leftPaneWidth_, kMinLeftPaneWidth);
        startupFolderPath_ = NormalizeFolderPath(std::move(startupFolderPath_));
    }

    void MainWindow::SaveWindowState() const
    {
        HKEY key{};
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) == ERROR_SUCCESS)
        {
            const std::wstring selectedFolderPath = !GetSelectedFolderTreePath().empty()
                ? GetSelectedFolderTreePath()
                : (browserModel_ ? browserModel_->FolderPath() : std::wstring{});

            WriteDwordValue(key, kRegistryValueLeftPaneWidth, static_cast<DWORD>(leftPaneWidth_));
            WriteDwordValue(key, kRegistryValueBrowserMode, static_cast<DWORD>(browserMode_));
            WriteDwordValue(key, kRegistryValueThemeMode, static_cast<DWORD>(themeMode_));
            WriteDwordValue(key, kRegistryValueRecursiveBrowsing, recursiveBrowsingEnabled_ ? 1UL : 0UL);
            WriteStringValue(key, kRegistryValueSelectedFolderPath, selectedFolderPath);
            RegCloseKey(key);
        }
    }

    void MainWindow::LoadFolderAsync(std::wstring folderPath)
    {
        if (folderPath.empty() || !browserModel_ || !folderEnumerationService_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));

        util::LogInfo(L"Queueing folder enumeration for " + folderPath);
        browserModel_->Reset(folderPath, recursiveBrowsingEnabled_);
        if (browserPaneController_)
        {
            browserPaneController_->ClearSelection();
        }
        ShowSelectedFolderInTree();
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        activeEnumerationRequestId_ = folderEnumerationService_->EnumerateFolderAsync(hwnd_, std::move(folderPath), recursiveBrowsingEnabled_);
    }

    LRESULT MainWindow::OnFolderEnumerationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderEnumerationUpdate> update(
            reinterpret_cast<services::FolderEnumerationUpdate*>(lParam));
        if (!update || !browserModel_ || update->requestId != activeEnumerationRequestId_)
        {
            return 0;
        }

        switch (update->kind)
        {
        case services::FolderEnumerationUpdateKind::Batch:
            browserModel_->AppendItems(std::move(update->items), update->totalCount, update->totalBytes);
            break;
        case services::FolderEnumerationUpdateKind::Completed:
            browserModel_->Complete();
            util::LogInfo(L"Completed folder enumeration for " + update->folderPath);
            break;
        case services::FolderEnumerationUpdateKind::Failed:
            browserModel_->Fail(update->message);
            util::LogError(update->message);
            break;
        default:
            break;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        UpdateStatusText();
        UpdateMenuState();
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        OpenItemInViewer(static_cast<int>(lParam));
        return 0;
    }

    LRESULT MainWindow::OnViewerZoomMessage(LPARAM lParam)
    {
        viewerZoomPercent_ = static_cast<int>(lParam);
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerActivityMessage(LPARAM lParam)
    {
        viewerWindowActive_ = lParam != 0;
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerClosedMessage()
    {
        viewerWindowActive_ = false;
        viewerZoomPercent_ = 0;
        UpdateStatusText();
        return 0;
    }

    bool MainWindow::HandleCommand(UINT commandId)
    {
        switch (commandId)
        {
        case ID_FILE_EXIT:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return true;
        case ID_FILE_REFRESH_TREE:
            RefreshFolderTree();
            return true;
        case ID_VIEW_THUMBNAILS:
            SetBrowserMode(BrowserMode::Thumbnails);
            return true;
        case ID_VIEW_DETAILS:
            SetBrowserMode(BrowserMode::Details);
            return true;
        case ID_VIEW_RECURSIVE:
            ToggleRecursiveBrowsing();
            return true;
        case ID_VIEW_SORT_FILENAME:
        case ID_VIEW_SORT_MODIFIED:
        case ID_VIEW_SORT_SIZE:
        case ID_VIEW_SORT_DIMENSIONS:
        case ID_VIEW_SORT_TYPE:
        case ID_VIEW_SORT_RANDOM:
            if (browserPaneController_)
            {
                browserPaneController_->SetSortMode(SortModeFromCommandId(commandId));
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_THEME_LIGHT:
            SetThemeMode(ThemeMode::Light);
            return true;
        case ID_VIEW_THEME_DARK:
            SetThemeMode(ThemeMode::Dark);
            return true;
        case ID_HELP_ABOUT:
            MessageBoxW(
                hwnd_,
                L"HyperBrowse browser shell scaffold\n\nPrompt 6 adds a separate viewer window with next and previous navigation, zoom controls, fit and actual size modes, pan, rotate, fullscreen, and viewer-zoom status updates on top of the Prompt 5 thumbnail pipeline.",
                L"About HyperBrowse",
                MB_OK | MB_ICONINFORMATION);
            return true;
        default:
            break;
        }

        return false;
    }

    void MainWindow::SetBrowserMode(BrowserMode mode)
    {
        if (browserMode_ == mode)
        {
            return;
        }

        browserMode_ = mode;
        util::LogInfo(browserMode_ == BrowserMode::Thumbnails
            ? L"Switched shell to thumbnail mode"
            : L"Switched shell to details mode");

        if (browserPaneController_)
        {
            browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
                ? browser::BrowserViewMode::Thumbnails
                : browser::BrowserViewMode::Details);
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    void MainWindow::ToggleRecursiveBrowsing()
    {
        recursiveBrowsingEnabled_ = !recursiveBrowsingEnabled_;
        util::LogInfo(recursiveBrowsingEnabled_
            ? L"Enabled recursive browsing for folder enumeration"
            : L"Disabled recursive browsing for folder enumeration");
        UpdateMenuState();

        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::SetThemeMode(ThemeMode themeMode)
    {
        if (themeMode_ == themeMode)
        {
            return;
        }

        themeMode_ = themeMode;
        util::LogInfo(themeMode_ == ThemeMode::Dark
            ? L"Switched shell to dark theme"
            : L"Switched shell to light theme");
        ApplyTheme();
        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    MainWindow::ThemePalette MainWindow::GetThemePalette() const
    {
        switch (themeMode_)
        {
        case ThemeMode::Dark:
            return ThemePalette{
                RGB(24, 28, 32),
                RGB(34, 38, 43),
                RGB(234, 238, 242),
                RGB(96, 102, 110),
                RGB(78, 84, 92),
            };
        case ThemeMode::Light:
        default:
            return ThemePalette{
                RGB(243, 245, 248),
                RGB(255, 255, 255),
                RGB(32, 36, 40),
                RGB(198, 204, 212),
                RGB(210, 215, 223),
            };
        }
    }

    std::wstring MainWindow::BuildShellStateText() const
    {
        std::wstring shellState = L"View: ";
        shellState.append(browserMode_ == BrowserMode::Thumbnails ? L"Thumbnails" : L"Details");
        shellState.append(L" | Sort: ");
        const std::wstring sortLabel = browserPaneController_
            ? browser::BrowserSortModeToLabel(browserPaneController_->GetSortMode())
            : std::wstring(L"Filename");
        shellState.append(sortLabel);
        shellState.append(L" | Recursive: ");
        shellState.append(recursiveBrowsingEnabled_ ? L"On" : L"Off");
        shellState.append(L" | Theme: ");
        shellState.append(themeMode_ == ThemeMode::Dark ? L"Dark" : L"Light");
        if (viewerWindowActive_ && viewerZoomPercent_ > 0)
        {
            shellState.append(L" | Viewer Zoom: ");
            shellState.append(std::to_wstring(viewerZoomPercent_));
            shellState.append(L"%");
        }
        return shellState;
    }

    std::wstring MainWindow::GetCurrentFolderDisplayName() const
    {
        return browserModel_ ? GetFolderDisplayName(browserModel_->FolderPath()) : L"No Folder";
    }

    void MainWindow::OnSize()
    {
        LayoutChildren();
    }

    void MainWindow::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const
    {
        minMaxInfo->ptMinTrackSize.x = kMinWindowWidth;
        minMaxInfo->ptMinTrackSize.y = kMinWindowHeight;
    }

    bool MainWindow::IsOverSplitter(int x) const
    {
        return x >= leftPaneWidth_ && x < leftPaneWidth_ + kSplitterWidth;
    }

    void MainWindow::OnLButtonDown(int x, int y)
    {
        (void)y;
        if (IsOverSplitter(x))
        {
            dragMode_ = DragMode::Splitter;
            SetCapture(hwnd_);
        }
    }

    void MainWindow::OnLButtonDoubleClick(int x, int y)
    {
        (void)y;
        if (IsOverSplitter(x))
        {
            leftPaneWidth_ = kDefaultLeftPaneWidth;
            util::LogInfo(L"Reset splitter to default width");
            LayoutChildren();
        }
    }

    void MainWindow::OnLButtonUp()
    {
        if (dragMode_ != DragMode::None)
        {
            dragMode_ = DragMode::None;
            ReleaseCapture();
        }
    }

    void MainWindow::OnMouseMove(int x, int y)
    {
        (void)y;
        if (dragMode_ == DragMode::Splitter)
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int maxLeft = std::max(kMinLeftPaneWidth, static_cast<int>(client.right) - kMinRightPaneWidth - kSplitterWidth);
            leftPaneWidth_ = std::clamp(x, kMinLeftPaneWidth, maxLeft);
            LayoutChildren();
        }
        else
        {
            SetCursor(LoadCursorW(nullptr, IsOverSplitter(x) ? IDC_SIZEWE : IDC_ARROW));
        }
    }

    LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_GETMINMAXINFO:
            OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_LBUTTONDOWN:
            OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDBLCLK:
            OnLButtonDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            OnLButtonUp();
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (dragMode_ == DragMode::Splitter || IsOverSplitter(point.x))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            break;
        }
        case services::FolderEnumerationService::kMessageId:
            return OnFolderEnumerationMessage(lParam);
        case browser::BrowserPane::kStateChangedMessage:
            return OnBrowserPaneStateMessage(wParam, lParam);
        case browser::BrowserPane::kOpenItemMessage:
            return OnBrowserPaneOpenItemMessage(wParam, lParam);
        case viewer::ViewerWindow::kZoomChangedMessage:
            return OnViewerZoomMessage(lParam);
        case viewer::ViewerWindow::kActivityChangedMessage:
            return OnViewerActivityMessage(lParam);
        case viewer::ViewerWindow::kClosedMessage:
            return OnViewerClosedMessage();
        case WM_NOTIFY:
            return OnFolderTreeNotify(lParam);
        case WM_COMMAND:
            if (HandleCommand(LOWORD(wParam)))
            {
                return 0;
            }
            break;
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            FillRect(
                reinterpret_cast<HDC>(wParam),
                &client,
                backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            RECT client{};
            GetClientRect(hwnd_, &client);
            RECT splitterRect{leftPaneWidth_, 0, leftPaneWidth_ + kSplitterWidth, client.bottom};
            const HBRUSH splitterBrush = CreateSolidBrush(GetThemePalette().splitter);
            FillRect(hdc, &splitterRect, splitterBrush);
            DeleteObject(splitterBrush);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_DESTROY:
            if (folderEnumerationService_)
            {
                folderEnumerationService_->Cancel();
            }
            SaveWindowState();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
