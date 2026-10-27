#include "ui/MainWindowInternal.h"
#include "viewer/ViewerOverlayDialog.h"

namespace
{

    HMONITOR ResolveViewerMonitor(HWND hwnd, bool preferSecondaryMonitor)
    {
        if (preferSecondaryMonitor)
        {
            return FindAlternateMonitorForWindow(hwnd);
        }

        if (hwnd && IsWindow(hwnd))
        {
            return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        }

        return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
}

namespace hyperbrowse::ui
{
    bool MainWindow::ConfigureViewerOverlays(HWND ownerWindow, const viewer::ViewerOverlaySettings* initialSettings)
    {
        auto settings = viewer::ViewerWindow::DefaultOverlaySettings();
        SIZE viewport{1280, 800};
        UINT dpi = 96;
        auto* active = FindViewerByHwnd(ownerWindow);
        if (!active)
        {
            const auto openViewers = OpenViewerWindows();
            if (!openViewers.empty()) active = openViewers.front();
        }
        if (active)
        {
            settings = active->OverlaySettings();
            RECT client{};
            GetClientRect(active->Hwnd(), &client);
            viewport = {client.right, client.bottom};
            dpi = GetDpiForWindow(active->Hwnd());
        }
        if (initialSettings) settings = *initialSettings;
        return viewer::ShowViewerOverlayDialog(ownerWindow ? ownerWindow : hwnd_, instance_,
            themeMode_ == ThemeMode::Dark, appTextSize_, settings, viewport, dpi,
            [this](const viewer::ViewerOverlaySettings& applied)
        {
            if (!viewer::ViewerWindow::SetDefaultOverlaySettings(applied)) return false;
            for (auto* openViewer : OpenViewerWindows()) openViewer->SetOverlaySettings(applied);
            UpdateMenuState();
            return true;
        });
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
        browserPaneController_->SetSortMode(sortMode_);
        browserPaneController_->SetSortAscending(sortAscending_);
        ApplyRawJpegPairingSettings();
        browserPaneController_->RefreshFromModel();
        UpdateDetailsPanel();
    }

    void MainWindow::PromptForBrowserItemNumber()
    {
        if (!browserPaneController_)
        {
            return;
        }

        const int itemCount = static_cast<int>(browserPaneController_->DisplayedItemCount());
        if (itemCount <= 0)
        {
            MessageBeep(MB_ICONWARNING);
            return;
        }

        const std::wstring title = L"Go to File";
        const std::wstring instruction = L"Enter a file number from 1 to " + std::to_wstring(itemCount) + L".";
        std::wstring candidate = std::to_wstring(browserPaneController_->CurrentItemNumber());
        while (PromptForSingleLineText(hwnd_,
                                       instance_,
                                       appTextSize_,
                                       themeMode_ == ThemeMode::Dark,
                                       title,
                                       instruction,
                                       L"Go",
                                       candidate,
                                       0,
                                       -1,
                                       &candidate))
        {
            int targetIndex = -1;
            if (!TryParseItemNumber(candidate, itemCount, &targetIndex))
            {
                MessageBeep(MB_ICONWARNING);
                MessageBoxW(hwnd_, instruction.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
                continue;
            }

            browserPaneController_->GoToItemNumber(targetIndex + 1);
            UpdateStatusText();
            return;
        }
    }

    bool MainWindow::ShouldDefaultViewerToSecondaryMonitor() const
    {
        if (!defaultViewerToSecondaryMonitor_)
        {
            return false;
        }
        return FindAlternateMonitorForWindow(hwnd_) != nullptr;
    }

    std::vector<browser::BrowserItem> MainWindow::ResolvePairedRawJpegViewerItems(
        std::vector<browser::BrowserItem> items,
        bool startSlideshow) const
    {
        if (!rawJpegPairedOperationsEnabled_ || items.empty())
        {
            return items;
        }

        const browser::RawJpegDisplayPreference preference = startSlideshow
            ? browser::RawJpegDisplayPreference::Jpeg
            : pairedRawJpegViewerPreference_;
        return PairedRawJpegResolver::ResolveItems(
            std::move(items),
            browserModel_ ? browserModel_->Items() : std::vector<browser::BrowserItem>{},
            preference,
            [](std::wstring_view lhs, std::wstring_view rhs)
            {
                return FolderPathsEqual(lhs, rhs);
            });
    }

    void MainWindow::OpenItemInViewer(int modelIndex, bool preferSecondaryMonitor)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const auto& modelItems = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
        {
            return;
        }

        const browser::BrowserItem& selectedItem = modelItems[static_cast<std::size_t>(modelIndex)];
        if (selectedItem.isDirectory)
        {
            LoadFolderAsync(selectedItem.filePath);
            return;
        }

        if (!viewerWindow_)
        {
            return;
        }

        const std::vector<int> orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
        ViewerItemSelectionPolicy::Result viewerSelection = ViewerItemSelectionPolicy::Build({
            modelItems,
            orderedModelIndices,
            modelIndex,
            {},
            {},
            -1});
        if (viewerSelection.selectedIndex < 0)
        {
            return;
        }

        OpenItemsInViewer(
            std::move(viewerSelection.items),
            viewerSelection.selectedIndex,
            false,
            preferSecondaryMonitor);
    }

