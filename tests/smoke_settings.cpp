#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunAppTextSizeScenario(HINSTANCE instance)
    {
        using hyperbrowse::util::AppTextSize;

        Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Small) == 0.84f,
               "Small app text size did not use the configured scale factor");
        Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Medium) == 1.0f,
               "Medium app text size did not preserve the baseline scale factor");
        Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Large) == 1.20f,
               "Large app text size did not use the configured scale factor");
        Expect(hyperbrowse::util::NormalizeAppTextSize(99) == AppTextSize::Medium,
               "Invalid app text size values did not normalize to Medium");
         Expect(hyperbrowse::util::ScaleAppTextDimension(100, AppTextSize::Small) == 84,
               "Small app text dimensions were not rounded as configured");
         Expect(hyperbrowse::util::ScaleAppTextDimension(100, AppTextSize::Large) == 120,
               "Large app text dimensions were not rounded as configured");

        ScopedRegistryDwordBackup appTextSizeBackup(kRegistryPath, kRegistryValueAppTextSize);
        ScopedRegistryDwordBackup overlayTextSizeBackup(kRegistryPath, kRegistryValueViewerInfoOverlayTextSize);
        ScopedRegistryDwordBackup escapeKeyBehaviorBackup(kRegistryPath, kRegistryValueViewerEscapeKeyBehavior);
        DeleteRegistryValue(kRegistryPath, kRegistryValueAppTextSize);
        SetRegistryDwordValue(kRegistryPath,
                              kRegistryValueViewerInfoOverlayTextSize,
                              static_cast<DWORD>(hyperbrowse::viewer::InfoOverlayTextSize::Large));
        SetRegistryDwordValue(kRegistryPath,
                              kRegistryValueViewerEscapeKeyBehavior,
                              static_cast<DWORD>(hyperbrowse::viewer::EscapeKeyBehavior::FitHeight));
        {
            hyperbrowse::ui::MainWindow mainWindow(instance);
            Expect(mainWindow.Create(), "Failed to create MainWindow for app text-size smoke coverage");

            Expect(mainWindow.ViewerEscapeKeyBehavior() == hyperbrowse::viewer::EscapeKeyBehavior::FitHeight,
                   "Viewer fullscreen Escape behavior did not restore its persisted setting");

            Expect(mainWindow.AppTextSize() == AppTextSize::Medium,
                   "App text size did not default to Medium when no value was persisted");

            HWND treeView = FindWindowExW(mainWindow.Hwnd(), nullptr, WC_TREEVIEWW, nullptr);
            Expect(treeView != nullptr, "MainWindow did not create a tree view for app text-size smoke coverage");
            HFONT mediumFont = reinterpret_cast<HFONT>(SendMessageW(treeView, WM_GETFONT, 0, 0));
            LOGFONTW mediumLogFont{};
            Expect(mediumFont != nullptr
                       && GetObjectW(mediumFont, sizeof(mediumLogFont), &mediumLogFont) == sizeof(mediumLogFont),
                   "Could not inspect the Medium app UI font");

            constexpr UINT smallCommand = 2223;
            constexpr UINT mediumCommand = 2224;
            constexpr UINT largeCommand = 2225;

            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(largeCommand, 0), 0);
            Expect(mainWindow.AppTextSize() == AppTextSize::Large,
                   "Large app text size command was not applied");
            DWORD overlayTextSize = 0;
            Expect(TryReadRegistryDwordValue(kRegistryPath,
                                             kRegistryValueViewerInfoOverlayTextSize,
                                             &overlayTextSize)
                       && overlayTextSize == static_cast<DWORD>(hyperbrowse::viewer::InfoOverlayTextSize::Large),
                   "App text size command changed the independent viewer overlay text-size preference");
            HFONT largeFont = reinterpret_cast<HFONT>(SendMessageW(treeView, WM_GETFONT, 0, 0));
            LOGFONTW largeLogFont{};
            Expect(largeFont != nullptr
                       && GetObjectW(largeFont, sizeof(largeLogFont), &largeLogFont) == sizeof(largeLogFont),
                   "Could not inspect the Large app UI font");
            Expect(-largeLogFont.lfHeight > -mediumLogFont.lfHeight,
                   "Large app text size did not replace the native control font");

            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(smallCommand, 0), 0);
            Expect(mainWindow.AppTextSize() == AppTextSize::Small,
                   "Small app text size command was not applied");
            HFONT smallFont = reinterpret_cast<HFONT>(SendMessageW(treeView, WM_GETFONT, 0, 0));
            LOGFONTW smallLogFont{};
            Expect(smallFont != nullptr
                       && GetObjectW(smallFont, sizeof(smallLogFont), &smallLogFont) == sizeof(smallLogFont),
                   "Could not inspect the Small app UI font");
            Expect(-smallLogFont.lfHeight < -mediumLogFont.lfHeight,
                   "Small app text size did not replace the native control font");

            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(mediumCommand, 0), 0);
            Expect(mainWindow.AppTextSize() == AppTextSize::Medium,
                   "Medium app text size command was not applied");
            DestroyWindow(mainWindow.Hwnd());
            PumpMessagesFor(100);
        }

        SetRegistryDwordValue(kRegistryPath, kRegistryValueAppTextSize, static_cast<DWORD>(AppTextSize::Large));
        {
            hyperbrowse::ui::MainWindow restoredMainWindow(instance);
            Expect(restoredMainWindow.Create(), "Failed to recreate MainWindow for app text-size persistence coverage");
            Expect(restoredMainWindow.AppTextSize() == AppTextSize::Large,
                   "App text size did not persist across MainWindow recreation");
            DestroyWindow(restoredMainWindow.Hwnd());
            PumpMessagesFor(100);
        }

        SetRegistryDwordValue(kRegistryPath, kRegistryValueAppTextSize, 99);
        {
            hyperbrowse::ui::MainWindow normalizedMainWindow(instance);
            Expect(normalizedMainWindow.Create(), "Failed to create MainWindow for invalid app text-size coverage");
            Expect(normalizedMainWindow.AppTextSize() == AppTextSize::Medium,
                   "MainWindow did not normalize an invalid persisted app text size");
            DestroyWindow(normalizedMainWindow.Hwnd());
            PumpMessagesFor(100);
        }
    }

        void RunMainWindowCascadeScenario(HINSTANCE instance)
        {
         ScopedRegistryDwordBackup windowLeftBackup(kRegistryPath, kRegistryValueWindowLeft);
         ScopedRegistryDwordBackup windowTopBackup(kRegistryPath, kRegistryValueWindowTop);
         ScopedRegistryDwordBackup windowWidthBackup(kRegistryPath, kRegistryValueWindowWidth);
         ScopedRegistryDwordBackup windowHeightBackup(kRegistryPath, kRegistryValueWindowHeight);

         const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTONEAREST);
         MONITORINFO monitorInfo{};
         monitorInfo.cbSize = sizeof(monitorInfo);
         Expect(monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo),
             "Could not read a monitor work area for main-window cascade coverage");

         const LONG workAreaWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
         const LONG workAreaHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
         const LONG persistedWidth = workAreaWidth - 128;
         const LONG persistedHeight = workAreaHeight - 128;
         Expect(persistedWidth >= 960 && persistedHeight >= 640,
             "The monitor work area is too small for main-window cascade coverage");

         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowLeft, static_cast<DWORD>(monitorInfo.rcWork.left));
         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowTop, static_cast<DWORD>(monitorInfo.rcWork.top));
         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowWidth, static_cast<DWORD>(persistedWidth));
         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowHeight, static_cast<DWORD>(persistedHeight));

         hyperbrowse::ui::MainWindow firstWindow(instance);
         hyperbrowse::ui::MainWindow secondWindow(instance);
         Expect(firstWindow.Create(), "Failed to create the first MainWindow for cascade coverage");
         Expect(secondWindow.Create(), "Failed to create the second MainWindow for cascade coverage");

         RECT firstRect{};
         RECT secondRect{};
         Expect(GetWindowRect(firstWindow.Hwnd(), &firstRect) != FALSE,
             "Could not read the first MainWindow rectangle for cascade coverage");
         Expect(GetWindowRect(secondWindow.Hwnd(), &secondRect) != FALSE,
             "Could not read the second MainWindow rectangle for cascade coverage");
         Expect(firstRect.right - firstRect.left == secondRect.right - secondRect.left
                 && firstRect.bottom - firstRect.top == secondRect.bottom - secondRect.top,
             "Cascaded MainWindows did not retain the persisted size");
         Expect(firstRect.left != secondRect.left || firstRect.top != secondRect.top,
             "A second MainWindow opened on top of the first");

         DestroyWindow(secondWindow.Hwnd());
         PumpMessagesFor(100);
         DestroyWindow(firstWindow.Hwnd());
         PumpMessagesFor(100);
        }

        void RunDialogDpiHelperScenario()
        {
         using hyperbrowse::ui::DialogDpiForWindow;
         using hyperbrowse::ui::ScaleDialogAppTextDimension;
         using hyperbrowse::ui::ScaleDialogDimension;

         Expect(DialogDpiForWindow(nullptr) == hyperbrowse::ui::kDefaultDpi,
             "Dialog DPI helper did not use the default DPI without a window");
         Expect(ScaleDialogDimension(100, 96) == 100,
             "Dialog DPI helper changed a base-size dimension at 96 DPI");
         Expect(ScaleDialogDimension(100, 144) == 150,
             "Dialog DPI helper did not scale dimensions to 144 DPI");
         Expect(ScaleDialogDimension(100, 192) == 200,
             "Dialog DPI helper did not scale dimensions to 192 DPI");
         Expect(ScaleDialogAppTextDimension(80, hyperbrowse::util::AppTextSize::Small, 96) == 67,
             "Dialog DPI helper did not preserve Small text sizing at 100% DPI");
         Expect(ScaleDialogAppTextDimension(80, hyperbrowse::util::AppTextSize::Medium, 96) == 80,
             "Dialog DPI helper changed Medium text sizing at 100% DPI");
         Expect(ScaleDialogAppTextDimension(80, hyperbrowse::util::AppTextSize::Large, 96) == 96,
             "Dialog DPI helper did not apply Large text sizing at 100% DPI");
         Expect(ScaleDialogAppTextDimension(80, hyperbrowse::util::AppTextSize::Small, 144) == 101,
             "Dialog DPI helper did not compose Small text sizing with 150% DPI");
         Expect(ScaleDialogAppTextDimension(80, hyperbrowse::util::AppTextSize::Large, 144) == 144,
             "Dialog DPI helper did not compose app text size with monitor DPI");
         Expect(ScaleDialogAppTextDimension(80, hyperbrowse::util::AppTextSize::Large, 192) == 192,
             "Dialog DPI helper did not compose Large text sizing with 200% DPI");
        }

        void RunDialogShellGeometryScenario()
        {
         using hyperbrowse::ui::ClampDialogFrameToWorkArea;
         using hyperbrowse::ui::DialogShellMetrics;

         const RECT workArea{-1920, 40, 0, 1120};
         const RECT oversizedFrame{-1500, -200, 200, 1300};
         const RECT clampedFrame = ClampDialogFrameToWorkArea(oversizedFrame, workArea);
         Expect(clampedFrame.left >= workArea.left
                    && clampedFrame.top >= workArea.top
                    && clampedFrame.right <= workArea.right
                    && clampedFrame.bottom <= workArea.bottom,
                "Dialog shell did not clamp an oversized frame to the work area");
         Expect(clampedFrame.right > clampedFrame.left && clampedFrame.bottom > clampedFrame.top,
                "Dialog shell returned an empty clamped frame");

         constexpr std::array<UINT, 5> dpis{96, 120, 144, 168, 192};
         constexpr std::array<hyperbrowse::util::AppTextSize, 3> textSizes{
             hyperbrowse::util::AppTextSize::Small,
             hyperbrowse::util::AppTextSize::Medium,
             hyperbrowse::util::AppTextSize::Large};
         for (const UINT dpi : dpis)
         {
             for (const auto textSize : textSizes)
             {
                 DialogShellMetrics metrics;
                 metrics.dpi = dpi;
                 metrics.appTextSize = textSize;
                 Expect(metrics.Scale(100) > 0 && metrics.ToPhysical(100) > 0,
                        "Dialog shell produced a non-positive scaled dimension");
                 Expect(metrics.ToPhysical(metrics.Scale(100)) >= metrics.ToPhysical(80),
                        "Dialog shell text metrics were not monotonic");
             }
         }
        }

        void RunDialogGeometryScenario()
        {
         struct DialogFamily
         {
             const char* name;
             int width;
             int height;
             int margin;
             int headerHeight;
             int controlHeight;
         };

         constexpr std::array<DialogFamily, 10> dialogFamilies{
             DialogFamily{"About", 720, 540, 28, 44, 38},
             DialogFamily{"Shortcut Reference", 760, 520, 18, 28, 30},
             DialogFamily{"Slideshow", 560, 368, 18, 44, 30},
             DialogFamily{"Image Information", 640, 370, 18, 28, 30},
             DialogFamily{"Text Input", 420, 190, 18, 28, 30},
             DialogFamily{"Batch Rename", 720, 480, 18, 28, 30},
             DialogFamily{"Performance", 720, 540, 18, 28, 30},
             DialogFamily{"File Associations", 760, 600, 18, 44, 30},
             DialogFamily{"Diagnostics", 1040, 760, 18, 34, 30},
             DialogFamily{"Experimental Settings", 1120, 760, 28, 44, 38}};
         constexpr std::array<UINT, 5> dpis{96, 120, 144, 168, 192};
         constexpr std::array<hyperbrowse::util::AppTextSize, 3> textSizes{
             hyperbrowse::util::AppTextSize::Small,
             hyperbrowse::util::AppTextSize::Medium,
             hyperbrowse::util::AppTextSize::Large};

         const auto isValidRect = [](const RECT& rect)
         {
             return rect.right > rect.left && rect.bottom > rect.top;
         };
         const auto contains = [](const RECT& outer, const RECT& inner)
         {
             return inner.left >= outer.left && inner.top >= outer.top
                 && inner.right <= outer.right && inner.bottom <= outer.bottom;
         };
         const auto overlaps = [](const RECT& first, const RECT& second)
         {
             return first.left < second.right && second.left < first.right
                 && first.top < second.bottom && second.top < first.bottom;
         };

         for (const DialogFamily& family : dialogFamilies)
         {
             int previousDpiWidth = 0;
             for (const UINT dpi : dpis)
             {
                 const int width = hyperbrowse::ui::ScaleDialogAppTextDimension(
                     family.width,
                     hyperbrowse::util::AppTextSize::Medium,
                     dpi);
                 Expect(width >= previousDpiWidth,
                        std::string(family.name) + " frame width was not monotonic across DPI values");
                 previousDpiWidth = width;
             }

             int previousTextWidth = 0;
             for (const auto textSize : textSizes)
             {
                 const int width = hyperbrowse::ui::ScaleDialogAppTextDimension(family.width, textSize, 96);
                 Expect(width >= previousTextWidth,
                        std::string(family.name) + " frame width was not monotonic across app text sizes");
                 previousTextWidth = width;
             }

             for (const UINT dpi : dpis)
             {
                 for (const auto textSize : textSizes)
                 {
                     const auto scale = [family, textSize, dpi](int value)
                     {
                         return hyperbrowse::ui::ScaleDialogAppTextDimension(value, textSize, dpi);
                     };
                     const RECT client{
                         0,
                         0,
                         scale(family.width),
                         scale(family.height)};
                     const int margin = scale(family.margin);
                     const int gap = scale(10);
                     const int controlHeight = scale(family.controlHeight);
                     const RECT header{margin, margin, client.right - margin, margin + scale(family.headerHeight)};
                     const RECT footer{
                         margin,
                         client.bottom - margin - controlHeight,
                         client.right - margin,
                         client.bottom - margin};
                     const RECT body{
                         margin,
                         header.bottom + gap,
                         client.right - margin,
                         footer.top - gap};
                     const RECT primaryButton{
                         footer.right - scale(96),
                         footer.top,
                         footer.right,
                         footer.bottom};
                     const RECT secondaryButton{
                         primaryButton.left - gap - scale(96),
                         footer.top,
                         primaryButton.left - gap,
                         footer.bottom};
                     const RECT nativeEdit{
                         body.left,
                         body.top,
                         std::min(body.right, body.left + scale(300)),
                         std::min(body.bottom, body.top + controlHeight)};

                     Expect(isValidRect(client) && isValidRect(header) && isValidRect(body) && isValidRect(footer),
                            std::string(family.name) + " produced an invalid dialog region");
                     Expect(!overlaps(header, body) && !overlaps(body, footer) && !overlaps(header, footer),
                            std::string(family.name) + " dialog regions overlap");
                     Expect(contains(client, header) && contains(client, body) && contains(client, footer),
                            std::string(family.name) + " dialog region escaped its client bounds");
                     Expect(isValidRect(primaryButton) && isValidRect(secondaryButton) && isValidRect(nativeEdit)
                                && contains(footer, primaryButton) && contains(footer, secondaryButton)
                                && contains(body, nativeEdit),
                            std::string(family.name) + " native control bounds were invalid");

                     const RECT workArea{-1600, 40, 0, 900};
                     const RECT suggestedFrame{
                         workArea.right - client.right / 2,
                         workArea.bottom - client.bottom / 2,
                         workArea.right + client.right / 2,
                         workArea.bottom + client.bottom / 2};
                     const RECT clampedFrame = hyperbrowse::ui::ClampDialogFrameToWorkArea(suggestedFrame, workArea);
                     Expect(clampedFrame.left >= workArea.left && clampedFrame.top >= workArea.top
                                && clampedFrame.right <= workArea.right && clampedFrame.bottom <= workArea.bottom
                                && isValidRect(clampedFrame),
                            std::string(family.name) + " frame was not clamped to the work area");
                 }
             }
         }
        }

        void RunSettingsLayoutGeometryScenario()
        {
         using hyperbrowse::ui::dialog_detail::ConsolidatedSettingsControl;
         using hyperbrowse::ui::dialog_detail::ConsolidatedSettingsPage;
         using hyperbrowse::ui::dialog_detail::MeasureSettingsLayout;

         constexpr std::array<UINT, 5> dpis{96, 120, 144, 168, 192};
         constexpr std::array<hyperbrowse::util::AppTextSize, 3> textSizes{
             hyperbrowse::util::AppTextSize::Small,
             hyperbrowse::util::AppTextSize::Medium,
             hyperbrowse::util::AppTextSize::Large};
         constexpr std::array<ConsolidatedSettingsPage, 5> pages{
             ConsolidatedSettingsPage::Slideshow,
             ConsolidatedSettingsPage::Viewer,
             ConsolidatedSettingsPage::Appearance,
             ConsolidatedSettingsPage::Performance,
             ConsolidatedSettingsPage::Behavior};
         constexpr std::array<ConsolidatedSettingsControl, 4> radioControls{
             ConsolidatedSettingsControl::ViewerWheelZoom,
             ConsolidatedSettingsControl::ViewerWheelNavigate,
             ConsolidatedSettingsControl::RawPreferRaw,
             ConsolidatedSettingsControl::RawPreferJpeg};

         for (const UINT dpi : dpis)
         {
             const int logicalWidth = MulDiv(1120, 96, static_cast<int>(dpi));
             const int logicalHeight = MulDiv(760, 96, static_cast<int>(dpi));
             const int narrowWidth = MulDiv(560, 96, static_cast<int>(dpi));
             const int shortHeight = MulDiv(360, 96, static_cast<int>(dpi));
             for (const auto textSize : textSizes)
             {
                 for (const auto page : pages)
                 {
                     const auto layout = MeasureSettingsLayout({
                         page,
                         textSize,
                         narrowWidth,
                         shortHeight,
                         [](std::wstring_view text)
                         {
                             return static_cast<int>(text.size()) * 8;
                         }});
                     const auto& metrics = layout.metrics;
                     Expect(metrics.bodyViewport.left >= metrics.left
                                && metrics.bodyViewport.right <= metrics.right
                                && metrics.bodyViewport.top <= metrics.bodyViewport.bottom
                                && metrics.bodyViewport.bottom <= metrics.footer.top,
                            "Settings layout produced an invalid body viewport");
                     Expect(layout.requiredContentHeight >= metrics.bodyViewport.top,
                            "Settings layout reported content shorter than its body origin");
                     Expect(layout.scrollExtent >= 0
                                && layout.requiresScroll == (layout.scrollExtent > 0),
                            "Settings layout reported an inconsistent scroll extent");

                     for (const RECT& tab : layout.tabRects)
                     {
                         Expect(tab.left >= 0 && tab.right <= metrics.width
                                    && tab.top >= 0 && tab.bottom <= metrics.contentTop
                                    && tab.right > tab.left && tab.bottom > tab.top,
                                "Settings layout produced an invalid tab rectangle");
                     }
                     for (const auto& label : layout.labels)
                     {
                         Expect(label.bounds.right > label.bounds.left
                                    && label.bounds.bottom > label.bounds.top
                                    && label.bounds.left >= metrics.left
                                    && label.bounds.right <= metrics.right
                                    && label.bounds.bottom <= layout.requiredContentHeight,
                                "Settings layout produced an invalid label rectangle");
                     }
                     for (const RECT& control : layout.controlRects)
                     {
                         if (control.right <= control.left || control.bottom <= control.top)
                         {
                             continue;
                         }
                         Expect(control.left >= metrics.left
                                    && control.right <= metrics.right
                                    && control.bottom <= layout.requiredContentHeight,
                                "Settings layout produced a control outside its content bounds");
                     }
                     for (const RECT& numeric : layout.numericEditRects)
                     {
                         if (numeric.right <= numeric.left || numeric.bottom <= numeric.top)
                         {
                             continue;
                         }
                         Expect(numeric.right > numeric.left
                                    && numeric.bottom > numeric.top
                                    && numeric.left >= metrics.left
                                    && numeric.right <= metrics.right
                                    && numeric.bottom <= layout.requiredContentHeight,
                                "Settings layout produced an invalid numeric field rectangle");
                     }
                     for (const ConsolidatedSettingsControl control : radioControls)
                     {
                         const RECT& radio = layout.controlRects[static_cast<std::size_t>(control)];
                         if (radio.right <= radio.left || radio.bottom <= radio.top)
                         {
                             continue;
                         }
                         Expect(radio.left >= metrics.left && radio.right <= metrics.right,
                                "Settings radio control escaped the available width");
                     }
                     if (page == ConsolidatedSettingsPage::Performance)
                     {
                         constexpr std::array<std::size_t, 4> numericIndices{2, 3, 4, 5};
                         constexpr std::array<ConsolidatedSettingsControl, 4> automaticControls{
                             ConsolidatedSettingsControl::ThumbnailCacheAutomatic,
                             ConsolidatedSettingsControl::MetadataCacheAutomatic,
                             ConsolidatedSettingsControl::PrefetchDepthAutomatic,
                             ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic};
                         for (std::size_t index = 0; index < numericIndices.size(); ++index)
                         {
                             const RECT& numeric = layout.numericEditRects[numericIndices[index]];
                             const RECT& automatic = layout.controlRects[static_cast<std::size_t>(automaticControls[index])];
                             Expect(automatic.left >= numeric.right
                                        && automatic.right <= metrics.right
                                        && automatic.top <= numeric.bottom
                                        && automatic.bottom >= numeric.top,
                                    "Settings automatic control was not placed beside its numeric spinner");
                         }
                         for (const auto& label : layout.labels)
                         {
                             Expect(label.text != L"Live cache status",
                                    "Settings Performance page still contains the removed live cache status section");
                         }
                         Expect(layout.requiresScroll,
                                "Short Settings work area did not request scrolling for Performance");
                     }
                 }

                 std::array<int, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> slideshowChoiceWidths{};
                 slideshowChoiceWidths[static_cast<std::size_t>(ConsolidatedSettingsControl::TransitionStyle)] = 250;
                 const auto slideshowLayout = MeasureSettingsLayout({
                     ConsolidatedSettingsPage::Slideshow,
                     textSize,
                     logicalWidth,
                     logicalHeight,
                     [](std::wstring_view text)
                     {
                         return static_cast<int>(text.size()) * 8;
                     },
                     slideshowChoiceWidths});
                 const RECT& transitionStyle = slideshowLayout.controlRects[
                     static_cast<std::size_t>(ConsolidatedSettingsControl::TransitionStyle)];
                 const int expectedChoiceHeight = hyperbrowse::util::ScaleAppTextDimension(46, textSize);
                 Expect(transitionStyle.right > transitionStyle.left
                            && transitionStyle.right - transitionStyle.left <= 250,
                        "Transition style selector exceeded its requested compact width");
                 Expect(transitionStyle.bottom - transitionStyle.top == expectedChoiceHeight,
                        "Transition style selector did not receive the taller choice-control height");
                 Expect(transitionStyle.top >= slideshowLayout.metrics.bodyViewport.top,
                        "Transition style selector escaped the top of its scroll viewport");
                 std::array<int, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> viewerChoiceWidths{};
                 viewerChoiceWidths[static_cast<std::size_t>(ConsolidatedSettingsControl::OverlayTextSize)] = 180;
                 viewerChoiceWidths[static_cast<std::size_t>(ConsolidatedSettingsControl::EscapeKeyBehavior)] = 180;
                 const auto viewerChoiceLayout = MeasureSettingsLayout({
                     ConsolidatedSettingsPage::Viewer,
                     textSize,
                     logicalWidth,
                     logicalHeight,
                     [](std::wstring_view text)
                     {
                         return static_cast<int>(text.size()) * 8;
                     },
                     viewerChoiceWidths});
                 const RECT& escapeKeyBehavior = viewerChoiceLayout.controlRects[
                     static_cast<std::size_t>(ConsolidatedSettingsControl::EscapeKeyBehavior)];
                 const RECT& overlayTextSize = viewerChoiceLayout.controlRects[
                     static_cast<std::size_t>(ConsolidatedSettingsControl::OverlayTextSize)];
                 Expect(overlayTextSize.right - overlayTextSize.left
                            == escapeKeyBehavior.right - escapeKeyBehavior.left,
                        "Viewer choice selectors did not use the same width");
                 Expect(overlayTextSize.bottom - overlayTextSize.top == expectedChoiceHeight
                            && escapeKeyBehavior.bottom - escapeKeyBehavior.top == expectedChoiceHeight,
                        "Viewer choice selectors did not use the taller choice-control height");
                 Expect(escapeKeyBehavior.right > escapeKeyBehavior.left
                            && escapeKeyBehavior.right - escapeKeyBehavior.left <= 180,
                        "Settings choice selector exceeded its requested compact width");
                 std::array<int, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> appearanceChoiceWidths{};
                 appearanceChoiceWidths[static_cast<std::size_t>(ConsolidatedSettingsControl::AppTextSize)] = 180;
                 appearanceChoiceWidths[static_cast<std::size_t>(ConsolidatedSettingsControl::ThumbnailSize)] = 180;
                 const auto appearanceChoiceLayout = MeasureSettingsLayout({
                     ConsolidatedSettingsPage::Appearance,
                     textSize,
                     logicalWidth,
                     logicalHeight,
                     [](std::wstring_view text)
                     {
                         return static_cast<int>(text.size()) * 8;
                     },
                     appearanceChoiceWidths});
                 const RECT& appTextSize = appearanceChoiceLayout.controlRects[
                     static_cast<std::size_t>(ConsolidatedSettingsControl::AppTextSize)];
                 const RECT& thumbnailSize = appearanceChoiceLayout.controlRects[
                     static_cast<std::size_t>(ConsolidatedSettingsControl::ThumbnailSize)];
                 Expect(appTextSize.right - appTextSize.left == thumbnailSize.right - thumbnailSize.left,
                        "Appearance choice selectors did not use the same width");
                 Expect(appTextSize.bottom - appTextSize.top == expectedChoiceHeight
                            && thumbnailSize.bottom - thumbnailSize.top == expectedChoiceHeight,
                        "Appearance choice selectors did not use the taller choice-control height");
                 const auto behaviorLayout = MeasureSettingsLayout({
                     ConsolidatedSettingsPage::Behavior,
                     textSize,
                     logicalWidth,
                     logicalHeight,
                     [](std::wstring_view text)
                     {
                         return static_cast<int>(text.size()) * 8;
                     }});
                 const RECT& quickSendOrder = behaviorLayout.controlRects[
                     static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)];
                 const int quickSendAvailableWidth = quickSendOrder.left == behaviorLayout.metrics.left
                     ? behaviorLayout.metrics.right - behaviorLayout.metrics.left
                     : behaviorLayout.metrics.right - quickSendOrder.left;
                 Expect(quickSendOrder.right - quickSendOrder.left
                            == std::max(1, quickSendAvailableWidth / 4),
                        "Quick Actions shortcut order field did not use one quarter of its original width");
                 constexpr std::array<std::wstring_view, 2> slideshowDurationLabels{
                     L"Slide duration (milliseconds)",
                     L"Transition duration (milliseconds)"};
                 for (std::size_t index = 0; index < slideshowDurationLabels.size(); ++index)
                 {
                     const RECT& numeric = slideshowLayout.numericEditRects[index];
                     Expect(numeric.bottom - numeric.top == transitionStyle.bottom - transitionStyle.top,
                            "Slideshow numeric field height did not match the transition style selector");
                     const auto label = std::find_if(
                         slideshowLayout.labels.begin(),
                         slideshowLayout.labels.end(),
                         [expectedText = slideshowDurationLabels[index]](const auto& candidate)
                         {
                             return candidate.text == expectedText;
                         });
                     Expect(label != slideshowLayout.labels.end(),
                            "Settings layout omitted a slideshow duration label");
                     if (label != slideshowLayout.labels.end())
                     {
                         if (numeric.left > label->bounds.left)
                         {
                             const LONG centerDifference = numeric.top + numeric.bottom
                                 - label->bounds.top - label->bounds.bottom;
                             Expect(centerDifference >= -1 && centerDifference <= 1,
                                    "Slideshow numeric field was not vertically centered with its label");
                         }
                         else
                         {
                             Expect(numeric.top >= label->bounds.bottom + slideshowLayout.metrics.rowGap,
                                    "Stacked slideshow numeric field overlapped its label");
                         }
                     }
                 }

                 const auto fullLayout = MeasureSettingsLayout({
                     ConsolidatedSettingsPage::Viewer,
                     textSize,
                     logicalWidth,
                     logicalHeight,
                     [](std::wstring_view text)
                     {
                         return static_cast<int>(text.size()) * 8;
                     }});
                 Expect(fullLayout.metrics.footer.top > fullLayout.metrics.bodyViewport.top,
                        "Settings footer was placed before the body");
             }
         }
        }

        void RunDefaultSettingsScenario(HINSTANCE instance)
    {
        using hyperbrowse::ui::command_ids::ID_VIEW_SETTINGS;

        ScopedMemorySnapshotOverride memorySnapshotOverride({
            16ULL * 1024ULL * 1024ULL * 1024ULL,
            8ULL * 1024ULL * 1024ULL * 1024ULL});
        constexpr wchar_t kDialogClassName[] = L"HyperBrowseExperimentalSettingsDialog";
        ScopedRegistryDwordBackup appTextSizeBackup(kRegistryPath, kRegistryValueAppTextSize);
        ScopedRegistryDwordBackup slideshowTransitionBackup(kRegistryPath, kRegistryValueUseSlideshowTransition);
        ScopedRegistryDwordBackup thumbnailCacheBackup(kRegistryPath, kRegistryValueThumbnailCacheCapacityOverrideBytes);
        ScopedRegistryDwordBackup metadataCacheBackup(kRegistryPath, kRegistryValueMetadataCacheCapacityOverrideEntries);
        ScopedRegistryDwordBackup prefetchDepthBackup(kRegistryPath, kRegistryValuePrefetchDepthOverride);
        DeleteRegistryValue(kRegistryPath, kRegistryValueAppTextSize);
        DeleteRegistryValue(kRegistryPath, kRegistryValueThumbnailCacheCapacityOverrideBytes);
        DeleteRegistryValue(kRegistryPath, kRegistryValueMetadataCacheCapacityOverrideEntries);
        SetRegistryDwordValue(kRegistryPath, kRegistryValueUseSlideshowTransition, 0);
        SetRegistryDwordValue(kRegistryPath, kRegistryValuePrefetchDepthOverride, 7);

        hyperbrowse::ui::MainWindow mainWindow(instance);
        Expect(mainWindow.Create(), "Failed to create MainWindow for experimental-settings smoke coverage");
        std::atomic_bool done{false};
        std::string failure;
        std::thread worker([&]()
        {
            ComScope workerComScope;
            if (!PostMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_SETTINGS, 0), 0))
            {
                failure = "Failed to post the experimental Settings command";
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
                failure = "Experimental Settings dialog did not open";
                done.store(true, std::memory_order_release);
                return;
            }
            const auto failAndClose = [&](std::string message)
            {
                failure = std::move(message);
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                done.store(true, std::memory_order_release);
            };
            const LRESULT settingsObjectResult = SendMessageW(dialog, WM_GETOBJECT, 0, OBJID_CLIENT);
            if (settingsObjectResult == 0)
            {
                failAndClose("Experimental Settings did not return an accessibility object");
                return;
            }
            IAccessible* settingsAccessible = nullptr;
            const HRESULT settingsObjectStatus = ObjectFromLresult(
                settingsObjectResult,
                IID_IAccessible,
                0,
                reinterpret_cast<void**>(&settingsAccessible));
            if (FAILED(settingsObjectStatus) || !settingsAccessible)
            {
                failAndClose("Could not materialize the Experimental Settings accessibility object");
                return;
            }
            long settingsChildCount = 0;
            bool foundSelectedSlideshowTab = false;
            bool foundTransitionStyle = false;
            bool foundApplyAction = false;
            if (FAILED(settingsAccessible->get_accChildCount(&settingsChildCount)) || settingsChildCount < 11)
            {
                settingsAccessible->Release();
                failAndClose("Experimental Settings accessibility object omitted semantic children");
                return;
            }
            for (long childId = 1; childId <= settingsChildCount; ++childId)
            {
                VARIANT child{};
                child.vt = VT_I4;
                child.lVal = childId;
                BSTR name = nullptr;
                VARIANT role{};
                VARIANT accessibleState{};
                if (FAILED(settingsAccessible->get_accName(child, &name))
                    || !name
                    || FAILED(settingsAccessible->get_accRole(child, &role))
                    || role.vt != VT_I4
                    || FAILED(settingsAccessible->get_accState(child, &accessibleState))
                    || accessibleState.vt != VT_I4)
                {
                    if (name)
                    {
                        SysFreeString(name);
                    }
                    VariantClear(&role);
                    VariantClear(&accessibleState);
                    settingsAccessible->Release();
                    failAndClose("Experimental Settings accessibility child omitted its name, role, or state");
                    return;
                }
                const std::wstring accessibleName(name, SysStringLen(name));
                foundSelectedSlideshowTab = foundSelectedSlideshowTab
                    || (accessibleName == L"Slideshow tab"
                        && role.lVal == ROLE_SYSTEM_PAGETAB
                        && (accessibleState.lVal & STATE_SYSTEM_SELECTED) != 0);
                foundTransitionStyle = foundTransitionStyle
                    || (accessibleName == L"Transition style" && role.lVal == ROLE_SYSTEM_COMBOBOX);
                foundApplyAction = foundApplyAction
                    || (accessibleName == L"Apply" && role.lVal == ROLE_SYSTEM_PUSHBUTTON);
                SysFreeString(name);
                VariantClear(&role);
                VariantClear(&accessibleState);
            }
            settingsAccessible->Release();
            if (!foundSelectedSlideshowTab || !foundTransitionStyle || !foundApplyAction)
            {
                failAndClose("Experimental Settings accessibility tree did not expose its selected tab, field, and footer action");
                return;
            }
            if (GetDlgItem(dialog, 5601) != nullptr)
            {
                failAndClose("Experimental Settings unexpectedly created a native OK button");
                return;
            }
            HWND transitionCombo = nullptr;
            HWND overlayTextCombo = nullptr;
            HWND appTextCombo = nullptr;
            HWND thumbnailCombo = nullptr;
            HWND resourceCombo = nullptr;
            HWND escapeBehaviorCombo = nullptr;
            const ULONGLONG controlsDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < controlsDeadline)
            {
                transitionCombo = GetDlgItem(dialog, 5801);
                overlayTextCombo = GetDlgItem(dialog, 5815);
                appTextCombo = GetDlgItem(dialog, 5818);
                thumbnailCombo = GetDlgItem(dialog, 5819);
                resourceCombo = GetDlgItem(dialog, 5823);
                escapeBehaviorCombo = GetDlgItem(dialog, 5836);
                if (transitionCombo && overlayTextCombo && appTextCombo && thumbnailCombo && resourceCombo && escapeBehaviorCombo
                    && SendMessageW(transitionCombo, CB_GETCOUNT, 0, 0) >= 2
                    && SendMessageW(overlayTextCombo, CB_GETCOUNT, 0, 0) == 3
                    && SendMessageW(appTextCombo, CB_GETCOUNT, 0, 0) == 3
                    && SendMessageW(thumbnailCombo, CB_GETCOUNT, 0, 0) >= 2
                    && SendMessageW(resourceCombo, CB_GETCOUNT, 0, 0) == 4
                    && SendMessageW(escapeBehaviorCombo, CB_GETCOUNT, 0, 0) == 4)
                {
                    break;
                }
                Sleep(10);
            }
            if (!transitionCombo || !overlayTextCombo || !appTextCombo || !thumbnailCombo || !resourceCombo || !escapeBehaviorCombo
                || SendMessageW(transitionCombo, CB_GETCOUNT, 0, 0) < 2
                || SendMessageW(overlayTextCombo, CB_GETCOUNT, 0, 0) != 3
                || SendMessageW(appTextCombo, CB_GETCOUNT, 0, 0) != 3
                || SendMessageW(thumbnailCombo, CB_GETCOUNT, 0, 0) < 2
                || SendMessageW(resourceCombo, CB_GETCOUNT, 0, 0) != 4
                || SendMessageW(escapeBehaviorCombo, CB_GETCOUNT, 0, 0) != 4)
            {
                failAndClose("Experimental Settings did not create/populate all native combo boxes (transition="
                    + std::to_string(transitionCombo != nullptr) + ", overlay="
                    + std::to_string(overlayTextCombo != nullptr) + ", app="
                    + std::to_string(appTextCombo != nullptr) + ", thumbnail="
                    + std::to_string(thumbnailCombo != nullptr) + ", resource="
                    + std::to_string(resourceCombo != nullptr) + ", escape="
                    + std::to_string(escapeBehaviorCombo != nullptr) + ")");
                return;
            }
            const HWND comboControls[] = {
                transitionCombo,
                overlayTextCombo,
                appTextCombo,
                thumbnailCombo,
                resourceCombo,
                escapeBehaviorCombo,
            };
            for (HWND combo : comboControls)
            {
                const LONG_PTR style = GetWindowLongPtrW(combo, GWL_STYLE);
                if ((style & CBS_DROPDOWNLIST) == 0 || (style & CBS_OWNERDRAWFIXED) == 0 || (style & CBS_HASSTRINGS) == 0)
                {
                    failAndClose("Experimental Settings combo box did not retain owner-draw list styles (style="
                        + std::to_string(static_cast<unsigned long long>(style)) + ")");
                    return;
                }
                MEASUREITEMSTRUCT measureItem{};
                measureItem.CtlType = ODT_COMBOBOX;
                SendMessageW(dialog, WM_MEASUREITEM, static_cast<WPARAM>(GetDlgCtrlID(combo)), reinterpret_cast<LPARAM>(&measureItem));
                if (measureItem.itemHeight == 0)
                {
                    failAndClose("Experimental Settings combo box did not provide an owner-draw item height");
                    return;
                }
            }
            SendMessageW(transitionCombo, CB_SHOWDROPDOWN, TRUE, 0);
            if (SendMessageW(transitionCombo, CB_GETDROPPEDSTATE, 0, 0) == FALSE)
            {
                failAndClose("Experimental Settings transition combo did not open a dropdown list");
                return;
            }
            SendMessageW(transitionCombo, CB_SHOWDROPDOWN, FALSE, 0);

            const HWND slideDurationEdit = GetDlgItem(dialog, 5700);
            const HWND transitionDurationEdit = GetDlgItem(dialog, 5701);
            const DWORD dialogThreadId = GetWindowThreadProcessId(dialog, nullptr);
            const auto focusedWindow = [&]() -> HWND
            {
                GUITHREADINFO threadInfo{sizeof(threadInfo)};
                return GetGUIThreadInfo(dialogThreadId, &threadInfo) != FALSE ? threadInfo.hwndFocus : nullptr;
            };
            const auto waitForFocus = [&](HWND expected)
            {
                const ULONGLONG focusDeadline = GetTickCount64() + 2000;
                while (GetTickCount64() < focusDeadline && focusedWindow() != expected)
                {
                    Sleep(10);
                }
                return focusedWindow() == expected;
            };
            if (!waitForFocus(transitionCombo))
            {
                failAndClose("Experimental Settings did not give initial focus to the transition field");
                return;
            }
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_TAB, 0)
                || !waitForFocus(slideDurationEdit))
            {
                failAndClose("Tab did not move from the transition field to the first numeric field");
                return;
            }
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_TAB, 0)
                || !waitForFocus(transitionDurationEdit))
            {
                failAndClose("Tab did not move between Experimental Settings numeric fields");
                return;
            }
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_TAB, 0)
                || !waitForFocus(dialog)
                || !PostMessageW(dialog, WM_KEYDOWN, VK_SPACE, 0))
            {
                failAndClose("Tab did not reach or Space did not activate the Experimental Settings Apply action");
                return;
            }
            Sleep(50);
            if (!FindWindowW(kDialogClassName, nullptr))
            {
                failAndClose("Space unexpectedly closed Experimental Settings from the Apply action");
                return;
            }

            const HWND thumbnailCacheEdit = GetDlgItem(dialog, 5702);
            const HWND metadataCacheEdit = GetDlgItem(dialog, 5703);
            const HWND prefetchDepthEdit = GetDlgItem(dialog, 5704);
            const HWND persistentCacheEdit = GetDlgItem(dialog, 5705);
            const HWND slideDurationSpin = GetDlgItem(dialog, 5850);
            const HWND transitionDurationSpin = GetDlgItem(dialog, 5851);
            const HWND thumbnailCacheSpin = GetDlgItem(dialog, 5852);
            const HWND metadataCacheSpin = GetDlgItem(dialog, 5853);
            const HWND prefetchDepthSpin = GetDlgItem(dialog, 5854);
            const HWND persistentCacheSpin = GetDlgItem(dialog, 5855);
            const HWND thumbnailCacheSlider = GetDlgItem(dialog, 5872);
            const HWND metadataCacheSlider = GetDlgItem(dialog, 5873);
            const HWND prefetchDepthSlider = GetDlgItem(dialog, 5874);
            const HWND persistentCacheSlider = GetDlgItem(dialog, 5875);
            if (!slideDurationEdit || !transitionDurationEdit || !thumbnailCacheEdit || !metadataCacheEdit
                || !prefetchDepthEdit || !persistentCacheEdit || !slideDurationSpin || !transitionDurationSpin || !thumbnailCacheSpin
                || !metadataCacheSpin || !prefetchDepthSpin || !persistentCacheSpin || !thumbnailCacheSlider || !metadataCacheSlider
                || !prefetchDepthSlider || !persistentCacheSlider
                || reinterpret_cast<HWND>(SendMessageW(slideDurationSpin, UDM_GETBUDDY, 0, 0)) != slideDurationEdit
                || reinterpret_cast<HWND>(SendMessageW(transitionDurationSpin, UDM_GETBUDDY, 0, 0)) != transitionDurationEdit
                || reinterpret_cast<HWND>(SendMessageW(thumbnailCacheSpin, UDM_GETBUDDY, 0, 0)) != thumbnailCacheEdit
                || reinterpret_cast<HWND>(SendMessageW(metadataCacheSpin, UDM_GETBUDDY, 0, 0)) != metadataCacheEdit
                || reinterpret_cast<HWND>(SendMessageW(prefetchDepthSpin, UDM_GETBUDDY, 0, 0)) != prefetchDepthEdit
                || reinterpret_cast<HWND>(SendMessageW(persistentCacheSpin, UDM_GETBUDDY, 0, 0)) != persistentCacheEdit)
            {
                failAndClose("Experimental Settings numeric fields did not create native spin buddies");
                return;
            }
            const auto hasNoTickMarks = [](HWND slider)
            {
                const LRESULT tickCount = SendMessageW(slider, TBM_GETNUMTICS, 0, 0);
                const LONG_PTR style = GetWindowLongPtrW(slider, GWL_STYLE);
                return (style & TBS_AUTOTICKS) == 0 && tickCount >= 0 && tickCount <= 2;
            };
            if (SendMessageW(thumbnailCacheSlider, TBM_GETRANGEMIN, 0, 0) != 0
                || SendMessageW(thumbnailCacheSlider, TBM_GETRANGEMAX, 0, 0) != 100
                || SendMessageW(metadataCacheSlider, TBM_GETRANGEMIN, 0, 0) != 0
                || SendMessageW(metadataCacheSlider, TBM_GETRANGEMAX, 0, 0) != 100
                || SendMessageW(prefetchDepthSlider, TBM_GETRANGEMIN, 0, 0) != 0
                || SendMessageW(prefetchDepthSlider, TBM_GETRANGEMAX, 0, 0) != 100
                || SendMessageW(persistentCacheSlider, TBM_GETRANGEMIN, 0, 0) != 0
                || SendMessageW(persistentCacheSlider, TBM_GETRANGEMAX, 0, 0) != 100)
            {
                failAndClose("Experimental Settings performance sliders did not retain their normalized range");
                return;
            }
            if (!hasNoTickMarks(thumbnailCacheSlider)
                || !hasNoTickMarks(metadataCacheSlider)
                || !hasNoTickMarks(prefetchDepthSlider)
                || !hasNoTickMarks(persistentCacheSlider))
            {
                failAndClose("Experimental Settings performance sliders still displayed tick marks");
                return;
            }
            Sleep(2200);
            if (FindWindowW(nullptr, L"Persistent Thumbnail Cache") != nullptr)
            {
                failAndClose("Performance feedback refresh unexpectedly opened the persistent cache dialog");
                return;
            }
            int slideMinimum = 0;
            int slideMaximum = 0;
            SendMessageW(slideDurationSpin, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&slideMinimum), reinterpret_cast<LPARAM>(&slideMaximum));
            if (slideMinimum != 250 || slideMaximum != 60000)
            {
                failAndClose("Experimental Settings duration spin range was not configured");
                return;
            }
            const auto readControlText = [](HWND control)
            {
                wchar_t buffer[64]{};
                GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
                return std::wstring(buffer);
            };
            int prefetchMinimum = 0;
            int prefetchMaximum = 0;
            SendMessageW(prefetchDepthSpin, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&prefetchMinimum), reinterpret_cast<LPARAM>(&prefetchMaximum));
            if (prefetchMinimum != hyperbrowse::util::kMinimumPrefetchDepth
                || prefetchMaximum != hyperbrowse::util::kMaximumPrefetchDepth
                || readControlText(prefetchDepthEdit) != L"7")
            {
                failAndClose("Experimental Settings prefetch depth did not restore its explicit override and range");
                return;
            }
            const auto selectedProfile = static_cast<hyperbrowse::util::ResourceProfile>(
                SendMessageW(resourceCombo, CB_GETCURSEL, 0, 0));
            const std::wstring expectedThumbnailCache = std::to_wstring(
                hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(0, selectedProfile)
                / (1024ULL * 1024ULL));
            const std::wstring expectedMetadataCache = std::to_wstring(
                hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(0, selectedProfile));
            if (readControlText(thumbnailCacheEdit) != expectedThumbnailCache
                || readControlText(metadataCacheEdit) != expectedMetadataCache)
            {
                failAndClose("Experimental Settings did not populate automatic cache values from the selected profile");
                return;
            }
            SendMessageW(resourceCombo, CB_SETCURSEL, 0, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(5823, CBN_SELCHANGE), reinterpret_cast<LPARAM>(resourceCombo));
            const std::wstring expectedConservativeThumbnailCache = std::to_wstring(
                hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(
                    0,
                    hyperbrowse::util::ResourceProfile::Conservative)
                / (1024ULL * 1024ULL));
            const std::wstring expectedConservativeMetadataCache = std::to_wstring(
                hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(
                    0,
                    hyperbrowse::util::ResourceProfile::Conservative));
            if (readControlText(thumbnailCacheEdit) != expectedConservativeThumbnailCache
                || readControlText(metadataCacheEdit) != expectedConservativeMetadataCache)
            {
                failAndClose("Experimental Settings did not refresh automatic cache values after changing profile");
                return;
            }

            RECT client{};
            GetClientRect(dialog, &client);
            const int tabWidth = (client.right - 56) / 5;
            SendMessageW(dialog, WM_SYSKEYDOWN, L'V', 0);
            auto* experimentalState = reinterpret_cast<hyperbrowse::ui::dialog_detail::ExperimentalSettingsDialogState*>(
                GetWindowLongPtrW(dialog, GWLP_USERDATA));
            if (!experimentalState || !waitForFocus(dialog)
                || experimentalState->page != hyperbrowse::ui::dialog_detail::ConsolidatedSettingsPage::Viewer)
            {
                failAndClose("Alt+V did not select the Viewer page in Experimental Settings");
                return;
            }
            SendMessageW(dialog, WM_SYSKEYDOWN, L'P', 0);
            if (experimentalState->page != hyperbrowse::ui::dialog_detail::ConsolidatedSettingsPage::Performance)
            {
                failAndClose("Alt+P did not select the Performance page in Experimental Settings");
                return;
            }
            if (experimentalState->scrollExtent > 0)
            {
                SendMessageW(dialog, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
            }
            const RECT& lastPerformanceControl = experimentalState->controlRects[
                static_cast<std::size_t>(hyperbrowse::ui::dialog_detail::ConsolidatedSettingsControl::LibRawOutOfProcess)];
            if (lastPerformanceControl.top < experimentalState->bodyViewport.top
                || lastPerformanceControl.bottom > experimentalState->bodyViewport.bottom
                || experimentalState->bodyViewport.bottom + 12 > experimentalState->applyButtonRect.top)
            {
                failAndClose("Experimental Settings Performance controls were not scrollable above its footer actions");
                return;
            }
            SendMessageW(dialog, WM_SYSKEYDOWN, L'V', 0);
            SendMessageW(dialog, WM_SYSKEYDOWN, L'U', 0);
            if (experimentalState->focusedTarget.kind
                    != hyperbrowse::ui::dialog_detail::ExperimentalSettingsFocusTargetKind::CustomControl
                || experimentalState->focusedTarget.index != 0)
            {
                failAndClose("Alt+U did not focus the custom Viewer transition setting");
                return;
            }
            SendMessageW(dialog, WM_KEYDOWN, VK_SPACE, 0);
            Sleep(50);
            if (!experimentalState->settings || !experimentalState->settings->useSlideshowTransition)
            {
                failAndClose("Space did not toggle the custom Viewer transition setting");
                return;
            }
            const UINT dialogDpi = hyperbrowse::ui::DialogDpiForWindow(dialog);
            const RECT applyButton = experimentalState->applyButtonRect;
            const int applyX = hyperbrowse::ui::ScaleDialogDimension((applyButton.left + applyButton.right) / 2, dialogDpi);
            const int applyY = hyperbrowse::ui::ScaleDialogDimension((applyButton.top + applyButton.bottom) / 2, dialogDpi);
            SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(applyX, applyY));
            SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(applyX, applyY));
            DWORD transitionEnabled = 0;
            if (!TryReadRegistryDwordValue(kRegistryPath, kRegistryValueUseSlideshowTransition, &transitionEnabled)
                || transitionEnabled == 0)
            {
                failAndClose("Custom Viewer setting activation did not persist through Apply (value="
                    + std::to_string(transitionEnabled) + ")");
                return;
            }

            for (int index = 0; index < 5; ++index)
            {
                const int x = 28 + (index * tabWidth) + (tabWidth / 2);
                SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 36));
                SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(x, 36));
                PumpMessagesFor(20);
                if (!FindWindowW(kDialogClassName, nullptr))
                {
                    failAndClose("Experimental Settings closed while switching pages");
                    return;
                }
            }

            const int appearanceTabX = 28 + (2 * tabWidth) + (tabWidth / 2);
            SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(appearanceTabX, 36));
            SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(appearanceTabX, 36));
            RECT mediumTextFrame{};
            if (!GetWindowRect(dialog, &mediumTextFrame))
            {
                failAndClose("Could not measure the Experimental Settings frame before text-size reflow");
                return;
            }
            const auto fontHeight = [](HWND control) -> LONG
            {
                const HFONT font = control
                    ? reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0))
                    : nullptr;
                LOGFONTW logFont{};
                return font && GetObjectW(font, sizeof(logFont), &logFont) == sizeof(logFont)
                    ? logFont.lfHeight
                    : 0;
            };
            const LONG mediumComboFontHeight = fontHeight(appTextCombo);
            const LONG mediumEditFontHeight = fontHeight(GetDlgItem(dialog, 5700));
            if (mediumComboFontHeight >= 0
                || mediumEditFontHeight >= 0
                || mediumComboFontHeight != mediumEditFontHeight)
            {
                failAndClose("Experimental Settings combo and numeric controls did not share their Medium font size");
                return;
            }
            SendMessageW(appTextCombo, CB_SETCURSEL, 2, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(5818, CBN_SELCHANGE), reinterpret_cast<LPARAM>(appTextCombo));
            const LONG largeComboFontHeight = fontHeight(appTextCombo);
            const LONG largeEditFontHeight = fontHeight(GetDlgItem(dialog, 5700));
            if (largeComboFontHeight >= mediumComboFontHeight
                || largeEditFontHeight >= mediumEditFontHeight
                || largeComboFontHeight != largeEditFontHeight)
            {
                failAndClose("Experimental Settings combo and numeric controls did not share their larger font size");
                return;
            }
            RECT largeTextFrame{};
            MONITORINFO textSizeMonitorInfo{sizeof(MONITORINFO)};
            const HMONITOR textSizeMonitor = MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST);
            if (!GetWindowRect(dialog, &largeTextFrame)
                || !textSizeMonitor
                || !GetMonitorInfoW(textSizeMonitor, &textSizeMonitorInfo))
            {
                failAndClose("Could not measure the Experimental Settings work area after Large text reflow");
                return;
            }
            const LONG mediumFrameHeight = mediumTextFrame.bottom - mediumTextFrame.top;
            const LONG largeFrameHeight = largeTextFrame.bottom - largeTextFrame.top;
            const LONG availableWorkAreaHeight = textSizeMonitorInfo.rcWork.bottom - textSizeMonitorInfo.rcWork.top;
            const bool frameGrew = largeFrameHeight > mediumFrameHeight;
            const bool frameReachedWorkAreaLimit = largeFrameHeight == availableWorkAreaHeight
                && mediumFrameHeight == largeFrameHeight;
            if (!frameGrew && !frameReachedWorkAreaLimit)
            {
                failAndClose("Experimental Settings large-text frame neither grew nor reached the work-area limit");
                return;
            }
            const RECT resizedApplyButton = experimentalState->applyButtonRect;
            const int resizedApplyX = hyperbrowse::ui::ScaleDialogDimension(
                (resizedApplyButton.left + resizedApplyButton.right) / 2,
                dialogDpi);
            const int resizedApplyY = hyperbrowse::ui::ScaleDialogDimension(
                (resizedApplyButton.top + resizedApplyButton.bottom) / 2,
                dialogDpi);
            SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(resizedApplyX, resizedApplyY));
            SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(resizedApplyX, resizedApplyY));
            PumpMessagesFor(100);
            if (mainWindow.AppTextSize() != hyperbrowse::util::AppTextSize::Large || !FindWindowW(kDialogClassName, nullptr))
            {
                failAndClose("Experimental Settings Apply did not update the live setting while keeping the dialog open");
                return;
            }
            SendMessageW(appTextCombo, CB_SETCURSEL, 1, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(5818, CBN_SELCHANGE), reinterpret_cast<LPARAM>(appTextCombo));
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_TAB, 0))
            {
                failAndClose("Failed to advance from Apply to OK in Experimental Settings");
                return;
            }
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_RETURN, 0))
            {
                failAndClose("Failed to post Enter to Experimental Settings");
                return;
            }
            const ULONGLONG enterDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < enterDeadline && FindWindowW(kDialogClassName, nullptr))
            {
                Sleep(10);
            }
            if (FindWindowW(kDialogClassName, nullptr)
                || mainWindow.AppTextSize() != hyperbrowse::util::AppTextSize::Medium)
            {
                failure = "Experimental Settings Enter did not apply and close the dialog";
                done.store(true, std::memory_order_release);
                return;
            }
            DWORD persistedPrefetchDepth = 0;
            if (!TryReadRegistryDwordValue(kRegistryPath, kRegistryValuePrefetchDepthOverride, &persistedPrefetchDepth)
                || persistedPrefetchDepth != 7)
            {
                failure = "Experimental Settings did not preserve the explicit prefetch depth override";
                done.store(true, std::memory_order_release);
                return;
            }

            if (!PostMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_SETTINGS, 0), 0))
            {
                failure = "Failed to reopen Experimental Settings for Escape coverage";
                done.store(true, std::memory_order_release);
                return;
            }
            dialog = nullptr;
            const ULONGLONG reopenDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < reopenDeadline && !(dialog = FindWindowW(kDialogClassName, nullptr)))
            {
                Sleep(10);
            }
            if (!dialog)
            {
                failure = "Experimental Settings did not reopen for Escape coverage";
                done.store(true, std::memory_order_release);
                return;
            }
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0))
            {
                failure = "Failed to post Escape to Experimental Settings";
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                done.store(true, std::memory_order_release);
                return;
            }
            const ULONGLONG escapeDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < escapeDeadline && FindWindowW(kDialogClassName, nullptr))
            {
                Sleep(10);
            }
            if (FindWindowW(kDialogClassName, nullptr))
            {
                failure = "Experimental Settings Escape did not close the dialog";
            }
            done.store(true, std::memory_order_release);
        });

        Expect(PumpMessagesUntil([&]() { return done.load(std::memory_order_acquire); }, 15000),
               "Experimental Settings interaction timed out");
        worker.join();
        Expect(failure.empty(), failure.empty() ? "Experimental Settings interaction failed" : failure.c_str());
        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }
