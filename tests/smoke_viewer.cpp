#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunBrowserPaneScenario(HINSTANCE instance)
    {
        HWND hostWindow = CreateUiHostWindow(instance);
        Expect(hostWindow != nullptr, "Failed to create the hidden UI host window");

        hyperbrowse::browser::BrowserPane browserPane(instance);
        Expect(browserPane.Create(hostWindow), "Failed to create the BrowserPane test control");

         WNDCLASSEXW browserPaneClass{};
         browserPaneClass.cbSize = sizeof(browserPaneClass);
         Expect(GetClassInfoExW(instance, L"HyperBrowseBrowserPane", &browserPaneClass) != FALSE,
             "Failed to query the BrowserPane window class");
         Expect((browserPaneClass.style & CS_DBLCLKS) != 0,
             "BrowserPane must register with CS_DBLCLKS so thumbnail double-clicks open the viewer");

        MoveWindow(browserPane.Hwnd(), 0, 0, 860, 620, TRUE);

        hyperbrowse::browser::BrowserModel model;
        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"alpha.jpg", L"C:\\Alpha\\alpha.jpg", L"JPG", L"2026-04-11 10:00", 100, 40, 320, 240});
        items.push_back(hyperbrowse::browser::BrowserItem{L"테스트-漢字.png", L"C:\\Alpha\\테스트-漢字.png", L"PNG", L"2026-04-11 10:01", 200, 10, 640, 480});
        items.push_back(hyperbrowse::browser::BrowserItem{L"gamma.gif", L"C:\\Alpha\\gamma.gif", L"GIF", L"2026-04-11 10:02", 300, 70, 160, 120});
        items.push_back(hyperbrowse::browser::BrowserItem{L"delta.nef", L"C:\\Alpha\\delta.nef", L"NEF", L"2026-04-11 10:03", 400, 55, 1024, 768});
        model.Reset(L"C:\\Alpha", false);
        model.AppendItems(std::move(items), 4, 175);
        model.Complete();

        browserPane.SetModel(&model);
        browserPane.RefreshFromModel();

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
        Expect(browserPane.SelectedCount() == 1, "Single-click selection in thumbnail mode failed");

        SetFocus(hostWindow);
        Expect(!browserPane.HandleNavigationKey(WM_SYSKEYDOWN, VK_LEFT, 0),
               "Alt+Left-style system key was consumed as thumbnail navigation");
        Expect(!browserPane.HandleNavigationKey(WM_SYSKEYDOWN, VK_RIGHT, 0),
               "Alt+Right-style system key was consumed as thumbnail navigation");
        Expect(browserPane.HandleNavigationKey(WM_KEYDOWN, VK_RIGHT, 0),
               "Thumbnail navigation did not handle an arrow key from the main window focus path");
        Expect(GetFocus() == browserPane.Hwnd(),
               "Thumbnail navigation did not return keyboard focus to the browser pane");
        Expect(browserPane.FocusedFilePathSnapshot() == L"C:\\Alpha\\delta.nef",
               "Arrow navigation from main window focus did not advance the focused thumbnail");
        SetFocus(hostWindow);
        Expect(browserPane.HandleNavigationKey(WM_KEYDOWN, VK_RETURN, 0),
               "Enter from main window focus was not forwarded to the thumbnail browser");
        MSG openItemMessage{};
        Expect(PeekMessageW(&openItemMessage,
                            hostWindow,
                            hyperbrowse::browser::BrowserPane::kOpenItemMessage,
                            hyperbrowse::browser::BrowserPane::kOpenItemMessage,
                            PM_REMOVE) != FALSE,
               "Enter forwarded to the thumbnail browser did not request opening the focused item");
        Expect(openItemMessage.wParam == reinterpret_cast<WPARAM>(browserPane.Hwnd())
                   && openItemMessage.lParam == model.FindItemIndexByPath(L"C:\\Alpha\\delta.nef"),
               "Enter did not request opening the focused thumbnail");
         Expect(browserPane.GoToItemNumber(2) && browserPane.CurrentItemNumber() == 2,
             "Browser item-number navigation did not select the requested visible item");
         Expect(!browserPane.GoToItemNumber(5) && browserPane.CurrentItemNumber() == 2,
             "Browser item-number navigation changed selection for an out-of-range item");
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(50, 50));

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, MAKELPARAM(500, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(500, 50));
        Expect(browserPane.SelectedCount() == 3, "Shift-range selection in thumbnail mode failed");

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, MAKELPARAM(300, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(300, 50));
        Expect(browserPane.SelectedCount() == 2, "Ctrl-toggle selection in thumbnail mode failed");

        browserPane.ClearSelection();
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
        SendMessageW(browserPane.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(390, 210));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(390, 210));
        Expect(browserPane.SelectedCount() == 2, "Rubber-band selection in thumbnail mode failed");

         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels192,
             "BrowserPane did not default to the expected thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane should default to the compact thumbnail layout");
         Expect(browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane should default to showing thumbnail details");

         browserPane.SetThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset::Pixels96);
         browserPane.SetCompactThumbnailLayout(true);
         browserPane.SetThumbnailDetailsVisible(false);
         browserPane.RefreshFromModel();
         browserPane.ClearSelection();
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels96,
             "BrowserPane did not store the compact thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane did not enable compact thumbnail layout");
         Expect(!browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane did not disable thumbnail details");
         Expect(browserPane.SelectedCount() == 1,
             "Thumbnail-only compact mode did not preserve thumbnail hit-testing");

         browserPane.SetThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset::Pixels320);
         browserPane.SetCompactThumbnailLayout(false);
         browserPane.SetThumbnailDetailsVisible(true);
         browserPane.RefreshFromModel();
         browserPane.ClearSelection();
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels320,
             "BrowserPane did not store the large thumbnail size preset");
         Expect(!browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane did not disable the compact thumbnail layout");
         Expect(browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane did not restore thumbnail details visibility");
         Expect(browserPane.SelectedCount() == 1,
             "Large thumbnail mode did not preserve thumbnail hit-testing");

        browserPane.SetViewMode(hyperbrowse::browser::BrowserViewMode::Details);
        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::FileSize);

        HWND listView = FindWindowExW(browserPane.Hwnd(), nullptr, WC_LISTVIEWW, nullptr);
        Expect(listView != nullptr, "Details-mode list view was not created");
        Expect(ListView_GetItemCount(listView) == 4, "Virtual details view item count is incorrect");
         Expect(SendMessageW(listView, LVM_GETUNICODEFORMAT, 0, 0) != FALSE,
             "Details-mode list view is not running in Unicode mode");

        wchar_t buffer[256]{};
        ListView_GetItemText(listView, 0, 0, buffer, static_cast<int>(std::size(buffer)));
         Expect(std::wstring(buffer) == L"테스트-漢字.png", "Details view did not preserve the Unicode filename text for the smallest item");

        Expect(model.UpdateDecodedDimensions(0, 20, 20), "Browser model did not accept a decoded-dimensions update");
        Expect(model.UpdateDecodedDimensions(1, 10, 10), "Browser model did not accept the second decoded-dimensions update");
        Expect(model.UpdateDecodedDimensions(2, 40, 40), "Browser model did not accept the third decoded-dimensions update");
        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::Dimensions);
        browserPane.RefreshFromModel();

        ListView_GetItemText(listView, 0, 0, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"테스트-漢字.png", "Dimension sort did not preserve the Unicode filename in details mode");
        ListView_GetItemText(listView, 0, 4, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"10x10", "Dimensions column did not surface the decoded dimensions");

        std::vector<hyperbrowse::browser::BrowserItem> pairedItems;
        pairedItems.push_back(hyperbrowse::browser::BrowserItem{L"pair.nef", L"C:\\Alpha\\pair.nef", L"NEF", L"2026-04-11 10:04", 500, 90, 4032, 3024});
        pairedItems.push_back(hyperbrowse::browser::BrowserItem{L"pair.jpg", L"C:\\Alpha\\pair.jpg", L"JPG", L"2026-04-11 10:05", 600, 20, 4032, 3024});
        pairedItems.push_back(hyperbrowse::browser::BrowserItem{L"solo.png", L"C:\\Alpha\\solo.png", L"PNG", L"2026-04-11 10:06", 700, 15, 640, 480});
        model.Reset(L"C:\\Alpha", false);
        model.AppendItems(std::move(pairedItems), 3, 205);
        model.Complete();

        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::FileName);
        browserPane.SetSortAscending(true);
        browserPane.SetRawJpegDisplayPreference(hyperbrowse::browser::RawJpegDisplayPreference::Jpeg);
        browserPane.SetRawJpegStackingEnabled(true);
        browserPane.RefreshFromModel();

        Expect(ListView_GetItemCount(listView) == 2, "Paired RAW+JPEG stacking did not collapse to a single details entry");
        browserPane.RestoreSelectionByFilePaths({L"C:\\Alpha\\pair.nef"}, L"C:\\Alpha\\pair.nef");
        Expect(browserPane.SelectedCount() == 1, "Stacked RAW+JPEG selection did not remap to the visible representative");
        const auto stackedSelectionPaths = browserPane.SelectedFilePathsSnapshot();
        Expect(stackedSelectionPaths.size() == 1
            && stackedSelectionPaths.front() == L"C:\\Alpha\\pair.jpg",
            "Stacked RAW+JPEG selection did not expose the JPEG browser representative");
        ListView_GetItemText(listView, 0, 1, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"JPG+NEF", "Stacked RAW+JPEG type label did not reveal the hidden companion");

        browserPane.SetRawJpegStackingEnabled(false);
        browserPane.RefreshFromModel();
        Expect(ListView_GetItemCount(listView) == 3, "Disabling RAW+JPEG stacking did not restore both browser items");

        DestroyWindow(hostWindow);
    }