    void MainWindow::OpenItemInNewViewerWindow(int modelIndex, bool preferSecondaryMonitor)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const auto& modelItems = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
        {
            return;
        }

        const browser::BrowserItem& selectedItem = modelItems[static_cast<std::size_t>(modelIndex)];
        if (selectedItem.isDirectory)
        {
            LoadFolderAsync(selectedItem.filePath);
            return;
        }

        const std::vector<int> orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
        ViewerItemSelectionPolicy::Result viewerSelection = ViewerItemSelectionPolicy::Build({
            modelItems,
            orderedModelIndices,
            modelIndex,
            {},
            {},
            -1});
        if (viewerSelection.selectedIndex < 0)
        {
            return;
        }

        OpenItemsInNewViewerWindow(
            std::move(viewerSelection.items),
            viewerSelection.selectedIndex,
            false,
            preferSecondaryMonitor);
    }

    std::vector<viewer::ViewerWindow*> MainWindow::OpenViewerWindows() const
    {
        std::vector<viewer::ViewerWindow*> viewers;
        if (viewerWindow_ && viewerWindow_->IsOpen())
        {
            viewers.push_back(viewerWindow_.get());
        }
        for (const std::unique_ptr<viewer::ViewerWindow>& viewer : additionalViewerWindows_)
        {
            if (viewer && viewer->IsOpen())
            {
                viewers.push_back(viewer.get());
            }
        }
        return viewers;
    }

