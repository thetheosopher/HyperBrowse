#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunViewerDeleteFocusScenario(HINSTANCE instance)
    {
        using hyperbrowse::services::FileOperationService;
        using hyperbrowse::services::FileOperationUpdate;
        using hyperbrowse::viewer::ViewerWindow;

        ScopedRegistryDwordBackup browserModeBackup(kRegistryPath, L"BrowserMode");
        ScopedRegistryDwordBackup sortModeBackup(kRegistryPath, L"SortMode");
        ScopedRegistryDwordBackup sortAscendingBackup(kRegistryPath, L"SortAscending");
        ScopedRegistryDwordBackup detailsStripBackup(kRegistryPath, L"DetailsStripVisible");
        ScopedRegistryDwordBackup pairedOperationsBackup(kRegistryPath, L"RawJpegPairedOperationsEnabled");
        ScopedRegistryDwordBackup subfoldersBackup(kRegistryPath, L"ShowSubfoldersInBrowser");
        ScopedRegistryDwordBackup thumbnailSizeBackup(kRegistryPath, L"ThumbnailSizePreset");
        SetRegistryDwordValue(kRegistryPath, L"BrowserMode", 0);
        SetRegistryDwordValue(kRegistryPath, L"SortMode",
                              static_cast<DWORD>(hyperbrowse::browser::BrowserSortMode::FileName));
        SetRegistryDwordValue(kRegistryPath, L"SortAscending", 1);
        SetRegistryDwordValue(kRegistryPath, L"DetailsStripVisible", 0);
        SetRegistryDwordValue(kRegistryPath, L"RawJpegPairedOperationsEnabled", 1);
        SetRegistryDwordValue(kRegistryPath, L"ShowSubfoldersInBrowser", 1);
        SetRegistryDwordValue(kRegistryPath, L"ThumbnailSizePreset", 360);

        const auto activateWindow = [](HWND window)
        {
            if (!SetForegroundWindow(window))
            {
                const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
                const DWORD currentThread = GetCurrentThreadId();
                // Background test runners may need the foreground input queue for setup.
                if (foregroundThread != 0 && foregroundThread != currentThread
                    && AttachThreadInput(currentThread, foregroundThread, TRUE))
                {
                    SetForegroundWindow(window);
                    AttachThreadInput(currentThread, foregroundThread, FALSE);
                }
            }
            SetFocus(window);
        };

        struct DeleteProbe
        {
            HWND mainWindow{};
            HWND confirmationOwner{};
            std::unique_ptr<FileOperationUpdate> completion;
            bool replaying{};
            bool holdCompletions{};
            bool watchActivation{};
            bool browserActivated{};
            HWND expectedViewer{};
            bool shellStoleForeground{};
            int completionCount{};
            static constexpr UINT_PTR TimerId() { return 0x48424446; }

            static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR, DWORD_PTR data)
            {
                auto& probe = *reinterpret_cast<DeleteProbe*>(data);
                if (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE && probe.watchActivation)
                {
                    probe.browserActivated = true;
                }
                if (message == FileOperationService::kMessageId && !probe.replaying)
                {
                    ++probe.completionCount;
                    if (probe.watchActivation && GetForegroundWindow() != probe.expectedViewer)
                    {
                        probe.shellStoleForeground = true;
                    }
                    if (probe.holdCompletions)
                    {
                        probe.completion.reset(reinterpret_cast<FileOperationUpdate*>(lParam));
                        return 0;
                    }
                }
                if (message == WM_TIMER && wParam == TimerId())
                {
                    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM parameter) -> BOOL
                    {
                        wchar_t className[32]{};
                        GetClassNameW(window, className, static_cast<int>(std::size(className)));
                        if (std::wstring_view(className) != L"#32770" || !GetDlgItem(window, IDCANCEL))
                        {
                            return TRUE;
                        }
                        auto& state = *reinterpret_cast<DeleteProbe*>(parameter);
                        state.confirmationOwner = GetWindow(window, GW_OWNER);
                        PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
                        return FALSE;
                    }, reinterpret_cast<LPARAM>(&probe));
                    return 0;
                }
                return DefSubclassProc(hwnd, message, wParam, lParam);
            }

            ~DeleteProbe()
            {
                KillTimer(mainWindow, TimerId());
                RemoveWindowSubclass(mainWindow, WindowProc, TimerId());
            }

            void Replay()
            {
                replaying = true;
                SendMessageW(mainWindow, FileOperationService::kMessageId, 0,
                             reinterpret_cast<LPARAM>(completion.release()));
                replaying = false;
            }
        };

        for (const bool additionalViewer : {false, true})
        {
            TempFolder root(L"HyperBrowseViewerDeleteFocus");
            std::vector<hyperbrowse::browser::BrowserItem> items;
            for (int index = 0; index < 80; ++index)
            {
                const std::wstring number = std::to_wstring(index);
                const fs::path path = root.Root() / (std::wstring(4 - number.size(), L'0') + number + L".png");
                WriteTestImage(path, TestImageFormat::Png, 64, 32);
                items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(path));
            }

            hyperbrowse::ui::MainWindow mainWindow(instance);
            mainWindow.SetStartupLaunchPath(root.Root().wstring());
            Expect(mainWindow.Create(), "Failed to create MainWindow for viewer delete focus coverage");
            mainWindow.Show(SW_SHOW);
            const HWND browserHwnd = FindWindowExW(mainWindow.Hwnd(), nullptr, L"HyperBrowseBrowserPane", nullptr);
            auto* browser = reinterpret_cast<hyperbrowse::browser::BrowserPane*>(
                GetWindowLongPtrW(browserHwnd, GWLP_USERDATA));
            Expect(browser != nullptr, "Could not find browser for viewer delete focus coverage");
            Expect(PumpMessagesUntil([&]() { return browser->DisplayedItemCount() == items.size(); }, 10000),
                   "Browser did not finish loading viewer delete focus items");
            activateWindow(mainWindow.Hwnd());
            Expect(browser->GoToItemNumber(1), "Could not select the first image for viewer delete focus coverage");
            if (additionalViewer)
            {
                SendMessageW(mainWindow.Hwnd(), WM_COMMAND,
                             MAKEWPARAM(hyperbrowse::ui::command_ids::ID_FILE_OPEN_SELECTED, 0), 0);
            }
            else
            {
                SendMessageW(browserHwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(50, 50));
            }
            Expect(PumpMessagesUntil([]()
            {
                return FindOpenViewerWindowHandles().size() == 1;
            }, 10000), "Primary viewer did not open for delete focus coverage");
            HWND viewerHwnd = FindOpenViewerWindowHandles().front();
            if (additionalViewer)
            {
                SendMessageW(mainWindow.Hwnd(), WM_COMMAND,
                             MAKEWPARAM(hyperbrowse::ui::command_ids::ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, 0), 0);
                Expect(PumpMessagesUntil([]()
                {
                    return FindOpenViewerWindowHandles().size() == 2;
                }, 10000), "Additional viewer did not open for delete focus coverage");
                for (HWND candidate : FindOpenViewerWindowHandles())
                {
                    if (candidate != viewerHwnd)
                    {
                        viewerHwnd = candidate;
                        break;
                    }
                }
            }
            auto* viewer = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(viewerHwnd, GWLP_USERDATA));
            Expect(viewer && viewer->IsFullScreen() && viewer->CurrentFilePath() == items[0].filePath,
                   "Browser did not open the first image in the fullscreen viewer");
            Expect(GetForegroundWindow() == viewerHwnd && GetFocus() == viewerHwnd,
                   "Could not activate the viewer before delete focus coverage: foreground="
                       + std::to_string(reinterpret_cast<std::uintptr_t>(GetForegroundWindow()))
                       + " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(GetFocus()))
                       + " viewer=" + std::to_string(reinterpret_cast<std::uintptr_t>(viewerHwnd)));

            DeleteProbe probe;
            probe.mainWindow = mainWindow.Hwnd();
            probe.expectedViewer = viewerHwnd;
            Expect(SetWindowSubclass(mainWindow.Hwnd(), DeleteProbe::WindowProc, DeleteProbe::TimerId(),
                                     reinterpret_cast<DWORD_PTR>(&probe)) != FALSE,
                   "Could not install viewer delete completion probe");
            PumpMessagesFor(500);
            Expect(GetForegroundWindow() == viewerHwnd && GetFocus() == viewerHwnd,
                   "Viewer lost activation while settling before the first delete");
            probe.watchActivation = true;
            SendMessageW(viewerHwnd, WM_KEYDOWN, VK_DELETE, 0);
            Expect(viewer->CurrentFilePath() == items[1].filePath,
                   "Viewer delete did not immediately advance to the next image");
            Expect(PumpMessagesUntil([&]() { return probe.completionCount == 1; }, 15000),
                   "Normal viewer delete did not complete");
            PumpMessagesFor(1500);
            Expect(!probe.browserActivated && GetForegroundWindow() == viewerHwnd && GetFocus() == viewerHwnd,
                   "Normal viewer delete activated the browser during asynchronous updates");
            Expect(!probe.shellStoleForeground,
                   "Viewer delete opened an activating shell progress window before completion");
            Expect(!fs::exists(items[0].filePath), "Normal viewer delete did not remove the requested file");
            probe.watchActivation = false;
            Expect(SetTimer(mainWindow.Hwnd(), DeleteProbe::TimerId(), 20, nullptr) != 0,
                   "Could not start viewer delete confirmation probe");
            SendMessageW(mainWindow.Hwnd(), ViewerWindow::kDeleteRequestedMessage,
                         ViewerWindow::kDeleteRequestPermanent, reinterpret_cast<LPARAM>(viewerHwnd));
            KillTimer(mainWindow.Hwnd(), DeleteProbe::TimerId());
            Expect(probe.confirmationOwner == viewerHwnd, "Viewer delete confirmation was owned by the browser");
            Expect(viewer->CurrentFilePath() == items[1].filePath && fs::exists(items[1].filePath),
                   "Cancelling viewer delete changed the current image or deleted its file");
            Expect(GetForegroundWindow() == viewerHwnd, "Cancelling viewer delete activated the browser");

            // Hold completion so both keypresses deterministically use the queued-delete path.
            probe.holdCompletions = true;
            SendMessageW(viewerHwnd, WM_KEYDOWN, VK_DELETE, 0);
            SendMessageW(viewerHwnd, WM_KEYDOWN, VK_DELETE, 0);
            Expect(viewer->CurrentFilePath() == items[3].filePath,
                   "Queued viewer deletes did not immediately advance to the next image");
            for (int operation = 0; operation < 2; ++operation)
            {
                Expect(PumpMessagesUntil([&]() { return probe.completion != nullptr; }, 15000),
                       "Viewer delete did not report completion");
                Expect(probe.completion->finished && !probe.completion->aborted
                           && probe.completion->failedCount == 0,
                       "Viewer delete failed during focus coverage");
                activateWindow(mainWindow.Hwnd());
                Expect(GetForegroundWindow() == mainWindow.Hwnd(), "Could not stage shell-dialog focus handoff");
                probe.Replay();
                Expect(GetForegroundWindow() == viewerHwnd && GetFocus() == viewerHwnd,
                       "Viewer delete completion did not restore the originating viewer");
            }
            Expect(!fs::exists(items[1].filePath) && !fs::exists(items[2].filePath),
                   "Queued viewer deletes did not remove both requested files");
            SendMessageW(GetFocus(), WM_KEYDOWN, VK_RIGHT, 0);
            Expect(viewer->CurrentFilePath() == items[4].filePath,
                   "Keyboard navigation did not work after viewer delete completion");

            SendMessageW(viewerHwnd, WM_CLOSE, 0, 0);
            PumpMessagesFor(100);
            const std::vector<HWND> remainingViewers = FindOpenViewerWindowHandles();
            activateWindow(mainWindow.Hwnd());
            browser->RestoreSelectionByFilePaths({items[4].filePath}, items[4].filePath);
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND,
                         MAKEWPARAM(additionalViewer
                             ? hyperbrowse::ui::command_ids::ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW
                             : hyperbrowse::ui::command_ids::ID_FILE_OPEN_SELECTED, 0), 0);
            viewerHwnd = nullptr;
            for (HWND candidate : FindOpenViewerWindowHandles())
            {
                if (std::find(remainingViewers.begin(), remainingViewers.end(), candidate) == remainingViewers.end())
                {
                    viewerHwnd = candidate;
                    break;
                }
            }
            Expect(viewerHwnd != nullptr, "Could not reopen the viewer after deleting an image");
            viewer = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(viewerHwnd, GWLP_USERDATA));
            probe.expectedViewer = viewerHwnd;
            probe.holdCompletions = false;
            probe.watchActivation = true;
            SendMessageW(mainWindow.Hwnd(), ViewerWindow::kDeleteRequestedMessage, 0,
                         reinterpret_cast<LPARAM>(viewerHwnd));
            Expect(viewer->CurrentFilePath() == items[5].filePath,
                   "Reopened viewer did not advance after controller-style deletion");
            Expect(PumpMessagesUntil([&]() { return probe.completionCount == 4; }, 15000),
                   "Reopened viewer delete did not complete");
            PumpMessagesFor(1500);
            Expect(!probe.browserActivated && !probe.shellStoleForeground
                       && GetForegroundWindow() == viewerHwnd && GetFocus() == viewerHwnd,
                   "Reopened viewer delete lost foreground during asynchronous updates");
            probe.watchActivation = false;

            for (HWND openViewer : FindOpenViewerWindowHandles())
            {
                SendMessageW(openViewer, WM_CLOSE, 0, 0);
            }
            DestroyWindow(mainWindow.Hwnd());
            PumpMessagesFor(100);
        }
    }

    void RunMultiViewerSettingsScenario(HINSTANCE instance)
    {
        using hyperbrowse::ui::command_ids::ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW;
        using hyperbrowse::ui::command_ids::ID_VIEW_SETTINGS;
        using hyperbrowse::viewer::InfoOverlayTextSize;

        constexpr wchar_t kDialogClassName[] = L"HyperBrowseExperimentalSettingsDialog";
        constexpr int kOverlayTextSizeControlId = 5815;
        constexpr int kSlideshowDurationControlId = 5700;
        constexpr int kApplyButtonId = 5600;
        constexpr int kCancelButtonId = 5602;
        ScopedRegistryDwordBackup overlaySettingBackup(kRegistryPath, kRegistryValueViewerInfoOverlaysVisible);
        ScopedRegistryDwordBackup overlayTextSizeBackup(kRegistryPath, kRegistryValueViewerInfoOverlayTextSize);
        ScopedRegistryDwordBackup windowedFullMetadataBackup(kRegistryPath, kRegistryValueViewerWindowedFullMetadataVisible);
        ScopedRegistryDwordBackup fullScreenFullMetadataBackup(kRegistryPath, kRegistryValueViewerFullScreenFullMetadataVisible);
        ScopedRegistryDwordBackup fullMetadataBackup(kRegistryPath, kRegistryValueViewerFullMetadataVisible);
        ScopedRegistryDwordBackup slideshowIntervalBackup(kRegistryPath, kRegistryValueSlideshowInterval);
        DeleteRegistryValue(kRegistryPath, kRegistryValueViewerInfoOverlaysVisible);
        DeleteRegistryValue(kRegistryPath, kRegistryValueViewerInfoOverlayTextSize);
        DeleteRegistryValue(kRegistryPath, kRegistryValueViewerWindowedFullMetadataVisible);
        DeleteRegistryValue(kRegistryPath, kRegistryValueViewerFullScreenFullMetadataVisible);
        DeleteRegistryValue(kRegistryPath, kRegistryValueViewerFullMetadataVisible);

        TempFolder root(L"HyperBrowseMultiViewerSettings");
        const fs::path firstPath = root.Root() / L"first.png";
        const fs::path secondPath = root.Root() / L"second.png";
        WriteTestImage(firstPath, TestImageFormat::Png, 64, 32);
        WriteTestImage(secondPath, TestImageFormat::Png, 32, 64);

        hyperbrowse::ui::MainWindow mainWindow(instance);
        Expect(mainWindow.Create(), "Failed to create MainWindow for multi-viewer settings smoke coverage");
        mainWindow.OpenViewerAtPath(firstPath.wstring());
        Expect(PumpMessagesUntil([]()
        {
            return FindOpenViewerWindowHandles().size() >= 1;
        }, 10000), "Primary viewer did not open for multi-viewer settings smoke coverage");

        SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, 0), 0);
        Expect(PumpMessagesUntil([]()
        {
            return FindOpenViewerWindowHandles().size() >= 2;
        }, 10000), "Additional viewer did not open for multi-viewer settings smoke coverage");

        const std::vector<HWND> viewerHandles = FindOpenViewerWindowHandles();
        std::vector<hyperbrowse::viewer::ViewerWindow*> viewers;
        const std::vector<hyperbrowse::browser::BrowserItem> slideshowItems{
            hyperbrowse::browser::BuildBrowserItemFromPath(firstPath),
            hyperbrowse::browser::BuildBrowserItemFromPath(secondPath)};
        viewers.reserve(viewerHandles.size());
        for (HWND viewerHandle : viewerHandles)
        {
            auto* viewer = reinterpret_cast<hyperbrowse::viewer::ViewerWindow*>(
                GetWindowLongPtrW(viewerHandle, GWLP_USERDATA));
            Expect(viewer != nullptr, "Could not recover a ViewerWindow for multi-viewer settings smoke coverage");
            viewers.push_back(viewer);
            Expect(viewer->ReplaceItems(slideshowItems, 0),
                   "Could not prepare a two-item viewer for slideshow settings coverage");
            viewer->StartSlideshow(60000);
            Expect(viewer->IsSlideshowActive(),
                   "A viewer did not start its slideshow for multi-viewer settings coverage");
            Expect(PostMessageW(viewerHandle, WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100)) != FALSE,
                   "Failed to request windowed mode for multi-viewer settings smoke coverage");
        }
        Expect(PumpMessagesUntil([&]()
        {
            return std::all_of(viewers.begin(), viewers.end(), [](const auto* viewer)
            {
                return !viewer->IsFullScreen();
            });
        }, 2000), "Viewer did not enter windowed mode for multi-viewer settings smoke coverage");

        std::atomic_bool done{false};
        std::string failure;
        std::thread worker([&]()
        {
            if (!PostMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_SETTINGS, 0), 0))
            {
                failure = "Failed to post the Settings command";
                done.store(true, std::memory_order_release);
                return;
            }

            HWND dialog = nullptr;
            const ULONGLONG deadline = GetTickCount64() + 10000;
            while (GetTickCount64() < deadline && !(dialog = FindWindowW(kDialogClassName, nullptr)))
            {
                Sleep(10);
            }
            if (!dialog)
            {
                failure = "Settings dialog did not open";
                done.store(true, std::memory_order_release);
                return;
            }

            const auto failAndClose = [&](std::string message)
            {
                failure = std::move(message);
                SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(kCancelButtonId, BN_CLICKED), 0);
                done.store(true, std::memory_order_release);
            };
            HWND slideshowDuration = nullptr;
            const ULONGLONG controlsDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < controlsDeadline
                   && !(slideshowDuration = GetDlgItem(dialog, kSlideshowDurationControlId)))
            {
                Sleep(10);
            }
            if (!slideshowDuration)
            {
                failAndClose("Settings did not create the slideshow duration control");
                return;
            }
            SetWindowTextW(slideshowDuration, L"4321");
            SendMessageW(dialog, WM_SYSKEYDOWN, L'V', 0);
            auto* experimentalState = reinterpret_cast<hyperbrowse::ui::dialog_detail::ExperimentalSettingsDialogState*>(
                GetWindowLongPtrW(dialog, GWLP_USERDATA));
            if (!experimentalState || !experimentalState->settings)
            {
                failAndClose("Settings did not expose its measured dialog state");
                return;
            }
            if (experimentalState->settings->infoOverlaysVisible)
            {
                SendMessageW(dialog, WM_SYSKEYDOWN, L'D', 0);
                SendMessageW(dialog, WM_KEYDOWN, VK_SPACE, 0);
            }
            if (!experimentalState->settings->windowedFullMetadataVisible)
            {
                SendMessageW(dialog, WM_SYSKEYDOWN, L'G', 0);
                SendMessageW(dialog, WM_KEYDOWN, VK_SPACE, 0);
            }
            if (experimentalState->settings->fullScreenFullMetadataVisible)
            {
                SendMessageW(dialog, WM_SYSKEYDOWN, L'F', 0);
                SendMessageW(dialog, WM_KEYDOWN, VK_SPACE, 0);
            }
            const HWND overlayTextSize = GetDlgItem(dialog, kOverlayTextSizeControlId);
            if (!overlayTextSize)
            {
                failAndClose("Settings did not create the Viewer overlay text size control");
                return;
            }
            SendMessageW(overlayTextSize, CB_SETCURSEL, static_cast<WPARAM>(InfoOverlayTextSize::Large), 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(kOverlayTextSizeControlId, CBN_SELCHANGE), reinterpret_cast<LPARAM>(overlayTextSize));
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(kApplyButtonId, BN_CLICKED), 0);

            for (const auto* viewer : viewers)
            {
                if (viewer->AreInfoOverlaysVisible()
                    || viewer->OverlayTextSize() != InfoOverlayTextSize::Large
                    || !viewer->IsFullMetadataVisible()
                    || !viewer->IsSlideshowActive()
                    || viewer->SlideshowIntervalMs() != 4321)
                {
                    failAndClose("Settings did not update every open viewer");
                    return;
                }
            }

            SendMessageW(dialog, WM_SYSKEYDOWN, L'S', 0);
            const HWND cancelledDuration = GetDlgItem(dialog, kSlideshowDurationControlId);
            if (!cancelledDuration)
            {
                failAndClose("Settings did not restore the slideshow page before Cancel coverage");
                return;
            }
            SetWindowTextW(cancelledDuration, L"5555");
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(kCancelButtonId, BN_CLICKED), 0);
            const ULONGLONG closeDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < closeDeadline && FindWindowW(kDialogClassName, nullptr))
            {
                Sleep(10);
            }
            if (FindWindowW(kDialogClassName, nullptr))
            {
                failure = "Settings did not close after cancelling an unapplied duration";
            }
            done.store(true, std::memory_order_release);
        });

        Expect(PumpMessagesUntil([&]() { return done.load(std::memory_order_acquire); }, 15000),
               "Multi-viewer Settings interaction timed out");
        worker.join();
        Expect(failure.empty(), failure.empty() ? "Multi-viewer Settings interaction failed" : failure.c_str());

        for (HWND viewerHandle : viewerHandles)
        {
            Expect(PostMessageW(viewerHandle, WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100)) != FALSE,
                   "Failed to request full-screen mode after Settings Apply");
        }
        Expect(PumpMessagesUntil([&]()
        {
            return std::all_of(viewers.begin(), viewers.end(), [](const auto* viewer)
            {
                return viewer->IsFullScreen();
            });
        }, 2000), "Viewer did not return to full-screen mode after Settings Apply");
        for (const auto* viewer : viewers)
        {
            Expect(!viewer->AreInfoOverlaysVisible()
                       && viewer->OverlayTextSize() == InfoOverlayTextSize::Large
                       && !viewer->IsFullMetadataVisible()
                       && viewer->IsSlideshowActive()
                       && viewer->SlideshowIntervalMs() == 4321,
                   "Settings did not apply the full-screen metadata preference to every viewer");
        }

        for (HWND viewerHandle : viewerHandles)
        {
            SendMessageW(viewerHandle, WM_CLOSE, 0, 0);
        }
        PumpMessagesFor(100);
        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);

    }

    void RunMainWindowFolderTreeScenario(HINSTANCE instance)
    {
         const std::vector<std::wstring> expectedSpecialRoots = ExpectedSpecialFolderRootTexts();
         std::wstring persistedDriveRootText;

         {
             hyperbrowse::ui::MainWindow mainWindow(instance);
             Expect(mainWindow.Create(), "Failed to create the MainWindow for the folder-tree scenario");

             HWND mainWindowHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(mainWindowHandle != nullptr, "Failed to find the created MainWindow instance");

             HWND treeView = FindWindowExW(mainWindowHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(treeView != nullptr, "MainWindow did not create the folder tree control");

             HTREEITEM rootItem = TreeView_GetRoot(treeView);
             Expect(rootItem != nullptr, "Folder tree did not populate any root items");

             HTREEITEM currentRoot = rootItem;
             for (const std::wstring& expectedRootText : expectedSpecialRoots)
             {
              Expect(currentRoot != nullptr, "Folder tree did not place special folders ahead of drive roots");
              Expect(ReadTreeItemText(treeView, currentRoot) == expectedRootText,
                  "Folder tree did not insert Desktop/Documents/Pictures above the drive roots in the expected order");
              currentRoot = TreeView_GetNextSibling(treeView, currentRoot);
             }

             Expect(currentRoot != nullptr, "Folder tree did not include any drive roots after the special folders");
             persistedDriveRootText = ReadTreeItemText(treeView, currentRoot);
             Expect(persistedDriveRootText.find(L"Open Folder") == std::wstring::npos,
                 "Folder tree still shows the old placeholder prompt instead of filesystem roots");

             TreeView_Expand(treeView, currentRoot, TVE_EXPAND);
             TreeView_SelectItem(treeView, currentRoot);
             PumpMessagesFor(300);

             wchar_t title[512]{};
             GetWindowTextW(mainWindowHandle, title, static_cast<int>(std::size(title)));
             Expect(std::wstring(title).find(L":\\") != std::wstring::npos,
                 "Selecting a tree node did not route the main window to a concrete filesystem folder");

             DestroyWindow(mainWindowHandle);
             PumpMessagesFor(100);
         }

         {
             hyperbrowse::ui::MainWindow restoredMainWindow(instance);
             Expect(restoredMainWindow.Create(), "Failed to recreate the MainWindow for persistence verification");

             HWND restoredHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(restoredHandle != nullptr, "Failed to find the recreated MainWindow instance");

             HWND restoredTreeView = FindWindowExW(restoredHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(restoredTreeView != nullptr, "Recreated MainWindow did not create the folder tree control");

             PumpMessagesFor(300);
             const HTREEITEM selectedItem = TreeView_GetSelection(restoredTreeView);
             Expect(selectedItem != nullptr, "MainWindow did not restore any tree selection from the previous session");
             Expect(ReadTreeItemText(restoredTreeView, selectedItem) == persistedDriveRootText,
                 "MainWindow did not restore the previously selected folder tree item on startup");

             wchar_t title[512]{};
             GetWindowTextW(restoredHandle, title, static_cast<int>(std::size(title)));
             Expect(std::wstring(title).find(L":\\") != std::wstring::npos,
                 "MainWindow did not reload the persisted folder selection on startup");

             DestroyWindow(restoredHandle);
             PumpMessagesFor(100);
         }

         {
             std::wstring visibleParent = TryGetKnownFolderPathForTest(FOLDERID_Documents);
             std::error_code parentError;
             if (visibleParent.empty() || !fs::is_directory(fs::path(visibleParent), parentError) || parentError)
             {
                 visibleParent = fs::current_path().wstring();
             }
             TempFolder root(fs::path(visibleParent),
                             L"HyperBrowseFolderTreeRestore-" + std::to_wstring(GetCurrentProcessId()));
             const fs::path activeFolder = root.Root() / L"active-parent" / L"active-folder";
             std::error_code error;
             Expect(fs::create_directories(activeFolder, error) && !error,
                 "Failed to create the nested active folder for restore coverage");

             hyperbrowse::ui::MainWindow mainWindow(instance);
             mainWindow.SetStartupLaunchPath(activeFolder.wstring());
             Expect(mainWindow.Create(), "Failed to create MainWindow for folder-tree restore coverage");

             HWND mainWindowHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(mainWindowHandle != nullptr, "Failed to find the folder-tree restore MainWindow");
             HWND treeView = FindWindowExW(mainWindowHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(treeView != nullptr, "Folder-tree restore MainWindow did not create the tree control");

             HTREEITEM selectedItem = nullptr;
             Expect(PumpMessagesUntil([&]()
             {
                 selectedItem = TreeView_GetSelection(treeView);
                 return selectedItem != nullptr
                     && ReadTreeItemText(treeView, selectedItem) == L"active-folder";
             }, 5000), "Folder tree did not select the active nested folder on startup");

             HTREEITEM rootItem = selectedItem;
             while (HTREEITEM parentItem = TreeView_GetParent(treeView, rootItem))
             {
                 rootItem = parentItem;
             }
             TreeView_Expand(treeView, rootItem, TVE_COLLAPSE);
             RECT collapsedItemRect{};
             Expect(TreeView_GetItemRect(treeView, selectedItem, &collapsedItemRect, TRUE) == FALSE,
                 "Folder tree restore setup did not hide the active folder");

             SendMessageW(mainWindowHandle, WM_SIZE, SIZE_RESTORED, 0);
             SendMessageW(mainWindowHandle, WM_ACTIVATE, WA_ACTIVE, 0);
             RECT restoredItemRect{};
             Expect(PumpMessagesUntil([&]()
             {
                 selectedItem = TreeView_GetSelection(treeView);
                 return selectedItem != nullptr
                     && ReadTreeItemText(treeView, selectedItem) == L"active-folder"
                     && TreeView_GetItemRect(treeView, selectedItem, &restoredItemRect, TRUE) != FALSE;
             }, 2000), "Folder tree did not restore the active folder after window restoration");

             DestroyWindow(mainWindowHandle);
             PumpMessagesFor(100);
         }
    }

    void RunMainWindowFolderTreeLargeScenario(HINSTANCE instance)
    {
        constexpr int kSubfolderCount = 5000;
        constexpr double kMaxDispatchMs = 250.0;

        std::wstring visibleParent = TryGetKnownFolderPathForTest(FOLDERID_Documents);
        std::error_code error;
        if (visibleParent.empty() || !fs::is_directory(fs::path(visibleParent), error) || error)
        {
            visibleParent = fs::current_path().wstring();
        }

        TempFolder root(fs::path(visibleParent),
                        L"HyperBrowseFolderTreeLarge-" + std::to_wstring(GetCurrentProcessId()));
        const fs::path largeFolder = root.Root() / L"large-parent";
        wchar_t nameBuffer[16]{};
        for (int index = 0; index < kSubfolderCount; ++index)
        {
            swprintf_s(nameBuffer, L"d%04d", index);
            Expect(fs::create_directories(largeFolder / nameBuffer, error) && !error,
                "Failed to create a subfolder for large folder-tree coverage");
        }
        Expect(fs::create_directories(largeFolder / L"d0000" / L"inner", error) && !error,
            "Failed to create the nested folder for large folder-tree presence coverage");
        const fs::path targetFolder = largeFolder / L"d4999";

        hyperbrowse::ui::MainWindow mainWindow(instance);
        mainWindow.SetStartupLaunchPath(targetFolder.wstring());
        Expect(mainWindow.Create(), "Failed to create MainWindow for large folder-tree coverage");

        HWND mainWindowHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
        Expect(mainWindowHandle != nullptr, "Failed to find the large folder-tree MainWindow");
        HWND treeView = FindWindowExW(mainWindowHandle, nullptr, WC_TREEVIEWW, nullptr);
        Expect(treeView != nullptr, "Large folder-tree MainWindow did not create the tree control");

        // The longest single message dispatch is the longest time the UI thread was blocked.
        double longestDispatchMs = 0.0;
        const auto pumpUntil = [&](const std::function<bool()>& predicate, DWORD timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            MSG message{};
            while (GetTickCount64() < deadline)
            {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    const auto start = std::chrono::steady_clock::now();
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                    longestDispatchMs = std::max(
                        longestDispatchMs,
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                }

                if (predicate())
                {
                    return true;
                }
                MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            return predicate();
        };

        HTREEITEM selectedItem = nullptr;
        Expect(pumpUntil([&]()
        {
            selectedItem = TreeView_GetSelection(treeView);
            return selectedItem != nullptr && ReadTreeItemText(treeView, selectedItem) == L"d4999";
        }, 60000), "Folder tree did not select the last folder of a very large parent");

        const HTREEITEM parentItem = TreeView_GetParent(treeView, selectedItem);
        Expect(parentItem != nullptr, "The selected large-folder child had no parent item");
        Expect(ReadTreeItemText(treeView, TreeView_GetChild(treeView, parentItem)) != L"Loading...",
            "The loading placeholder was not removed after the large folder finished filling");

        int childCount = 0;
        HTREEITEM firstItem = nullptr;
        for (HTREEITEM child = TreeView_GetChild(treeView, parentItem); child;
             child = TreeView_GetNextSibling(treeView, child))
        {
            ++childCount;
            if (!firstItem && ReadTreeItemText(treeView, child) == L"d0000")
            {
                firstItem = child;
            }
        }
        Expect(childCount == kSubfolderCount, "Folder tree did not contain every subfolder of the large parent");
        Expect(firstItem != nullptr, "Folder tree did not contain the first subfolder of the large parent");

        Expect(pumpUntil([&]()
        {
            TVITEMW treeItem{};
            treeItem.mask = TVIF_CHILDREN;
            treeItem.hItem = firstItem;
            return TreeView_GetItem(treeView, &treeItem) != FALSE && treeItem.cChildren == 1;
        }, 60000), "Child presence results did not reach the large folder tree");

        std::wcout << L"Large folder-tree longest UI dispatch: " << longestDispatchMs << L" ms\n";
        Expect(longestDispatchMs < kMaxDispatchMs,
            "Applying a 5,000-folder child list blocked the UI thread for too long");

        DestroyWindow(mainWindowHandle);
        PumpMessagesFor(100);
    }

    void RunMainWindowKeyboardFocusScenario(HINSTANCE instance)
    {
        using hyperbrowse::ui::command_ids::ID_ACTION_FILTER_EDIT;

        ScopedRegistryDwordBackup detailsStripBackup(kRegistryPath, L"DetailsStripVisible");
        SetRegistryDwordValue(kRegistryPath, L"DetailsStripVisible", 1);

        struct KeyboardStateGuard
        {
            std::array<BYTE, 256> original{};
            bool captured{GetKeyboardState(original.data()) != FALSE};

            ~KeyboardStateGuard()
            {
                if (captured)
                {
                    SetKeyboardState(original.data());
                }
            }
        } keyboardState;

        Expect(keyboardState.captured, "Failed to capture the keyboard state for main-window focus coverage");

        hyperbrowse::ui::MainWindow mainWindow(instance);
        Expect(mainWindow.Create(), "Failed to create MainWindow for keyboard focus coverage");
        mainWindow.Show(SW_SHOW);
        PumpMessagesFor(100);

        const HWND filterEdit = GetDlgItem(mainWindow.Hwnd(), ID_ACTION_FILTER_EDIT);
        const HWND treePane = FindWindowExW(mainWindow.Hwnd(), nullptr, WC_TREEVIEWW, nullptr);
        const HWND browserPane = FindWindowExW(mainWindow.Hwnd(), nullptr, L"HyperBrowseBrowserPane", nullptr);
        struct DetailsTextSearch
        {
            HWND parent{};
            HWND result{};
        } detailsTextSearch{mainWindow.Hwnd(), nullptr};
        EnumChildWindows(mainWindow.Hwnd(), [](HWND child, LPARAM parameter) -> BOOL
        {
            auto* search = reinterpret_cast<DetailsTextSearch*>(parameter);
            if (GetParent(child) != search->parent)
            {
                return TRUE;
            }

            wchar_t className[64]{};
            GetClassNameW(child, className, static_cast<int>(std::size(className)));
            const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
            if ((style & ES_READONLY) != 0
                || std::wstring_view(className) == L"RICHEDIT50W"
                || std::wstring_view(className) == L"RichEdit20W")
            {
                search->result = child;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&detailsTextSearch));
        const HWND detailsText = detailsTextSearch.result;

        Expect(filterEdit != nullptr, "MainWindow did not create the filter edit for keyboard focus coverage");
        Expect(treePane != nullptr, "MainWindow did not create the folder tree for keyboard focus coverage");
        Expect(browserPane != nullptr, "MainWindow did not create the browser pane for keyboard focus coverage");
        Expect(detailsText != nullptr, "MainWindow did not create the read-only details text control for keyboard focus coverage");
        Expect(IsWindowVisible(detailsText) != FALSE, "Details text control was not visible for keyboard focus coverage");

        const auto sendKey = [&](HWND target, WPARAM key)
        {
            MSG message{};
            message.hwnd = target;
            message.message = WM_KEYDOWN;
            message.wParam = key;
            return mainWindow.TranslateAcceleratorMessage(&message);
        };

        SetForegroundWindow(mainWindow.Hwnd());
        SetFocus(filterEdit);
        Expect(GetFocus() == filterEdit, "Could not focus the filter edit for keyboard traversal coverage");

        Expect(sendKey(filterEdit, VK_TAB), "Tab from the filter edit was not consumed by main-window focus routing");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab from the filter edit did not reach the custom toolbar focus surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab between custom toolbar targets was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab between custom toolbar targets lost the main-window focus surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_SPACE), "Space did not activate the focused custom toolbar target");
        Expect(sendKey(mainWindow.Hwnd(), VK_SPACE), "Space did not activate the custom toolbar target a second time");

        SetFocus(filterEdit);
        int customToolbarFocusCount = 0;
        bool reachedTree = false;
        for (int step = 0; step < 64; ++step)
        {
            const HWND currentFocus = GetFocus();
            Expect(currentFocus != nullptr, "Keyboard traversal lost focus before reaching the folder tree");
            Expect(sendKey(currentFocus, VK_TAB), "Tab was not consumed while traversing the main-window focus sequence");
            const HWND nextFocus = GetFocus();
            if (nextFocus == mainWindow.Hwnd())
            {
                ++customToolbarFocusCount;
            }
            if (nextFocus == treePane)
            {
                reachedTree = true;
                break;
            }
        }
        Expect(customToolbarFocusCount > 0, "Main-window Tab traversal skipped all custom toolbar targets");
        Expect(reachedTree, "Main-window Tab traversal did not reach the folder tree");

        Expect(sendKey(treePane, VK_TAB), "Tab from the folder tree was not consumed");
        Expect(GetFocus() == browserPane, "Tab from the folder tree did not focus the browser pane");

        Expect(sendKey(browserPane, VK_TAB), "Tab from the browser pane was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab from the browser pane did not focus the custom details tab surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab between details tabs was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab between details tabs lost the custom focus surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab from the details tabs to the close button was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab from the Cache Stats tab did not retain the custom focus surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab from the details close button was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab from the details close button did not retain the custom focus surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab from the details tabs to the close button was not consumed");
        Expect(GetFocus() == detailsText, "Tab did not reach the native details text control");

        std::array<BYTE, 256> shiftedKeyboardState = keyboardState.original;
        shiftedKeyboardState[VK_SHIFT] |= 0x80;
        Expect(SetKeyboardState(shiftedKeyboardState.data()) != FALSE,
               "Failed to stage Shift for reverse main-window focus traversal");
        Expect(sendKey(detailsText, VK_TAB), "Shift+Tab from the details text control was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Shift+Tab from the details text control did not reach the custom close-button surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Shift+Tab from the details close button was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Shift+Tab from the details close button did not reach the Cache Stats tab");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Shift+Tab from the Cache Stats tab was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Shift+Tab from the Cache Stats tab did not reach Quick Actions");
        Expect(SetKeyboardState(keyboardState.original.data()) != FALSE,
               "Failed to restore the keyboard state after reverse focus traversal");

        Expect(sendKey(mainWindow.Hwnd(), VK_RETURN), "Enter did not activate the Quick Actions details tab");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab from the Quick Actions details tab was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab from Quick Actions did not reach the Cache Stats tab");
        Expect(sendKey(mainWindow.Hwnd(), VK_TAB), "Tab from the Cache Stats tab was not consumed");
        Expect(GetFocus() == mainWindow.Hwnd(), "Tab from the Cache Stats tab did not reach the custom close-button surface");
        Expect(sendKey(mainWindow.Hwnd(), VK_SPACE), "Space did not activate the custom details close button");
        Expect(GetFocus() == browserPane, "Closing the details panel did not restore focus to the browser pane");

        SendMessageW(mainWindow.Hwnd(),
                     WM_COMMAND,
                     MAKEWPARAM(hyperbrowse::ui::command_ids::ID_VIEW_THUMBNAILS, 0),
                     0);
        SetFocus(browserPane);
        Expect(GetFocus() == browserPane, "Could not focus the thumbnail browser for rating shortcut coverage");
        auto* browserPaneController = reinterpret_cast<hyperbrowse::browser::BrowserPane*>(
            GetWindowLongPtrW(browserPane, GWLP_USERDATA));
        Expect(browserPaneController != nullptr, "Could not resolve the thumbnail browser for rating shortcut coverage");
        browserPaneController->ClearSelection();
        Expect(sendKey(browserPane, '5') && sendKey(browserPane, VK_NUMPAD5),
               "The focused thumbnail browser did not consume top-row and numpad rating keys");
        SetFocus(filterEdit);
        Expect(!sendKey(filterEdit, '5') && !sendKey(filterEdit, VK_NUMPAD5),
               "Thumbnail rating keys were intercepted while the filter edit owned focus");

        SetWindowTextW(filterEdit, L"clear filter smoke");
        Expect(GetWindowTextLengthW(filterEdit) > 0, "Could not set the filter text for clear-button coverage");
        SendMessageW(mainWindow.Hwnd(),
                     WM_COMMAND,
                     MAKEWPARAM(hyperbrowse::ui::command_ids::ID_ACTION_CLEAR_FILTER, 0),
                     0);
        PumpMessagesFor(50);
        Expect(GetWindowTextLengthW(filterEdit) == 0, "Clear-filter command did not clear the filter edit");
        Expect(GetFocus() == filterEdit, "Clear-filter command did not return focus to the filter edit");

        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }

    void RunBrowserInlineRenameScenario(HINSTANCE instance)
    {
        using namespace hyperbrowse::ui::command_ids;

        ScopedRegistryDwordBackup browserModeBackup(kRegistryPath, L"BrowserMode");
        ScopedRegistryDwordBackup subfoldersBackup(kRegistryPath, L"ShowSubfoldersInBrowser");
        ScopedRegistryDwordBackup thumbnailDetailsBackup(kRegistryPath, L"ThumbnailDetailsVisible");
        ScopedRegistryDwordBackup sortModeBackup(kRegistryPath, L"SortMode");
        ScopedRegistryDwordBackup sortAscendingBackup(kRegistryPath, L"SortAscending");
        ScopedRegistryDwordBackup detailsStripBackup(kRegistryPath, L"DetailsStripVisible");
        SetRegistryDwordValue(kRegistryPath, L"BrowserMode", 0);
        SetRegistryDwordValue(kRegistryPath, L"ShowSubfoldersInBrowser", 0);
        SetRegistryDwordValue(kRegistryPath, L"ThumbnailDetailsVisible", 1);
        SetRegistryDwordValue(kRegistryPath,
                              L"SortMode",
                              static_cast<DWORD>(hyperbrowse::browser::BrowserSortMode::FileName));
        SetRegistryDwordValue(kRegistryPath, L"SortAscending", 1);
        SetRegistryDwordValue(kRegistryPath, L"DetailsStripVisible", 0);

        TempFolder root(L"HyperBrowseInlineRename");
        root.WriteFile(L"before.jpg", 16);
        const fs::path beforePath = root.Root() / L"before.jpg";
        const fs::path thumbnailRenamePath = root.Root() / L"thumbnail.jpg";
        const fs::path detailsRenamePath = root.Root() / L"details.jpg";
        const fs::path tabRenamePath = root.Root() / L"tab.jpg";
        const fs::path cancelledPath = root.Root() / L"cancelled.jpg";

        hyperbrowse::ui::MainWindow mainWindow(instance);
        mainWindow.SetStartupLaunchPath(root.Root().wstring());
        Expect(mainWindow.Create(), "Failed to create MainWindow for inline rename coverage");
        mainWindow.Show(SW_SHOW);

        const HWND browserPane = FindWindowExW(
            mainWindow.Hwnd(), nullptr, L"HyperBrowseBrowserPane", nullptr);
        const HWND detailsList = browserPane
            ? FindWindowExW(browserPane, nullptr, WC_LISTVIEWW, nullptr)
            : nullptr;
        const HWND filterEdit = GetDlgItem(mainWindow.Hwnd(), ID_ACTION_FILTER_EDIT);
        Expect(browserPane != nullptr && detailsList != nullptr,
               "Could not find the browser pane and details list for inline rename coverage");
        Expect(filterEdit != nullptr, "Could not find the filter edit for inline rename coverage");
        Expect(PumpMessagesUntil([&]()
        {
            return ListView_GetItemCount(detailsList) == 1;
        }, 10000), "The inline rename fixture did not finish loading");

        const auto readDetailsName = [&]()
        {
            wchar_t name[512]{};
            ListView_GetItemText(detailsList, 0, 0, name, static_cast<int>(std::size(name)));
            return std::wstring(name);
        };
        const auto startInlineRename = [&](HWND focusTarget, const wchar_t* expectedName)
        {
            SetFocus(focusTarget);
            Expect(GetFocus() == focusTarget, "Could not focus the browser view before inline rename");
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_FILE_SELECT_ALL, 0), 0);

            MSG f2Message{};
            f2Message.hwnd = focusTarget;
            f2Message.message = WM_KEYDOWN;
            f2Message.wParam = VK_F2;
            Expect(mainWindow.TranslateAcceleratorMessage(&f2Message),
                   "F2 did not start the browser rename command");

            const HWND edit = FindWindowExW(browserPane, nullptr, L"Edit", nullptr);
            Expect(edit != nullptr && IsWindowVisible(edit) != FALSE,
                   "F2 did not create a visible inline rename editor");
            wchar_t text[512]{};
            GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
            Expect(std::wstring(text) == expectedName,
                   "The inline rename editor did not contain the selected filename");
            DWORD selectionStart = 0;
            DWORD selectionEnd = 0;
            SendMessageW(edit,
                         EM_GETSEL,
                         reinterpret_cast<WPARAM>(&selectionStart),
                         reinterpret_cast<LPARAM>(&selectionEnd));
            const std::wstring stem = fs::path(expectedName).stem().wstring();
            const std::wstring extension = fs::path(expectedName).extension().wstring();
            const std::size_t expectedSelectionEnd = !stem.empty() && !extension.empty()
                ? stem.size()
                : std::wstring(expectedName).size();
            Expect(selectionStart == 0 && selectionEnd == expectedSelectionEnd,
                   "Inline rename did not select the filename stem while preserving its extension");
            return edit;
        };
        const auto waitForRename = [&](const fs::path& oldPath,
                                       const fs::path& newPath,
                                       const wchar_t* expectedName)
        {
            return PumpMessagesUntil([&]()
            {
                return !fs::exists(oldPath)
                    && fs::exists(newPath)
                    && ListView_GetItemCount(detailsList) == 1
                    && readDetailsName() == expectedName;
            }, 10000);
        };

        const auto activateWindow = [](HWND window)
        {
            if (!SetForegroundWindow(window))
            {
                const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
                const DWORD currentThread = GetCurrentThreadId();
                if (foregroundThread != 0 && foregroundThread != currentThread
                    && AttachThreadInput(currentThread, foregroundThread, TRUE))
                {
                    SetForegroundWindow(window);
                    AttachThreadInput(currentThread, foregroundThread, FALSE);
                }
            }
            SetFocus(window);
        };
        activateWindow(mainWindow.Hwnd());
        HWND edit = startInlineRename(browserPane, L"before.jpg");
        SetWindowTextW(edit, L"thumbnail.jpg");
        SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
        Expect(FindWindowExW(browserPane, nullptr, L"Edit", nullptr) == nullptr,
               "Enter did not close the thumbnail inline rename editor");
        Expect(waitForRename(beforePath, thumbnailRenamePath, L"thumbnail.jpg"),
               "Enter did not finish the thumbnail-view rename and refresh the browser model");

        SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_DETAILS, 0), 0);
        Expect(PumpMessagesUntil([&]()
        {
            return IsWindowVisible(detailsList) != FALSE
                && readDetailsName() == L"thumbnail.jpg";
        }, 5000), "Details mode did not show the renamed thumbnail item");

        edit = startInlineRename(detailsList, L"thumbnail.jpg");
        SetWindowTextW(edit, L"details.jpg");
        SetFocus(browserPane);
        Expect(GetFocus() == detailsList,
               "Focus loss did not return focus to the Details view");
        const bool focusLossRenameCompleted = waitForRename(
            thumbnailRenamePath, detailsRenamePath, L"details.jpg");
        Expect(focusLossRenameCompleted,
               "Focus loss did not finish the details-view rename and refresh the browser model");

        edit = startInlineRename(detailsList, L"details.jpg");
        SetWindowTextW(edit, L"tab.jpg");
        SendMessageW(edit, WM_KEYDOWN, VK_TAB, 0);
        Expect(GetFocus() != edit && GetFocus() != detailsList,
               "Tab did not leave the inline editor through the main-window focus sequence");
        Expect(waitForRename(detailsRenamePath, tabRenamePath, L"tab.jpg"),
               "Tab did not commit the details-view rename and refresh the browser model");

        SetFocus(detailsList);
        edit = startInlineRename(detailsList, L"tab.jpg");
        SetWindowTextW(edit, L"cancelled.jpg");
        SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
        Expect(FindWindowExW(browserPane, nullptr, L"Edit", nullptr) == nullptr,
               "Escape did not close the inline rename editor");
        Expect(fs::exists(tabRenamePath) && !fs::exists(cancelledPath),
               "Escape committed an inline rename instead of cancelling it");

        std::atomic_bool fallbackDialogOpened{false};
        std::thread fallbackDialogCloser([&]()
        {
            ComScope workerComScope;
            const ULONGLONG deadline = GetTickCount64() + 10000;
            HWND dialog = nullptr;
            while (GetTickCount64() < deadline
                   && !(dialog = FindWindowW(L"HyperBrowseTextInputDialog", nullptr)))
            {
                Sleep(10);
            }
            if (dialog)
            {
                fallbackDialogOpened.store(true, std::memory_order_release);
                SendMessageW(dialog, WM_CLOSE, 0, 0);
            }
            else
            {
                SendMessageW(mainWindow.Hwnd(), WM_CLOSE, 0, 0);
            }
        });
        SetFocus(filterEdit);
        MSG fallbackMessage{};
        fallbackMessage.hwnd = filterEdit;
        fallbackMessage.message = WM_KEYDOWN;
        fallbackMessage.wParam = VK_F2;
        const bool fallbackHandled = mainWindow.TranslateAcceleratorMessage(&fallbackMessage);
        fallbackDialogCloser.join();
        Expect(fallbackHandled && fallbackDialogOpened.load(std::memory_order_acquire),
               "F2 outside the browser did not open the modal rename fallback");
        Expect(fs::exists(tabRenamePath) && !fs::exists(cancelledPath),
               "Cancelling the modal rename fallback changed the selected file");

        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }

        void RunMainWindowAccessibilityScenario(HINSTANCE instance)
        {
         ScopedRegistryDwordBackup detailsStripBackup(kRegistryPath, L"DetailsStripVisible");
         SetRegistryDwordValue(kRegistryPath, L"DetailsStripVisible", 1);
             TempFolder breadcrumbRoot(L"HyperBrowseAccessibilityBreadcrumb");
         fs::path breadcrumbFolder = breadcrumbRoot.Root();
         for (int index = 0; index < 14; ++index)
         {
             breadcrumbFolder /= L"Parent";
         }
         breadcrumbFolder /= L"Current";
             std::error_code breadcrumbError;
             fs::create_directories(breadcrumbFolder, breadcrumbError);
             Expect(!breadcrumbError, "Failed to create the nested breadcrumb accessibility fixture");
         hyperbrowse::ui::MainWindow mainWindow(instance);
             mainWindow.SetStartupLaunchPath(breadcrumbFolder.wstring());
         Expect(mainWindow.Create(), "Failed to create MainWindow for accessibility coverage");
         mainWindow.Show(SW_SHOW);
         PumpMessagesFor(100);

             std::vector<HWND> breadcrumbControls;
             EnumChildWindows(mainWindow.Hwnd(), [](HWND child, LPARAM context) -> BOOL
             {
                 wchar_t className[32]{};
                 if (GetClassNameW(child, className, static_cast<int>(std::size(className))) == 0
                     || _wcsicmp(className, L"Button") != 0)
                 {
                     return TRUE;
                 }

                 const int textLength = GetWindowTextLengthW(child);
                 if (textLength <= 0)
                 {
                     return TRUE;
                 }
                 std::wstring text(static_cast<std::size_t>(textLength) + 1, L'\0');
                 GetWindowTextW(child, text.data(), static_cast<int>(text.size()));
                 text.resize(wcslen(text.c_str()));
                 if (text.rfind(L"Navigate to ", 0) == 0 || text == L"More parent folders")
                 {
                     reinterpret_cast<std::vector<HWND>*>(context)->push_back(child);
                 }
                 return TRUE;
             }, reinterpret_cast<LPARAM>(&breadcrumbControls));

             HWND currentFolderButton = nullptr;
             HWND moreFoldersButton = nullptr;
             for (HWND button : breadcrumbControls)
             {
                 const int textLength = GetWindowTextLengthW(button);
                 std::wstring text(static_cast<std::size_t>(textLength) + 1, L'\0');
                 GetWindowTextW(button, text.data(), static_cast<int>(text.size()));
                 text.resize(wcslen(text.c_str()));
                 if (text.rfind(L"Navigate to ", 0) == 0 && text.find(L"Current") != std::wstring::npos)
                 {
                     currentFolderButton = button;
                 }
                 else if (text == L"More parent folders")
                 {
                     moreFoldersButton = button;
                 }
             }
             Expect(currentFolderButton && IsWindowVisible(currentFolderButton) != FALSE,
                    "The active breadcrumb segment was not visible as a native child button");
             Expect((GetWindowLongPtrW(currentFolderButton, GWL_STYLE) & WS_TABSTOP) != 0,
                    "The active breadcrumb segment was not keyboard reachable");
             Expect(moreFoldersButton && IsWindowVisible(moreFoldersButton) != FALSE,
                    "Long-path breadcrumb overflow was not exposed as a visible button");

             const auto captureBreadcrumbTextPixels = [&]()
             {
                 std::vector<POINT> textPixels;
                 RECT buttonRect{};
                 Expect(GetWindowRect(currentFolderButton, &buttonRect) != FALSE,
                        "Failed to read the active breadcrumb button bounds");
                 POINT buttonOrigin{buttonRect.left, buttonRect.top};
                 MapWindowPoints(nullptr, mainWindow.Hwnd(), &buttonOrigin, 1);
                 const int buttonWidth = buttonRect.right - buttonRect.left;
                 const int buttonHeight = buttonRect.bottom - buttonRect.top;
                 HDC buttonDc = GetDC(currentFolderButton);
                 Expect(buttonDc != nullptr, "Failed to sample the active breadcrumb button");
                 const COLORREF background = GetPixel(buttonDc, buttonWidth - 2, buttonHeight / 2);
                 for (int y = 4; y < buttonHeight - 4; ++y)
                 {
                     for (int x = 6; x < buttonWidth - 6; ++x)
                     {
                         const COLORREF pixel = GetPixel(buttonDc, x, y);
                         if (pixel != CLR_INVALID && pixel != background)
                         {
                             textPixels.push_back(POINT{buttonOrigin.x + x, buttonOrigin.y + y});
                         }
                     }
                 }
                 ReleaseDC(currentFolderButton, buttonDc);
                 return std::pair{std::move(textPixels), background};
             };

             HWND treeView = FindWindowExW(mainWindow.Hwnd(), nullptr, WC_TREEVIEWW, nullptr);
             Expect(treeView != nullptr, "Could not find the folder tree for breadcrumb splitter coverage");
             RECT treeRect{};
             Expect(GetWindowRect(treeView, &treeRect) != FALSE,
                    "Could not read the folder tree bounds for breadcrumb splitter coverage");
             POINT treeCorners[2]{{treeRect.left, treeRect.top}, {treeRect.right, treeRect.bottom}};
             MapWindowPoints(nullptr, mainWindow.Hwnd(), treeCorners, 2);
             RECT mainClient{};
             GetClientRect(mainWindow.Hwnd(), &mainClient);
             const int splitterX = treeCorners[1].x + 2;
             const int splitterY = treeCorners[0].y + 4;
             SendMessageW(mainWindow.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(splitterX, splitterY));
             SendMessageW(mainWindow.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(mainClient.right - 1, splitterY));
             const auto [breadcrumbTextPixels, breadcrumbBackground] = captureBreadcrumbTextPixels();
             Expect(!breadcrumbTextPixels.empty(), "Could not find rendered text in the active breadcrumb button");
             SendMessageW(mainWindow.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(0, splitterY));
             SendMessageW(mainWindow.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(0, splitterY));
             PumpMessagesFor(50);

             POINT exposedTextPixel{};
             bool foundExposedTextPixel = false;
             for (const POINT candidate : breadcrumbTextPixels)
             {
                 bool coveredByBreadcrumb = false;
                 for (const HWND button : breadcrumbControls)
                 {
                     if (!button || IsWindowVisible(button) == FALSE)
                     {
                         continue;
                     }
                     RECT buttonRect{};
                     if (GetWindowRect(button, &buttonRect) == FALSE)
                     {
                         continue;
                     }
                     POINT corners[2]{{buttonRect.left, buttonRect.top}, {buttonRect.right, buttonRect.bottom}};
                     MapWindowPoints(nullptr, mainWindow.Hwnd(), corners, 2);
                     const RECT clientButtonRect{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
                     if (PtInRect(&clientButtonRect, candidate) != FALSE)
                     {
                         coveredByBreadcrumb = true;
                         break;
                     }
                 }
                 if (!coveredByBreadcrumb)
                 {
                     exposedTextPixel = candidate;
                     foundExposedTextPixel = true;
                     break;
                 }
             }
             Expect(foundExposedTextPixel,
                    "Splitter movement did not expose any pixels from the breadcrumb's previous position");
             COLORREF exposedPixelColor{};
             Expect(ReadClientPixel(mainWindow.Hwnd(), exposedTextPixel, &exposedPixelColor),
                    "Failed to sample the exposed breadcrumb area after splitter movement");
             Expect(exposedPixelColor == breadcrumbBackground,
                    "Breadcrumb text remained visible at its previous position after splitter movement");

             IAccessible* breadcrumbAccessible = nullptr;
             const HRESULT breadcrumbAccessibleStatus = AccessibleObjectFromWindow(
                 currentFolderButton,
                 static_cast<DWORD>(OBJID_CLIENT),
                 IID_IAccessible,
                 reinterpret_cast<void**>(&breadcrumbAccessible));
             Expect(SUCCEEDED(breadcrumbAccessibleStatus) && breadcrumbAccessible != nullptr,
                    "Breadcrumb button did not expose a native accessibility provider");
             VARIANT breadcrumbSelf{};
             breadcrumbSelf.vt = VT_I4;
             breadcrumbSelf.lVal = CHILDID_SELF;
             BSTR breadcrumbName = nullptr;
             Expect(SUCCEEDED(breadcrumbAccessible->get_accName(breadcrumbSelf, &breadcrumbName))
                        && breadcrumbName != nullptr,
                    "Breadcrumb button did not expose its full-path accessible name");
             const std::wstring breadcrumbAccessibleName(breadcrumbName, SysStringLen(breadcrumbName));
             SysFreeString(breadcrumbName);
             Expect(breadcrumbAccessibleName.rfind(L"Navigate to ", 0) == 0
                        && breadcrumbAccessibleName.find(L"Current") != std::wstring::npos,
                    "Breadcrumb button did not expose its full target path as its accessible name");
             VARIANT breadcrumbRole{};
             Expect(SUCCEEDED(breadcrumbAccessible->get_accRole(breadcrumbSelf, &breadcrumbRole))
                        && breadcrumbRole.vt == VT_I4
                        && breadcrumbRole.lVal == ROLE_SYSTEM_PUSHBUTTON,
                    "Breadcrumb button did not expose the push-button role");
             VariantClear(&breadcrumbRole);
             breadcrumbAccessible->Release();

              IAccessible* moreFoldersAccessible = nullptr;
              const HRESULT moreFoldersAccessibleStatus = AccessibleObjectFromWindow(
                  moreFoldersButton,
                  static_cast<DWORD>(OBJID_CLIENT),
                  IID_IAccessible,
                  reinterpret_cast<void**>(&moreFoldersAccessible));
              Expect(SUCCEEDED(moreFoldersAccessibleStatus) && moreFoldersAccessible != nullptr,
                  "Breadcrumb overflow button did not expose a native accessibility provider");
              VARIANT moreFoldersSelf{};
              moreFoldersSelf.vt = VT_I4;
              moreFoldersSelf.lVal = CHILDID_SELF;
              BSTR moreFoldersName = nullptr;
              Expect(SUCCEEDED(moreFoldersAccessible->get_accName(moreFoldersSelf, &moreFoldersName))
                      && moreFoldersName != nullptr,
                  "Breadcrumb overflow button did not expose an accessible name");
              const std::wstring moreFoldersAccessibleName(moreFoldersName, SysStringLen(moreFoldersName));
              SysFreeString(moreFoldersName);
              Expect(moreFoldersAccessibleName == L"More parent folders",
                  "Breadcrumb overflow button exposed the wrong accessible name");
              VARIANT moreFoldersRole{};
              Expect(SUCCEEDED(moreFoldersAccessible->get_accRole(moreFoldersSelf, &moreFoldersRole))
                      && moreFoldersRole.vt == VT_I4
                      && moreFoldersRole.lVal == ROLE_SYSTEM_PUSHBUTTON,
                  "Breadcrumb overflow button did not expose the push-button role");
              VariantClear(&moreFoldersRole);
              moreFoldersAccessible->Release();

         const LRESULT objectResult = SendMessageW(mainWindow.Hwnd(), WM_GETOBJECT, 0, OBJID_CLIENT);
         Expect(objectResult != 0, "MainWindow did not return an accessibility object");

         IAccessible* accessible = nullptr;
         const HRESULT objectStatus = ObjectFromLresult(
             objectResult,
             IID_IAccessible,
             0,
             reinterpret_cast<void**>(&accessible));
         Expect(SUCCEEDED(objectStatus) && accessible != nullptr,
             "Could not materialize the MainWindow accessibility object");

         long childCount = 0;
         Expect(SUCCEEDED(accessible->get_accChildCount(&childCount)) && childCount > 0,
             "MainWindow accessibility object did not expose custom children");

         bool foundFileMenu = false;
         bool foundImageBrowser = false;
         bool foundCacheStatsTab = false;
         LONG browserChildId = 0;
         for (LONG childId = 1; childId <= childCount; ++childId)
         {
             VARIANT child{};
             child.vt = VT_I4;
             child.lVal = childId;
             BSTR name = nullptr;
             Expect(SUCCEEDED(accessible->get_accName(child, &name)) && name != nullptr && SysStringLen(name) > 0,
                 "MainWindow accessibility child did not expose a name");

             VARIANT role{};
             Expect(SUCCEEDED(accessible->get_accRole(child, &role)) && role.vt == VT_I4,
                 "MainWindow accessibility child did not expose a role");

             LONG left = 0;
             LONG top = 0;
             LONG width = 0;
             LONG height = 0;
             Expect(SUCCEEDED(accessible->accLocation(&left, &top, &width, &height, child))
                  && width > 0 && height > 0,
                 "MainWindow accessibility child did not expose screen bounds");

             const std::wstring childName(name, SysStringLen(name));
             foundFileMenu = foundFileMenu || childName == L"File";
             if (childName == L"Cache Stats")
             {
                 foundCacheStatsTab = role.lVal == ROLE_SYSTEM_PAGETAB;
             }
             if (childName == L"Image browser")
             {
              foundImageBrowser = true;
              browserChildId = childId;
             }
             SysFreeString(name);
             VariantClear(&role);
         }
         Expect(foundFileMenu, "MainWindow accessibility tree did not expose the File menu");
         Expect(foundImageBrowser && browserChildId > 0,
             "MainWindow accessibility tree did not expose the custom image browser surface");
         Expect(foundCacheStatsTab,
             "MainWindow accessibility tree did not expose the Cache Stats page tab");

         VARIANT firstChild{};
         Expect(SUCCEEDED(accessible->accNavigate(NAVDIR_FIRSTCHILD, VARIANT{VT_I4, {CHILDID_SELF}}, &firstChild))
                 && firstChild.vt == VT_I4 && firstChild.lVal == 1,
             "MainWindow accessibility navigation did not expose its first child");
         VariantClear(&firstChild);

         VARIANT browserChild{};
         browserChild.vt = VT_I4;
         browserChild.lVal = browserChildId;
         Expect(SUCCEEDED(accessible->accSelect(SELFLAG_TAKEFOCUS, browserChild)),
             "MainWindow accessibility could not focus the custom image browser surface");
         Expect(GetFocus() == FindWindowExW(mainWindow.Hwnd(), nullptr, L"HyperBrowseBrowserPane", nullptr),
             "MainWindow accessibility focus selection did not reach the browser surface");

         VARIANT focus{};
         Expect(SUCCEEDED(accessible->get_accFocus(&focus))
                 && focus.vt == VT_I4 && focus.lVal == browserChildId,
             "MainWindow accessibility object did not report the focused custom child");
         VariantClear(&focus);

         VARIANT browserState{};
         Expect(SUCCEEDED(accessible->get_accState(browserChild, &browserState))
                 && browserState.vt == VT_I4
                 && (browserState.lVal & STATE_SYSTEM_FOCUSED) != 0,
             "MainWindow accessibility object did not report the focused state");
         VariantClear(&browserState);

         accessible->Release();
         DestroyWindow(mainWindow.Hwnd());
         PumpMessagesFor(100);
        }

    void RunMainWindowTextInputAcceleratorScenario(HINSTANCE instance)
    {
        hyperbrowse::ui::MainWindow mainWindow(instance);
        Expect(mainWindow.Create(), "Failed to create the MainWindow for text-input accelerator coverage");

        HWND edit = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0,
            0,
            240,
            24,
            mainWindow.Hwnd(),
            nullptr,
            instance,
            nullptr);
        Expect(edit != nullptr, "Failed to create the text-input accelerator test edit control");

        constexpr std::array<WPARAM, 12> ratingKeys{
            '0', '1', '2', '3', '4', '5',
            VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5};
        for (const WPARAM key : ratingKeys)
        {
            MSG message{};
            message.hwnd = edit;
            message.message = WM_KEYDOWN;
            message.wParam = key;
            Expect(!mainWindow.TranslateAcceleratorMessage(&message),
                   "MainWindow consumed a thumbnail rating key while a text-input control owned focus");
        }

        constexpr std::array<WPARAM, 4> oemSizeKeys{
            VK_OEM_MINUS,
            VK_OEM_PLUS,
            VK_OEM_MINUS,
            VK_OEM_PLUS};
        for (const WPARAM key : oemSizeKeys)
        {
            MSG message{};
            message.hwnd = edit;
            message.message = WM_KEYDOWN;
            message.wParam = key;
            Expect(!mainWindow.TranslateAcceleratorMessage(&message),
                   "MainWindow consumed an OEM size key before the focused edit control could receive it");
        }

        constexpr std::array<WPARAM, 8> navigationKeys{
            VK_LEFT,
            VK_RIGHT,
            VK_UP,
            VK_DOWN,
            VK_PRIOR,
            VK_NEXT,
            VK_HOME,
            VK_END};
        for (const WPARAM key : navigationKeys)
        {
            MSG message{};
            message.hwnd = edit;
            message.message = WM_KEYDOWN;
            message.wParam = key;
            Expect(!mainWindow.TranslateAcceleratorMessage(&message),
                   "MainWindow consumed a navigation key before the focused edit control could receive it");
        }

        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }

    void RunStartupViewerEnumerationScenario(HINSTANCE instance)
    {
        TempFolder root(L"HyperBrowseStartupViewerEnumeration");
        ScopedRegistryDwordBackup escapeKeyBehaviorBackup(kRegistryPath, kRegistryValueViewerEscapeKeyBehavior);
        SetRegistryDwordValue(kRegistryPath,
                              kRegistryValueViewerEscapeKeyBehavior,
                              static_cast<DWORD>(hyperbrowse::viewer::EscapeKeyBehavior::FitHeight));
        const fs::path targetPath = root.Root() / L"000-target.jpg";
        WriteTestImage(targetPath, TestImageFormat::Jpeg, 48, 24, 6);
        for (int index = 1; index < 40; ++index)
        {
            WriteTestImage(root.Root() / (L"image_" + std::to_wstring(index) + L".jpg"),
                           TestImageFormat::Jpeg,
                           48,
                           24,
                           6);
        }

        hyperbrowse::ui::MainWindow mainWindow(instance);
        mainWindow.SetStartupLaunchPath(targetPath.wstring());
        Expect(mainWindow.Create(), "Failed to create MainWindow for startup viewer enumeration coverage");

        HWND viewerHandle = nullptr;
        const bool openedWithCompleteFolder = PumpMessagesUntil([&]()
        {
            viewerHandle = FindWindowW(L"HyperBrowseViewerWindow", nullptr);
            if (!viewerHandle)
            {
                return false;
            }

            wchar_t title[512]{};
            GetWindowTextW(viewerHandle, title, static_cast<int>(std::size(title)));
            return std::wstring(title).find(L"/40)") != std::wstring::npos;
        }, 5000);
        Expect(openedWithCompleteFolder,
               "Viewer opened from a startup launch path with only the initial enumeration batch");

         Expect(mainWindow.ViewerEscapeKeyBehavior() == hyperbrowse::viewer::EscapeKeyBehavior::FitHeight,
             "Startup viewer did not receive the persisted Escape behavior");
         // Escape only fits the window once the viewer has an image; before that it just leaves full screen.
         Expect(PumpMessagesUntil([&]() { return mainWindow.ActiveViewerZoomPercent() > 0; }, 10000),
             "Startup viewer did not finish loading its image before the Escape routing check");
         MONITORINFO monitorInfo{sizeof(MONITORINFO)};
         const HMONITOR monitor = MonitorFromWindow(mainWindow.Hwnd(), MONITOR_DEFAULTTONEAREST);
         Expect(monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo) != FALSE,
             "Failed to read the monitor work area for startup viewer Escape coverage");
         MSG escapeMessage{};
         escapeMessage.hwnd = mainWindow.Hwnd();
         escapeMessage.message = WM_KEYDOWN;
         escapeMessage.wParam = VK_ESCAPE;
         Expect(mainWindow.TranslateAcceleratorMessage(&escapeMessage),
             "MainWindow did not consume the forwarded startup viewer Escape message");
         PumpMessagesFor(100);
         RECT escapeRect{};
         Expect(viewerHandle && GetWindowRect(viewerHandle, &escapeRect) != FALSE,
             "MainWindow Escape routing left no readable viewer window");
         Expect(escapeRect.top == monitorInfo.rcWork.top && escapeRect.bottom == monitorInfo.rcWork.bottom,
             "MainWindow Escape routing did not apply the viewer's Fit Height behavior"
             " viewer=" + std::to_string(escapeRect.left) + "," + std::to_string(escapeRect.top) + "," + std::to_string(escapeRect.right) + "," + std::to_string(escapeRect.bottom)
             + " work=" + std::to_string(monitorInfo.rcWork.left) + "," + std::to_string(monitorInfo.rcWork.top) + "," + std::to_string(monitorInfo.rcWork.right) + "," + std::to_string(monitorInfo.rcWork.bottom));

        if (viewerHandle && IsWindow(viewerHandle) != FALSE)
        {
            SendMessageW(viewerHandle, WM_CLOSE, 0, 0);
            PumpMessagesFor(100);
        }
        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }

    void RunMainWindowChromeScenario(HINSTANCE instance)
    {
        using namespace hyperbrowse::ui::command_ids;

        ScopedRegistryDwordBackup browserModeBackup(kRegistryPath, L"BrowserMode");
        ScopedRegistryDwordBackup themeModeBackup(kRegistryPath, L"ThemeMode");
        SetRegistryDwordValue(kRegistryPath, L"BrowserMode", 0);
        SetRegistryDwordValue(kRegistryPath, L"ThemeMode", 0);

        const auto readWindowText = [](HWND window)
        {
            const int textLength = GetWindowTextLengthW(window);
            std::wstring text(static_cast<std::size_t>(textLength) + 1, L'\0');
            const int copiedLength = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
            if (copiedLength <= 0)
            {
                return std::wstring{};
            }
            text.resize(static_cast<std::size_t>(copiedLength));
            return text;
        };
        const auto findStatusStrip = [](HWND parent)
        {
            HWND statusStrip{};
            EnumChildWindows(parent, [](HWND child, LPARAM parameter) -> BOOL
            {
                wchar_t className[32]{};
                if (GetClassNameW(child, className, static_cast<int>(std::size(className))) > 0
                    && _wcsicmp(className, L"STATIC") == 0
                    && (GetWindowLongPtrW(child, GWL_STYLE) & SS_TYPEMASK) == SS_OWNERDRAW)
                {
                    *reinterpret_cast<HWND*>(parameter) = child;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&statusStrip));
            return statusStrip;
        };
        const auto readAccessibleName = [](HWND window, LONG objectId, const char* failureMessage)
        {
            IAccessible* accessible = nullptr;
            const HRESULT objectResult = AccessibleObjectFromWindow(
                window,
                objectId,
                IID_IAccessible,
                reinterpret_cast<void**>(&accessible));
            if (FAILED(objectResult) || !accessible)
            {
                if (accessible)
                {
                    accessible->Release();
                }
                Expect(false, failureMessage);
            }

            VARIANT self{};
            self.vt = VT_I4;
            self.lVal = CHILDID_SELF;
            BSTR name = nullptr;
            const HRESULT nameResult = accessible->get_accName(self, &name);
            std::wstring value;
            if (name)
            {
                value.assign(name, SysStringLen(name));
                SysFreeString(name);
            }
            accessible->Release();
            Expect(SUCCEEDED(nameResult) && !value.empty(), failureMessage);
            return value;
        };

        TempFolder firstFolder(L"HyperBrowseWindowChromeFirst");
        TempFolder secondFolder(L"HyperBrowseWindowChromeSecond");
        const std::wstring firstFolderPath = firstFolder.Root().wstring();
        const std::wstring secondFolderPath = secondFolder.Root().wstring();
        const std::wstring firstTitle = L"HyperBrowse - " + firstFolderPath;
        const std::wstring secondTitle = L"HyperBrowse - " + secondFolderPath;
        const std::wstring initialContext = L"View: Thumbnails | Recursive: Off | Theme: Light";
        const std::wstring changedContext = L"View: Details | Recursive: On | Theme: Dark";

        hyperbrowse::ui::MainWindow firstWindow(instance);
        firstWindow.SetStartupLaunchPath(firstFolderPath);
        Expect(firstWindow.Create(), "Failed to create the first MainWindow for chrome coverage");
        firstWindow.Show(SW_SHOW);
        const HWND firstStatusStrip = findStatusStrip(firstWindow.Hwnd());
        Expect(firstStatusStrip != nullptr, "Could not find the first MainWindow status strip");
        Expect(PumpMessagesUntil([&]()
        {
            return readWindowText(firstWindow.Hwnd()) == firstTitle
                && readWindowText(firstStatusStrip) == initialContext;
        }, 10000), "First MainWindow did not expose its full-path title and browser state");

        hyperbrowse::ui::MainWindow secondWindow(instance);
        secondWindow.SetStartupLaunchPath(secondFolderPath);
        Expect(secondWindow.Create(), "Failed to create the second MainWindow for chrome coverage");
        secondWindow.Show(SW_SHOW);
        const HWND secondStatusStrip = findStatusStrip(secondWindow.Hwnd());
        Expect(secondStatusStrip != nullptr, "Could not find the second MainWindow status strip");
        Expect(PumpMessagesUntil([&]()
        {
            return readWindowText(secondWindow.Hwnd()) == secondTitle
                && readWindowText(secondStatusStrip) == initialContext;
        }, 10000), "Second MainWindow did not expose its own full-path title and browser state");

        Expect(readAccessibleName(firstWindow.Hwnd(), OBJID_WINDOW,
                                  "Could not read the first MainWindow's accessible window name") == firstTitle,
               "First MainWindow accessible name did not include its active folder path");
        Expect(readAccessibleName(firstStatusStrip, OBJID_CLIENT,
                                  "Could not read the first status strip's accessible name") == initialContext,
               "Status strip accessible name did not expose the initial browser state");

        SendMessageW(firstWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_DETAILS, 0), 0);
        SendMessageW(firstWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_RECURSIVE, 0), 0);
        SendMessageW(firstWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_THEME_DARK, 0), 0);
        Expect(PumpMessagesUntil([&]()
        {
            return readWindowText(firstStatusStrip) == changedContext;
        }, 5000), "Status strip did not update its view, recursive, and theme states");
        Expect(readWindowText(firstWindow.Hwnd()) == firstTitle,
               "Changing view, recursive browsing, or theme changed the simplified title");
        Expect(readAccessibleName(firstWindow.Hwnd(), OBJID_WINDOW,
                                  "Could not read the changed MainWindow accessible name") == firstTitle,
               "MainWindow accessible name did not retain the active folder path");
        Expect(readAccessibleName(firstStatusStrip, OBJID_CLIENT,
                                  "Could not read the changed status strip accessible name") == changedContext,
               "Status strip accessible name did not track the changed browser state");
        Expect(readWindowText(secondStatusStrip) == initialContext,
               "Changing one MainWindow changed another window's browser-state summary");

        SendMessageW(secondWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_THUMBNAILS, 0), 0);
        Expect(PumpMessagesUntil([&]()
        {
            return readWindowText(secondWindow.Hwnd()) == secondTitle;
        }, 5000), "Changing the second MainWindow view changed its folder-path title");
        Expect(readWindowText(firstStatusStrip) == changedContext,
               "Changing the second MainWindow changed the first window's browser-state summary");

        Expect(SetWindowPos(firstWindow.Hwnd(),
                            nullptr,
                            0,
                            0,
                            480,
                            640,
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
               "Could not resize the first MainWindow for narrow-layout coverage");
        PumpMessagesFor(100);
        RECT clientRect{};
        RECT statusRect{};
        Expect(GetClientRect(firstWindow.Hwnd(), &clientRect) != FALSE
                   && GetWindowRect(firstStatusStrip, &statusRect) != FALSE,
               "Could not read the narrow MainWindow status-strip bounds");
        MapWindowPoints(nullptr, firstWindow.Hwnd(), reinterpret_cast<POINT*>(&statusRect), 2);
        Expect(statusRect.left >= 0 && statusRect.top >= 0
                   && statusRect.right <= clientRect.right && statusRect.bottom <= clientRect.bottom,
               "Status strip exceeded the client bounds in a narrow MainWindow");
        Expect(readWindowText(firstStatusStrip) == changedContext,
               "Narrow layout truncated the accessible browser-state summary");

        DestroyWindow(secondWindow.Hwnd());
        DestroyWindow(firstWindow.Hwnd());
        PumpMessagesFor(100);
    }

    void RunQuickSendPolishScenario(HINSTANCE instance)
    {
        TempFolder sourceFolder(L"HyperBrowseQuickSendSource");
        TempFolder favoriteFolder(L"HyperBrowseQuickSendFavorite");
        TempFolder recentFolder(L"HyperBrowseQuickSendRecent");
        const fs::path sourceFile = sourceFolder.Root() / L"drag-source.jpg";
        const fs::path favoriteExistingFile = favoriteFolder.Root() / L"existing.png";
        const fs::path recentFirstFile = recentFolder.Root() / L"recent-one.jpg";
        const fs::path recentSecondFile = recentFolder.Root() / L"recent-two.png";
        WriteTestImage(sourceFile, TestImageFormat::Jpeg, 64, 32, 6);
        WriteTestImage(favoriteExistingFile, TestImageFormat::Png, 32, 16);
        WriteTestImage(recentFirstFile, TestImageFormat::Jpeg, 32, 16, 6);
        WriteTestImage(recentSecondFile, TestImageFormat::Png, 32, 16);

        const std::wstring favoritePath = favoriteFolder.Root().wstring();
        const std::wstring recentPath = recentFolder.Root().wstring();
        hyperbrowse::ui::QuickSendPersistedState persistedQuickSendState;
        persistedQuickSendState.favoriteDestinationFolders = {favoritePath};
        FILETIME recentLastUsed{};
        GetSystemTimeAsFileTime(&recentLastUsed);
        ULARGE_INTEGER recentLastUsedFileTime{};
        recentLastUsedFileTime.LowPart = recentLastUsed.dwLowDateTime;
        recentLastUsedFileTime.HighPart = recentLastUsed.dwHighDateTime;
        persistedQuickSendState.destinationUsage = {
            {recentPath, recentLastUsedFileTime.QuadPart}};
        hyperbrowse::ui::QuickSendPersistence::Save(
            persistedQuickSendState,
            [](std::wstring_view valueName, std::wstring_view value)
            {
                const std::wstring registryValueName(valueName);
                SetRegistryStringValue(kRegistryPath, registryValueName.c_str(), value);
            },
            [](std::wstring_view valueName)
            {
                const std::wstring registryValueName(valueName);
                DeleteRegistryValue(kRegistryPath, registryValueName.c_str());
            });
        SetRegistryStringValue(
            kRegistryPath,
            L"RecentDestinationFolders",
            favoritePath + L"\n" + recentPath);
        SetRegistryDwordValue(kRegistryPath, L"BrowserMode", 0);
        SetRegistryDwordValue(kRegistryPath, L"DetailsStripVisible", 1);

        hyperbrowse::ui::MainWindow mainWindow(instance);
        mainWindow.SetStartupLaunchPath(sourceFolder.Root().wstring());
        Expect(mainWindow.Create(), "Failed to create MainWindow for Quick Actions polish coverage");
        mainWindow.Show(SW_SHOW);
        Expect(SetWindowPos(mainWindow.Hwnd(),
                            nullptr,
                            100,
                            100,
                            1400,
                            900,
                            SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
               "Could not size MainWindow for Quick Actions polish coverage");

        const HWND browserHwnd = FindWindowExW(
            mainWindow.Hwnd(),
            nullptr,
            L"HyperBrowseBrowserPane",
            nullptr);
        auto* browser = reinterpret_cast<hyperbrowse::browser::BrowserPane*>(
            GetWindowLongPtrW(browserHwnd, GWLP_USERDATA));
        Expect(browser != nullptr, "Could not find the browser pane for Quick Actions drag coverage");
        Expect(PumpMessagesUntil([&]()
        {
            return browser->DisplayedItemCount() == 1;
        }, 10000), "Browser did not finish loading the Quick Actions drag source image");

        const auto getAccessible = [](HWND window)
        {
            ComPtr<IAccessible> accessible;
            const LRESULT objectResult = SendMessageW(window, WM_GETOBJECT, 0, OBJID_CLIENT);
            Expect(objectResult != 0, "MainWindow did not return its accessibility object for Quick Actions coverage");
            const HRESULT result = ObjectFromLresult(
                objectResult,
                IID_IAccessible,
                0,
                reinterpret_cast<void**>(accessible.GetAddressOf()));
            Expect(SUCCEEDED(result) && accessible.Get() != nullptr,
                   "Could not materialize MainWindow accessibility for Quick Actions coverage");
            return accessible;
        };
        const auto selectQuickActionsTab = [&]()
        {
            ComPtr<IAccessible> accessible = getAccessible(mainWindow.Hwnd());
            long childCount = 0;
            Expect(SUCCEEDED(accessible->get_accChildCount(&childCount)),
                   "Could not enumerate MainWindow accessibility children for Quick Actions");
            for (LONG childId = 1; childId <= childCount; ++childId)
            {
                VARIANT child{};
                child.vt = VT_I4;
                child.lVal = childId;
                BSTR name = nullptr;
                const HRESULT nameResult = accessible->get_accName(child, &name);
                const std::wstring childName = SUCCEEDED(nameResult) && name
                    ? std::wstring(name, SysStringLen(name))
                    : std::wstring{};
                SysFreeString(name);
                if (childName == L"Quick Actions")
                {
                    Expect(SUCCEEDED(accessible->accDoDefaultAction(child)),
                           "Could not select the Quick Actions tab through accessibility");
                    return;
                }
            }
            Expect(false, "MainWindow accessibility did not expose the Quick Actions tab");
        };
        struct QuickSendRowProbe
        {
            RECT clientBounds{};
            std::wstring description;
        };
        const auto readQuickSendRow = [&](std::wstring_view destinationPath)
            -> std::optional<QuickSendRowProbe>
        {
            ComPtr<IAccessible> accessible = getAccessible(mainWindow.Hwnd());
            long childCount = 0;
            Expect(SUCCEEDED(accessible->get_accChildCount(&childCount)),
                   "Could not enumerate MainWindow accessibility rows for Quick Actions");
            for (LONG childId = 1; childId <= childCount; ++childId)
            {
                VARIANT child{};
                child.vt = VT_I4;
                child.lVal = childId;
                BSTR value = nullptr;
                const HRESULT valueResult = accessible->get_accValue(child, &value);
                const std::wstring childValue = SUCCEEDED(valueResult) && value
                    ? std::wstring(value, SysStringLen(value))
                    : std::wstring{};
                SysFreeString(value);
                if (_wcsicmp(childValue.c_str(), std::wstring(destinationPath).c_str()) != 0)
                {
                    continue;
                }

                BSTR description = nullptr;
                const HRESULT descriptionResult = accessible->get_accDescription(child, &description);
                const std::wstring childDescription = SUCCEEDED(descriptionResult) && description
                    ? std::wstring(description, SysStringLen(description))
                    : std::wstring{};
                SysFreeString(description);
                LONG left = 0;
                LONG top = 0;
                LONG width = 0;
                LONG height = 0;
                Expect(SUCCEEDED(accessible->accLocation(&left, &top, &width, &height, child))
                           && width > 0 && height > 0,
                       "Quick Actions row did not expose accessible bounds");
                RECT clientBounds{left, top, left + width, top + height};
                MapWindowPoints(nullptr,
                                mainWindow.Hwnd(),
                                reinterpret_cast<POINT*>(&clientBounds),
                                2);
                return QuickSendRowProbe{clientBounds, childDescription};
            }
            return std::nullopt;
        };

        selectQuickActionsTab();
        Expect(PumpMessagesUntil([&]()
        {
            const auto favoriteRow = readQuickSendRow(favoritePath);
            const auto recentRow = readQuickSendRow(recentPath);
            return favoriteRow && recentRow
                && favoriteRow->description.find(L"1 image") != std::wstring::npos
                && recentRow->description.find(L"2 images") != std::wstring::npos;
        }, 10000), "Quick Actions did not expose deduplicated favorite and recent image counts");
        const auto initialFavoriteRow = readQuickSendRow(favoritePath);
        const auto initialRecentRow = readQuickSendRow(recentPath);
        Expect(initialFavoriteRow && initialRecentRow
                   && initialFavoriteRow->description.find(L"Last used: Never") != std::wstring::npos
                   && initialRecentRow->description.find(L"Last used: Never") == std::wstring::npos,
               "Quick Actions row accessibility did not expose per-destination last-use state");

        struct TooltipWindowSearch
        {
            HWND owner{};
            std::vector<HWND> tooltips;
        };
        TooltipWindowSearch tooltipSearch{mainWindow.Hwnd(), {}};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM parameter) -> BOOL
        {
            auto& search = *reinterpret_cast<TooltipWindowSearch*>(parameter);
            wchar_t className[64]{};
            if (GetClassNameW(candidate, className, static_cast<int>(std::size(className))) > 0
                && _wcsicmp(className, TOOLTIPS_CLASSW) == 0
                && GetWindow(candidate, GW_OWNER) == search.owner)
            {
                search.tooltips.push_back(candidate);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&tooltipSearch));

        const auto readRowTooltip = [&](const QuickSendRowProbe& row)
        {
            HWND tooltipWindow = nullptr;
            UINT_PTR rowToolId = 0;
            std::string enumerationSummary;
            for (HWND candidate : tooltipSearch.tooltips)
            {
                const LRESULT toolCount = SendMessageW(candidate, TTM_GETTOOLCOUNT, 0, 0);
                enumerationSummary.append(" window-tools=");
                enumerationSummary.append(std::to_string(toolCount));
                for (LRESULT index = 0; index < toolCount; ++index)
                {
                    TOOLINFOW toolInfo{};
                    toolInfo.cbSize = sizeof(toolInfo);
                    if (SendMessageW(candidate,
                                     TTM_ENUMTOOLSW,
                                     static_cast<WPARAM>(index),
                                     reinterpret_cast<LPARAM>(&toolInfo)) == FALSE)
                    {
                        enumerationSummary.append(" enum-failed");
                        continue;
                    }
                    if (toolInfo.hwnd == mainWindow.Hwnd()
                        && EqualRect(&toolInfo.rect, &row.clientBounds) != FALSE)
                    {
                        tooltipWindow = candidate;
                        rowToolId = toolInfo.uId;
                        break;
                    }
                }
                if (rowToolId != 0)
                {
                    break;
                }
            }
            if (rowToolId == 0)
            {
                Expect(false,
                       "Quick Actions row did not register a hover tooltip; count="
                           + std::to_string(tooltipSearch.tooltips.size())
                           + " row="
                           + std::to_string(row.clientBounds.left) + ","
                           + std::to_string(row.clientBounds.top) + ","
                           + std::to_string(row.clientBounds.right) + ","
                           + std::to_string(row.clientBounds.bottom)
                           + enumerationSummary);
            }

            NMTTDISPINFOW notification{};
            notification.hdr.hwndFrom = tooltipWindow;
            notification.hdr.idFrom = rowToolId;
            notification.hdr.code = TTN_GETDISPINFOW;
            SendMessageW(mainWindow.Hwnd(),
                         WM_NOTIFY,
                         static_cast<WPARAM>(rowToolId),
                         reinterpret_cast<LPARAM>(&notification));
            Expect(notification.lpszText != nullptr, "Quick Actions tooltip callback returned no text");
            return std::wstring(notification.lpszText);
        };
        const std::wstring favoriteTooltip = readRowTooltip(*initialFavoriteRow);
        const std::wstring recentTooltip = readRowTooltip(*initialRecentRow);
        Expect(favoriteTooltip.find(L"1 image") != std::wstring::npos
                   && favoriteTooltip.find(L"Last used: Never") != std::wstring::npos
                   && recentTooltip.find(L"2 images") != std::wstring::npos
                   && recentTooltip.find(L"Last used: Never") == std::wstring::npos,
               "Quick Actions hover tooltips omitted image counts or last-use timestamps");

        RECT browserClient{};
        Expect(GetClientRect(browserHwnd, &browserClient) != FALSE,
               "Could not read the thumbnail browser bounds for Quick Actions drag");
        const POINT thumbnailStart{50, 50};
        POINT thumbnailStartScreen = thumbnailStart;
        ClientToScreen(browserHwnd, &thumbnailStartScreen);
        SetCursorPos(thumbnailStartScreen.x, thumbnailStartScreen.y);
        SendMessageW(browserHwnd,
                     WM_LBUTTONDOWN,
                     MK_LBUTTON,
                     MAKELPARAM(thumbnailStart.x, thumbnailStart.y));
        const int dragDistance = (std::max)(1, GetSystemMetrics(SM_CXDRAG)) + 2;
        const POINT dragThresholdPoint{thumbnailStart.x + dragDistance, thumbnailStart.y};
        POINT dragThresholdScreen = dragThresholdPoint;
        ClientToScreen(browserHwnd, &dragThresholdScreen);
        SetCursorPos(dragThresholdScreen.x, dragThresholdScreen.y);
        SendMessageW(browserHwnd,
                     WM_MOUSEMOVE,
                     MK_LBUTTON,
                     MAKELPARAM(dragThresholdPoint.x, dragThresholdPoint.y));
        Expect(GetCapture() == mainWindow.Hwnd(),
               "Dragging a thumbnail did not start the in-app Quick Actions drag");

        const POINT dropPoint{
            (initialFavoriteRow->clientBounds.left + initialFavoriteRow->clientBounds.right) / 2,
            (initialFavoriteRow->clientBounds.top + initialFavoriteRow->clientBounds.bottom) / 2};
        POINT dropScreen = dropPoint;
        ClientToScreen(mainWindow.Hwnd(), &dropScreen);
        SetCursorPos(dropScreen.x, dropScreen.y);
        SendMessageW(mainWindow.Hwnd(),
                     WM_MOUSEMOVE,
                     MK_LBUTTON,
                     MAKELPARAM(dropPoint.x, dropPoint.y));
        SendMessageW(mainWindow.Hwnd(),
                     WM_LBUTTONUP,
                     0,
                     MAKELPARAM(dropPoint.x, dropPoint.y));

        const fs::path deliveredFile = favoriteFolder.Root() / sourceFile.filename();
        Expect(PumpMessagesUntil([&]()
        {
            std::error_code error;
            return fs::exists(deliveredFile, error) && !error;
        }, 10000), "In-app thumbnail drag did not deliver the image to its Quick Actions row");
        Expect(PumpMessagesUntil([&]()
        {
            const auto favoriteRow = readQuickSendRow(favoritePath);
            return favoriteRow
                && favoriteRow->description.find(L"2 images") != std::wstring::npos
                && favoriteRow->description.find(L"Last used: Never") == std::wstring::npos;
        }, 10000), "Quick Actions did not refresh image count and last-used time after the row drop");

        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }
}