namespace
{

    void RunViewerWindowFitModeChecks(hyperbrowse::viewer::ViewerWindow& viewer,
                                      int currentImageWidth,
                                      int currentImageHeight)
    {
        Expect(viewer.CurrentIndex() == 1, "Viewer fit-mode scenario did not start on the middle image");
        while (viewer.RotationQuarterTurns() != 0)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'L', 0);
            PumpMessagesFor(100);
        }

        for (int index = 0; index < 5; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        }
        PumpMessagesFor(300);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, 120));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 150));
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(180, 150));
        PumpMessagesFor(100);
        Expect(viewer.PanOffset().x != 0 || viewer.PanOffset().y != 0,
            "Viewer fullscreen fit test did not establish a non-zero pan offset");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen()
                && viewer.PanOffset().x == 0
                && viewer.PanOffset().y == 0,
            "Viewer exiting fullscreen did not fit the image to the resized window");

        for (int index = 0; index < 5; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        }
        PumpMessagesFor(300);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, 120));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 150));
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(180, 150));
        PumpMessagesFor(100);

        const POINT panBeforeHeightFit = viewer.PanOffset();
        Expect(panBeforeHeightFit.x != 0 || panBeforeHeightFit.y != 0,
            "Viewer fit-height test did not establish a non-zero pan offset");
        RECT heightFitBeforeRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &heightFitBeforeRect) != FALSE,
            "Failed to read the viewer bounds before windowed FitHeight");
        const DWORD heightFitBeforeStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE));

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        RECT heightFitAfterRect{};
        RECT heightFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &heightFitAfterRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &heightFitClientRect) != FALSE,
            "Failed to read the viewer bounds after windowed FitHeight");
        Expect(heightFitAfterRect.left == heightFitBeforeRect.left
                && heightFitAfterRect.top == heightFitBeforeRect.top
                && heightFitAfterRect.right == heightFitBeforeRect.right
                && heightFitAfterRect.bottom == heightFitBeforeRect.bottom,
            "Viewer FitHeight changed the windowed window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == heightFitBeforeStyle,
            "Viewer FitHeight changed the windowed window style");
        const int expectedHeightFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(heightFitClientRect.bottom) / currentImageHeight * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedHeightFitZoom,
            "Viewer FitHeight did not fit the image to the existing client height");
        Expect(viewer.PanOffset().x == 0 && viewer.PanOffset().y == 0,
            "Viewer FitHeight did not reset the pan offset");
        const LONG movedHeightFitLeft = heightFitAfterRect.left > 100
            ? heightFitAfterRect.left - 100
            : heightFitAfterRect.left + 100;
        Expect(SetWindowPos(viewer.Hwnd(), nullptr,
                            movedHeightFitLeft,
                            heightFitAfterRect.top,
                            heightFitAfterRect.right - heightFitAfterRect.left,
                            heightFitAfterRect.bottom - heightFitAfterRect.top,
                            SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
            "Failed to move the window after FitHeight");
        RECT movedHeightFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &movedHeightFitRect) != FALSE
                && movedHeightFitRect.left == movedHeightFitLeft,
            "Viewer FitHeight test did not establish a moved window position");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_PRIOR, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation failed while in FitHeight");
        PumpMessagesFor(100);
        RECT previousHeightFitRect{};
        RECT previousHeightFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &previousHeightFitRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &previousHeightFitClientRect) != FALSE,
            "Failed to read the viewer bounds after FitHeight navigation");
        Expect(previousHeightFitRect.left == movedHeightFitRect.left
                && previousHeightFitRect.top == movedHeightFitRect.top,
            "Viewer FitHeight navigation changed the window position");
        const int expectedPreviousHeightFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(previousHeightFitClientRect.bottom) / 48.0 * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedPreviousHeightFitZoom,
            "Viewer FitHeight navigation did not recompute the image height scale");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_NEXT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation failed while returning from FitHeight");
        RECT restoredHeightFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &restoredHeightFitRect) != FALSE
                && restoredHeightFitRect.left == movedHeightFitRect.left
                && restoredHeightFitRect.top == movedHeightFitRect.top,
            "Viewer FitHeight navigation did not preserve the moved window position");

        RECT widthFitBeforeRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &widthFitBeforeRect) != FALSE,
            "Failed to read the viewer bounds before FitWidth");
        const DWORD widthFitBeforeStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE));
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT widthFitAfterRect{};
        RECT widthFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &widthFitAfterRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &widthFitClientRect) != FALSE,
            "Failed to read the viewer bounds after FitWidth");
        Expect(widthFitAfterRect.left == widthFitBeforeRect.left
                && widthFitAfterRect.top == widthFitBeforeRect.top
                && widthFitAfterRect.right == widthFitBeforeRect.right
                && widthFitAfterRect.bottom == widthFitBeforeRect.bottom,
            "Viewer FitWidth changed the window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == widthFitBeforeStyle,
            "Viewer FitWidth changed the window style");
        const int expectedWidthFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(widthFitClientRect.right) / currentImageWidth * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedWidthFitZoom,
            "Viewer FitWidth did not fit the image to the existing client width");
        Expect(viewer.PanOffset().x == 0 && viewer.PanOffset().y == 0,
            "Viewer FitWidth did not reset the pan offset");
        const LONG movedWidthFitLeft = widthFitAfterRect.left + 100;
        Expect(SetWindowPos(viewer.Hwnd(), nullptr,
                            movedWidthFitLeft,
                            widthFitAfterRect.top,
                            widthFitAfterRect.right - widthFitAfterRect.left,
                            widthFitAfterRect.bottom - widthFitAfterRect.top,
                            SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
            "Failed to move the window after FitWidth");
        RECT movedWidthFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &movedWidthFitRect) != FALSE
                && movedWidthFitRect.left == movedWidthFitLeft,
            "Viewer FitWidth test did not establish a moved window position");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation failed while in FitWidth");
        RECT nextWidthFitRect{};
        RECT nextWidthFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &nextWidthFitRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &nextWidthFitClientRect) != FALSE,
            "Failed to read the viewer bounds after FitWidth navigation");
        Expect(nextWidthFitRect.left == movedWidthFitRect.left
                && nextWidthFitRect.top == movedWidthFitRect.top,
            "Viewer FitWidth navigation changed the window position");
        const int expectedNextWidthFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(nextWidthFitClientRect.right) / 40.0 * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedNextWidthFitZoom,
            "Viewer FitWidth navigation did not recompute the image width scale");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer portrait-image navigation failed while in FitWidth");
        RECT constrainedWidthFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &constrainedWidthFitRect) != FALSE,
            "Failed to read the viewer bounds after the portrait FitWidth navigation");
        Expect(constrainedWidthFitRect.left == movedWidthFitRect.left
                && constrainedWidthFitRect.top == movedWidthFitRect.top,
            "Viewer FitWidth navigation changed the window position for an oversized image height");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation failed after the constrained FitWidth check");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation failed while returning from FitWidth");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen(), "Viewer double-click did not re-enter full screen");
        RECT fullScreenHeightFitBeforeRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &fullScreenHeightFitBeforeRect) != FALSE,
            "Failed to read the fullscreen bounds before FitHeight");
        const DWORD fullScreenHeightFitBeforeStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE));
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        RECT fullScreenHeightFitAfterRect{};
        RECT fullScreenHeightFitClientRect{};
        Expect(viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &fullScreenHeightFitAfterRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &fullScreenHeightFitClientRect) != FALSE,
            "Viewer FitHeight shortcut did not preserve F11 fullscreen");
        Expect(fullScreenHeightFitAfterRect.left == fullScreenHeightFitBeforeRect.left
                && fullScreenHeightFitAfterRect.top == fullScreenHeightFitBeforeRect.top
                && fullScreenHeightFitAfterRect.right == fullScreenHeightFitBeforeRect.right
                && fullScreenHeightFitAfterRect.bottom == fullScreenHeightFitBeforeRect.bottom,
            "Viewer FitHeight changed fullscreen window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == fullScreenHeightFitBeforeStyle,
            "Viewer FitHeight changed fullscreen window style");
        const int expectedFullScreenHeightFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(fullScreenHeightFitClientRect.bottom) / currentImageHeight * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedFullScreenHeightFitZoom,
            "Viewer fullscreen FitHeight did not fit the image to the client height");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        PumpMessagesFor(300);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen(), "Viewer FitHeight shortcut exited fullscreen after a custom zoom");
        RECT fullScreenCustomHeightFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &fullScreenCustomHeightFitRect) != FALSE
                && fullScreenCustomHeightFitRect.left == fullScreenHeightFitBeforeRect.left
                && fullScreenCustomHeightFitRect.top == fullScreenHeightFitBeforeRect.top
                && fullScreenCustomHeightFitRect.right == fullScreenHeightFitBeforeRect.right
                && fullScreenCustomHeightFitRect.bottom == fullScreenHeightFitBeforeRect.bottom,
            "Viewer custom-zoom FitHeight changed fullscreen window geometry");

        Expect(viewer.IsFullScreen(), "Viewer fit-height fullscreen state was not preserved before testing FitWidth");
        const int fullScreenWidthFitImageWidth = currentImageWidth;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT fullScreenWidthFitRect{};
        RECT fullScreenWidthFitClientRect{};
        Expect(viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &fullScreenWidthFitRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &fullScreenWidthFitClientRect) != FALSE,
            "Viewer FitWidth shortcut did not preserve fullscreen");
        Expect(fullScreenWidthFitRect.left == fullScreenHeightFitBeforeRect.left
                && fullScreenWidthFitRect.top == fullScreenHeightFitBeforeRect.top
                && fullScreenWidthFitRect.right == fullScreenHeightFitBeforeRect.right
                && fullScreenWidthFitRect.bottom == fullScreenHeightFitBeforeRect.bottom,
            "Viewer FitWidth changed fullscreen window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == fullScreenHeightFitBeforeStyle,
            "Viewer FitWidth changed fullscreen window style");
        const int expectedFullScreenWidthFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(fullScreenWidthFitClientRect.right) / fullScreenWidthFitImageWidth * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedFullScreenWidthFitZoom,
            "Viewer fullscreen FitWidth did not fit the image to the client width");

        BYTE originalKeyboardState[256]{};
        BYTE modifiedKeyboardState[256]{};
        Expect(GetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing window geometry shortcuts");
        std::copy(std::begin(originalKeyboardState), std::end(originalKeyboardState), std::begin(modifiedKeyboardState));
        modifiedKeyboardState[VK_CONTROL] |= 0x80;
        modifiedKeyboardState[VK_SHIFT] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Ctrl+Shift state before testing window geometry shortcuts");
        const RECT fullScreenGeometryBefore = fullScreenWidthFitRect;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT fullScreenGeometryAfter{};
        Expect(viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &fullScreenGeometryAfter) != FALSE
                && fullScreenGeometryAfter.left == fullScreenGeometryBefore.left
                && fullScreenGeometryAfter.top == fullScreenGeometryBefore.top
                && fullScreenGeometryAfter.right == fullScreenGeometryBefore.right
                && fullScreenGeometryAfter.bottom == fullScreenGeometryBefore.bottom,
            "Ctrl+Shift+H/W changed fullscreen window geometry");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing fullscreen geometry shortcuts");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::ActualSize);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen(), "Failed to leave fullscreen before testing window geometry shortcuts");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, '0', 0);
        PumpMessagesFor(100);
        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromWindow(viewer.Hwnd(), MONITOR_DEFAULTTONEAREST);
        Expect(monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo) != FALSE,
            "Failed to read the monitor work area before testing window geometry shortcuts");

        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Ctrl+Shift state for window geometry shortcuts");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        RECT workAreaHeightRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &workAreaHeightRect) != FALSE,
            "Failed to read the window bounds after Ctrl+Shift+H");
        Expect(workAreaHeightRect.top == monitorInfo.rcWork.top
                && workAreaHeightRect.bottom == monitorInfo.rcWork.bottom,
            "Ctrl+Shift+H did not size the window to the monitor work-area height");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT workAreaWidthRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &workAreaWidthRect) != FALSE,
            "Failed to read the window bounds after Ctrl+Shift+W");
        Expect(workAreaWidthRect.left == monitorInfo.rcWork.left
                && workAreaWidthRect.right == monitorInfo.rcWork.right,
            "Ctrl+Shift+W did not size the window to the monitor work-area width");
        RECT fitWidthClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &fitWidthClientRect) != FALSE,
            "Failed to read the client area after Ctrl+Shift+W");
        const DWORD fitWidthStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE))
            & ~(WS_MAXIMIZE | WS_MINIMIZE);
        const DWORD fitWidthExStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_EXSTYLE));
        RECT fitWidthFrameRect{0, 0, 0, 0};
        Expect(AdjustWindowRectEx(&fitWidthFrameRect, fitWidthStyle, FALSE, fitWidthExStyle) != FALSE,
            "Failed to calculate the viewer frame for Ctrl+Shift+W");
        const LONG fitWidthFrameHeight = fitWidthFrameRect.bottom - fitWidthFrameRect.top;
        const LONG workAreaHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        const LONG availableFitWidthClientHeight = std::max<LONG>(1, workAreaHeight - fitWidthFrameHeight);
        const auto expectedFitWidthClientHeight = [&](int imageWidth, int imageHeight) {
            return std::min(
                availableFitWidthClientHeight,
                std::max<LONG>(1, static_cast<LONG>(std::lround(
                    static_cast<double>(fitWidthClientRect.right) * imageHeight
                    / std::max(1, imageWidth)))));
        };
        Expect(fitWidthClientRect.bottom == expectedFitWidthClientHeight(currentImageWidth, currentImageHeight),
            "Ctrl+Shift+W did not preserve the image aspect within the monitor work area");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_NEXT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer window FitWidth navigation failed");
        RECT nextFitWidthRect{};
        RECT nextFitWidthClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &nextFitWidthRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &nextFitWidthClientRect) != FALSE,
            "Failed to read the viewer bounds after window FitWidth navigation");
        Expect(nextFitWidthRect.left == monitorInfo.rcWork.left
                && nextFitWidthRect.right == monitorInfo.rcWork.right
                && nextFitWidthClientRect.bottom == std::min(
                    availableFitWidthClientHeight,
                    std::max<LONG>(1, static_cast<LONG>(std::lround(
                        static_cast<double>(nextFitWidthClientRect.right) * 16.0 / 40.0)))),
            "Viewer window FitWidth navigation did not apply the monitor-bounded image aspect");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_PRIOR, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer window FitWidth navigation did not restore the source image");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing window geometry shortcuts");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::FitWidth);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        RECT escapeWidthRect{};
        Expect(!viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &escapeWidthRect) != FALSE
                && escapeWidthRect.left == monitorInfo.rcWork.left
                && escapeWidthRect.right == monitorInfo.rcWork.right,
            "Fullscreen Escape Fit Width did not use the window work-area width action");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::FitHeight);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        RECT escapeHeightRect{};
        Expect(!viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &escapeHeightRect) != FALSE
                && escapeHeightRect.top == monitorInfo.rcWork.top
                && escapeHeightRect.bottom == monitorInfo.rcWork.bottom,
            "Fullscreen Escape Fit Height did not use the window work-area height action");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::ActualSize);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen() && viewer.CurrentZoomPercent() == 100,
            "Fullscreen Escape Actual Size did not use native image scale when it fit");

    }
}


    void RunViewerWindowFitModeScenario(HINSTANCE instance, HWND ownerWindow)
    {
        TempFolder root(L"HyperBrowsePrompt6ViewerFit");
        const fs::path firstPath = root.Root() / L"first.jpg";
        const fs::path secondPath = root.Root() / L"second.png";
        const fs::path thirdPath = root.Root() / L"third.png";
        const fs::path tallPath = root.Root() / L"tall.png";
        WriteTestImage(firstPath, TestImageFormat::Jpeg, 48, 24, 6);
        WriteTestImage(secondPath, TestImageFormat::Png, 64, 32);
        WriteTestImage(thirdPath, TestImageFormat::Png, 40, 16);
        WriteTestImage(tallPath, TestImageFormat::Png, 24, 48);

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"first.jpg", firstPath.wstring(), L"JPG", L"2026-04-11 12:00", 1, 10, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"second.png", secondPath.wstring(), L"PNG", L"2026-04-11 12:01", 2, 20, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"third.png", thirdPath.wstring(), L"PNG", L"2026-04-11 12:02", 3, 30, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"tall.png", tallPath.wstring(), L"PNG", L"2026-04-11 12:03", 4, 40, 256, 256});

        hyperbrowse::viewer::ViewerWindow viewer(instance);
        Expect(viewer.Open(ownerWindow, items, 1, false), "Viewer fit-mode window failed to open");
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer fit-mode window did not finish the initial image decode");
        Expect(viewer.IsFullScreen(), "Viewer fit-mode window should open in full screen by default");
        RunViewerWindowFitModeChecks(viewer, 64, 32);

        Expect(viewer.CurrentIndex() == 1, "Viewer wraparound scenario did not start from the middle image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation failed before the wraparound boundary");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation did not reach the last image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation did not wrap from the last image to the first");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation did not wrap from the first image to the last");

        BYTE originalKeyboardState[256]{};
        BYTE controlKeyboardState[256]{};
        Expect(GetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing viewer boundary shortcuts");
        for (int index = 0; index < static_cast<int>(std::size(originalKeyboardState)); ++index)
        {
            controlKeyboardState[index] = originalKeyboardState[index];
        }
        controlKeyboardState[VK_CONTROL] |= 0x80;
        Expect(SetKeyboardState(controlKeyboardState) != FALSE,
            "Failed to stage Ctrl state for the viewer boundary shortcut test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_HOME, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Ctrl+Home did not navigate to the first image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_END, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Ctrl+End did not navigate to the last image");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing viewer boundary shortcuts");

        SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);

        const fs::path oversizedPath = root.Root() / L"oversized.png";
        MONITORINFO oversizedMonitorInfo{sizeof(MONITORINFO)};
        const HMONITOR oversizedMonitor = MonitorFromWindow(ownerWindow, MONITOR_DEFAULTTONEAREST);
        Expect(oversizedMonitor != nullptr && GetMonitorInfoW(oversizedMonitor, &oversizedMonitorInfo) != FALSE,
            "Failed to read the monitor work area for oversized-image Escape coverage");
        const LONG workWidth = oversizedMonitorInfo.rcWork.right - oversizedMonitorInfo.rcWork.left;
        const LONG workHeight = oversizedMonitorInfo.rcWork.bottom - oversizedMonitorInfo.rcWork.top;
        const LONG oversizedWidth = workWidth + std::max<LONG>(32, workWidth / 10);
        const LONG oversizedHeight = workHeight + std::max<LONG>(32, workHeight / 10);
        WriteTestImage(oversizedPath, TestImageFormat::Png,
                       static_cast<int>(oversizedWidth), static_cast<int>(oversizedHeight));
        std::vector<hyperbrowse::browser::BrowserItem> oversizedItems;
        oversizedItems.push_back(hyperbrowse::browser::BrowserItem{
            L"oversized.png", oversizedPath.wstring(), L"PNG", L"2026-04-11 12:04", 5, 50, 256, 256});
        hyperbrowse::viewer::ViewerWindow oversizedViewer(instance);
        oversizedViewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::ActualSize);
        Expect(oversizedViewer.Open(ownerWindow, oversizedItems, 0, false),
            "Oversized-image viewer failed to open for fullscreen Escape fallback coverage");
        Expect(PumpMessagesUntil([&]() { return oversizedViewer.CurrentZoomPercent() > 0; }, 5000),
            "Oversized-image viewer did not finish its initial image decode");
        SendMessageW(oversizedViewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(!oversizedViewer.IsFullScreen(),
            "Fullscreen Escape Actual Size did not leave fullscreen for an oversized image");
        Expect(oversizedViewer.CurrentZoomPercent() < 100,
            "Fullscreen Escape Actual Size showed an oversized image at clipped native scale");
        SendMessageW(oversizedViewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);
    }

    void RunViewerWindowScenario(HINSTANCE instance, HWND ownerWindow)
    {
        ScopedRegistryDwordBackup overlaySettingBackup(kRegistryPath, kRegistryValueViewerInfoOverlaysVisible);
        ScopedRegistryDwordBackup overlayTextSizeBackup(kRegistryPath, kRegistryValueViewerInfoOverlayTextSize);
        ScopedRegistryDwordBackup windowedFullMetadataBackup(kRegistryPath, kRegistryValueViewerWindowedFullMetadataVisible);
        ScopedRegistryDwordBackup fullScreenFullMetadataBackup(kRegistryPath, kRegistryValueViewerFullScreenFullMetadataVisible);
        ScopedRegistryDwordBackup fullMetadataBackup(kRegistryPath, kRegistryValueViewerFullMetadataVisible);
        auto* state = reinterpret_cast<TestWindowState*>(GetWindowLongPtrW(ownerWindow, GWLP_USERDATA));
        Expect(state != nullptr, "Failed to locate the hidden test window state");

        {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_WRITE, &key) == ERROR_SUCCESS)
            {
                RegDeleteValueW(key, kRegistryValueViewerInfoOverlaysVisible);
                RegDeleteValueW(key, kRegistryValueViewerInfoOverlayTextSize);
                RegDeleteValueW(key, kRegistryValueViewerWindowedFullMetadataVisible);
                RegDeleteValueW(key, kRegistryValueViewerFullScreenFullMetadataVisible);
                RegDeleteValueW(key, kRegistryValueViewerFullMetadataVisible);
                RegCloseKey(key);
            }
        }

        TempFolder root(L"HyperBrowsePrompt6Viewer");
        const fs::path firstPath = root.Root() / L"first.jpg";
        const fs::path secondPath = root.Root() / L"second.png";
        const fs::path thirdPath = root.Root() / L"third.png";
        const fs::path tallPath = root.Root() / L"tall.png";
        WriteTestImage(firstPath, TestImageFormat::Jpeg, 48, 24, 6);
        WriteTestImage(secondPath, TestImageFormat::Png, 640, 320);
        WriteTestImage(thirdPath, TestImageFormat::Png, 40, 16);
        WriteTestImage(tallPath, TestImageFormat::Png, 24, 48);

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"first.jpg", firstPath.wstring(), L"JPG", L"2026-04-11 12:00", 1, 10, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"second.png", secondPath.wstring(), L"PNG", L"2026-04-11 12:01", 2, 20, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"third.png", thirdPath.wstring(), L"PNG", L"2026-04-11 12:02", 3, 30, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"tall.png", tallPath.wstring(), L"PNG", L"2026-04-11 12:03", 4, 40, 256, 256});

        hyperbrowse::viewer::ViewerWindow viewer(instance);
        Expect(viewer.Open(ownerWindow, items, 0, false), "Viewer window failed to open");
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 0; }, 5000),
               "Viewer window did not finish the initial image decode");
        Expect(viewer.IsFullScreen(), "Viewer should open in full screen by default");
        Expect(viewer.AreInfoOverlaysVisible(), "Viewer should default to showing info overlays when no persisted preference exists");
        Expect(viewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Small,
            "Viewer should default to the small overlay text size when no persisted preference exists");
        Expect(!viewer.IsFullMetadataVisible(), "Viewer should default to hiding the full metadata pane when no persisted preference exists");

         Expect(!viewer.BeginCompareSession({0}, {}), "Compare session accepted fewer than two tiles");
         Expect(!viewer.BeginCompareSession({0, 0}, {}), "Compare session accepted duplicate tile identities");
         Expect(!viewer.BeginCompareSession({0, 1, 2, 3, 4}, {}), "Compare session accepted more than four tiles");
         Expect(viewer.BeginCompareSession({0, 1}, {}), "Viewer did not start a two-tile compare session");
         RECT compareClientRect{};
         GetClientRect(viewer.Hwnd(), &compareClientRect);
         SendMessageW(viewer.Hwnd(),
                      WM_MOUSEMOVE,
                      0,
                      MAKELPARAM(compareClientRect.left + 1,
                                 (compareClientRect.top + compareClientRect.bottom) / 2));
         Expect(GetCursor() == LoadCursorW(nullptr, IDC_ARROW),
             "Compare session showed the previous-image navigation cursor at the left edge");
         SendMessageW(viewer.Hwnd(),
                      WM_MOUSEMOVE,
                      0,
                      MAKELPARAM(compareClientRect.right - 1,
                                 (compareClientRect.top + compareClientRect.bottom) / 2));
         Expect(GetCursor() == LoadCursorW(nullptr, IDC_ARROW),
             "Compare session showed the next-image navigation cursor at the right edge");
         Expect(viewer.CompareTileCount() == 2 && viewer.FocusedCompareTile() == 0
                 && viewer.CompareTileItemIndex(0) == 0 && viewer.CompareTileItemIndex(1) == 1,
             "Two-tile compare did not preserve selection order and primary focus");
         Expect(viewer.SetCompareViewsSynchronized(false) && !viewer.AreCompareViewsSynchronized(),
             "Compare sync could not be disabled");
         Expect(viewer.SetCompareViewsSynchronized(true) && viewer.AreCompareViewsSynchronized(),
             "Compare sync could not be re-enabled");

         BYTE compareKeyboardState[256]{};
         BYTE compareModifiedKeyboardState[256]{};
         Expect(GetKeyboardState(compareKeyboardState) != FALSE,
             "Failed to read keyboard state for compare tile focus coverage");
         std::copy(std::begin(compareKeyboardState), std::end(compareKeyboardState),
                std::begin(compareModifiedKeyboardState));
         compareModifiedKeyboardState[VK_CONTROL] = 0;
         compareModifiedKeyboardState[VK_SHIFT] = 0;
         compareModifiedKeyboardState[VK_MENU] = 0;
         Expect(SetKeyboardState(compareModifiedKeyboardState) != FALSE,
             "Failed to clear modifiers for compare rating shortcut coverage");
         const int compareZoomPercent = viewer.CurrentZoomPercent();
         for (int rating = 0; rating <= 5; ++rating)
         {
             state->viewerCompareRatingRequests = 0;
             SendMessageW(viewer.Hwnd(), WM_KEYDOWN, static_cast<WPARAM>('0' + rating), 0);
             Expect(PumpMessagesUntil([&]() { return state->viewerCompareRatingRequests == 1; }, 2000),
                 "A compare-mode number key did not dispatch its rating command");
             Expect(state->lastViewerCompareRating == rating
                        && state->lastViewerCompareRatingSource == viewer.Hwnd(),
                 "A compare-mode number key dispatched the wrong focused-image rating");
         }
         Expect(viewer.CurrentZoomPercent() == compareZoomPercent,
             "Compare-mode rating keys invoked the viewer's fit or actual-size shortcuts");
         state->viewerCompareRatingRequests = 0;
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_NUMPAD5, 0);
         Expect(PumpMessagesUntil([&]() { return state->viewerCompareRatingRequests == 1; }, 2000)
                    && state->lastViewerCompareRating == 5
                    && state->lastViewerCompareRatingSource == viewer.Hwnd(),
             "A compare-mode numpad rating key did not dispatch its focused-image rating");
         Expect(SetKeyboardState(compareKeyboardState) != FALSE,
             "Failed to restore keyboard state after compare rating shortcut coverage");
         std::copy(std::begin(compareKeyboardState), std::end(compareKeyboardState),
                std::begin(compareModifiedKeyboardState));
         compareModifiedKeyboardState[VK_CONTROL] |= 0x80;
         Expect(SetKeyboardState(compareModifiedKeyboardState) != FALSE,
             "Failed to stage Ctrl for compare tile focus coverage");
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
         SetKeyboardState(compareKeyboardState);
         Expect(viewer.FocusedCompareTile() == 1,
             "Ctrl+Tab did not focus the next compare tile");

         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PERIOD, 0);
         Expect(PumpMessagesUntil([&]() { return viewer.CompareTileItemIndex(1) == 2; }, 5000),
             "Compare candidate cycling did not replace only the focused tile");

         Expect(GetKeyboardState(compareKeyboardState) != FALSE,
             "Failed to read keyboard state for adjacent compare coverage");
         std::copy(std::begin(compareKeyboardState), std::end(compareKeyboardState),
                std::begin(compareModifiedKeyboardState));
         compareModifiedKeyboardState[VK_SHIFT] |= 0x80;
         Expect(SetKeyboardState(compareModifiedKeyboardState) != FALSE,
             "Failed to stage Shift for adjacent compare coverage");
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
         SetKeyboardState(compareKeyboardState);
         Expect(PumpMessagesUntil([&]() { return viewer.CompareTileItemIndex(0) == 1; }, 5000),
             "Shift+Left did not preserve two-up adjacent comparison behavior");

         Expect(viewer.BeginCompareSession({0, 2, 3}, {}), "Viewer did not start a three-tile compare session");
         Expect(viewer.CompareTileCount() == 3 && viewer.FocusedCompareTile() == 0,
             "Three-tile compare did not retain the primary-selected focus");
         Expect(GetKeyboardState(compareKeyboardState) != FALSE,
             "Failed to read keyboard state for n-up candidate coverage");
         std::copy(std::begin(compareKeyboardState), std::end(compareKeyboardState),
                std::begin(compareModifiedKeyboardState));
         compareModifiedKeyboardState[VK_SHIFT] |= 0x80;
         Expect(SetKeyboardState(compareModifiedKeyboardState) != FALSE,
             "Failed to stage Shift for n-up candidate coverage");
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
         SetKeyboardState(compareKeyboardState);
         Expect(PumpMessagesUntil([&]() { return viewer.CompareTileItemIndex(0) == 1; }, 5000),
             "Shift+Right did not cycle the focused tile in n-up compare");

         Expect(viewer.BeginCompareSession({0, 1, 2, 3}, {}), "Viewer did not start a four-tile compare session");
         Expect(viewer.CompareTileCount() == 4 && viewer.FocusedCompareTile() == 0,
             "Four-tile compare did not retain the primary-selected focus");
         Expect(GetKeyboardState(compareKeyboardState) != FALSE,
             "Failed to read keyboard state for reverse tile focus coverage");
         std::copy(std::begin(compareKeyboardState), std::end(compareKeyboardState),
                std::begin(compareModifiedKeyboardState));
         compareModifiedKeyboardState[VK_CONTROL] |= 0x80;
         compareModifiedKeyboardState[VK_SHIFT] |= 0x80;
         Expect(SetKeyboardState(compareModifiedKeyboardState) != FALSE,
             "Failed to stage Ctrl+Shift for reverse tile focus coverage");
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
         SetKeyboardState(compareKeyboardState);
         Expect(viewer.FocusedCompareTile() == 3,
             "Ctrl+Shift+Tab did not focus the previous compare tile");
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'C', 0);
         Expect(!viewer.HasCompareSession() && viewer.CurrentIndex() == 3,
             "C did not promote the focused tile when exiting n-up compare");
         Expect(viewer.ReplaceItems(items, 0), "Viewer could not reset after compare-session smoke coverage");
         Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
             "Viewer did not reload after compare-session smoke coverage");

        viewer.StartSlideshow(1000);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_SPACE, 0);
        Expect(!viewer.IsSlideshowActive(), "Viewer Space key did not pause the slideshow");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_SPACE, 0);
        Expect(viewer.IsSlideshowActive() && viewer.SlideshowIntervalMs() == 1000,
            "Viewer Space key did not resume the slideshow with its configured interval");
        const int slideshowStartIndex = viewer.CurrentIndex();
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() != slideshowStartIndex; }, 2500),
            "Viewer Space key resumed the state but did not restart slideshow playback");
        viewer.StopSlideshow();
        Expect(viewer.ReplaceItems(items, 0), "Viewer could not reset after slideshow pause/resume coverage");
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer did not reload its initial item after slideshow pause/resume coverage");

        RECT originalViewerRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &originalViewerRect) != FALSE,
            "Failed to save the viewer bounds for the zoom transition test");
        SetWindowPos(viewer.Hwnd(), nullptr, 0, 0, 32, 32, SWP_NOZORDER | SWP_NOACTIVATE);
        PumpMessagesFor(50);
        const int fitZoomPercent = viewer.CurrentZoomPercent();
        Expect(fitZoomPercent < 100, "Viewer zoom regression setup did not produce a sub-100% fit scale");
        RECT viewerClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &viewerClientRect) != FALSE,
            "Failed to read the viewer client area for the zoom transition test");
        const double fitScale = std::min(
            static_cast<double>(viewerClientRect.right) / 24.0,
            static_cast<double>(viewerClientRect.bottom) / 48.0);
        const int firstZoomTargetPercent = std::max(
            1,
            static_cast<int>(std::lround(fitScale * 1.25 * 100.0)) + 2);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        PumpMessagesFor(50);
        Expect(viewer.CurrentZoomPercent() <= firstZoomTargetPercent,
            "Viewer zoom stepped past the target while leaving fit mode");
        for (int index = 0; index < 4; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        }
        PumpMessagesFor(250);
        const LONG initialHorizontalPan = viewer.PanOffset().x;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        const POINT pannedRight = viewer.PanOffset();
        Expect(viewer.CurrentIndex() == 0 && pannedRight.x < initialHorizontalPan,
            "Viewer right arrow did not pan the viewport to the right by default");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(viewer.CurrentIndex() == 0 && viewer.PanOffset().x == initialHorizontalPan,
            "Viewer left arrow did not pan the horizontally zoomed image back");
        viewer.SetKeyboardPanningInverted(true);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(viewer.IsKeyboardPanningInverted() && viewer.PanOffset().x > initialHorizontalPan,
            "Viewer inverted keyboard panning option did not restore the legacy right-arrow direction");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(viewer.PanOffset().x == initialHorizontalPan,
            "Viewer inverted keyboard panning option did not restore the legacy left-arrow direction");
        viewer.SetKeyboardPanningInverted(false);
        const int zoomBeforeGamepadMotion = viewer.CurrentZoomPercent();
        viewer.ApplyGamepadMotion(1.1, 0.0, 0.0);
        PumpMessagesFor(250);
        Expect(viewer.CurrentZoomPercent() > zoomBeforeGamepadMotion,
            "Gamepad zoom did not increase the viewer zoom");
        viewer.ApplyGamepadMotion(1.0, 10.0, 0.0);
        Expect(viewer.PanOffset().x > initialHorizontalPan,
            "Gamepad pan did not move the zoomed viewer image");
        SetWindowPos(viewer.Hwnd(), nullptr,
                     originalViewerRect.left,
                     originalViewerRect.top,
                     originalViewerRect.right - originalViewerRect.left,
                     originalViewerRect.bottom - originalViewerRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        PumpMessagesFor(50);

        viewer.SetOverlayTextSize(hyperbrowse::viewer::InfoOverlayTextSize::Large);
        Expect(viewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Large,
            "Viewer did not apply the requested overlay text size");
        viewer.SetFullMetadataVisible(true);
        Expect(viewer.IsFullMetadataVisible(), "Viewer did not apply the requested full metadata visibility");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen() && !viewer.IsFullMetadataVisible(),
            "Viewer did not switch to the independent windowed full metadata preference");
        viewer.SetFullMetadataVisible(true);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen() && viewer.IsFullMetadataVisible(),
            "Viewer did not restore the full-screen full metadata preference");
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen() && viewer.IsFullMetadataVisible(),
            "Viewer did not restore the windowed full metadata preference");
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen() && viewer.IsFullMetadataVisible(),
            "Viewer did not restore the full-screen metadata state after returning from windowed mode");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
               "Viewer next-image navigation failed");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer up arrow did not navigate to the previous image when fit to window");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer down arrow did not navigate to the next image when fit to window");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_PRIOR, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Page Up did not navigate to the previous image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_NEXT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Page Down did not navigate to the next image");
        PumpMessagesFor(100);
        RECT metadataClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &metadataClientRect) != FALSE,
            "Failed to read the viewer client area for the metadata pane test");
        const POINT metadataSamplePoint{
            metadataClientRect.right - 32,
            metadataClientRect.top + 32};
        COLORREF metadataPixelWithOverlays{};
        Expect(ReadClientPixel(viewer.Hwnd(), metadataSamplePoint, &metadataPixelWithOverlays),
            "Failed to sample the visible metadata pane");

        viewer.SetInfoOverlaysVisible(true);
        viewer.SetFullMetadataVisible(false);
        PumpMessagesFor(100);
        Expect(viewer.AreInfoOverlaysVisible() && !viewer.IsFullMetadataVisible(),
            "Viewer did not apply the overlays-on and full-metadata-off state");
        COLORREF metadataPixelWithoutFullMetadata{};
        Expect(ReadClientPixel(viewer.Hwnd(), metadataSamplePoint, &metadataPixelWithoutFullMetadata),
            "Failed to sample the viewer with full metadata hidden");
        Expect(metadataPixelWithoutFullMetadata != metadataPixelWithOverlays,
            "Viewer full metadata pane remained visible when full metadata was disabled");

        viewer.SetFullMetadataVisible(true);
        PumpMessagesFor(100);
        Expect(viewer.IsFullMetadataVisible(),
            "Viewer did not re-enable the full metadata pane");

        state->viewerStartFolderSlideshowRequests = 0;
        state->lastViewerStartFolderSlideshowSource = nullptr;
        BYTE originalKeyboardState[256]{};
        BYTE modifiedKeyboardState[256]{};
        Expect(GetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing the folder slideshow shortcut");
        for (int index = 0; index < static_cast<int>(std::size(originalKeyboardState)); ++index)
        {
            modifiedKeyboardState[index] = originalKeyboardState[index];
        }
        modifiedKeyboardState[VK_CONTROL] |= 0x80;
        modifiedKeyboardState[VK_SHIFT] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage the keyboard state for the folder slideshow shortcut test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'F', 0);
        PumpMessagesFor(100);
        SetKeyboardState(originalKeyboardState);
        Expect(state->viewerStartFolderSlideshowRequests == 1,
            "Viewer Ctrl+Shift+F did not request a folder slideshow from the owner window");
        Expect(state->lastViewerStartFolderSlideshowSource == viewer.Hwnd(),
            "Viewer folder slideshow request did not identify the active viewer window");

        state->viewerQuickSendRequests = 0;
        state->lastViewerQuickSendSource = nullptr;
        BYTE unmodifiedShortcutKeyboardState[256]{};
        Expect(GetKeyboardState(unmodifiedShortcutKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing viewer Quick Send shortcuts");
        unmodifiedShortcutKeyboardState[VK_CONTROL] &= ~0x80;
        unmodifiedShortcutKeyboardState[VK_SHIFT] &= ~0x80;
        unmodifiedShortcutKeyboardState[VK_MENU] &= ~0x80;
        Expect(SetKeyboardState(unmodifiedShortcutKeyboardState) != FALSE,
            "Failed to clear modifier state for the viewer Quick Send shortcut test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F7, 0);
        PumpMessagesFor(100);
        Expect(state->viewerQuickSendRequests == 1
                && state->lastViewerQuickSendOperation == hyperbrowse::viewer::QuickSendOperation::Move,
            "Viewer F7 did not dispatch a Quick Send move request");
        Expect(state->lastViewerQuickSendSource == viewer.Hwnd(),
            "Viewer F7 Quick Send request did not identify the active viewer window");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F7, 1LL << 30);
        PumpMessagesFor(100);
        Expect(state->viewerQuickSendRequests == 1,
            "Viewer Quick Send move request did not ignore key auto-repeat");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F8, 0);
        PumpMessagesFor(100);
        Expect(state->viewerQuickSendRequests == 2
                && state->lastViewerQuickSendOperation == hyperbrowse::viewer::QuickSendOperation::Copy,
            "Viewer F8 did not dispatch a Quick Send copy request");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing viewer Quick Send shortcuts");

        modifiedKeyboardState[VK_CONTROL] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Ctrl state for the viewer Quick Send modifier test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F7, 0);
        PumpMessagesFor(100);
        SetKeyboardState(originalKeyboardState);
        Expect(state->viewerQuickSendRequests == 2,
            "Viewer Quick Send did not ignore a Ctrl-modified shortcut");

        modifiedKeyboardState[VK_MENU] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Alt state for the viewer Quick Send modifier test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F8, 0);
        PumpMessagesFor(100);
        SetKeyboardState(originalKeyboardState);
        Expect(state->viewerQuickSendRequests == 2,
            "Viewer Quick Send did not ignore an Alt-modified shortcut");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
        PumpMessagesFor(100);
        Expect(!viewer.AreInfoOverlaysVisible(), "Viewer Tab key did not hide the info overlays");
        COLORREF metadataPixelWithoutOverlays{};
        Expect(ReadClientPixel(viewer.Hwnd(), metadataSamplePoint, &metadataPixelWithoutOverlays),
            "Failed to sample the metadata pane with info overlays hidden");
        Expect(metadataPixelWithoutOverlays != metadataPixelWithOverlays,
            "Viewer full metadata pane remained visible when info overlays were hidden");

        RECT secondImageClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &secondImageClientRect) != FALSE,
            "Failed to read the viewer client area for the Enter zoom toggle test");
        const int secondImageFitZoomPercent = std::max(
            1,
            static_cast<int>(std::lround(std::min(
                static_cast<double>(secondImageClientRect.right) / 640.0,
                static_cast<double>(secondImageClientRect.bottom) / 320.0) * 100.0)));

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == 100; }, 1000),
               "Viewer Enter key did not switch from fit-to-window to actual size");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_MINUS, 0);
        PumpMessagesFor(300);
        Expect(viewer.CurrentZoomPercent() == 100,
            "Viewer zoom-out should not shrink the image below the current window-fit bound");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 100; }, 1000),
            "Viewer zoom-in command failed before testing the Enter fit fallback");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == secondImageFitZoomPercent; }, 1000),
            "Viewer Enter key did not choose fit-to-window from custom zoom");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == 100; }, 1000),
            "Viewer Enter key did not switch from fit-to-window to actual size");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == secondImageFitZoomPercent; }, 1000),
            "Viewer Enter key did not switch from actual size to fit-to-window");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'R', 0);
        PumpMessagesFor(100);
        Expect(viewer.RotationQuarterTurns() == 1, "Viewer rotate-right command failed");

        RECT wheelZoomClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &wheelZoomClientRect) != FALSE,
               "Failed to read the viewer client area for the immediate pan-after-wheel-zoom test");
        POINT wheelZoomScreenPoint{
            wheelZoomClientRect.right / 2,
            std::max<LONG>(1, wheelZoomClientRect.bottom / 16)};
        Expect(ClientToScreen(viewer.Hwnd(), &wheelZoomScreenPoint) != FALSE,
               "Failed to convert the wheel-zoom anchor to screen coordinates");
        for (int index = 0; index < 2; ++index)
        {
            SendMessageW(viewer.Hwnd(),
                         WM_MOUSEWHEEL,
                         MAKEWPARAM(0, WHEEL_DELTA),
                         MAKELPARAM(wheelZoomScreenPoint.x, wheelZoomScreenPoint.y));
        }

        const POINT initialPan = viewer.PanOffset();
         const int indexBeforeImmediateArrow = viewer.CurrentIndex();
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
         const POINT immediateArrowPan = viewer.PanOffset();
         Expect(viewer.CurrentIndex() == indexBeforeImmediateArrow,
             "Viewer immediate wheel-zoom arrow input navigated instead of panning");
         Expect(immediateArrowPan.y != initialPan.y,
             "Viewer immediate wheel-zoom arrow input did not pan the viewport");

        const POINT dragStart{
            wheelZoomClientRect.right / 2,
            wheelZoomClientRect.bottom / 2};
        const POINT dragEnd{dragStart.x, dragStart.y - 60};
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(dragStart.x, dragStart.y));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(dragEnd.x, dragEnd.y));
        const POINT draggedPan = viewer.PanOffset();
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(dragEnd.x, dragEnd.y));
        PumpMessagesFor(300);
        const POINT movedPan = viewer.PanOffset();
        Expect(draggedPan.x != initialPan.x || draggedPan.y != initialPan.y,
               "Viewer immediate wheel-zoom pan did not apply the drag movement");
        Expect(movedPan.x == draggedPan.x && movedPan.y == draggedPan.y,
               "Viewer pan immediately after wheel zoom was lost when the smooth-zoom timer completed");

        const LONG panBeforeArrow = viewer.PanOffset().y;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        const LONG panAfterUp = viewer.PanOffset().y;
        Expect(panAfterUp > panBeforeArrow, "Viewer up arrow did not pan the viewport upward");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(viewer.PanOffset().y == panBeforeArrow, "Viewer down arrow did not pan the zoomed image downward");
        viewer.SetKeyboardPanningInverted(true);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        const LONG invertedPanAfterUp = viewer.PanOffset().y;
        Expect(invertedPanAfterUp < panBeforeArrow,
            "Viewer inverted keyboard panning option did not restore the legacy up-arrow direction");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(viewer.PanOffset().y > invertedPanAfterUp,
            "Viewer inverted keyboard panning option did not restore the legacy down-arrow direction");
        viewer.SetKeyboardPanningInverted(false);

        for (int index = 0; index < 128; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        }
        const LONG topPan = viewer.PanOffset().y;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        Expect(viewer.PanOffset().y == topPan, "Viewer up arrow exceeded the top image pan limit");

        for (int index = 0; index < 128; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        }
        const LONG bottomPan = viewer.PanOffset().y;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(viewer.PanOffset().y == bottomPan, "Viewer down arrow exceeded the bottom image pan limit");
        Expect(topPan > bottomPan, "Viewer vertical pan limits did not reflect image overflow");

         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
         PumpMessagesFor(100);
         const POINT fitPan = viewer.PanOffset();
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
         Expect(fitPan.x == 0 && fitPan.y == 0 && viewer.PanOffset().x == 0 && viewer.PanOffset().y == 0,
             "Viewer arrows panned an image while it was fit to the window");

        RunViewerWindowFitModeChecks(viewer, 640, 320);

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::Close);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        const HWND closeHandle = viewer.Hwnd();
        SendMessageW(closeHandle, WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(closeHandle != nullptr && IsWindow(closeHandle) == FALSE && !viewer.IsOpen(),
            "Fullscreen Escape Close did not close the viewer window");

        SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);

         viewer.SetOverlayTextSize(hyperbrowse::viewer::InfoOverlayTextSize::Small);
         hyperbrowse::viewer::ViewerWindow::SetDefaultOverlayTextSize(
             hyperbrowse::viewer::InfoOverlayTextSize::Large);
         Expect(viewer.Open(ownerWindow, items, 0, false),
             "Viewer failed to reopen after changing the closed-viewer overlay default");
         Expect(viewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Large,
             "Viewer did not reload the saved overlay text size when reopening a reused window object");
         SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
         PumpMessagesFor(100);

         hyperbrowse::viewer::ViewerWindow restoredViewer(instance);
         Expect(restoredViewer.Open(ownerWindow, items, 0, false), "Restored viewer window failed to open");
         Expect(PumpMessagesUntil([&]() { return restoredViewer.CurrentZoomPercent() > 0; }, 5000),
             "Restored viewer window did not finish the initial image decode");
         Expect(!restoredViewer.AreInfoOverlaysVisible(), "Viewer did not restore the persisted info-overlay visibility");
         Expect(restoredViewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Large,
             "Viewer did not restore the persisted overlay text size");
         Expect(restoredViewer.IsFullMetadataVisible(), "Viewer did not restore the persisted full metadata visibility");
         SendMessageW(restoredViewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
         PumpMessagesFor(100);
         Expect(restoredViewer.AreInfoOverlaysVisible(), "Viewer Tab key did not restore the info overlays after reopening");
         SendMessageW(restoredViewer.Hwnd(), WM_CLOSE, 0, 0);
         PumpMessagesFor(100);
    }
}