namespace
{

    struct ColorMenuProbe
    {
        bool found{};
        bool checked{};
        bool accessibleFound{};
        bool accessibleChecked{};
        bool accessibleRole{};
        ULONGLONG deadline{};
    };

    LRESULT CALLBACK ColorMenuProbeSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR subclassId, DWORD_PTR reference)
    {
        auto& probe = *reinterpret_cast<ColorMenuProbe*>(reference);
        if (message == WM_TIMER && wParam == subclassId)
        {
            HWND popup = nullptr;
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM context)
            {
                wchar_t className[32]{};
                MENUITEMINFOW item{};
                item.cbSize = sizeof(item);
                item.fMask = MIIM_STATE;
                const HMENU menu = reinterpret_cast<HMENU>(SendMessageW(window, MN_GETHMENU, 0, 0));
                if (GetClassNameW(window, className, static_cast<int>(std::size(className)))
                    && _wcsicmp(className, L"#32768") == 0
                    && GetMenuItemInfoW(menu, hyperbrowse::ui::command_ids::ID_VIEW_COLOR_MANAGEMENT, FALSE, &item))
                {
                    *reinterpret_cast<HWND*>(context) = window;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&popup));
            if (!popup)
            {
                if (GetTickCount64() >= probe.deadline) PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
                return DefSubclassProc(hwnd, message, wParam, lParam);
            }
            MENUITEMINFOW item{};
            item.cbSize = sizeof(item);
            item.fMask = MIIM_STATE;
            const HMENU popupMenu = reinterpret_cast<HMENU>(SendMessageW(popup, MN_GETHMENU, 0, 0));
            GetMenuItemInfoW(popupMenu, hyperbrowse::ui::command_ids::ID_VIEW_COLOR_MANAGEMENT, FALSE, &item);
            probe.found = true;
            probe.checked = (item.fState & MFS_CHECKED) != 0;
            Microsoft::WRL::ComPtr<IAccessible> menu;
            if (SUCCEEDED(AccessibleObjectFromWindow(popup, static_cast<DWORD>(OBJID_CLIENT),
                                                    IID_IAccessible, reinterpret_cast<void**>(menu.GetAddressOf()))))
            {
                long children = 0;
                menu->get_accChildCount(&children);
                for (long index = 1; index <= children; ++index)
                {
                    VARIANT child{};
                    child.vt = VT_I4;
                    child.lVal = index;
                    BSTR name = nullptr;
                    menu->get_accName(child, &name);
                    const bool isColor = name && std::wstring_view(name, SysStringLen(name)).find(L"Color Management") != std::wstring_view::npos;
                    SysFreeString(name);
                    if (!isColor) continue;
                    probe.accessibleFound = true;
                    VARIANT state{}, role{};
                    if (SUCCEEDED(menu->get_accState(child, &state)) && state.vt == VT_I4)
                        probe.accessibleChecked = (state.lVal & STATE_SYSTEM_CHECKED) != 0;
                    if (SUCCEEDED(menu->get_accRole(child, &role)) && role.vt == VT_I4)
                        probe.accessibleRole = role.lVal == ROLE_SYSTEM_MENUITEM;
                    VariantClear(&state);
                    VariantClear(&role);
                }
            }
            PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    void ExpectColorMenuState(HWND hwnd, bool enabled)
    {
        constexpr UINT_PTR kProbeId = 0x48424350;
        ColorMenuProbe probe;
        probe.deadline = GetTickCount64() + 5000;
        Expect(SetWindowSubclass(hwnd, ColorMenuProbeSubclass, kProbeId, reinterpret_cast<DWORD_PTR>(&probe)) != FALSE,
               "Could not inspect the existing View popup for color-management state");
        SetTimer(hwnd, kProbeId, 50, nullptr);
        SendMessageW(hwnd, WM_SYSKEYDOWN, L'V', 0);
        KillTimer(hwnd, kProbeId);
        RemoveWindowSubclass(hwnd, ColorMenuProbeSubclass, kProbeId);
        Expect(probe.found, "The color-management View popup could not be located");
        Expect(probe.checked == enabled, "Color-management View menu checked state is incorrect");
        Expect(probe.accessibleFound && probe.accessibleRole && probe.accessibleChecked == enabled,
               "Color-management popup did not expose its accessible name, menu-item role, and checked state");
    }
}


    void RunColorManagementSettingsScenario(HINSTANCE instance)
    {
        using hyperbrowse::ui::command_ids::ID_VIEW_COLOR_MANAGEMENT;
        ScopedRegistryDwordBackup colorBackup(kRegistryPath, L"ColorManagementEnabled");
        DeleteRegistryValue(kRegistryPath, L"ColorManagementEnabled");
        TempFolder root(L"HyperBrowseColorSettings");
        const fs::path path = root.Root() / L"color.png";
        WriteTestImage(path, TestImageFormat::Png, 64, 32);
        {
            hyperbrowse::ui::MainWindow mainWindow(instance);
            Expect(mainWindow.Create(), "Color-management settings MainWindow did not open");
            mainWindow.Show(SW_SHOWNORMAL);
            auto* browser = reinterpret_cast<hyperbrowse::browser::BrowserPane*>(GetWindowLongPtrW(
                FindWindowExW(mainWindow.Hwnd(), nullptr, L"HyperBrowseBrowserPane", nullptr), GWLP_USERDATA));
            Expect(browser && browser->IsColorManagementEnabled(), "Missing color-management setting did not default on in the browser");
            ExpectColorMenuState(mainWindow.Hwnd(), true);
            mainWindow.OpenViewerAtPath(path.wstring());
            Expect(PumpMessagesUntil([]() { return !FindOpenViewerWindowHandles().empty(); }, 10000),
                   "Color-management primary viewer did not open");
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND,
                         hyperbrowse::ui::command_ids::ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, 0);
            Expect(PumpMessagesUntil([]() { return FindOpenViewerWindowHandles().size() >= 2; }, 10000),
                   "Color-management independent viewer did not open");
            const auto expectViewers = [](bool enabled)
            {
                for (HWND hwnd : FindOpenViewerWindowHandles())
                {
                    auto* viewer = reinterpret_cast<hyperbrowse::viewer::ViewerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                    Expect(viewer && viewer->IsColorManagementEnabled() == enabled,
                           "Color-management toggle did not propagate to every open viewer");
                }
            };
            expectViewers(true);
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, ID_VIEW_COLOR_MANAGEMENT, 0);
            Expect(!browser->IsColorManagementEnabled(), "Color-management opt-out did not propagate immediately to the browser");
            expectViewers(false);
            ExpectColorMenuState(mainWindow.Hwnd(), false);
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND,
                         hyperbrowse::ui::command_ids::ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, 0);
            Expect(PumpMessagesUntil([]() { return FindOpenViewerWindowHandles().size() >= 3; }, 10000),
                   "Color-management opt-out new viewer did not open");
            expectViewers(false);
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, ID_VIEW_COLOR_MANAGEMENT, 0);
            Expect(browser->IsColorManagementEnabled(), "Color-management re-enable did not propagate immediately to the browser");
            expectViewers(true);
            ExpectColorMenuState(mainWindow.Hwnd(), true);
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, ID_VIEW_COLOR_MANAGEMENT, 0);
            for (HWND hwnd : FindOpenViewerWindowHandles()) SendMessageW(hwnd, WM_CLOSE, 0, 0);
            DestroyWindow(mainWindow.Hwnd());
            PumpMessagesFor(100);
        }
        hyperbrowse::ui::MainWindow restored(instance);
        Expect(restored.Create(), "Persisted color-management MainWindow did not reopen");
        auto* browser = reinterpret_cast<hyperbrowse::browser::BrowserPane*>(GetWindowLongPtrW(
            FindWindowExW(restored.Hwnd(), nullptr, L"HyperBrowseBrowserPane", nullptr), GWLP_USERDATA));
        Expect(browser && !browser->IsColorManagementEnabled(), "Persisted opt-out was not applied before browser creation");
        DestroyWindow(restored.Hwnd());
        PumpMessagesFor(100);
    }
}
