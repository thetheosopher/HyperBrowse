#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

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
}
