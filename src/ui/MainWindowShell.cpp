#include "ui/MainWindowInternal.h"

namespace
{
    constexpr UINT kTrayOpenCommand = 1;
    constexpr UINT kTrayExitCommand = 2;

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

    bool ShowMultiFilePropertiesDialog(const std::vector<std::wstring>& selectedPaths)
    {
        if (selectedPaths.size() < 2)
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCIDLIST_ABSOLUTE> absolutePidls;
        itemPidls.reserve(selectedPaths.size());
        absolutePidls.reserve(selectedPaths.size());
        for (const std::wstring& path : selectedPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }

            itemPidls.push_back(itemPidl);
            absolutePidls.push_back(itemPidl);
        }

        HRESULT result = E_FAIL;
        if (absolutePidls.size() >= 2)
        {
            Microsoft::WRL::ComPtr<IShellItemArray> shellItemArray;
            result = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(absolutePidls.size()),
                                                       absolutePidls.data(),
                                                       shellItemArray.GetAddressOf());
            if (SUCCEEDED(result) && shellItemArray)
            {
                Microsoft::WRL::ComPtr<IDataObject> dataObject;
                result = shellItemArray->BindToHandler(nullptr,
                                                       BHID_DataObject,
                                                       IID_PPV_ARGS(dataObject.GetAddressOf()));
                if (SUCCEEDED(result) && dataObject)
                {
                    result = SHMultiFileProperties(dataObject.Get(), 0);
                }
            }
        }

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }

        return SUCCEEDED(result);
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::UpdateTaskbarProgress(ULONGLONG completed, ULONGLONG total)
    {
        if (!hwnd_)
        {
            return;
        }

        if (!taskbarList_)
        {
            // Lazy-init; ITaskbarList3 is available on Windows 7+.
            if (FAILED(CoCreateInstance(CLSID_TaskbarList,
                                        nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&taskbarList_)))
                || !taskbarList_)
            {
                return;
            }
            taskbarList_->HrInit();
        }

        taskbarProgressActive_ = true;
        taskbarList_->SetProgressState(hwnd_, TBPF_NORMAL);
        taskbarList_->SetProgressValue(hwnd_, completed, total);
    }

    void MainWindow::ClearTaskbarProgress()
    {
        if (!taskbarList_ || !hwnd_ || !taskbarProgressActive_)
        {
            return;
        }

        taskbarList_->SetProgressState(hwnd_, TBPF_NOPROGRESS);
        taskbarProgressActive_ = false;
    }

    void MainWindow::RequestApplicationExit()
    {
        exitRequested_ = true;
        if (hwnd_)
        {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
    }

    void MainWindow::RestoreFromTray()
    {
        if (!hwnd_)
        {
            return;
        }

        if (IsIconic(hwnd_))
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }
        else if (!IsWindowVisible(hwnd_))
        {
            ShowWindow(hwnd_, SW_SHOW);
        }
        SetForegroundWindow(hwnd_);
        UpdateWindow(hwnd_);
    }

    void MainWindow::ShowTrayContextMenu()
    {
        if (!hwnd_)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        AppendMenuW(menu, MF_STRING, kTrayOpenCommand, L"&Open");
        AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"E&xit");

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(hwnd_);
        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_RETURNCMD | TPM_NONOTIFY,
            cursor.x,
            cursor.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);

        switch (command)
        {
        case kTrayOpenCommand:
            RestoreFromTray();
            break;
        case kTrayExitCommand:
            RequestApplicationExit();
            break;
        default:
            break;
        }
    }

    LRESULT MainWindow::HandleTrayIconMessage(LPARAM lParam)
    {
        switch (static_cast<UINT>(lParam))
        {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case NIN_BALLOONUSERCLICK:
            RestoreFromTray();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayContextMenu();
            return 0;
        default:
            return 0;
        }
    }

    void MainWindow::HideToTray()
    {
        if (!hwnd_)
        {
            return;
        }

        EnsureTrayIcon();
        SaveWindowState();
        ShowWindow(hwnd_, SW_HIDE);
    }

    bool MainWindow::ShouldKeepInNotificationArea() const
    {
        return app::Application::IsSingleInstanceEnabled()
            && app::Application::IsKeepInNotificationAreaEnabled();
    }

    void MainWindow::EnsureTrayIcon()
    {
        if (trayIconAdded_ || !hwnd_)
        {
            return;
        }

        if (trayIconMessageId_ == 0)
        {
            trayIconMessageId_ = RegisterWindowMessageW(L"TheTheosopher.HyperBrowse.TrayIcon");
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = hwnd_;
        iconData.uID = 1;
        iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        iconData.uCallbackMessage = trayIconMessageId_;
        iconData.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        if (!iconData.hIcon)
        {
            iconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        wcsncpy_s(iconData.szTip, L"HyperBrowse", _TRUNCATE);
        trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &iconData) != FALSE;
    }

    void MainWindow::RemoveTrayIcon()
    {
        if (!trayIconAdded_ || !hwnd_)
        {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = hwnd_;
        iconData.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &iconData);
        trayIconAdded_ = false;
    }

    void MainWindow::NotifyLongOperationComplete(const std::wstring& title, const std::wstring& message)
    {
        // Only notify when the user has moved focus elsewhere; an in-focus completion
        // is already visible in the status bar / dialogs.
        if (GetForegroundWindow() == hwnd_)
        {
            return;
        }

        EnsureTrayIcon();
        if (!trayIconAdded_)
        {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = hwnd_;
        iconData.uID = 1;
        iconData.uFlags = NIF_INFO;
        iconData.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(iconData.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(iconData.szInfo, message.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &iconData);
    }

    LRESULT MainWindow::OnFileOperationProgressMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FileOperationProgress> progress(
            reinterpret_cast<services::FileOperationProgress*>(lParam));
        if (!progress || progress->requestId != activeFileOperationRequestId_)
        {
            return 0;
        }

        if (progress->total > 0)
        {
            UpdateTaskbarProgress(progress->completed, progress->total);
        }
        return 0;
    }

    void MainWindow::RevealSelectedInExplorer() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Reveal in Explorer", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!RevealPathsInExplorer(selectedPaths))
        {
            MessageBoxW(hwnd_, L"Failed to reveal the selected items in Explorer.", L"Reveal in Explorer", MB_OK | MB_ICONERROR);
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

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
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

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.size() > 1)
        {
            if (!ShowMultiFilePropertiesDialog(selectedPaths))
            {
                MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
            }
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
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

        SHELLEXECUTEINFOW executeInfo{};
        executeInfo.cbSize = sizeof(executeInfo);
        executeInfo.fMask = SEE_MASK_INVOKEIDLIST;
        executeInfo.hwnd = hwnd_;
        executeInfo.lpVerb = L"properties";
        executeInfo.lpFile = targetPath.c_str();
        executeInfo.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&executeInfo) == FALSE)
        {
            MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectionFilesToClipboard(bool movePreferred) const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_,
                        L"Select one or more images first.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!CopyFilePathsToClipboard(hwnd_, selectedPaths, movePreferred))
        {
            MessageBoxW(hwnd_,
                        movePreferred ? L"Failed to cut the selected files to the clipboard."
                                      : L"Failed to copy the selected files to the clipboard.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::PasteClipboardFilesIntoCurrentFolder()
    {
        if (!browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        if (fileOperationActive_)
        {
            MessageBoxW(hwnd_, L"A file operation is already in progress.", L"Paste", MB_OK | MB_ICONINFORMATION);
            return;
        }

        DWORD pasteEffect = DROPEFFECT_COPY;
        std::vector<std::wstring> sourcePaths = ReadClipboardFilePaths(hwnd_, &pasteEffect);
        if (sourcePaths.empty())
        {
            return;
        }

        const std::wstring destinationFolder = browserModel_->FolderPath();
        // A "cut" paste is a move; an Explorer copy is a copy. Same-drive copies stay
        // copies; cross-drive moves from a cut degrade gracefully via shell conflicts.
        const services::FileOperationType type = (pasteEffect & DROPEFFECT_MOVE) != 0
            ? services::FileOperationType::Move
            : services::FileOperationType::Copy;
        StartFileOperation(type,
                           std::move(sourcePaths),
                           destinationFolder,
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::SetDesktopWallpaperFromImageFile(const std::wstring& imagePath)
    {
        if (imagePath.empty())
        {
            return;
        }

        if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<PWCHAR>(imagePath.c_str()), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) == FALSE)
        {
            MessageBoxW(hwnd_, L"Failed to set the desktop wallpaper.", L"Set Wallpaper", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectedImagePixelsToClipboard(std::wstring_view preferredPath)
    {
        if (!browserPaneController_ || !browserModel_ || !imageCommandService_)
        {
            return;
        }

        const int modelIndex = preferredPath.empty()
            ? browserPaneController_->PrimarySelectedModelIndex()
            : browserModel_->FindItemIndexByPath(preferredPath);
        if (modelIndex < 0 || modelIndex >= static_cast<int>(browserModel_->Items().size()))
        {
            MessageBoxW(hwnd_, L"Select a single image first.", L"Copy Image", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const browser::BrowserItem item = browserModel_->Items()[static_cast<std::size_t>(modelIndex)];
        HWND dialogOwner = hwnd_;
        if (!preferredPath.empty())
        {
            for (viewer::ViewerWindow* viewer : OpenViewerWindows())
            {
                if (util::NormalizedPathEquals(viewer->CurrentFilePath(), preferredPath))
                {
                    dialogOwner = viewer->Hwnd();
                    break;
                }
            }
        }

        imageCommandActive_ = true;
        imageCommandCancelling_ = false;
        imageCommandActivityLabel_ = L"Decoding image for the clipboard";
        activeImageCommandRequestId_ = imageCommandService_->StartCopyPixels(hwnd_, dialogOwner, item);
        UpdateStatusText();
    }

    void MainWindow::CopyDecodedImageToClipboard(
        std::shared_ptr<const cache::CachedThumbnail> image,
        HWND ownerWindow)
    {
        const HWND dialogOwner = ownerWindow && IsWindow(ownerWindow) ? ownerWindow : hwnd_;
        if (!image || !image->Bitmap())
        {
            MessageBoxW(dialogOwner, L"Unable to decode the selected image.", L"Copy Image", MB_OK | MB_ICONERROR);
            return;
        }
        // Convert the decoded bitmap into a packed CF_DIB (BITMAPINFOHEADER + pixels),
        // which is the clipboard format most image editors read.
        HDC screenDc = GetDC(nullptr);
        HDC memoryDc = CreateCompatibleDC(screenDc);
        HGDIOBJ oldBitmap = memoryDc ? SelectObject(memoryDc, image->Bitmap()) : nullptr;

        BITMAP bitmapInfo{};
        GetObjectW(image->Bitmap(), sizeof(bitmapInfo), &bitmapInfo);
        const int width = bitmapInfo.bmWidth;
        const int height = bitmapInfo.bmHeight;

        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = width;
        header.biHeight = height; // bottom-up
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;

        const std::size_t pixelBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        const std::size_t totalBytes = sizeof(BITMAPINFOHEADER) + pixelBytes;
        HGLOBAL dibBuffer = GlobalAlloc(GMEM_MOVEABLE, totalBytes);
        bool success = false;
        if (dibBuffer)
        {
            if (BYTE* dib = static_cast<BYTE*>(GlobalLock(dibBuffer)))
            {
                memcpy(dib, &header, sizeof(header));
                const int scanLines = memoryDc
                    ? GetDIBits(memoryDc,
                                image->Bitmap(),
                                0,
                                static_cast<UINT>(height),
                                dib + sizeof(BITMAPINFOHEADER),
                                reinterpret_cast<BITMAPINFO*>(dib),
                                DIB_RGB_COLORS)
                    : 0;
                GlobalUnlock(dibBuffer);
                success = scanLines == height;
            }
            if (!success)
            {
                GlobalFree(dibBuffer);
                dibBuffer = nullptr;
            }
        }

        if (memoryDc)
        {
            if (oldBitmap)
            {
                SelectObject(memoryDc, oldBitmap);
            }
            DeleteDC(memoryDc);
        }
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        if (success && OpenClipboard(dialogOwner))
        {
            success = EmptyClipboard() != FALSE && SetClipboardData(CF_DIB, dibBuffer) != nullptr;
            CloseClipboard();
        }

        if (!success)
        {
            if (dibBuffer)
            {
                GlobalFree(dibBuffer);
            }
            MessageBoxW(dialogOwner, L"Failed to copy the image to the clipboard.", L"Copy Image", MB_OK | MB_ICONERROR);
        }
    }
}
