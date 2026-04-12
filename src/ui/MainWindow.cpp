#include "ui/MainWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "decode/ImageDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderWatchService.h"
#include "services/JpegTransformService.h"
#include "util/Diagnostics.h"
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
    constexpr wchar_t kRegistryValueNvJpegEnabled[] = L"NvJpegEnabled";
    constexpr wchar_t kRegistryValueSelectedFolderPath[] = L"SelectedFolderPath";

    constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacyAttribute = 19;

    constexpr UINT ID_FILE_OPEN_FOLDER = 1000;
    constexpr UINT ID_FILE_REFRESH_TREE = 1001;
    constexpr UINT ID_FILE_EXIT = 1002;
    constexpr UINT ID_FILE_IMAGE_INFORMATION = 1003;
    constexpr UINT ID_FILE_OPEN_SELECTED = 1004;
    constexpr UINT ID_FILE_COPY_SELECTION = 1005;
    constexpr UINT ID_FILE_MOVE_SELECTION = 1006;
    constexpr UINT ID_FILE_DELETE_SELECTION = 1007;
    constexpr UINT ID_FILE_DELETE_SELECTION_PERMANENT = 1008;
    constexpr UINT ID_FILE_REVEAL_IN_EXPLORER = 1009;
    constexpr UINT ID_FILE_BATCH_CONVERT_SELECTION_JPEG = 1010;
    constexpr UINT ID_FILE_BATCH_CONVERT_SELECTION_PNG = 1011;
    constexpr UINT ID_FILE_BATCH_CONVERT_SELECTION_TIFF = 1012;
    constexpr UINT ID_FILE_BATCH_CONVERT_FOLDER_JPEG = 1013;
    constexpr UINT ID_FILE_BATCH_CONVERT_FOLDER_PNG = 1014;
    constexpr UINT ID_FILE_BATCH_CONVERT_FOLDER_TIFF = 1015;
    constexpr UINT ID_FILE_BATCH_CONVERT_CANCEL = 1016;
    constexpr UINT ID_FILE_ROTATE_JPEG_LEFT = 1017;
    constexpr UINT ID_FILE_ROTATE_JPEG_RIGHT = 1018;
    constexpr UINT ID_FILE_OPEN_CONTAINING_FOLDER = 1019;
    constexpr UINT ID_FILE_COPY_PATH = 1020;
    constexpr UINT ID_FILE_PROPERTIES = 1021;
    constexpr UINT ID_VIEW_THUMBNAILS = 2001;
    constexpr UINT ID_VIEW_DETAILS = 2002;
    constexpr UINT ID_VIEW_RECURSIVE = 2003;
    constexpr UINT ID_VIEW_THEME_LIGHT = 2101;
    constexpr UINT ID_VIEW_THEME_DARK = 2102;
    constexpr UINT ID_VIEW_NVJPEG_ACCELERATION = 2103;
    constexpr UINT ID_VIEW_SORT_FILENAME = 2201;
    constexpr UINT ID_VIEW_SORT_MODIFIED = 2202;
    constexpr UINT ID_VIEW_SORT_SIZE = 2203;
    constexpr UINT ID_VIEW_SORT_DIMENSIONS = 2204;
    constexpr UINT ID_VIEW_SORT_TYPE = 2205;
    constexpr UINT ID_VIEW_SORT_RANDOM = 2206;
    constexpr UINT ID_VIEW_SLIDESHOW_SELECTION = 2301;
    constexpr UINT ID_VIEW_SLIDESHOW_FOLDER = 2302;
    constexpr UINT ID_HELP_ABOUT = 9001;
    constexpr UINT ID_HELP_DIAGNOSTICS_SNAPSHOT = 9002;
    constexpr UINT ID_HELP_DIAGNOSTICS_RESET = 9003;

    bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    bool HasNvJpegCapability()
    {
        return hyperbrowse::decode::IsNvJpegBuildEnabled()
            && hyperbrowse::decode::IsNvJpegRuntimeAvailable();
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

    bool CopyTextToClipboard(HWND ownerWindow, std::wstring_view text)
    {
        if (!OpenClipboard(ownerWindow))
        {
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            return false;
        }

        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!buffer)
        {
            CloseClipboard();
            return false;
        }

        void* locked = GlobalLock(buffer);
        if (!locked)
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(locked)[text.size()] = L'\0';
        GlobalUnlock(buffer);

        if (!SetClipboardData(CF_UNICODETEXT, buffer))
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

    std::wstring BuildDeleteConfirmationMessage(std::size_t itemCount, bool permanent)
    {
        if (itemCount <= 1)
        {
            return permanent
                ? L"Permanently delete the selected image?\n\nThis cannot be undone."
                : L"Move the selected image to the Recycle Bin?";
        }

        return permanent
            ? L"Permanently delete " + std::to_wstring(itemCount) + L" selected images?\n\nThis cannot be undone."
            : L"Move " + std::to_wstring(itemCount) + L" selected images to the Recycle Bin?";
    }

    bool ConfirmFileDeletion(HWND ownerWindow, std::size_t itemCount, bool permanent)
    {
        const std::wstring prompt = BuildDeleteConfirmationMessage(itemCount, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete" : L"Delete",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }

    std::wstring JoinLines(const std::vector<std::wstring>& lines)
    {
        std::wstring combined;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (index > 0)
            {
                combined.append(L"\r\n");
            }
            combined.append(lines[index]);
        }
        return combined;
    }

    bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target)
    {
        if (target.empty())
        {
            return false;
        }

        const std::wstring path(target);
        const HINSTANCE result = ShellExecuteW(ownerWindow, verb, path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    bool RevealPathsInExplorer(const std::vector<std::wstring>& selectedPaths)
    {
        if (selectedPaths.empty())
        {
            return false;
        }

        std::vector<std::wstring> revealPaths;
        revealPaths.push_back(selectedPaths.front());

        const std::wstring primaryParent = NormalizeFolderPath(fs::path(selectedPaths.front()).parent_path().wstring());
        bool sameParent = !primaryParent.empty();
        for (const std::wstring& path : selectedPaths)
        {
            if (!FolderPathsEqual(primaryParent, fs::path(path).parent_path().wstring()))
            {
                sameParent = false;
                break;
            }
        }

        if (sameParent)
        {
            revealPaths = selectedPaths;
        }

        PIDLIST_ABSOLUTE folderPidl = ILCreateFromPathW(primaryParent.c_str());
        if (!folderPidl)
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCUITEMID_CHILD> childPidls;
        itemPidls.reserve(revealPaths.size());
        childPidls.reserve(revealPaths.size());
        for (const std::wstring& path : revealPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }

            itemPidls.push_back(itemPidl);
            childPidls.push_back(ILFindLastID(itemPidl));
        }

        const HRESULT result = SHOpenFolderAndSelectItems(folderPidl,
                                                          static_cast<UINT>(childPidls.size()),
                                                          childPidls.empty() ? nullptr : childPidls.data(),
                                                          0);

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }
        ILFree(folderPidl);

        return SUCCEEDED(result);
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

    bool IsJpegBrowserItem(const hyperbrowse::browser::BrowserItem& item)
    {
        return hyperbrowse::decode::IsWicFileType(item.fileType)
            && (_wcsicmp(item.fileType.c_str(), L"JPG") == 0 || _wcsicmp(item.fileType.c_str(), L"JPEG") == 0);
    }
}

namespace hyperbrowse::ui
{
    MainWindow::MainWindow(HINSTANCE instance)
        : instance_(instance)
        , browserModel_(std::make_unique<browser::BrowserModel>())
        , browserPaneController_(std::make_unique<browser::BrowserPane>(instance))
        , batchConvertService_(std::make_unique<services::BatchConvertService>())
        , fileOperationService_(std::make_unique<services::FileOperationService>())
        , folderEnumerationService_(std::make_unique<services::FolderEnumerationService>())
        , folderWatchService_(std::make_unique<services::FolderWatchService>())
        , viewerWindow_(std::make_unique<viewer::ViewerWindow>(instance))
    {
    }

    MainWindow::~MainWindow()
    {
        if (folderEnumerationService_)
        {
            folderEnumerationService_->Cancel();
        }

        if (folderWatchService_)
        {
            folderWatchService_->Stop();
        }

        if (batchConvertService_)
        {
            batchConvertService_->Cancel();
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
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

        if (!CreateAccelerators() || !CreateMenuBar() || !CreateChildWindows())
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

    bool MainWindow::TranslateAcceleratorMessage(MSG* message) const
    {
        return hwnd_ && accelerators_ && message && TranslateAcceleratorW(hwnd_, accelerators_, message) != 0;
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

    bool MainWindow::CreateAccelerators()
    {
        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
            accelerators_ = nullptr;
        }

        ACCEL accelerators[] = {
            {FVIRTKEY | FCONTROL, static_cast<WORD>('O'), ID_FILE_OPEN_FOLDER},
            {FVIRTKEY, VK_F5, ID_FILE_REFRESH_TREE},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('I'), ID_FILE_IMAGE_INFORMATION},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('C'), ID_FILE_COPY_PATH},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('E'), ID_FILE_REVEAL_IN_EXPLORER},
            {FVIRTKEY | FALT, VK_RETURN, ID_FILE_PROPERTIES},
            {FVIRTKEY, VK_DELETE, ID_FILE_DELETE_SELECTION},
            {FVIRTKEY | FSHIFT, VK_DELETE, ID_FILE_DELETE_SELECTION_PERMANENT},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('1'), ID_VIEW_THUMBNAILS},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('2'), ID_VIEW_DETAILS},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('R'), ID_VIEW_RECURSIVE},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('S'), ID_VIEW_SLIDESHOW_SELECTION},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('D'), ID_HELP_DIAGNOSTICS_SNAPSHOT},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('X'), ID_HELP_DIAGNOSTICS_RESET},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('L'), ID_VIEW_THEME_LIGHT},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('D'), ID_VIEW_THEME_DARK},
        };

        accelerators_ = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));
        return accelerators_ != nullptr;
    }

    bool MainWindow::CreateMenuBar()
    {
        menu_ = CreateMenu();
        HMENU fileMenu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU batchConvertFolderMenu = CreatePopupMenu();
        HMENU viewMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        HMENU themeMenu = CreatePopupMenu();
        HMENU helpMenu = CreatePopupMenu();

        if (!menu_ || !fileMenu || !batchConvertSelectionMenu || !batchConvertFolderMenu || !viewMenu || !sortMenu || !themeMenu || !helpMenu)
        {
            return false;
        }

        AppendMenuW(fileMenu, MF_STRING, ID_FILE_OPEN_FOLDER, L"&Open Folder...\tCtrl+O");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree\tF5");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information\tCtrl+I");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer\tCtrl+E");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h\tCtrl+Shift+C");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties\tAlt+Enter");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_COPY_SELECTION, L"Cop&y Selection...");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_MOVE_SELECTION, L"Mo&ve Selection...");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete\tDel");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently\tShift+Del");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_JPEG, L"Folder to JPE&G");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_PNG, L"Folder to P&NG");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_TIFF, L"Folder to TIF&F");
        AppendMenuW(fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
        AppendMenuW(fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertFolderMenu), L"Batch Convert &Folder");
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_BATCH_CONVERT_CANCEL, L"&Cancel Batch Convert");
        AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu, MF_STRING, ID_FILE_EXIT, L"E&xit");

        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode\tCtrl+1");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode\tCtrl+2");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_RECURSIVE, L"&Recursive Browsing\tCtrl+R");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"Slideshow from &Selection\tCtrl+Shift+S");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_SLIDESHOW_FOLDER, L"Slideshow from &Folder");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(themeMenu, MF_STRING, ID_VIEW_THEME_LIGHT, L"&Light\tCtrl+L");
        AppendMenuW(themeMenu, MF_STRING, ID_VIEW_THEME_DARK, L"&Dark\tCtrl+D");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"&Theme");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NVJPEG_ACCELERATION, L"Enable &NVIDIA JPEG Acceleration");

        AppendMenuW(helpMenu, MF_STRING, ID_HELP_ABOUT, L"&About");
        AppendMenuW(helpMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_DIAGNOSTICS_SNAPSHOT, L"Diagnostics &Snapshot\tCtrl+Shift+D");
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_DIAGNOSTICS_RESET, L"Reset Diagnostics\tCtrl+Shift+X");

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
        decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);

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

        if (batchConvertActive_)
        {
            progressText.append(L" | Convert: ");
            progressText.append(std::to_wstring(batchConvertCompleted_));
            progressText.append(L"/");
            progressText.append(std::to_wstring(batchConvertTotal_));
        }

        if (fileOperationActive_ && !activeFileOperationLabel_.empty())
        {
            progressText.append(L" | File: ");
            progressText.append(activeFileOperationLabel_);
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

        OpenItemsInViewer(std::move(viewerItems), selectedViewerIndex, false);
    }

    void MainWindow::OpenItemsInViewer(std::vector<browser::BrowserItem> items, int selectedIndex, bool startSlideshow)
    {
        if (!viewerWindow_ || items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        if (viewerWindow_->Open(hwnd_, std::move(items), selectedIndex, themeMode_ == ThemeMode::Dark))
        {
            if (startSlideshow)
            {
                viewerWindow_->StartSlideshow();
            }
            viewerWindowActive_ = true;
            UpdateStatusText();
        }
    }

    std::vector<browser::BrowserItem> MainWindow::CollectItemsForScope(bool selectionScope) const
    {
        std::vector<browser::BrowserItem> items;
        if (!browserModel_)
        {
            return items;
        }

        const auto& modelItems = browserModel_->Items();
        if (selectionScope && browserPaneController_)
        {
            for (const int modelIndex : browserPaneController_->OrderedSelectedModelIndicesSnapshot())
            {
                if (modelIndex >= 0 && modelIndex < static_cast<int>(modelItems.size()))
                {
                    items.push_back(modelItems[static_cast<std::size_t>(modelIndex)]);
                }
            }
            return items;
        }

        std::vector<int> orderedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedModelIndicesSnapshot()
            : std::vector<int>{};
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        for (const int modelIndex : orderedModelIndices)
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(modelItems.size()))
            {
                items.push_back(modelItems[static_cast<std::size_t>(modelIndex)]);
            }
        }

        return items;
    }

    void MainWindow::OpenFolder()
    {
        std::wstring folderPath;
        if (!ChooseFolder(&folderPath) || folderPath.empty())
        {
            return;
        }

        LoadFolderAsync(std::move(folderPath));
    }

    bool MainWindow::ChooseFolder(std::wstring* folderPath) const
    {
        if (!folderPath)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,
                                          nullptr,
                                          CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result) || !dialog)
        {
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        result = dialog->Show(hwnd_);
        if (FAILED(result))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellItem> shellItem;
        result = dialog->GetResult(&shellItem);
        if (FAILED(result) || !shellItem)
        {
            return false;
        }

        PWSTR rawFolderPath = nullptr;
        result = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &rawFolderPath);
        if (FAILED(result) || !rawFolderPath)
        {
            return false;
        }

        *folderPath = rawFolderPath;
        CoTaskMemFree(rawFolderPath);
        return true;
    }

    bool MainWindow::HasSelectedJpegItems() const
    {
        for (const browser::BrowserItem& item : CollectItemsForScope(true))
        {
            if (IsJpegBrowserItem(item))
            {
                return true;
            }
        }

        return false;
    }

    void MainWindow::ShowBrowserContextMenu(POINT screenPoint)
    {
        if (!hwnd_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool allowMutatingFileCommands = hasSelection && !fileOperationActive_;

        HMENU menu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        if (!menu || !batchConvertSelectionMenu || !sortMenu)
        {
            if (sortMenu)
            {
                DestroyMenu(sortMenu);
            }
            if (batchConvertSelectionMenu)
            {
                DestroyMenu(batchConvertSelectionMenu);
            }
            if (menu)
            {
                DestroyMenu(menu);
            }
            return;
        }

        AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
        AppendMenuW(menu, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information");
        AppendMenuW(menu, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer");
        AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
        AppendMenuW(menu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h");
        AppendMenuW(menu, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_FILE_COPY_SELECTION, L"Cop&y Selection...");
        AppendMenuW(menu, MF_STRING, ID_FILE_MOVE_SELECTION, L"Mo&ve Selection...");
        AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete");
        AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"Slideshow from &Selection");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
        AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
        AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode");
        AppendMenuW(menu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_FOLDER, L"Open &Folder...");
        AppendMenuW(menu, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree");

        EnableMenuItem(menu, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_COPY_PATH, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_PROPERTIES, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_COPY_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_MOVE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_DELETE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_DELETE_SELECTION_PERMANENT, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_VIEW_DETAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_FILENAME, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_MODIFIED, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_SIZE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_DIMENSIONS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_TYPE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_RANDOM, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            menu,
            ID_VIEW_THUMBNAILS,
            ID_VIEW_DETAILS,
            browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
            MF_BYCOMMAND);

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        CheckMenuRadioItem(
            sortMenu,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_RANDOM,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);

        SetForegroundWindow(hwnd_);
        TrackPopupMenu(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            screenPoint.x,
            screenPoint.y,
            0,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void MainWindow::ShowDiagnosticsSnapshot() const
    {
        std::wstring report = L"HyperBrowse Diagnostics\r\n\r\nJPEG Path: ";
        report.append(decode::DescribeJpegAccelerationState());
        report.append(L"\r\nFolder Scope: ");
        report.append(browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"));
        report.append(L"\r\n\r\n");
        report.append(util::BuildDiagnosticsReport());

        util::LogInfo(report);
        MessageBoxW(hwnd_, report.c_str(), L"Diagnostics Snapshot", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ResetDiagnosticsState() const
    {
        util::ResetDiagnostics();
        util::LogInfo(L"Diagnostics timings and counters were reset.");
        MessageBoxW(hwnd_, L"Diagnostics timings and counters were reset.", L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ShowImageInformation()
    {
        if (!browserPaneController_)
        {
            return;
        }

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        if (modelIndex < 0)
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Image Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring report = browserPaneController_->BuildMetadataReportForModelIndex(modelIndex);
        MessageBoxW(hwnd_, report.c_str(), L"Image Information", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::StartCopySelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartFileOperation(services::FileOperationType::Copy, std::vector<std::wstring>(sourcePaths), std::move(destinationFolder));
    }

    void MainWindow::StartMoveSelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Move Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartFileOperation(services::FileOperationType::Move, std::vector<std::wstring>(sourcePaths), std::move(destinationFolder));
    }

    void MainWindow::StartDeleteSelection(bool permanent)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", permanent ? L"Permanent Delete" : L"Delete", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!ConfirmFileDeletion(hwnd_, sourcePaths.size(), permanent))
        {
            return;
        }

        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           std::vector<std::wstring>(sourcePaths),
                           {});
    }

    void MainWindow::StartFileOperation(services::FileOperationType type,
                                        std::vector<std::wstring> sourcePaths,
                                        std::wstring destinationFolder)
    {
        if (!fileOperationService_ || sourcePaths.empty() || fileOperationActive_)
        {
            return;
        }

        activeFileOperationLabel_ = services::FileOperationTypeToActivityLabel(type);
        activeFileOperationLabel_.append(L" ");
        activeFileOperationLabel_.append(std::to_wstring(sourcePaths.size()));
        activeFileOperationLabel_.append(L" item(s)");
        fileOperationActive_ = true;
        activeFileOperationRequestId_ = fileOperationService_->Start(hwnd_, hwnd_, type, std::move(sourcePaths), std::move(destinationFolder));
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::RevealSelectedInExplorer() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Reveal in Explorer", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!RevealPathsInExplorer(selectedPaths))
        {
            MessageBoxW(hwnd_, L"Failed to reveal the selected item in Explorer.", L"Reveal in Explorer", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::OpenSelectedContainingFolder() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            if (!selectedPaths.empty())
            {
                targetPath = selectedPaths.front();
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Open Containing Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring containingFolder = fs::path(targetPath).parent_path().wstring();
        if (!LaunchShellTarget(hwnd_, L"open", containingFolder))
        {
            MessageBoxW(hwnd_, L"Failed to open the containing folder.", L"Open Containing Folder", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectedPathsToClipboard() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Path", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!CopyTextToClipboard(hwnd_, JoinLines(selectedPaths)))
        {
            MessageBoxW(hwnd_, L"Failed to copy the selected file paths to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::ShowSelectedFileProperties() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            if (!selectedPaths.empty())
            {
                targetPath = selectedPaths.front();
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Properties", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!LaunchShellTarget(hwnd_, L"properties", targetPath))
        {
            MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::StartSlideshow(bool selectionScope)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Slideshow",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        int selectedIndex = 0;
        if (!selectionScope)
        {
            const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
            const auto orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
            for (int index = 0; index < static_cast<int>(orderedModelIndices.size()); ++index)
            {
                if (orderedModelIndices[static_cast<std::size_t>(index)] == primaryModelIndex)
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        OpenItemsInViewer(std::move(items), selectedIndex, true);
    }

    void MainWindow::StartBatchConvert(bool selectionScope, services::BatchConvertFormat format)
    {
        if (!batchConvertService_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Batch Convert",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring outputFolder;
        if (!ChooseFolder(&outputFolder) || outputFolder.empty())
        {
            return;
        }

        batchConvertOutputFolder_ = outputFolder;
        batchConvertCompleted_ = 0;
        batchConvertTotal_ = items.size();
        batchConvertFailed_ = 0;
        batchConvertCurrentFile_.clear();
        batchConvertActive_ = true;
        activeBatchConvertRequestId_ = batchConvertService_->Start(hwnd_, std::move(items), outputFolder, format);
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::AdjustSelectedJpegOrientation(int quarterTurnsDelta)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const std::vector<browser::BrowserItem> items = CollectItemsForScope(true);
        if (items.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more JPEG images first.", L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

        std::vector<std::wstring> updatedPaths;
        std::size_t successCount = 0;
        std::size_t failureCount = 0;
        for (const browser::BrowserItem& item : items)
        {
            if (!decode::IsWicFileType(item.fileType) || (_wcsicmp(item.fileType.c_str(), L"JPG") != 0 && _wcsicmp(item.fileType.c_str(), L"JPEG") != 0))
            {
                continue;
            }

            std::wstring errorMessage;
            if (services::AdjustJpegOrientation(item.filePath, quarterTurnsDelta, &errorMessage))
            {
                ++successCount;
                updatedPaths.push_back(item.filePath);
                browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(fs::path(item.filePath)));
            }
            else
            {
                ++failureCount;
            }
        }

        if (!updatedPaths.empty())
        {
            browserPaneController_->InvalidateMediaCacheForPaths(updatedPaths);
            RefreshBrowserPane();
            browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
        }

        std::wstring summary = L"Updated orientation metadata for " + std::to_wstring(successCount) + L" JPEG file(s).";
        if (failureCount > 0)
        {
            summary.append(L"\nFailed: ");
            summary.append(std::to_wstring(failureCount));
            summary.append(L".");
        }
        MessageBoxW(hwnd_, summary.c_str(), L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ApplyCompletedFileOperation(const services::FileOperationUpdate& update)
    {
        fileOperationActive_ = false;
        activeFileOperationLabel_.clear();

        bool modelChanged = false;
        if (browserModel_ && browserPaneController_)
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

            auto upsertVisiblePath = [&](const std::wstring& path)
            {
                if (!IsPathInCurrentScope(path))
                {
                    return;
                }

                const fs::path filePath(path);
                if (!browser::IsSupportedImageExtension(filePath.extension().wstring()))
                {
                    return;
                }

                std::error_code error;
                if (!fs::is_regular_file(filePath, error) || error)
                {
                    return;
                }

                modelChanged = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(filePath)) || modelChanged;
            };

            switch (update.type)
            {
            case services::FileOperationType::Copy:
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::Move:
                for (const std::wstring& sourcePath : update.succeededSourcePaths)
                {
                    modelChanged = browserModel_->RemoveItemByPath(sourcePath) || modelChanged;
                }
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::DeleteRecycleBin:
            case services::FileOperationType::DeletePermanent:
                for (const std::wstring& sourcePath : update.succeededSourcePaths)
                {
                    modelChanged = browserModel_->RemoveItemByPath(sourcePath) || modelChanged;
                }
                break;
            default:
                break;
            }

            std::vector<std::wstring> affectedPaths = update.succeededSourcePaths;
            affectedPaths.insert(affectedPaths.end(), update.createdPaths.begin(), update.createdPaths.end());
            if (!affectedPaths.empty())
            {
                browserPaneController_->InvalidateMediaCacheForPaths(affectedPaths);
            }

            if (modelChanged)
            {
                RefreshBrowserPane();
                browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
                UpdateWindowTitle();
            }
        }

        UpdateStatusText();
        UpdateMenuState();

        if (!update.message.empty())
        {
            const UINT icon = (update.failedCount > 0 || update.aborted) ? MB_ICONWARNING : MB_ICONINFORMATION;
            MessageBoxW(hwnd_, update.message.c_str(), L"File Operation", MB_OK | icon);
        }
    }

    bool MainWindow::IsPathInCurrentScope(std::wstring_view path) const
    {
        if (!browserModel_ || browserModel_->FolderPath().empty() || path.empty())
        {
            return false;
        }

        if (browserModel_->IsRecursive())
        {
            return browser::PathHasPrefix(path, browserModel_->FolderPath());
        }

        return FolderPathsEqual(fs::path(path).parent_path().wstring(), browserModel_->FolderPath());
    }

    void MainWindow::ApplyFolderWatchChanges(const services::FolderWatchUpdate& update)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        if (update.requiresFullReload)
        {
            LoadFolderAsync(update.folderPath.empty() ? browserModel_->FolderPath() : update.folderPath);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();
        std::vector<std::wstring> invalidatedPaths;
        bool changed = false;

        auto upsertFromPath = [&](const std::wstring& path)
        {
            std::error_code error;
            const fs::path watchedPath(path);
            if (fs::is_regular_file(watchedPath, error) && !error)
            {
                if (browser::IsSupportedImageExtension(watchedPath.extension().wstring()))
                {
                    changed = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(watchedPath)) || changed;
                    invalidatedPaths.push_back(path);
                }
                return;
            }

            if (!recursiveBrowsingEnabled_ || !fs::is_directory(watchedPath, error) || error)
            {
                return;
            }

            const fs::directory_options options = fs::directory_options::skip_permission_denied;
            for (fs::recursive_directory_iterator iterator(watchedPath, options, error), end; iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    error.clear();
                    continue;
                }

                std::error_code statusError;
                if (!iterator->is_regular_file(statusError) || statusError)
                {
                    continue;
                }

                if (!browser::IsSupportedImageExtension(iterator->path().extension().wstring()))
                {
                    continue;
                }

                changed = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(iterator->path())) || changed;
                invalidatedPaths.push_back(iterator->path().wstring());
            }
        };

        for (const services::FolderWatchEvent& event : update.events)
        {
            switch (event.kind)
            {
            case services::FolderWatchEventKind::Added:
            case services::FolderWatchEventKind::Modified:
                upsertFromPath(event.path);
                break;
            case services::FolderWatchEventKind::Removed:
                changed = browserModel_->RemoveItemByPath(event.path) || changed;
                changed = browserModel_->RemoveItemsByPathPrefix(event.path) || changed;
                invalidatedPaths.push_back(event.path);
                break;
            case services::FolderWatchEventKind::Renamed:
            {
                const bool renamed = browserModel_->ReplacePathPrefix(event.oldPath, event.path);
                changed = renamed || changed;
                invalidatedPaths.push_back(event.oldPath);
                invalidatedPaths.push_back(event.path);
                if (!renamed)
                {
                    changed = browserModel_->RemoveItemByPath(event.oldPath) || changed;
                    changed = browserModel_->RemoveItemsByPathPrefix(event.oldPath) || changed;
                    upsertFromPath(event.path);
                }
                break;
            }
            default:
                break;
            }
        }

        if (!changed && invalidatedPaths.empty())
        {
            return;
        }

        browserPaneController_->InvalidateMediaCacheForPaths(invalidatedPaths);
        RefreshBrowserPane();
        browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::UpdateMenuState() const
    {
        if (!menu_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSelectedJpeg = HasSelectedJpegItems();

        EnableMenuItem(menu_, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_PATH, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_PROPERTIES, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_MOVE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION_PERMANENT,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_JPEG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_PNG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_TIFF,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_FOLDER, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));

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

        CheckMenuItem(
            menu_,
            ID_VIEW_NVJPEG_ACCELERATION,
            MF_BYCOMMAND | ((nvJpegEnabled_ && HasNvJpegCapability()) ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(
            menu_,
            ID_VIEW_NVJPEG_ACCELERATION,
            MF_BYCOMMAND | (HasNvJpegCapability() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(
            menu_,
            ID_FILE_BATCH_CONVERT_CANCEL,
            MF_BYCOMMAND | (batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

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

            if (TryReadDwordValue(key, kRegistryValueNvJpegEnabled, &value))
            {
                nvJpegEnabled_ = value != 0;
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
            WriteDwordValue(key, kRegistryValueNvJpegEnabled, nvJpegEnabled_ ? 1UL : 0UL);
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
        if (folderWatchService_)
        {
            folderWatchService_->Stop();
            activeFolderWatchRequestId_ = 0;
        }
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
            if (folderWatchService_)
            {
                activeFolderWatchRequestId_ = folderWatchService_->StartWatching(hwnd_, update->folderPath, browserModel_->IsRecursive());
            }
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

    LRESULT MainWindow::OnFolderWatchMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderWatchUpdate> update(
            reinterpret_cast<services::FolderWatchUpdate*>(lParam));
        if (!update || update->requestId != activeFolderWatchRequestId_)
        {
            return 0;
        }

        ApplyFolderWatchChanges(*update);
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

    LRESULT MainWindow::OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ShowBrowserContextMenu(screenPoint);
        return 0;
    }

    LRESULT MainWindow::OnBatchConvertMessage(LPARAM lParam)
    {
        std::unique_ptr<services::BatchConvertUpdate> update(
            reinterpret_cast<services::BatchConvertUpdate*>(lParam));
        if (!update || update->requestId != activeBatchConvertRequestId_)
        {
            return 0;
        }

        batchConvertCompleted_ = update->completedCount;
        batchConvertTotal_ = update->totalCount;
        batchConvertFailed_ = update->failedCount;
        batchConvertCurrentFile_ = update->currentFileName;
        batchConvertOutputFolder_ = update->outputFolder;

        if (update->finished)
        {
            batchConvertActive_ = false;
            UpdateMenuState();

            std::wstring summary;
            if (update->cancelled)
            {
                summary = L"Batch conversion was cancelled.";
            }
            else
            {
                summary = L"Batch conversion completed. Converted ";
                summary.append(std::to_wstring(update->completedCount - update->failedCount));
                summary.append(L" of ");
                summary.append(std::to_wstring(update->totalCount));
                summary.append(L" image(s).\nFailures: ");
                summary.append(std::to_wstring(update->failedCount));
                summary.append(L".");
            }
            MessageBoxW(hwnd_, summary.c_str(), L"Batch Convert", MB_OK | MB_ICONINFORMATION);
        }

        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnFileOperationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FileOperationUpdate> update(
            reinterpret_cast<services::FileOperationUpdate*>(lParam));
        if (!update || update->requestId != activeFileOperationRequestId_)
        {
            return 0;
        }

        ApplyCompletedFileOperation(*update);
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
        case ID_FILE_OPEN_FOLDER:
            OpenFolder();
            return true;
        case ID_FILE_EXIT:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return true;
        case ID_FILE_REFRESH_TREE:
            RefreshFolderTree();
            return true;
        case ID_FILE_OPEN_SELECTED:
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1);
            return true;
        case ID_FILE_IMAGE_INFORMATION:
            ShowImageInformation();
            return true;
        case ID_FILE_COPY_SELECTION:
            StartCopySelection();
            return true;
        case ID_FILE_MOVE_SELECTION:
            StartMoveSelection();
            return true;
        case ID_FILE_DELETE_SELECTION:
            StartDeleteSelection(false);
            return true;
        case ID_FILE_DELETE_SELECTION_PERMANENT:
            StartDeleteSelection(true);
            return true;
        case ID_FILE_REVEAL_IN_EXPLORER:
            RevealSelectedInExplorer();
            return true;
        case ID_FILE_OPEN_CONTAINING_FOLDER:
            OpenSelectedContainingFolder();
            return true;
        case ID_FILE_COPY_PATH:
            CopySelectedPathsToClipboard();
            return true;
        case ID_FILE_PROPERTIES:
            ShowSelectedFileProperties();
            return true;
        case ID_FILE_ROTATE_JPEG_LEFT:
            AdjustSelectedJpegOrientation(-1);
            return true;
        case ID_FILE_ROTATE_JPEG_RIGHT:
            AdjustSelectedJpegOrientation(+1);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_JPEG:
            StartBatchConvert(true, services::BatchConvertFormat::Jpeg);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_PNG:
            StartBatchConvert(true, services::BatchConvertFormat::Png);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_TIFF:
            StartBatchConvert(true, services::BatchConvertFormat::Tiff);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_JPEG:
            StartBatchConvert(false, services::BatchConvertFormat::Jpeg);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_PNG:
            StartBatchConvert(false, services::BatchConvertFormat::Png);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_TIFF:
            StartBatchConvert(false, services::BatchConvertFormat::Tiff);
            return true;
        case ID_FILE_BATCH_CONVERT_CANCEL:
            if (batchConvertService_)
            {
                batchConvertService_->Cancel();
                batchConvertActive_ = false;
                UpdateStatusText();
                UpdateMenuState();
            }
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
        case ID_VIEW_NVJPEG_ACCELERATION:
            if (HasNvJpegCapability())
            {
                nvJpegEnabled_ = !nvJpegEnabled_;
                decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
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
        case ID_VIEW_SLIDESHOW_SELECTION:
            StartSlideshow(true);
            return true;
        case ID_VIEW_SLIDESHOW_FOLDER:
            StartSlideshow(false);
            return true;
        case ID_HELP_ABOUT:
            MessageBoxW(
                hwnd_,
                L"HyperBrowse native Win32 image browser\n\nThe current build includes async folder browsing, virtualized thumbnail and details views, a separate viewer, RAW support, metadata, slideshow, batch convert, live folder refresh, and Prompt 13 menu and shortcut polish.",
                L"About HyperBrowse",
                MB_OK | MB_ICONINFORMATION);
            return true;
        case ID_HELP_DIAGNOSTICS_SNAPSHOT:
            ShowDiagnosticsSnapshot();
            return true;
        case ID_HELP_DIAGNOSTICS_RESET:
            ResetDiagnosticsState();
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
        shellState.append(L" | JPEG: ");
        shellState.append(decode::DescribeJpegAccelerationState());
        if (viewerWindowActive_ && viewerZoomPercent_ > 0)
        {
            shellState.append(L" | Viewer Zoom: ");
            shellState.append(std::to_wstring(viewerZoomPercent_));
            shellState.append(L"%");
        }
        if (viewerWindow_ && viewerWindow_->IsSlideshowActive())
        {
            shellState.append(L" | Slideshow: On");
        }
        if (batchConvertActive_)
        {
            shellState.append(L" | Convert: ");
            shellState.append(std::to_wstring(batchConvertCompleted_));
            shellState.append(L"/");
            shellState.append(std::to_wstring(batchConvertTotal_));
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
        case services::FolderWatchService::kMessageId:
            return OnFolderWatchMessage(lParam);
        case browser::BrowserPane::kStateChangedMessage:
            return OnBrowserPaneStateMessage(wParam, lParam);
        case browser::BrowserPane::kOpenItemMessage:
            return OnBrowserPaneOpenItemMessage(wParam, lParam);
        case browser::BrowserPane::kContextMenuMessage:
            return OnBrowserPaneContextMenuMessage(wParam, lParam);
        case services::BatchConvertService::kMessageId:
            return OnBatchConvertMessage(lParam);
        case services::FileOperationService::kMessageId:
            return OnFileOperationMessage(lParam);
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
            if (folderWatchService_)
            {
                folderWatchService_->Stop();
            }
            if (batchConvertService_)
            {
                batchConvertService_->Cancel();
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