    viewer::ViewerWindow* MainWindow::FindViewerByHwnd(HWND viewerHwnd) const
    {
        if (!viewerHwnd)
        {
            return nullptr;
        }
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            if (viewer->Hwnd() == viewerHwnd)
            {
                return viewer;
            }
        }
        return nullptr;
    }

    viewer::ViewerWindow* MainWindow::ActiveViewer() const
    {
        if (viewer::ViewerWindow* viewer = FindViewerByHwnd(activeViewerWindow_))
        {
            return viewer;
        }
        if (viewerWindow_ && viewerWindow_->IsOpen())
        {
            return viewerWindow_.get();
        }
        const std::vector<viewer::ViewerWindow*> viewers = OpenViewerWindows();
        return viewers.empty() ? nullptr : viewers.back();
    }

    int MainWindow::ActiveViewerZoomPercent() const
    {
        const viewer::ViewerWindow* viewer = ActiveViewer();
        return viewer ? viewer->CurrentZoomPercent() : 0;
    }

    bool MainWindow::OpenItemsInViewer(std::vector<browser::BrowserItem> items,
                                       int selectedIndex,
                                       bool startSlideshow,
                                       bool preferSecondaryMonitor,
                                       bool resolvePairedRawJpegItems)
    {
        if (std::any_of(items.begin(), items.end(), [](const browser::BrowserItem& item)
        {
            return item.isDirectory;
        }))
        {
            std::vector<browser::BrowserItem> imageItems;
            imageItems.reserve(items.size());
            int imageSelectedIndex = -1;
            for (int index = 0; index < static_cast<int>(items.size()); ++index)
            {
                if (items[static_cast<std::size_t>(index)].isDirectory)
                {
                    continue;
                }

                if (index == selectedIndex)
                {
                    imageSelectedIndex = static_cast<int>(imageItems.size());
                }
                imageItems.push_back(std::move(items[static_cast<std::size_t>(index)]));
            }
            items = std::move(imageItems);
            selectedIndex = imageSelectedIndex;
        }

        if (!viewerWindow_ || items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return false;
        }

        if (resolvePairedRawJpegItems)
        {
            items = ResolvePairedRawJpegViewerItems(std::move(items), startSlideshow);
        }

        const HMONITOR targetMonitor = ResolveViewerMonitor(hwnd_, preferSecondaryMonitor);
        if (preferSecondaryMonitor && !targetMonitor)
        {
            MessageBoxW(hwnd_,
                        L"A secondary monitor is not currently available.",
                        L"View on Secondary Monitor",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        viewerWindow_->SetAppTextSize(appTextSize_);
        viewerWindow_->SetResourceProfile(resourceProfile_);
        viewerWindow_->SetPrefetchDepthOverride(prefetchDepthOverride_);
        viewerWindow_->SetMemoryPressureActive(thumbnailMemoryPressureActive_);
        viewerWindow_->SetEscapeKeyBehavior(viewerEscapeKeyBehavior_);
        viewerWindow_->SetMouseWheelBehavior(viewerMouseWheelBehavior_);
        viewerWindow_->SetKeyboardPanningInverted(invertKeyboardPanning_);
        viewerWindow_->SetTransitionSettings(slideshowTransitionStyle_, slideshowTransitionDurationMs_);
        viewerWindow_->SetManualTransitionEnabled(useSlideshowTransition_);
        viewerWindow_->SetColorManagementEnabled(colorManagementEnabled_);
        if (viewerWindow_->Open(hwnd_, std::move(items), selectedIndex, themeMode_ == ThemeMode::Dark, targetMonitor))
        {
            if (startSlideshow)
            {
                viewerWindow_->StartSlideshow(slideshowIntervalMs_);
            }
            activeViewerWindow_ = viewerWindow_->Hwnd();
            viewerWindowActive_ = true;
            UpdateStatusText();
            return true;
        }

        return false;
    }

    bool MainWindow::OpenItemsInNewViewerWindow(std::vector<browser::BrowserItem> items,
                                                int selectedIndex,
                                                bool startSlideshow,
                                                bool preferSecondaryMonitor,
                                                bool resolvePairedRawJpegItems)
    {
        if (std::any_of(items.begin(), items.end(), [](const browser::BrowserItem& item)
        {
            return item.isDirectory;
        }))
        {
            std::vector<browser::BrowserItem> imageItems;
            imageItems.reserve(items.size());
            int imageSelectedIndex = -1;
            for (int index = 0; index < static_cast<int>(items.size()); ++index)
            {
                if (items[static_cast<std::size_t>(index)].isDirectory)
                {
                    continue;
                }

                if (index == selectedIndex)
                {
                    imageSelectedIndex = static_cast<int>(imageItems.size());
                }
                imageItems.push_back(std::move(items[static_cast<std::size_t>(index)]));
            }
            items = std::move(imageItems);
            selectedIndex = imageSelectedIndex;
        }

        if (items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return false;
        }

        if (resolvePairedRawJpegItems)
        {
            items = ResolvePairedRawJpegViewerItems(std::move(items), startSlideshow);
        }

        const HMONITOR targetMonitor = ResolveViewerMonitor(hwnd_, preferSecondaryMonitor);
        if (preferSecondaryMonitor && !targetMonitor)
        {
            MessageBoxW(hwnd_,
                        L"A secondary monitor is not currently available.",
                        L"View on Secondary Monitor",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        auto viewer = std::make_unique<viewer::ViewerWindow>(instance_);
        ApplyViewerMouseWheelSetting();
        ApplyViewerTransitionSettings();
        viewer->SetAppTextSize(appTextSize_);
        viewer->SetResourceProfile(resourceProfile_);
        viewer->SetPrefetchDepthOverride(prefetchDepthOverride_);
        viewer->SetMemoryPressureActive(thumbnailMemoryPressureActive_);
        viewer->SetEscapeKeyBehavior(viewerEscapeKeyBehavior_);
        viewer->SetMouseWheelBehavior(viewerMouseWheelBehavior_);
        viewer->SetKeyboardPanningInverted(invertKeyboardPanning_);
        viewer->SetTransitionSettings(slideshowTransitionStyle_, slideshowTransitionDurationMs_);
        viewer->SetManualTransitionEnabled(useSlideshowTransition_);
        viewer->SetColorManagementEnabled(colorManagementEnabled_);
        viewer->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        if (!viewer->Open(hwnd_, std::move(items), selectedIndex, themeMode_ == ThemeMode::Dark, targetMonitor))
        {
            return false;
        }

        if (startSlideshow)
        {
            viewer->StartSlideshow(slideshowIntervalMs_);
        }

        const HWND viewerHwnd = viewer->Hwnd();
        additionalViewerWindows_.push_back(std::move(viewer));
        activeViewerWindow_ = viewerHwnd;
        viewerWindowActive_ = true;
        SetForegroundWindow(viewerHwnd);
        SetFocus(viewerHwnd);
        UpdateStatusText();
        return true;
    }

    bool MainWindow::SyncViewerToBrowserModel(std::wstring_view preferredPath)
    {
        if (!viewerWindow_)
        {
            return false;
        }
        return SyncViewerToBrowserModel(*viewerWindow_, preferredPath);
    }

    bool MainWindow::SyncViewerToBrowserModel(viewer::ViewerWindow& viewer, std::wstring_view preferredPath)
    {
        if (!viewer.IsOpen() || !browserModel_)
        {
            return false;
        }

        const std::vector<int> orderedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedModelIndicesSnapshot()
            : std::vector<int>{};
        ViewerSynchronizer::Result synchronization = ViewerSynchronizer::Build(
            browserModel_->Items(),
            orderedModelIndices,
            preferredPath,
            viewer.CurrentFilePath(),
            viewer.CurrentIndex(),
            viewer.IsSlideshowActive(),
            [this](std::vector<browser::BrowserItem> items, bool startSlideshow)
            {
                return ResolvePairedRawJpegViewerItems(std::move(items), startSlideshow);
            });
        if (synchronization.closeRequested)
        {
            const HWND viewerHwnd = viewer.Hwnd();
            if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
            {
                PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
            }
            return false;
        }

        return viewer.ReplaceItems(
            std::move(synchronization.items),
            synchronization.selectedIndex);
    }
}
