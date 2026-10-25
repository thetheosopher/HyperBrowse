#include "ui/MainWindowInternal.h"

namespace
{
    using hyperbrowse::ui::dialog_detail::ConsolidatedSettingsDialogState;
    using hyperbrowse::ui::dialog_detail::ConsolidatedSettingsPage;
    using hyperbrowse::ui::dialog_detail::EscapeKeyBehaviorOption;
    using hyperbrowse::ui::dialog_detail::ExperimentalSettingsDialogResult;
    using hyperbrowse::ui::dialog_detail::ExperimentalSettingsFocusTarget;
    using hyperbrowse::ui::dialog_detail::ExperimentalSettingsFocusTargetKind;
    using hyperbrowse::ui::dialog_detail::ExperimentalSettingsLabel;
    using hyperbrowse::ui::dialog_detail::MeasureSettingsLayout;
    using hyperbrowse::ui::dialog_detail::SettingsLayoutLabel;
    using hyperbrowse::ui::dialog_detail::SettingsLayoutResult;
    using hyperbrowse::ui::ScaleDialogDimension;
    constexpr wchar_t kExperimentalSettingsDialogClassName[] = L"HyperBrowseExperimentalSettingsDialog";

    wchar_t ExperimentalSettingsPageMnemonic(ConsolidatedSettingsPage page);
    wchar_t ExperimentalSettingsControlMnemonic(ConsolidatedSettingsControl control);
    int ExperimentalSettingsMnemonicIndex(std::wstring_view text, wchar_t mnemonic);
    std::vector<ConsolidatedSettingsControl> ExperimentalSettingsPageControlOrder(ConsolidatedSettingsPage page);

    constexpr std::array<EscapeKeyBehaviorOption, 4> kEscapeKeyBehaviorOptions = {{
        {hyperbrowse::viewer::EscapeKeyBehavior::Close, L"Close"},
        {hyperbrowse::viewer::EscapeKeyBehavior::FitWidth, L"Fit Width"},
        {hyperbrowse::viewer::EscapeKeyBehavior::FitHeight, L"Fit Height"},
        {hyperbrowse::viewer::EscapeKeyBehavior::ActualSize, L"Actual Size"},
    }};

    int EscapeKeyBehaviorComboIndex(hyperbrowse::viewer::EscapeKeyBehavior behavior)
    {
        for (std::size_t index = 0; index < kEscapeKeyBehaviorOptions.size(); ++index)
        {
            if (kEscapeKeyBehaviorOptions[index].behavior == behavior)
            {
                return static_cast<int>(index);
            }
        }

        return 0;
    }

    constexpr int kExperimentalSettingsDialogWidth = 1120;
    constexpr int kExperimentalSettingsDialogHeight = 760;
    constexpr int kExperimentalSettingsApplyId = 5600;
    constexpr int kExperimentalSettingsOkId = 5601;
    constexpr int kExperimentalSettingsCancelId = 5602;
    constexpr DWORD kExperimentalSettingsWindowStyle = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN | WS_VSCROLL;
    constexpr DWORD kExperimentalSettingsWindowExStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    int ExperimentalSettingsPreferredClientHeight(int logicalWidth,
                                                  hyperbrowse::util::AppTextSize textSize)
    {
        const int fallbackCharacterWidth = hyperbrowse::util::ScaleAppTextDimension(8, textSize);
        int requiredHeight = hyperbrowse::util::ScaleAppTextDimension(kExperimentalSettingsDialogHeight, textSize);
        for (int pageIndex = 0; pageIndex < static_cast<int>(ConsolidatedSettingsPage::Count); ++pageIndex)
        {
            const SettingsLayoutResult layout = MeasureSettingsLayout({
                static_cast<ConsolidatedSettingsPage>(pageIndex),
                textSize,
                logicalWidth,
                1,
                [fallbackCharacterWidth](std::wstring_view text)
                {
                    return fallbackCharacterWidth * static_cast<int>(text.size());
                }});
            requiredHeight = std::max(
                requiredHeight,
                layout.requiredContentHeight
                    + layout.metrics.margin
                    + layout.metrics.footerHeight
                    + hyperbrowse::util::ScaleAppTextDimension(18, textSize));
        }
        return requiredHeight;
    }

    RECT ExperimentalSettingsInitialWindowRect(HWND ownerWindow,
                                                hyperbrowse::util::AppTextSize textSize,
                                                UINT dpi)
    {
        const int logicalWidth = hyperbrowse::util::ScaleAppTextDimension(kExperimentalSettingsDialogWidth, textSize);
        const int logicalHeight = ExperimentalSettingsPreferredClientHeight(logicalWidth, textSize);
        RECT windowRect{
            0,
            0,
            ScaleDialogDimension(logicalWidth, dpi),
            ScaleDialogDimension(logicalHeight, dpi)};
        AdjustDialogWindowRectForDpi(&windowRect,
                                     kExperimentalSettingsWindowStyle,
                                     FALSE,
                                     kExperimentalSettingsWindowExStyle,
                                     dpi);
        const DialogShellMetrics shell = MeasureDialogShellMetrics(ownerWindow, textSize);
        return ClampDialogFrameToWorkArea(windowRect, shell.workArea);
    }

    void CenterExperimentalSettingsOnWorkArea(HWND window,
                                              HWND ownerWindow,
                                              hyperbrowse::util::AppTextSize textSize)
    {
        if (!window)
        {
            return;
        }
        const DialogShellMetrics shell = MeasureDialogShellMetrics(ownerWindow, textSize);
        CenterDialogInWorkArea(window, shell.workArea);
    }

    void ResizeExperimentalSettingsToContent(ExperimentalSettingsDialogState& state,
                                             const RECT* suggestedFrame = nullptr)
    {
        if (!state.dialogWindow || !state.settings)
        {
            return;
        }

        RECT currentFrame{};
        if (!GetWindowRect(state.dialogWindow, &currentFrame))
        {
            return;
        }
        const RECT measuredFrame = ExperimentalSettingsInitialWindowRect(
            state.ownerWindow,
            state.settings->appTextSize,
            state.dpi);
        RECT nextFrame = suggestedFrame ? *suggestedFrame : currentFrame;
        nextFrame.right = nextFrame.left + (measuredFrame.right - measuredFrame.left);
        nextFrame.bottom = nextFrame.top + (measuredFrame.bottom - measuredFrame.top);
        const DialogShellMetrics shell = MeasureDialogShellMetrics(
            state.ownerWindow,
            state.settings->appTextSize);
        nextFrame = ClampDialogFrameToWorkArea(nextFrame, shell.workArea);
        SetWindowPos(state.dialogWindow,
                     nullptr,
                     nextFrame.left,
                     nextFrame.top,
                     nextFrame.right - nextFrame.left,
                     nextFrame.bottom - nextFrame.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    const wchar_t* ExperimentalSettingsPageTitle(ConsolidatedSettingsPage page)
    {
        constexpr const wchar_t* titles[] = {L"Slideshow", L"Viewer", L"Appearance", L"Performance", L"Behavior"};
        const std::size_t index = static_cast<std::size_t>(page);
        return index < std::size(titles) ? titles[index] : L"Settings";
    }

    wchar_t ExperimentalSettingsPageMnemonic(ConsolidatedSettingsPage page)
    {
        switch (page)
        {
        case ConsolidatedSettingsPage::Slideshow: return L'S';
        case ConsolidatedSettingsPage::Viewer: return L'V';
        case ConsolidatedSettingsPage::Appearance: return L'E';
        case ConsolidatedSettingsPage::Performance: return L'P';
        case ConsolidatedSettingsPage::Behavior: return L'B';
        default: return 0;
        }
    }

    wchar_t ExperimentalSettingsControlMnemonic(ConsolidatedSettingsControl control)
    {
        switch (control)
        {
        case ConsolidatedSettingsControl::TransitionStyle: return L'T';
        case ConsolidatedSettingsControl::SlideshowDuration: return L'D';
        case ConsolidatedSettingsControl::TransitionDuration: return L'R';
        case ConsolidatedSettingsControl::TransitionEnabled: return L'U';
        case ConsolidatedSettingsControl::ViewerWheelZoom: return L'Z';
        case ConsolidatedSettingsControl::ViewerWheelNavigate: return L'N';
        case ConsolidatedSettingsControl::InvertKeyboardPanning: return L'I';
        case ConsolidatedSettingsControl::RawPairingEnabled: return L'R';
        case ConsolidatedSettingsControl::RawPreferRaw: return L'W';
        case ConsolidatedSettingsControl::RawPreferJpeg: return L'J';
        case ConsolidatedSettingsControl::SecondaryMonitor: return L'M';
        case ConsolidatedSettingsControl::InfoOverlays: return L'D';
        case ConsolidatedSettingsControl::WindowedFullMetadata: return L'G';
        case ConsolidatedSettingsControl::FullScreenFullMetadata: return L'F';
        case ConsolidatedSettingsControl::OverlayTextSize: return L'X';
        case ConsolidatedSettingsControl::EscapeKeyBehavior: return L'K';
        case ConsolidatedSettingsControl::ThemeLight: return L'L';
        case ConsolidatedSettingsControl::ThemeDark: return L'K';
        case ConsolidatedSettingsControl::AppTextSize: return L'X';
        case ConsolidatedSettingsControl::ThumbnailSize: return L'H';
        case ConsolidatedSettingsControl::ThumbnailDetails: return L'T';
        case ConsolidatedSettingsControl::CompactLayout: return L'M';
        case ConsolidatedSettingsControl::DetailsPanel: return L'D';
        case ConsolidatedSettingsControl::ResourceProfile: return L'R';
        case ConsolidatedSettingsControl::PersistentCache: return L'K';
        case ConsolidatedSettingsControl::ThumbnailCache: return L'T';
        case ConsolidatedSettingsControl::ThumbnailCacheAutomatic: return L'F';
        case ConsolidatedSettingsControl::MetadataCache: return L'M';
        case ConsolidatedSettingsControl::MetadataCacheAutomatic: return L'L';
        case ConsolidatedSettingsControl::PrefetchDepth: return L'D';
        case ConsolidatedSettingsControl::PrefetchDepthAutomatic: return L'I';
        case ConsolidatedSettingsControl::PressureStatus: return L'Y';
        case ConsolidatedSettingsControl::NvJpeg: return L'N';
        case ConsolidatedSettingsControl::LibRawOutOfProcess: return L'U';
        case ConsolidatedSettingsControl::RecursiveBrowsing: return L'R';
        case ConsolidatedSettingsControl::ShowSubfolders: return L'F';
        case ConsolidatedSettingsControl::CloseOnEscape: return L'L';
        case ConsolidatedSettingsControl::SingleInstance: return L'I';
        case ConsolidatedSettingsControl::KeepInNotificationArea: return L'K';
        case ConsolidatedSettingsControl::QuickSendShortcutOrder: return L'Q';
        case ConsolidatedSettingsControl::PersistentCacheCapacity: return L'P';
        case ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic: return L'F';
        case ConsolidatedSettingsControl::TrimPersistentCache: return L'T';
        default: return 0;
        }
    }

    int ExperimentalSettingsMnemonicIndex(std::wstring_view text, wchar_t mnemonic)
    {
        if (mnemonic == 0)
        {
            return -1;
        }

        const wchar_t normalized = static_cast<wchar_t>(towupper(mnemonic));
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (static_cast<wchar_t>(towupper(text[index])) == normalized)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    bool ExperimentalSettingsControlIsChoice(ConsolidatedSettingsControl control)
    {
        switch (control)
        {
        case ConsolidatedSettingsControl::TransitionStyle:
        case ConsolidatedSettingsControl::OverlayTextSize:
        case ConsolidatedSettingsControl::EscapeKeyBehavior:
        case ConsolidatedSettingsControl::AppTextSize:
        case ConsolidatedSettingsControl::ThumbnailSize:
        case ConsolidatedSettingsControl::ResourceProfile:
            return true;
        default:
            return false;
        }
    }

    bool ExperimentalSettingsControlIsAction(ConsolidatedSettingsControl control)
    {
        return control == ConsolidatedSettingsControl::TrimPersistentCache;
    }

    std::wstring ExperimentalSettingsChoiceValue(const ConsolidatedSettingsDialogState& settings,
                                                 ConsolidatedSettingsControl control)
    {
        switch (control)
        {
        case ConsolidatedSettingsControl::TransitionStyle:
        {
            const int index = SlideshowTransitionComboIndex(settings.slideshowTransitionStyle);
            return index >= 0 && index < static_cast<int>(kSlideshowTransitionOptions.size())
                ? std::wstring(kSlideshowTransitionOptions[static_cast<std::size_t>(index)].label)
                : std::wstring(L"Crossfade");
        }
        case ConsolidatedSettingsControl::OverlayTextSize:
            return settings.overlayTextSize == hyperbrowse::viewer::InfoOverlayTextSize::Large
                ? std::wstring(L"Large")
                : settings.overlayTextSize == hyperbrowse::viewer::InfoOverlayTextSize::Medium ? std::wstring(L"Medium") : std::wstring(L"Small");
        case ConsolidatedSettingsControl::EscapeKeyBehavior:
        {
            const int index = EscapeKeyBehaviorComboIndex(settings.viewerEscapeKeyBehavior);
            return std::wstring(kEscapeKeyBehaviorOptions[static_cast<std::size_t>(index)].label);
        }
        case ConsolidatedSettingsControl::AppTextSize:
            return settings.appTextSize == hyperbrowse::util::AppTextSize::Large
                ? std::wstring(L"Large")
                : settings.appTextSize == hyperbrowse::util::AppTextSize::Small ? std::wstring(L"Small") : std::wstring(L"Medium");
        case ConsolidatedSettingsControl::ThumbnailSize:
            return std::to_wstring(static_cast<int>(settings.thumbnailSizePreset)).append(L" px");
        case ConsolidatedSettingsControl::ResourceProfile:
        {
            constexpr const wchar_t* profiles[] = {L"Conservative", L"Balanced", L"Performance", L"Aggressive"};
            const int index = static_cast<int>(settings.resourceProfile);
            return index >= 0 && index < static_cast<int>(std::size(profiles)) ? std::wstring(profiles[index]) : std::wstring(L"Balanced");
        }
        default:
            return {};
        }
    }

    void ExperimentalSettingsAddLabel(ExperimentalSettingsDialogState& state,
                                      int left,
                                      int top,
                                      int right,
                                      int bottom,
                                      const wchar_t* text,
                                      bool muted = false,
                                      wchar_t mnemonic = 0,
                                      ConsolidatedSettingsControl mnemonicControl = ConsolidatedSettingsControl::Count)
    {
        state.labels.push_back(ExperimentalSettingsLabel{
            {left, top, right, bottom},
            text ? text : L"",
            muted,
            mnemonic,
            mnemonicControl});
    }

    void LayoutExperimentalSettings(ExperimentalSettingsDialogState& state)
    {
        if (!state.dialogWindow || !state.settings)
        {
            return;
        }
        RECT client{};
        GetClientRect(state.dialogWindow, &client);
        const UINT dpi = std::max<UINT>(96, state.dpi);
        const auto toLogical = [dpi](LONG value)
        {
            return MulDiv(value, 96, static_cast<int>(dpi));
        };
        const auto toPhysical = [dpi](const RECT& logical) -> RECT
        {
            return RECT{
                ScaleDialogDimension(logical.left, dpi),
                ScaleDialogDimension(logical.top, dpi),
                ScaleDialogDimension(logical.right, dpi),
                ScaleDialogDimension(logical.bottom, dpi)};
        };
        const auto measureTextWidth = [&](std::wstring_view text)
        {
            if (!state.controlFont)
            {
                return hyperbrowse::util::ScaleAppTextDimension(8, state.settings->appTextSize)
                    * static_cast<int>(text.size());
            }
            HDC dc = GetDC(state.dialogWindow);
            if (!dc)
            {
                return hyperbrowse::util::ScaleAppTextDimension(8, state.settings->appTextSize)
                    * static_cast<int>(text.size());
            }
            const HGDIOBJ previousFont = SelectObject(dc, state.controlFont);
            SIZE size{};
            GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
            SelectObject(dc, previousFont);
            ReleaseDC(state.dialogWindow, dc);
            return toLogical(size.cx);
        };
        std::array<int, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> choiceControlWidths{};
        const HFONT choiceFont = state.choiceFont
            ? state.choiceFont
            : state.controlFont
                ? state.controlFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HDC choiceDc = GetDC(state.dialogWindow);
        const HGDIOBJ previousChoiceFont = choiceDc
            ? SelectObject(choiceDc, choiceFont)
            : nullptr;
        const bool canMeasureChoiceText = previousChoiceFont && previousChoiceFont != HGDI_ERROR;
        for (std::size_t index = 0; index < state.nativeControls.size(); ++index)
        {
            const auto control = static_cast<ConsolidatedSettingsControl>(index);
            if (!ExperimentalSettingsControlIsChoice(control) || !state.nativeControls[index])
            {
                continue;
            }

            const HWND choice = state.nativeControls[index];
            const LRESULT itemCount = SendMessageW(choice, CB_GETCOUNT, 0, 0);
            if (itemCount == CB_ERR)
            {
                continue;
            }

            int widestTextPixels = 0;
            int widestFallbackTextWidth = 0;
            for (LRESULT itemIndex = 0; itemIndex < itemCount; ++itemIndex)
            {
                const LRESULT itemLength = SendMessageW(
                    choice,
                    CB_GETLBTEXTLEN,
                    static_cast<WPARAM>(itemIndex),
                    0);
                if (itemLength == CB_ERR)
                {
                    continue;
                }

                std::wstring label(static_cast<std::size_t>(itemLength) + 1, L'\0');
                const LRESULT copiedLength = SendMessageW(
                    choice,
                    CB_GETLBTEXT,
                    static_cast<WPARAM>(itemIndex),
                    reinterpret_cast<LPARAM>(label.data()));
                if (copiedLength == CB_ERR || copiedLength > itemLength)
                {
                    continue;
                }
                label.resize(static_cast<std::size_t>(copiedLength));

                if (canMeasureChoiceText)
                {
                    SIZE textSize{};
                    if (GetTextExtentPoint32W(choiceDc,
                                              label.data(),
                                              static_cast<int>(label.size()),
                                              &textSize) != FALSE)
                    {
                        widestTextPixels = std::max(widestTextPixels, static_cast<int>(textSize.cx));
                        continue;
                    }
                }
                widestFallbackTextWidth = std::max(
                    widestFallbackTextWidth,
                    MulDiv(measureTextWidth(label), 4, 3));
            }

            const int widestTextWidth = std::max(
                canMeasureChoiceText ? toLogical(widestTextPixels) : 0,
                widestFallbackTextWidth);
            if (widestTextWidth > 0)
            {
                choiceControlWidths[index] = widestTextWidth
                    + hyperbrowse::util::ScaleAppTextDimension(48, state.settings->appTextSize);
            }
        }
        if (canMeasureChoiceText)
        {
            SelectObject(choiceDc, previousChoiceFont);
        }
        if (choiceDc)
        {
            ReleaseDC(state.dialogWindow, choiceDc);
        }
        const std::size_t overlayTextSizeIndex = static_cast<std::size_t>(
            ConsolidatedSettingsControl::OverlayTextSize);
        const std::size_t escapeKeyBehaviorIndex = static_cast<std::size_t>(
            ConsolidatedSettingsControl::EscapeKeyBehavior);
        const int viewerChoiceWidth = std::max(
            choiceControlWidths[overlayTextSizeIndex],
            choiceControlWidths[escapeKeyBehaviorIndex]);
        if (viewerChoiceWidth > 0)
        {
            choiceControlWidths[overlayTextSizeIndex] = viewerChoiceWidth;
            choiceControlWidths[escapeKeyBehaviorIndex] = viewerChoiceWidth;
        }
        const std::size_t appTextSizeIndex = static_cast<std::size_t>(
            ConsolidatedSettingsControl::AppTextSize);
        const std::size_t thumbnailSizeIndex = static_cast<std::size_t>(
            ConsolidatedSettingsControl::ThumbnailSize);
        const int appearanceChoiceWidth = std::max(
            choiceControlWidths[appTextSizeIndex],
            choiceControlWidths[thumbnailSizeIndex]);
        if (appearanceChoiceWidth > 0)
        {
            choiceControlWidths[appTextSizeIndex] = appearanceChoiceWidth;
            choiceControlWidths[thumbnailSizeIndex] = appearanceChoiceWidth;
        }
        const SettingsLayoutResult layout = MeasureSettingsLayout({
            state.page,
            state.settings->appTextSize,
            toLogical(client.right),
            toLogical(client.bottom),
            measureTextWidth,
            choiceControlWidths});

        state.bodyViewport = layout.metrics.bodyViewport;
        state.requiredContentHeight = layout.requiredContentHeight;
        state.scrollExtent = layout.scrollExtent;
        state.scrollOffset = std::clamp(state.scrollOffset, 0, state.scrollExtent);
        state.labels.clear();
        state.labels.reserve(layout.labels.size());
        for (const SettingsLayoutLabel& label : layout.labels)
        {
            RECT bounds = label.bounds;
            OffsetRect(&bounds, 0, -state.scrollOffset);
            ExperimentalSettingsAddLabel(state,
                                         bounds.left,
                                         bounds.top,
                                         bounds.right,
                                         bounds.bottom,
                                         label.text.c_str(),
                                         label.muted,
                                         ExperimentalSettingsControlMnemonic(label.mnemonicControl),
                                         label.mnemonicControl);
        }
        state.tabRects = layout.tabRects;
        state.controlRects = layout.controlRects;
        for (RECT& bounds : state.controlRects)
        {
            if (bounds.right > bounds.left && bounds.bottom > bounds.top)
            {
                OffsetRect(&bounds, 0, -state.scrollOffset);
            }
        }
        const int buttonTop = layout.metrics.footer.top;
        const int right = layout.metrics.footer.right;
        const int buttonWidth = layout.metrics.buttonWidth;
        const int buttonGap = layout.metrics.buttonGap;
        state.cancelButtonRect = {right - buttonWidth, buttonTop, right, buttonTop + layout.metrics.footerHeight};
        state.okButtonRect = {state.cancelButtonRect.left - buttonGap - buttonWidth, buttonTop,
                      state.cancelButtonRect.left - buttonGap, buttonTop + layout.metrics.footerHeight};
        state.applyButtonRect = {state.okButtonRect.left - buttonGap - buttonWidth, buttonTop,
                     state.okButtonRect.left - buttonGap, buttonTop + layout.metrics.footerHeight};

        const int spinWidth = hyperbrowse::util::ScaleAppTextDimension(28, state.settings->appTextSize);
        for (std::size_t index = 0; index < state.numericEdits.size(); ++index)
        {
            RECT logicalBounds = layout.numericEditRects[index];
            RECT logicalSpinBounds = layout.numericSpinRects[index];
            RECT logicalSliderBounds = layout.numericSliderRects[index];
            OffsetRect(&logicalBounds, 0, -state.scrollOffset);
            OffsetRect(&logicalSpinBounds, 0, -state.scrollOffset);
            OffsetRect(&logicalSliderBounds, 0, -state.scrollOffset);
            if (state.numericEdits[index])
            {
                const RECT physicalBounds = toPhysical(logicalBounds);
                const int spinWidthPixels = ScaleDialogDimension(spinWidth, dpi);
                const int editLeft = static_cast<int>(physicalBounds.left);
                const int editTop = static_cast<int>(physicalBounds.top);
                const int editRight = std::max(editLeft + 1, static_cast<int>(physicalBounds.right) - spinWidthPixels);
                const int editHeight = std::max(1, static_cast<int>(physicalBounds.bottom - physicalBounds.top));
                SetWindowPos(state.numericEdits[index], nullptr,
                             editLeft,
                             editTop,
                             editRight - editLeft,
                             editHeight,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                const bool visible = (state.page == ConsolidatedSettingsPage::Slideshow && index < 2)
                    || (state.page == ConsolidatedSettingsPage::Performance && index >= 2);
                const bool inBody = logicalBounds.top >= state.bodyViewport.top
                    && logicalBounds.bottom <= state.bodyViewport.bottom;
                ShowWindow(state.numericEdits[index], visible && inBody ? SW_SHOW : SW_HIDE);
            }
            if (state.numericSpins[index])
            {
                const RECT physicalSpinBounds = toPhysical(logicalSpinBounds);
                const int spinLeft = static_cast<int>(physicalSpinBounds.left);
                const int spinTop = static_cast<int>(physicalSpinBounds.top);
                const int spinWidthPixels = std::max(1, static_cast<int>(physicalSpinBounds.right - physicalSpinBounds.left));
                const int spinHeight = std::max(1, static_cast<int>(physicalSpinBounds.bottom - physicalSpinBounds.top));
                SetWindowPos(state.numericSpins[index], nullptr,
                             spinLeft,
                             spinTop,
                             spinWidthPixels,
                             spinHeight,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                const bool visible = (state.page == ConsolidatedSettingsPage::Slideshow && index < 2)
                    || (state.page == ConsolidatedSettingsPage::Performance && index >= 2);
                const bool inBody = logicalSpinBounds.top >= state.bodyViewport.top
                    && logicalSpinBounds.bottom <= state.bodyViewport.bottom;
                ShowWindow(state.numericSpins[index], visible && inBody ? SW_SHOW : SW_HIDE);
            }
            if (state.numericSliders[index])
            {
                const RECT physicalSliderBounds = toPhysical(logicalSliderBounds);
                const int sliderLeft = static_cast<int>(physicalSliderBounds.left);
                const int sliderTop = static_cast<int>(physicalSliderBounds.top);
                const int sliderWidth = std::max(1, static_cast<int>(physicalSliderBounds.right - physicalSliderBounds.left));
                const int sliderHeight = std::max(1, static_cast<int>(physicalSliderBounds.bottom - physicalSliderBounds.top));
                SetWindowPos(state.numericSliders[index], nullptr,
                             sliderLeft,
                             sliderTop,
                             sliderWidth,
                             sliderHeight,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                const bool visible = state.page == ConsolidatedSettingsPage::Performance && index >= 2;
                const bool inBody = logicalSliderBounds.top >= state.bodyViewport.top
                    && logicalSliderBounds.bottom <= state.bodyViewport.bottom;
                ShowWindow(state.numericSliders[index], visible && inBody ? SW_SHOW : SW_HIDE);
            }
        }

        const auto setChoiceVisibility = [&](ConsolidatedSettingsControl control, ConsolidatedSettingsPage page)
        {
            if (const HWND choice = state.nativeControls[static_cast<std::size_t>(control)])
            {
                ShowWindow(choice, state.page == page ? SW_SHOW : SW_HIDE);
            }
        };
        setChoiceVisibility(ConsolidatedSettingsControl::TransitionStyle, ConsolidatedSettingsPage::Slideshow);
        setChoiceVisibility(ConsolidatedSettingsControl::OverlayTextSize, ConsolidatedSettingsPage::Viewer);
        setChoiceVisibility(ConsolidatedSettingsControl::EscapeKeyBehavior, ConsolidatedSettingsPage::Viewer);
        setChoiceVisibility(ConsolidatedSettingsControl::AppTextSize, ConsolidatedSettingsPage::Appearance);
        setChoiceVisibility(ConsolidatedSettingsControl::ThumbnailSize, ConsolidatedSettingsPage::Appearance);
        setChoiceVisibility(ConsolidatedSettingsControl::ResourceProfile, ConsolidatedSettingsPage::Performance);
        setChoiceVisibility(ConsolidatedSettingsControl::QuickSendShortcutOrder, ConsolidatedSettingsPage::Behavior);
        for (const ConsolidatedSettingsControl control : {
                 ConsolidatedSettingsControl::TransitionStyle,
                 ConsolidatedSettingsControl::OverlayTextSize,
                 ConsolidatedSettingsControl::EscapeKeyBehavior,
                 ConsolidatedSettingsControl::AppTextSize,
                 ConsolidatedSettingsControl::ThumbnailSize,
                 ConsolidatedSettingsControl::ResourceProfile,
                 ConsolidatedSettingsControl::QuickSendShortcutOrder})
        {
            if (const HWND choice = state.nativeControls[static_cast<std::size_t>(control)])
            {
                const RECT physicalBounds = toPhysical(state.controlRects[static_cast<std::size_t>(control)]);
                const int physicalWidth = std::max(1, static_cast<int>(physicalBounds.right - physicalBounds.left));
                const int physicalHeight = std::max(1, static_cast<int>(physicalBounds.bottom - physicalBounds.top));
                SetWindowPos(choice, nullptr,
                             physicalBounds.left,
                             physicalBounds.top,
                             physicalWidth,
                             physicalHeight,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                const RECT& logicalBounds = state.controlRects[static_cast<std::size_t>(control)];
                const bool inBody = logicalBounds.top >= state.bodyViewport.top
                    && logicalBounds.bottom <= state.bodyViewport.bottom;
                ShowWindow(choice, inBody ? SW_SHOW : SW_HIDE);
            }
        }
        const int bodyHeight = std::max(1, static_cast<int>(state.bodyViewport.bottom - state.bodyViewport.top));
        SCROLLINFO scrollInfo{sizeof(scrollInfo), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                              state.scrollExtent + bodyHeight, static_cast<UINT>(bodyHeight),
                              state.scrollOffset, 0};
        SetScrollInfo(state.dialogWindow, SB_VERT, &scrollInfo, TRUE);
        ShowScrollBar(state.dialogWindow, SB_VERT, state.scrollExtent > 0 ? TRUE : FALSE);
        InvalidateRect(state.dialogWindow, nullptr, FALSE);
    }

    bool SetExperimentalSettingsScrollOffset(ExperimentalSettingsDialogState& state, int offset)
    {
        const int nextOffset = std::clamp(offset, 0, state.scrollExtent);
        if (nextOffset == state.scrollOffset)
        {
            return false;
        }
        state.scrollOffset = nextOffset;
        LayoutExperimentalSettings(state);
        return true;
    }

    bool ExperimentalSettingsChecked(const ConsolidatedSettingsDialogState& settings, ConsolidatedSettingsControl control)
    {
        switch (control)
        {
        case ConsolidatedSettingsControl::TransitionEnabled: return settings.useSlideshowTransition;
        case ConsolidatedSettingsControl::ViewerWheelZoom: return settings.viewerMouseWheelBehavior == hyperbrowse::viewer::MouseWheelBehavior::Zoom;
        case ConsolidatedSettingsControl::ViewerWheelNavigate: return settings.viewerMouseWheelBehavior == hyperbrowse::viewer::MouseWheelBehavior::Navigate;
        case ConsolidatedSettingsControl::InvertKeyboardPanning: return settings.invertKeyboardPanning;
        case ConsolidatedSettingsControl::RawPairingEnabled: return settings.rawJpegPairedOperationsEnabled;
        case ConsolidatedSettingsControl::RawPreferRaw: return settings.pairedRawJpegViewerPreference == hyperbrowse::browser::RawJpegDisplayPreference::Raw;
        case ConsolidatedSettingsControl::RawPreferJpeg: return settings.pairedRawJpegViewerPreference == hyperbrowse::browser::RawJpegDisplayPreference::Jpeg;
        case ConsolidatedSettingsControl::SecondaryMonitor: return settings.defaultViewerToSecondaryMonitor;
        case ConsolidatedSettingsControl::InfoOverlays: return settings.infoOverlaysVisible;
        case ConsolidatedSettingsControl::WindowedFullMetadata: return settings.windowedFullMetadataVisible;
        case ConsolidatedSettingsControl::FullScreenFullMetadata: return settings.fullScreenFullMetadataVisible;
        case ConsolidatedSettingsControl::ThemeLight: return !settings.darkTheme;
        case ConsolidatedSettingsControl::ThemeDark: return settings.darkTheme;
        case ConsolidatedSettingsControl::ThumbnailDetails: return settings.thumbnailDetailsVisible;
        case ConsolidatedSettingsControl::CompactLayout: return settings.compactThumbnailLayout;
        case ConsolidatedSettingsControl::DetailsPanel: return settings.detailsStripVisible;
        case ConsolidatedSettingsControl::PersistentCache: return settings.persistentThumbnailCacheEnabled;
        case ConsolidatedSettingsControl::ThumbnailCacheAutomatic: return settings.thumbnailCacheCapacityOverrideBytes == 0;
        case ConsolidatedSettingsControl::MetadataCacheAutomatic: return settings.metadataCacheCapacityOverrideEntries == 0;
        case ConsolidatedSettingsControl::PrefetchDepthAutomatic: return settings.prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth;
        case ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic: return settings.persistentThumbnailCacheCapacityOverrideBytes == 0;
        case ConsolidatedSettingsControl::PressureStatus: return settings.showPressureStateInStatusBar;
        case ConsolidatedSettingsControl::NvJpeg: return settings.nvJpegEnabled;
        case ConsolidatedSettingsControl::LibRawOutOfProcess: return settings.libRawOutOfProcessEnabled;
        case ConsolidatedSettingsControl::RecursiveBrowsing: return settings.recursiveBrowsingEnabled;
        case ConsolidatedSettingsControl::ShowSubfolders: return settings.showSubfoldersInBrowser;
        case ConsolidatedSettingsControl::CloseOnEscape: return settings.closeMainWindowOnEscape;
        case ConsolidatedSettingsControl::SingleInstance: return settings.singleInstanceEnabled;
        case ConsolidatedSettingsControl::KeepInNotificationArea: return settings.keepInNotificationAreaEnabled;
        default: return false;
        }
    }

    void RebuildExperimentalSettingsTextResources(ExperimentalSettingsDialogState& state);

    void ExperimentalSettingsToggle(ExperimentalSettingsDialogState& state, ConsolidatedSettingsControl control)
    {
        auto& settings = *state.settings;
        if (ExperimentalSettingsControlIsAction(control))
        {
            if (settings.trimPersistentCache)
            {
                settings.trimPersistentCache();
            }
            return;
        }
        if (ExperimentalSettingsControlIsChoice(control))
        {
            if (control == ConsolidatedSettingsControl::TransitionStyle)
            {
                const int current = SlideshowTransitionComboIndex(settings.slideshowTransitionStyle);
                settings.slideshowTransitionStyle = kSlideshowTransitionOptions[static_cast<std::size_t>((current + 1) % kSlideshowTransitionOptions.size())].style;
            }
            else if (control == ConsolidatedSettingsControl::OverlayTextSize)
            {
                settings.overlayTextSize = static_cast<hyperbrowse::viewer::InfoOverlayTextSize>((static_cast<int>(settings.overlayTextSize) + 1) % 3);
            }
            else if (control == ConsolidatedSettingsControl::EscapeKeyBehavior)
            {
                const int current = EscapeKeyBehaviorComboIndex(settings.viewerEscapeKeyBehavior);
                settings.viewerEscapeKeyBehavior = kEscapeKeyBehaviorOptions[static_cast<std::size_t>((current + 1) % kEscapeKeyBehaviorOptions.size())].behavior;
            }
            else if (control == ConsolidatedSettingsControl::AppTextSize)
            {
                settings.appTextSize = static_cast<hyperbrowse::util::AppTextSize>((static_cast<int>(settings.appTextSize) + 1) % 3);
                RebuildExperimentalSettingsTextResources(state);
                LayoutExperimentalSettings(state);
            }
            else if (control == ConsolidatedSettingsControl::ThumbnailSize)
            {
                const auto iterator = std::find(kThumbnailSizePresets.begin(), kThumbnailSizePresets.end(), settings.thumbnailSizePreset);
                const std::size_t next = iterator == kThumbnailSizePresets.end() ? 0 : static_cast<std::size_t>(iterator - kThumbnailSizePresets.begin() + 1) % kThumbnailSizePresets.size();
                settings.thumbnailSizePreset = kThumbnailSizePresets[next];
            }
            else
            {
                settings.resourceProfile = static_cast<hyperbrowse::util::ResourceProfile>((static_cast<int>(settings.resourceProfile) + 1) % 4);
            }
            return;
        }

        switch (control)
        {
        case ConsolidatedSettingsControl::ViewerWheelZoom:
            settings.viewerMouseWheelBehavior = hyperbrowse::viewer::MouseWheelBehavior::Zoom;
            break;
        case ConsolidatedSettingsControl::ViewerWheelNavigate:
            settings.viewerMouseWheelBehavior = hyperbrowse::viewer::MouseWheelBehavior::Navigate;
            break;
        case ConsolidatedSettingsControl::RawPreferRaw:
            settings.pairedRawJpegViewerPreference = hyperbrowse::browser::RawJpegDisplayPreference::Raw;
            break;
        case ConsolidatedSettingsControl::RawPreferJpeg:
            settings.pairedRawJpegViewerPreference = hyperbrowse::browser::RawJpegDisplayPreference::Jpeg;
            break;
        case ConsolidatedSettingsControl::ThemeLight:
            settings.darkTheme = false;
            break;
        case ConsolidatedSettingsControl::ThemeDark:
            settings.darkTheme = true;
            break;
        default:
        {
            bool* value = nullptr;
            switch (control)
            {
            case ConsolidatedSettingsControl::TransitionEnabled: value = &settings.useSlideshowTransition; break;
            case ConsolidatedSettingsControl::InvertKeyboardPanning: value = &settings.invertKeyboardPanning; break;
            case ConsolidatedSettingsControl::RawPairingEnabled: value = &settings.rawJpegPairedOperationsEnabled; break;
            case ConsolidatedSettingsControl::SecondaryMonitor: value = &settings.defaultViewerToSecondaryMonitor; break;
            case ConsolidatedSettingsControl::InfoOverlays: value = &settings.infoOverlaysVisible; break;
            case ConsolidatedSettingsControl::WindowedFullMetadata: value = &settings.windowedFullMetadataVisible; break;
            case ConsolidatedSettingsControl::FullScreenFullMetadata: value = &settings.fullScreenFullMetadataVisible; break;
            case ConsolidatedSettingsControl::ThumbnailDetails: value = &settings.thumbnailDetailsVisible; break;
            case ConsolidatedSettingsControl::CompactLayout: value = &settings.compactThumbnailLayout; break;
            case ConsolidatedSettingsControl::DetailsPanel: value = &settings.detailsStripVisible; break;
            case ConsolidatedSettingsControl::PersistentCache: value = &settings.persistentThumbnailCacheEnabled; break;
            case ConsolidatedSettingsControl::PressureStatus: value = &settings.showPressureStateInStatusBar; break;
            case ConsolidatedSettingsControl::NvJpeg: value = &settings.nvJpegEnabled; break;
            case ConsolidatedSettingsControl::LibRawOutOfProcess: value = &settings.libRawOutOfProcessEnabled; break;
            case ConsolidatedSettingsControl::RecursiveBrowsing: value = &settings.recursiveBrowsingEnabled; break;
            case ConsolidatedSettingsControl::ShowSubfolders: value = &settings.showSubfoldersInBrowser; break;
            case ConsolidatedSettingsControl::CloseOnEscape: value = &settings.closeMainWindowOnEscape; break;
            case ConsolidatedSettingsControl::SingleInstance: value = &settings.singleInstanceEnabled; break;
            case ConsolidatedSettingsControl::KeepInNotificationArea: value = &settings.keepInNotificationAreaEnabled; break;
            case ConsolidatedSettingsControl::ThumbnailCacheAutomatic:
                settings.thumbnailCacheCapacityOverrideBytes = settings.thumbnailCacheCapacityOverrideBytes == 0
                    ? hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(0, settings.resourceProfile)
                    : 0;
                return;
            case ConsolidatedSettingsControl::MetadataCacheAutomatic:
                settings.metadataCacheCapacityOverrideEntries = settings.metadataCacheCapacityOverrideEntries == 0
                    ? hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(0, settings.resourceProfile)
                    : 0;
                return;
            case ConsolidatedSettingsControl::PrefetchDepthAutomatic:
                settings.prefetchDepthOverride = settings.prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth
                    ? hyperbrowse::util::ResolvePrefetchDepth(settings.resourceProfile, hyperbrowse::util::kAutomaticPrefetchDepth)
                    : hyperbrowse::util::kAutomaticPrefetchDepth;
                return;
            case ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic:
                settings.persistentThumbnailCacheCapacityOverrideBytes = settings.persistentThumbnailCacheCapacityOverrideBytes == 0
                    ? hyperbrowse::services::ThumbnailScheduler::ResolvePersistentCacheCapacityBytes(0, settings.resourceProfile)
                    : 0;
                return;
            default: break;
            }
            if (value)
            {
                *value = !*value;
            }
            break;
        }
        }
    }

    void UpdateExperimentalSettingsCacheValues(ExperimentalSettingsDialogState& state);
    void ApplyExperimentalSettingsTheme(ExperimentalSettingsDialogState& state);

    HWND ExperimentalSettingsNativeWindow(const ExperimentalSettingsDialogState& state,
                                          ConsolidatedSettingsControl control)
    {
        switch (control)
        {
        case ConsolidatedSettingsControl::SlideshowDuration: return state.numericEdits[0];
        case ConsolidatedSettingsControl::SlideshowDurationSpin: return state.numericSpins[0];
        case ConsolidatedSettingsControl::TransitionDuration: return state.numericEdits[1];
        case ConsolidatedSettingsControl::TransitionDurationSpin: return state.numericSpins[1];
        case ConsolidatedSettingsControl::ThumbnailCache: return state.numericEdits[2];
        case ConsolidatedSettingsControl::MetadataCache: return state.numericEdits[3];
        case ConsolidatedSettingsControl::PrefetchDepth: return state.numericEdits[4];
        case ConsolidatedSettingsControl::PersistentCacheCapacity: return state.numericEdits[5];
        default:
            break;
        }

        const std::size_t index = static_cast<std::size_t>(control);
        return index < state.nativeControls.size() ? state.nativeControls[index] : nullptr;
    }

    bool ExperimentalSettingsCustomControlEnabled(const ExperimentalSettingsDialogState& state,
                                                  ConsolidatedSettingsControl control)
    {
        if (!state.settings)
        {
            return false;
        }

        switch (control)
        {
        case ConsolidatedSettingsControl::RawPreferRaw:
        case ConsolidatedSettingsControl::RawPreferJpeg:
            return state.settings->rawJpegPairedOperationsEnabled;
        case ConsolidatedSettingsControl::SecondaryMonitor:
            return state.settings->secondaryMonitorAvailable;
        case ConsolidatedSettingsControl::NvJpeg:
            return state.settings->nvJpegAvailable;
        case ConsolidatedSettingsControl::LibRawOutOfProcess:
            return state.settings->libRawAvailable;
        case ConsolidatedSettingsControl::KeepInNotificationArea:
            return state.settings->singleInstanceEnabled;
        default:
            return true;
        }
    }

    std::vector<ConsolidatedSettingsControl> ExperimentalSettingsPageControlOrder(ConsolidatedSettingsPage page);

    bool ExperimentalSettingsControlAvailable(const ExperimentalSettingsDialogState& state,
                                              ConsolidatedSettingsControl control)
    {
        const std::size_t index = static_cast<std::size_t>(control);
        if (index >= state.controlRects.size())
        {
            return false;
        }

        if (const HWND nativeWindow = ExperimentalSettingsNativeWindow(state, control))
        {
            const auto pageControls = ExperimentalSettingsPageControlOrder(state.page);
            return IsWindow(nativeWindow) != FALSE
                && IsWindowEnabled(nativeWindow) != FALSE
                && std::find(pageControls.begin(), pageControls.end(), control) != pageControls.end();
        }

        const RECT& bounds = state.controlRects[index];
        return bounds.right > bounds.left
            && bounds.bottom > bounds.top
            && ExperimentalSettingsCustomControlEnabled(state, control);
    }

    std::vector<ConsolidatedSettingsControl> ExperimentalSettingsPageControlOrder(ConsolidatedSettingsPage page)
    {
        switch (page)
        {
        case ConsolidatedSettingsPage::Slideshow:
            return {
                ConsolidatedSettingsControl::TransitionStyle,
                ConsolidatedSettingsControl::SlideshowDuration,
                ConsolidatedSettingsControl::TransitionDuration};
        case ConsolidatedSettingsPage::Viewer:
            return {
                ConsolidatedSettingsControl::TransitionEnabled,
                ConsolidatedSettingsControl::ViewerWheelZoom,
                ConsolidatedSettingsControl::ViewerWheelNavigate,
                ConsolidatedSettingsControl::InvertKeyboardPanning,
                ConsolidatedSettingsControl::RawPairingEnabled,
                ConsolidatedSettingsControl::RawPreferRaw,
                ConsolidatedSettingsControl::RawPreferJpeg,
                ConsolidatedSettingsControl::SecondaryMonitor,
                ConsolidatedSettingsControl::InfoOverlays,
                ConsolidatedSettingsControl::WindowedFullMetadata,
                ConsolidatedSettingsControl::FullScreenFullMetadata,
                ConsolidatedSettingsControl::OverlayTextSize,
                ConsolidatedSettingsControl::EscapeKeyBehavior};
        case ConsolidatedSettingsPage::Appearance:
            return {
                ConsolidatedSettingsControl::ThemeLight,
                ConsolidatedSettingsControl::ThemeDark,
                ConsolidatedSettingsControl::AppTextSize,
                ConsolidatedSettingsControl::ThumbnailSize,
                ConsolidatedSettingsControl::ThumbnailDetails,
                ConsolidatedSettingsControl::CompactLayout,
                ConsolidatedSettingsControl::DetailsPanel};
        case ConsolidatedSettingsPage::Performance:
            return {
                ConsolidatedSettingsControl::ResourceProfile,
                ConsolidatedSettingsControl::PersistentCache,
                ConsolidatedSettingsControl::ThumbnailCache,
                ConsolidatedSettingsControl::ThumbnailCacheAutomatic,
                ConsolidatedSettingsControl::MetadataCache,
                ConsolidatedSettingsControl::MetadataCacheAutomatic,
                ConsolidatedSettingsControl::PrefetchDepth,
                ConsolidatedSettingsControl::PrefetchDepthAutomatic,
                ConsolidatedSettingsControl::PersistentCacheCapacity,
                ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic,
                ConsolidatedSettingsControl::TrimPersistentCache,
                ConsolidatedSettingsControl::PressureStatus,
                ConsolidatedSettingsControl::NvJpeg,
                ConsolidatedSettingsControl::LibRawOutOfProcess};
        case ConsolidatedSettingsPage::Behavior:
            return {
                ConsolidatedSettingsControl::RecursiveBrowsing,
                ConsolidatedSettingsControl::ShowSubfolders,
                ConsolidatedSettingsControl::CloseOnEscape,
                ConsolidatedSettingsControl::SingleInstance,
                ConsolidatedSettingsControl::KeepInNotificationArea,
                ConsolidatedSettingsControl::QuickSendShortcutOrder};
        default:
            return {};
        }
    }

    bool ExperimentalSettingsFocusTargetAvailable(const ExperimentalSettingsDialogState& state,
                                                  const ExperimentalSettingsFocusTarget& target)
    {
        switch (target.kind)
        {
        case ExperimentalSettingsFocusTargetKind::Tab:
            return target.index >= 0 && target.index < static_cast<int>(state.tabRects.size());
        case ExperimentalSettingsFocusTargetKind::CustomControl:
        {
            if (target.index < 0 || target.index >= static_cast<int>(ConsolidatedSettingsControl::Count))
            {
                return false;
            }
            const auto control = static_cast<ConsolidatedSettingsControl>(target.index);
            return ExperimentalSettingsNativeWindow(state, control) == nullptr
                && ExperimentalSettingsControlAvailable(state, control);
        }
        case ExperimentalSettingsFocusTargetKind::NativeControl:
        {
            if (target.index < 0 || target.index >= static_cast<int>(ConsolidatedSettingsControl::Count))
            {
                return false;
            }
            const HWND nativeWindow = ExperimentalSettingsNativeWindow(
                state,
                static_cast<ConsolidatedSettingsControl>(target.index));
            return nativeWindow
                && IsWindow(nativeWindow) != FALSE
                && IsWindowVisible(nativeWindow) != FALSE
                && IsWindowEnabled(nativeWindow) != FALSE;
        }
        case ExperimentalSettingsFocusTargetKind::ApplyButton:
        case ExperimentalSettingsFocusTargetKind::OkButton:
        case ExperimentalSettingsFocusTargetKind::CancelButton:
            return true;
        case ExperimentalSettingsFocusTargetKind::None:
        default:
            return false;
        }
    }

    std::vector<ExperimentalSettingsFocusTarget> ExperimentalSettingsFocusSequence(
        const ExperimentalSettingsDialogState& state)
    {
        std::vector<ExperimentalSettingsFocusTarget> sequence;
        for (int index = 0; index < static_cast<int>(state.tabRects.size()); ++index)
        {
            sequence.push_back({ExperimentalSettingsFocusTargetKind::Tab, index});
        }

        for (const auto control : ExperimentalSettingsPageControlOrder(state.page))
        {
            if (!ExperimentalSettingsControlAvailable(state, control))
            {
                continue;
            }

            const auto kind = ExperimentalSettingsNativeWindow(state, control)
                ? ExperimentalSettingsFocusTargetKind::NativeControl
                : ExperimentalSettingsFocusTargetKind::CustomControl;
            sequence.push_back({kind, static_cast<int>(control)});
        }

        sequence.push_back({ExperimentalSettingsFocusTargetKind::ApplyButton});
        sequence.push_back({ExperimentalSettingsFocusTargetKind::OkButton});
        sequence.push_back({ExperimentalSettingsFocusTargetKind::CancelButton});
        return sequence;
    }

    ExperimentalSettingsFocusTarget ExperimentalSettingsCurrentFocusTarget(
        const ExperimentalSettingsDialogState& state)
    {
        const HWND focus = GetFocus();
        if (focus == state.dialogWindow
            && ExperimentalSettingsFocusTargetAvailable(state, state.focusedTarget))
        {
            return state.focusedTarget;
        }

        for (const auto& target : ExperimentalSettingsFocusSequence(state))
        {
            if (target.kind == ExperimentalSettingsFocusTargetKind::NativeControl
                && ExperimentalSettingsNativeWindow(
                       state,
                       static_cast<ConsolidatedSettingsControl>(target.index)) == focus)
            {
                return target;
            }
        }
        return {};
    }

    bool ExperimentalSettingsChoiceDropped(const ExperimentalSettingsDialogState& state)
    {
        const HWND focus = GetFocus();
        for (const HWND choice : state.nativeControls)
        {
            if (choice == focus && SendMessageW(choice, CB_GETDROPPEDSTATE, 0, 0) != FALSE)
            {
                return true;
            }
        }
        return false;
    }

    bool ScrollExperimentalSettingsTargetIntoView(ExperimentalSettingsDialogState& state,
                                                  const ExperimentalSettingsFocusTarget& target)
    {
        if (target.kind != ExperimentalSettingsFocusTargetKind::CustomControl
            && target.kind != ExperimentalSettingsFocusTargetKind::NativeControl)
        {
            return false;
        }
        if (target.index < 0 || target.index >= static_cast<int>(ConsolidatedSettingsControl::Count))
        {
            return false;
        }

        RECT bounds{};
        const auto control = static_cast<ConsolidatedSettingsControl>(target.index);
        if (target.kind == ExperimentalSettingsFocusTargetKind::NativeControl)
        {
            const HWND nativeWindow = ExperimentalSettingsNativeWindow(state, control);
            if (!nativeWindow || !GetWindowRect(nativeWindow, &bounds))
            {
                return false;
            }
            POINT topLeft{bounds.left, bounds.top};
            POINT bottomRight{bounds.right, bounds.bottom};
            ScreenToClient(state.dialogWindow, &topLeft);
            ScreenToClient(state.dialogWindow, &bottomRight);
            bounds = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        }
        else
        {
            bounds = state.controlRects[static_cast<std::size_t>(target.index)];
        }

        int nextOffset = state.scrollOffset;
        if (bounds.top < state.bodyViewport.top)
        {
            nextOffset += bounds.top - state.bodyViewport.top;
        }
        else if (bounds.bottom > state.bodyViewport.bottom)
        {
            nextOffset += bounds.bottom - state.bodyViewport.bottom;
        }
        return SetExperimentalSettingsScrollOffset(state, nextOffset);
    }

    bool FocusExperimentalSettingsTarget(ExperimentalSettingsDialogState& state,
                                         const ExperimentalSettingsFocusTarget& target)
    {
        if (!ExperimentalSettingsFocusTargetAvailable(state, target))
        {
            return false;
        }

        HWND focusWindow = state.dialogWindow;
        if (target.kind == ExperimentalSettingsFocusTargetKind::NativeControl)
        {
            focusWindow = ExperimentalSettingsNativeWindow(
                state,
                static_cast<ConsolidatedSettingsControl>(target.index));
        }

        ScrollExperimentalSettingsTargetIntoView(state, target);

        const ExperimentalSettingsFocusTarget previousTarget = state.focusedTarget;
        state.focusedTarget = target;
        if (GetFocus() != focusWindow)
        {
            SetFocus(focusWindow);
        }

        if (GetFocus() != focusWindow)
        {
            state.focusedTarget = previousTarget;
            return false;
        }

        InvalidateRect(state.dialogWindow, nullptr, FALSE);
        if (state.accessibility)
        {
            state.accessibility->NotifyFocusChanged();
        }
        return true;
    }

    bool FocusFirstExperimentalSettingsPageControl(ExperimentalSettingsDialogState& state)
    {
        for (const auto& target : ExperimentalSettingsFocusSequence(state))
        {
            if (target.kind != ExperimentalSettingsFocusTargetKind::Tab
                && target.kind != ExperimentalSettingsFocusTargetKind::ApplyButton
                && target.kind != ExperimentalSettingsFocusTargetKind::OkButton
                && target.kind != ExperimentalSettingsFocusTargetKind::CancelButton)
            {
                return FocusExperimentalSettingsTarget(state, target);
            }
        }
        return FocusExperimentalSettingsTarget(state, {ExperimentalSettingsFocusTargetKind::ApplyButton});
    }

    void UpdateExperimentalSettingsCustomControlState(ExperimentalSettingsDialogState& state,
                                                      ConsolidatedSettingsControl control)
    {
        if (control == ConsolidatedSettingsControl::ThumbnailCacheAutomatic)
        {
            const bool enabled = !ExperimentalSettingsChecked(*state.settings, control);
            EnableWindow(state.numericEdits[2], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSpins[2], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSliders[2], enabled ? TRUE : FALSE);
        }
        else if (control == ConsolidatedSettingsControl::MetadataCacheAutomatic)
        {
            const bool enabled = !ExperimentalSettingsChecked(*state.settings, control);
            EnableWindow(state.numericEdits[3], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSpins[3], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSliders[3], enabled ? TRUE : FALSE);
        }
        else if (control == ConsolidatedSettingsControl::PrefetchDepthAutomatic)
        {
            const bool enabled = !ExperimentalSettingsChecked(*state.settings, control);
            EnableWindow(state.numericEdits[4], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSpins[4], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSliders[4], enabled ? TRUE : FALSE);
        }
        else if (control == ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic)
        {
            const bool enabled = !ExperimentalSettingsChecked(*state.settings, control);
            EnableWindow(state.numericEdits[5], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSpins[5], enabled ? TRUE : FALSE);
            EnableWindow(state.numericSliders[5], enabled ? TRUE : FALSE);
        }

        if (control == ConsolidatedSettingsControl::ThemeLight
            || control == ConsolidatedSettingsControl::ThemeDark)
        {
            ApplyExperimentalSettingsTheme(state);
        }
        if (control == ConsolidatedSettingsControl::ThumbnailCacheAutomatic
            || control == ConsolidatedSettingsControl::MetadataCacheAutomatic
            || control == ConsolidatedSettingsControl::PrefetchDepthAutomatic
            || control == ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic)
        {
            UpdateExperimentalSettingsCacheValues(state);
        }
        InvalidateRect(state.dialogWindow, nullptr, FALSE);
        if (state.accessibility)
        {
            state.accessibility->NotifyStateChanged(CHILDID_SELF);
        }
    }

    void ActivateExperimentalSettingsTarget(ExperimentalSettingsDialogState& state,
                                            const ExperimentalSettingsFocusTarget& target)
    {
        if (!ExperimentalSettingsFocusTargetAvailable(state, target))
        {
            return;
        }

        switch (target.kind)
        {
        case ExperimentalSettingsFocusTargetKind::Tab:
            state.page = static_cast<ConsolidatedSettingsPage>(target.index);
            state.scrollOffset = 0;
            LayoutExperimentalSettings(state);
            FocusFirstExperimentalSettingsPageControl(state);
            return;
        case ExperimentalSettingsFocusTargetKind::CustomControl:
            ExperimentalSettingsToggle(
                state,
                static_cast<ConsolidatedSettingsControl>(target.index));
            UpdateExperimentalSettingsCustomControlState(
                state,
                static_cast<ConsolidatedSettingsControl>(target.index));
            return;
        case ExperimentalSettingsFocusTargetKind::ApplyButton:
            SendMessageW(state.dialogWindow, WM_COMMAND, MAKEWPARAM(kExperimentalSettingsApplyId, BN_CLICKED), 0);
            return;
        case ExperimentalSettingsFocusTargetKind::OkButton:
            SendMessageW(state.dialogWindow, WM_COMMAND, MAKEWPARAM(kExperimentalSettingsOkId, BN_CLICKED), 0);
            return;
        case ExperimentalSettingsFocusTargetKind::CancelButton:
            SendMessageW(state.dialogWindow, WM_COMMAND, MAKEWPARAM(kExperimentalSettingsCancelId, BN_CLICKED), 0);
            return;
        case ExperimentalSettingsFocusTargetKind::NativeControl:
        case ExperimentalSettingsFocusTargetKind::None:
        default:
            return;
        }
    }

    bool TryGetAdjacentExperimentalSettingsRadioControl(ConsolidatedSettingsControl control,
                                                        ConsolidatedSettingsControl* adjacent)
    {
        if (!adjacent)
        {
            return false;
        }

        switch (control)
        {
        case ConsolidatedSettingsControl::ViewerWheelZoom:
        case ConsolidatedSettingsControl::ViewerWheelNavigate:
            *adjacent = control == ConsolidatedSettingsControl::ViewerWheelZoom
                ? ConsolidatedSettingsControl::ViewerWheelNavigate
                : ConsolidatedSettingsControl::ViewerWheelZoom;
            return true;
        case ConsolidatedSettingsControl::RawPreferRaw:
        case ConsolidatedSettingsControl::RawPreferJpeg:
            *adjacent = control == ConsolidatedSettingsControl::RawPreferRaw
                ? ConsolidatedSettingsControl::RawPreferJpeg
                : ConsolidatedSettingsControl::RawPreferRaw;
            return true;
        case ConsolidatedSettingsControl::ThemeLight:
        case ConsolidatedSettingsControl::ThemeDark:
            *adjacent = control == ConsolidatedSettingsControl::ThemeLight
                ? ConsolidatedSettingsControl::ThemeDark
                : ConsolidatedSettingsControl::ThemeLight;
            return true;
        default:
            return false;
        }
    }

    bool HandleExperimentalSettingsMnemonic(ExperimentalSettingsDialogState& state,
                                             WPARAM wParam)
    {
        wchar_t mnemonic = static_cast<wchar_t>(wParam);
        if (mnemonic >= L'a' && mnemonic <= L'z')
        {
            mnemonic = static_cast<wchar_t>(mnemonic - (L'a' - L'A'));
        }

        for (int index = 0; index < static_cast<int>(state.tabRects.size()); ++index)
        {
            if (ExperimentalSettingsPageMnemonic(static_cast<ConsolidatedSettingsPage>(index)) == mnemonic)
            {
                ActivateExperimentalSettingsTarget(
                    state,
                    {ExperimentalSettingsFocusTargetKind::Tab, index});
                return true;
            }
        }

        for (const auto control : ExperimentalSettingsPageControlOrder(state.page))
        {
            if (ExperimentalSettingsControlMnemonic(control) != mnemonic
                || !ExperimentalSettingsControlAvailable(state, control))
            {
                continue;
            }

            const auto kind = ExperimentalSettingsNativeWindow(state, control)
                ? ExperimentalSettingsFocusTargetKind::NativeControl
                : ExperimentalSettingsFocusTargetKind::CustomControl;
            return FocusExperimentalSettingsTarget(
                state,
                {kind, static_cast<int>(control)});
        }

        if (mnemonic == L'A')
        {
            ActivateExperimentalSettingsTarget(state, {ExperimentalSettingsFocusTargetKind::ApplyButton});
            return true;
        }
        if (mnemonic == L'O')
        {
            ActivateExperimentalSettingsTarget(state, {ExperimentalSettingsFocusTargetKind::OkButton});
            return true;
        }
        if (mnemonic == L'C')
        {
            ActivateExperimentalSettingsTarget(state, {ExperimentalSettingsFocusTargetKind::CancelButton});
            return true;
        }
        return false;
    }

    bool HandleExperimentalSettingsKeyboardInput(ExperimentalSettingsDialogState& state,
                                                 UINT message,
                                                 WPARAM wParam)
    {
        if ((message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
            || (wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000) != 0))
        {
            return false;
        }

        if (message == WM_SYSKEYDOWN)
        {
            if (wParam == VK_F10)
            {
                return false;
            }
            return HandleExperimentalSettingsMnemonic(state, wParam);
        }

        if (wParam == VK_TAB && ExperimentalSettingsChoiceDropped(state))
        {
            return false;
        }

        const ExperimentalSettingsFocusTarget current = ExperimentalSettingsCurrentFocusTarget(state);
        const auto sequence = ExperimentalSettingsFocusSequence(state);
        if (wParam == VK_TAB)
        {
            if (sequence.empty())
            {
                return false;
            }

            const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const auto currentIterator = std::find(sequence.begin(), sequence.end(), current);
            const int currentIndex = currentIterator == sequence.end()
                ? (reverse ? 0 : static_cast<int>(sequence.size()) - 1)
                : static_cast<int>(std::distance(sequence.begin(), currentIterator));
            const int direction = reverse ? -1 : 1;
            const int nextIndex = (currentIndex + direction + static_cast<int>(sequence.size()))
                % static_cast<int>(sequence.size());
            return FocusExperimentalSettingsTarget(state, sequence[static_cast<std::size_t>(nextIndex)]);
        }

        if (current.kind == ExperimentalSettingsFocusTargetKind::Tab
            && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN))
        {
            const int direction = (wParam == VK_LEFT || wParam == VK_UP) ? -1 : 1;
            const int pageCount = static_cast<int>(state.tabRects.size());
            const int nextPage = (current.index + direction + pageCount) % pageCount;
            state.page = static_cast<ConsolidatedSettingsPage>(nextPage);
            state.scrollOffset = 0;
            LayoutExperimentalSettings(state);
            FocusFirstExperimentalSettingsPageControl(state);
            return true;
        }

        if (current.kind == ExperimentalSettingsFocusTargetKind::CustomControl
            && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN))
        {
            ConsolidatedSettingsControl adjacent{};
            if (TryGetAdjacentExperimentalSettingsRadioControl(
                    static_cast<ConsolidatedSettingsControl>(current.index),
                    &adjacent)
                && ExperimentalSettingsControlAvailable(state, adjacent))
            {
                ActivateExperimentalSettingsTarget(
                    state,
                    {ExperimentalSettingsFocusTargetKind::CustomControl, static_cast<int>(adjacent)});
                FocusExperimentalSettingsTarget(
                    state,
                    {ExperimentalSettingsFocusTargetKind::CustomControl, static_cast<int>(adjacent)});
                return true;
            }
        }

        if (wParam != VK_RETURN && wParam != VK_SPACE)
        {
            return false;
        }

        switch (current.kind)
        {
        case ExperimentalSettingsFocusTargetKind::Tab:
        case ExperimentalSettingsFocusTargetKind::CustomControl:
        case ExperimentalSettingsFocusTargetKind::ApplyButton:
        case ExperimentalSettingsFocusTargetKind::OkButton:
        case ExperimentalSettingsFocusTargetKind::CancelButton:
            ActivateExperimentalSettingsTarget(state, current);
            return true;
        case ExperimentalSettingsFocusTargetKind::NativeControl:
        case ExperimentalSettingsFocusTargetKind::None:
        default:
            return false;
        }
    }

    void RebuildExperimentalSettingsTextResources(ExperimentalSettingsDialogState& state)
    {
        if (!state.settings)
        {
            return;
        }

        auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
        const auto size = state.settings->appTextSize;
        state.bodyFormat = renderer.CreateTextFormat(L"Segoe UI", hyperbrowse::util::ScaleAppTextPointSize(16.0f, size));
        state.smallFormat = renderer.CreateTextFormat(L"Segoe UI", hyperbrowse::util::ScaleAppTextPointSize(13.0f, size));
        state.buttonFormat = renderer.CreateTextFormat(L"Segoe UI", hyperbrowse::util::ScaleAppTextPointSize(15.0f, size), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        for (IDWriteTextFormat* format : {state.bodyFormat.Get(), state.smallFormat.Get(), state.buttonFormat.Get()})
        {
            if (format)
            {
                format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }

        DeleteFontIfOwned(state.controlFont);
        state.controlFont = CreateSystemUiFont(size, state.dpi);
        const HFONT font = state.controlFont
            ? state.controlFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        DeleteFontIfOwned(state.numericFont);
        state.numericFont = CreateDialogUiFont(16, FW_NORMAL, size, state.dpi);
        const HFONT numericFont = state.numericFont ? state.numericFont : font;
        DeleteFontIfOwned(state.choiceFont);
        state.choiceFont = CreateDialogUiFont(16, FW_NORMAL, size, state.dpi);
        for (std::size_t index = 0; index < state.nativeControls.size(); ++index)
        {
            const HWND control = state.nativeControls[index];
            if (control)
            {
                const auto controlId = static_cast<ConsolidatedSettingsControl>(index);
                HFONT controlFont = font;
                if (controlId == ConsolidatedSettingsControl::QuickSendShortcutOrder && numericFont)
                {
                    controlFont = numericFont;
                }
                else if (ExperimentalSettingsControlIsChoice(controlId) && state.choiceFont)
                {
                    controlFont = state.choiceFont;
                }
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
            }
        }
        for (const HWND edit : state.numericEdits)
        {
            if (edit)
            {
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(numericFont), TRUE);
            }
        }
    }
    void DrawExperimentalSettingsText(ID2D1RenderTarget* target,
                                      IDWriteTextFormat* format,
                                      const std::wstring& text,
                                      const RECT& bounds,
                                      ID2D1Brush* brush,
                                      wchar_t mnemonic = 0)
    {
        if (target && format && brush && !text.empty())
        {
            auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
            const int mnemonicIndex = ExperimentalSettingsMnemonicIndex(text, mnemonic);
            if (mnemonicIndex >= 0)
            {
                const auto layout = renderer.CreateTextLayout(
                    text,
                    format,
                    static_cast<float>(std::max<LONG>(1, bounds.right - bounds.left)),
                    static_cast<float>(std::max<LONG>(1, bounds.bottom - bounds.top)));
                if (layout)
                {
                    layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{
                        static_cast<UINT32>(mnemonicIndex), 1});
                    target->DrawTextLayout(
                        D2D1::Point2F(static_cast<float>(bounds.left), static_cast<float>(bounds.top)),
                        layout.Get(),
                        brush);
                    return;
                }
            }
            target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format,
                             hyperbrowse::render::ToD2DRect(bounds), brush);
        }
    }

    struct PerformanceSliderRange
    {
        std::uint64_t minimum{};
        std::uint64_t maximum{};
    };

    constexpr int kPerformanceSliderPositionMinimum = 0;
    constexpr int kPerformanceSliderPositionMaximum = 100;

    PerformanceSliderRange GetPerformanceSliderRange(hyperbrowse::util::ResourceProfile profile,
                                                     std::size_t index)
    {
        switch (index)
        {
        case 2:
        {
            const auto range = hyperbrowse::util::RecommendedThumbnailCacheRange(profile);
            return {std::max<std::uint64_t>(1, range.minimum / (1024ULL * 1024ULL)),
                    std::max<std::uint64_t>(1, range.maximum / (1024ULL * 1024ULL))};
        }
        case 3:
        {
            const auto range = hyperbrowse::util::RecommendedMetadataCacheRange(profile);
            return {range.minimum, range.maximum};
        }
        case 4:
            return {hyperbrowse::util::kMinimumPrefetchDepth, hyperbrowse::util::kMaximumPrefetchDepth};
        case 5:
        {
            const auto range = hyperbrowse::util::RecommendedPersistentThumbnailCacheRange(profile);
            return {std::max<std::uint64_t>(1, range.minimum / (1024ULL * 1024ULL)),
                    std::max<std::uint64_t>(1, range.maximum / (1024ULL * 1024ULL))};
        }
        default:
            return {1, 1};
        }
    }

    int PerformanceSliderPosition(PerformanceSliderRange range, std::uint64_t value)
    {
        if (range.maximum <= range.minimum)
        {
            return kPerformanceSliderPositionMinimum;
        }
        const std::uint64_t clamped = std::clamp(value, range.minimum, range.maximum);
        return static_cast<int>(((clamped - range.minimum)
            * static_cast<std::uint64_t>(kPerformanceSliderPositionMaximum))
            / (range.maximum - range.minimum));
    }

    std::uint64_t PerformanceSliderValue(PerformanceSliderRange range, int position)
    {
        const std::uint64_t clampedPosition = static_cast<std::uint64_t>(std::clamp(
            position,
            kPerformanceSliderPositionMinimum,
            kPerformanceSliderPositionMaximum));
        return range.minimum + ((range.maximum - range.minimum) * clampedPosition)
            / static_cast<std::uint64_t>(kPerformanceSliderPositionMaximum);
    }

    void UpdateExperimentalSettingsCacheValues(ExperimentalSettingsDialogState& state)
    {
        if (!state.settings)
        {
            return;
        }

        const HWND profileCombo = state.nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::ResourceProfile)];
        const int profileIndex = profileCombo
            ? static_cast<int>(SendMessageW(profileCombo, CB_GETCURSEL, 0, 0))
            : static_cast<int>(state.settings->resourceProfile);
        if (profileIndex < 0 || profileIndex > 3)
        {
            return;
        }

        const auto profile = static_cast<hyperbrowse::util::ResourceProfile>(profileIndex);
        const bool thumbnailAutomatic = state.settings->thumbnailCacheCapacityOverrideBytes == 0;
        const bool metadataAutomatic = state.settings->metadataCacheCapacityOverrideEntries == 0;
        const bool prefetchAutomatic = state.settings->prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth;
        const bool persistentAutomatic = state.settings->persistentThumbnailCacheCapacityOverrideBytes == 0;
        if (thumbnailAutomatic && state.numericEdits[2])
        {
            const auto capacityMegabytes = hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(0, profile)
                / (1024ULL * 1024ULL);
            SetWindowTextW(state.numericEdits[2], std::to_wstring(capacityMegabytes).c_str());
            if (state.numericSpins[2])
            {
                SendMessageW(state.numericSpins[2], UDM_SETPOS32, 0, static_cast<LPARAM>(capacityMegabytes));
            }
        }
        if (metadataAutomatic && state.numericEdits[3])
        {
            const auto capacityEntries = hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(0, profile);
            SetWindowTextW(state.numericEdits[3], std::to_wstring(capacityEntries).c_str());
            if (state.numericSpins[3])
            {
                SendMessageW(state.numericSpins[3], UDM_SETPOS32, 0, static_cast<LPARAM>(capacityEntries));
            }
        }
        if (prefetchAutomatic && state.numericEdits[4])
        {
            const int depth = hyperbrowse::util::ResolvePrefetchDepth(profile, hyperbrowse::util::kAutomaticPrefetchDepth);
            SetWindowTextW(state.numericEdits[4], std::to_wstring(depth).c_str());
            if (state.numericSpins[4])
            {
                SendMessageW(state.numericSpins[4], UDM_SETPOS32, 0, static_cast<LPARAM>(depth));
            }
        }
        if (persistentAutomatic && state.numericEdits[5])
        {
            const auto capacityMegabytes = hyperbrowse::services::ThumbnailScheduler::ResolvePersistentCacheCapacityBytes(0, profile)
                / (1024ULL * 1024ULL);
            SetWindowTextW(state.numericEdits[5], std::to_wstring(capacityMegabytes).c_str());
            if (state.numericSpins[5])
            {
                SendMessageW(state.numericSpins[5], UDM_SETPOS32, 0, static_cast<LPARAM>(capacityMegabytes));
            }
        }

        for (const std::size_t index : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}})
        {
            if (!state.numericSliders[index])
            {
                continue;
            }
            const PerformanceSliderRange range = GetPerformanceSliderRange(profile, index);
            std::uint64_t value = 1;
            std::size_t parsed = 0;
            if (index == 2)
            {
                value = TryParsePositiveSizeValue(ReadWindowText(state.numericEdits[index]), &parsed)
                    ? parsed
                    : range.minimum;
            }
            else if (index == 3)
            {
                value = TryParsePositiveSizeValue(ReadWindowText(state.numericEdits[index]), &parsed)
                    ? parsed
                    : range.minimum;
            }
            else if (index == 4)
            {
                UINT depth = 0;
                value = TryReadDialogUInt(state.numericEdits[index], 1, 16, &depth) ? depth : range.minimum;
            }
            else
            {
                value = TryParsePositiveSizeValue(ReadWindowText(state.numericEdits[index]), &parsed)
                    ? parsed
                    : range.minimum;
            }
            SendMessageW(state.numericSliders[index], TBM_SETPOS, TRUE,
                         PerformanceSliderPosition(range, value));
        }
    }

    void HandleExperimentalSettingsSliderChange(ExperimentalSettingsDialogState& state, HWND source)
    {
        if (!state.settings || !source)
        {
            return;
        }

        std::size_t index = state.numericSliders.size();
        for (std::size_t candidate = 2; candidate < state.numericSliders.size(); ++candidate)
        {
            if (state.numericSliders[candidate] == source)
            {
                index = candidate;
                break;
            }
        }
        if (index >= state.numericSliders.size())
        {
            return;
        }

        const auto profile = state.settings->resourceProfile;
        const PerformanceSliderRange range = GetPerformanceSliderRange(profile, index);
        const std::uint64_t value = PerformanceSliderValue(
            range,
            static_cast<int>(SendMessageW(source, TBM_GETPOS, 0, 0)));
        if (index == 2)
        {
            state.settings->thumbnailCacheCapacityOverrideBytes = hyperbrowse::util::SaturatingCastToSizeT(value * 1024ULL * 1024ULL);
            SetWindowTextW(state.numericEdits[index], std::to_wstring(value).c_str());
        }
        else if (index == 3)
        {
            state.settings->metadataCacheCapacityOverrideEntries = hyperbrowse::util::SaturatingCastToSizeT(value);
            SetWindowTextW(state.numericEdits[index], std::to_wstring(value).c_str());
        }
        else if (index == 4)
        {
            state.settings->prefetchDepthOverride = static_cast<int>(value);
            SetWindowTextW(state.numericEdits[index], std::to_wstring(value).c_str());
        }
        else
        {
            state.settings->persistentThumbnailCacheCapacityOverrideBytes = hyperbrowse::util::SaturatingCastToSizeT(value * 1024ULL * 1024ULL);
            SetWindowTextW(state.numericEdits[index], std::to_wstring(value).c_str());
        }
        if (state.numericSpins[index])
        {
            SendMessageW(state.numericSpins[index], UDM_SETPOS32, 0, static_cast<LPARAM>(value));
        }
        InvalidateRect(state.dialogWindow, nullptr, FALSE);
        if (state.accessibility)
        {
            state.accessibility->NotifyStateChanged(CHILDID_SELF);
        }
    }

    void PaintExperimentalSettings(ExperimentalSettingsDialogState& state)
    {
        if (!state.renderTarget || !state.settings)
        {
            return;
        }
        RECT client{};
        GetClientRect(state.dialogWindow, &client);
        const UINT dpi = std::max<UINT>(96, state.dpi);
        const auto textSize = state.settings->appTextSize;
        const auto scale = [textSize](int value)
        {
            return hyperbrowse::util::ScaleAppTextDimension(value, textSize);
        };
        const auto scaleF = [textSize](float value)
        {
            return value * hyperbrowse::util::AppTextSizeScale(textSize);
        };
        const float logicalWidth = static_cast<float>(MulDiv(client.right, 96, static_cast<int>(dpi)));
        const COLORREF windowColor = hyperbrowse::ui::MakeDialogTheme(
            state.settings->darkTheme).windowBackground;
        const bool dialogHasSemanticFocus = GetFocus() == state.dialogWindow;
        const auto customTargetFocused = [&](ExperimentalSettingsFocusTargetKind kind, int index)
        {
            return dialogHasSemanticFocus
                && state.focusedTarget.kind == kind
                && state.focusedTarget.index == index;
        };
        state.renderTarget->BeginDraw();
        state.renderTarget->Clear(hyperbrowse::render::ToD2DColor(windowColor));
        state.renderTarget->FillRectangle(
            D2D1::RectF(static_cast<float>(scale(20)), static_cast<float>(scale(14)),
                        logicalWidth - static_cast<float>(scale(20)), static_cast<float>(scale(60))),
            state.panelBrush.Get());

        for (std::size_t index = 0; index < state.tabRects.size(); ++index)
        {
            const RECT& tab = state.tabRects[index];
            const bool selected = index == static_cast<std::size_t>(state.page);
            const bool hovered = state.hoveredTab == static_cast<int>(index);
            if (selected)
            {
                const float underlineTop = static_cast<float>(tab.bottom - scale(3));
                state.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(static_cast<float>(tab.left + scale(18)), underlineTop,
                                                   static_cast<float>(tab.right - scale(18)), static_cast<float>(tab.bottom)),
                                      scaleF(1.5f), scaleF(1.5f)),
                    state.accentBrush.Get());
            }
            else if (hovered)
            {
                state.renderTarget->FillRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(tab, scaleF(6.0f), scaleF(6.0f)), state.accentFillBrush.Get());
            }
            if (customTargetFocused(ExperimentalSettingsFocusTargetKind::Tab, static_cast<int>(index)))
            {
                RECT focusBounds = tab;
                InflateRect(&focusBounds, -scale(2), -scale(2));
                state.renderTarget->DrawRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(focusBounds, scaleF(5.0f), scaleF(5.0f)),
                    state.accentBrush.Get(),
                    scaleF(2.0f));
            }
            RECT textBounds = tab;
            textBounds.left += scale(12);
            textBounds.right -= scale(10);
            DrawExperimentalSettingsText(state.renderTarget.Get(), state.bodyFormat.Get(),
                                         ExperimentalSettingsPageTitle(static_cast<ConsolidatedSettingsPage>(index)), textBounds,
                                         selected ? state.accentBrush.Get() : state.mutedTextBrush.Get(),
                                         ExperimentalSettingsPageMnemonic(static_cast<ConsolidatedSettingsPage>(index)));
        }

        state.renderTarget->PushAxisAlignedClip(
            D2D1::RectF(static_cast<float>(state.bodyViewport.left),
                        static_cast<float>(state.bodyViewport.top),
                        static_cast<float>(state.bodyViewport.right),
                        static_cast<float>(state.bodyViewport.bottom)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (std::size_t index = 0; index < state.labels.size(); ++index)
        {
            const ExperimentalSettingsLabel& label = state.labels[index];
            const bool enabled = ExperimentalSettingsCustomControlEnabled(state, label.mnemonicControl);
            DrawExperimentalSettingsText(state.renderTarget.Get(),
                                         enabled && !label.muted ? state.bodyFormat.Get() : state.smallFormat.Get(),
                                         label.text,
                                         label.bounds,
                                         enabled && !label.muted ? state.textBrush.Get() : state.mutedTextBrush.Get(),
                                         label.mnemonic);
        }

        for (std::size_t index = 0; index < state.controlRects.size(); ++index)
        {
            const auto control = static_cast<ConsolidatedSettingsControl>(index);
            const RECT& bounds = state.controlRects[index];
            if (bounds.right <= bounds.left || bounds.bottom <= bounds.top || state.nativeControls[index])
            {
                continue;
            }
            const bool enabled = ExperimentalSettingsCustomControlEnabled(state, control);
            const bool checked = ExperimentalSettingsChecked(*state.settings, control);
            const bool hovered = enabled && state.hoveredControl == static_cast<int>(index);
            if (ExperimentalSettingsControlIsAction(control))
            {
                state.renderTarget->FillRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(bounds, scaleF(5.0f), scaleF(5.0f)),
                    hovered ? state.accentFillBrush.Get() : state.fieldBrush.Get());
                state.renderTarget->DrawRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(bounds, scaleF(5.0f), scaleF(5.0f)),
                    enabled ? state.borderBrush.Get() : state.mutedTextBrush.Get(), scaleF(1.0f));
                RECT textBounds = bounds;
                textBounds.left += scale(14);
                textBounds.right -= scale(10);
                DrawExperimentalSettingsText(state.renderTarget.Get(),
                                             state.bodyFormat.Get(),
                                             L"Trim persistent cache now",
                                             textBounds,
                                             enabled ? state.textBrush.Get() : state.mutedTextBrush.Get(),
                                             L'T');
                if (customTargetFocused(ExperimentalSettingsFocusTargetKind::CustomControl, static_cast<int>(index)))
                {
                    RECT focusBounds = bounds;
                    InflateRect(&focusBounds, -scale(2), -scale(2));
                    state.renderTarget->DrawRoundedRectangle(
                        hyperbrowse::render::ToD2DRoundedRect(focusBounds, scaleF(5.0f), scaleF(5.0f)),
                        state.accentBrush.Get(),
                        scaleF(2.0f));
                }
                continue;
            }
            if (ExperimentalSettingsControlIsChoice(control))
            {
                state.renderTarget->FillRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(bounds, scaleF(5.0f), scaleF(5.0f)),
                    enabled && hovered ? state.accentFillBrush.Get() : state.fieldBrush.Get());
                state.renderTarget->DrawRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(bounds, scaleF(5.0f), scaleF(5.0f)),
                    enabled ? state.borderBrush.Get() : state.mutedTextBrush.Get(), scaleF(1.0f));
                RECT valueBounds = bounds;
                valueBounds.left += scale(14);
                DrawExperimentalSettingsText(state.renderTarget.Get(),
                                             state.bodyFormat.Get(),
                                             ExperimentalSettingsChoiceValue(*state.settings, control),
                                             valueBounds,
                                             enabled ? state.textBrush.Get() : state.mutedTextBrush.Get());
            }
            else
            {
                const RECT indicator{bounds.left + scale(3), bounds.top + scale(7), bounds.left + scale(22), bounds.top + scale(26)};
                if (control == ConsolidatedSettingsControl::ViewerWheelZoom || control == ConsolidatedSettingsControl::ViewerWheelNavigate
                    || control == ConsolidatedSettingsControl::RawPreferRaw || control == ConsolidatedSettingsControl::RawPreferJpeg
                    || control == ConsolidatedSettingsControl::ThemeLight || control == ConsolidatedSettingsControl::ThemeDark)
                {
                    state.renderTarget->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F((indicator.left + indicator.right) / 2.0f, (indicator.top + indicator.bottom) / 2.0f),
                                      scaleF(8.0f), scaleF(8.0f)),
                        enabled ? state.borderBrush.Get() : state.mutedTextBrush.Get(), scaleF(1.5f));
                    if (checked)
                    {
                        state.renderTarget->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F((indicator.left + indicator.right) / 2.0f, (indicator.top + indicator.bottom) / 2.0f),
                                          scaleF(4.5f), scaleF(4.5f)),
                            enabled ? state.accentBrush.Get() : state.mutedTextBrush.Get());
                    }
                }
                else
                {
                    state.renderTarget->DrawRoundedRectangle(
                        hyperbrowse::render::ToD2DRoundedRect(indicator, scaleF(3.0f), scaleF(3.0f)),
                        enabled ? state.borderBrush.Get() : state.mutedTextBrush.Get(), scaleF(1.5f));
                    if (checked)
                    {
                        state.renderTarget->FillRoundedRectangle(
                            hyperbrowse::render::ToD2DRoundedRect(indicator, scaleF(3.0f), scaleF(3.0f)),
                            enabled ? state.accentBrush.Get() : state.mutedTextBrush.Get());

                        state.renderTarget->DrawLine(
                            D2D1::Point2F(static_cast<float>(indicator.left + scale(5)), static_cast<float>(indicator.top + scale(10))),
                            D2D1::Point2F(static_cast<float>(indicator.left + scale(9)), static_cast<float>(indicator.top + scale(14))),
                            state.panelBrush.Get(), scaleF(2.0f));
                        state.renderTarget->DrawLine(
                            D2D1::Point2F(static_cast<float>(indicator.left + scale(9)), static_cast<float>(indicator.top + scale(14))),
                            D2D1::Point2F(static_cast<float>(indicator.left + scale(16)), static_cast<float>(indicator.top + scale(5))),
                            state.panelBrush.Get(), scaleF(2.0f));
                    }
                }
            }
            if (customTargetFocused(ExperimentalSettingsFocusTargetKind::CustomControl, static_cast<int>(index)))
            {
                RECT focusBounds = bounds;
                InflateRect(&focusBounds, -scale(2), -scale(2));
                state.renderTarget->DrawRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(focusBounds, scaleF(5.0f), scaleF(5.0f)),
                    state.accentBrush.Get(),
                    scaleF(2.0f));
            }
        }
        state.renderTarget->PopAxisAlignedClip();
        const auto drawButton = [&](const RECT& bounds,
                                    const wchar_t* text,
                                    bool primary,
                                    int hoverId,
                                    ExperimentalSettingsFocusTargetKind focusKind)
        {
            const bool hovered = state.hoveredControl == hoverId;
            state.renderTarget->FillRoundedRectangle(hyperbrowse::render::ToD2DRoundedRect(bounds, scaleF(5.0f), scaleF(5.0f)),
                                                     primary ? state.accentBrush.Get() : hovered ? state.accentFillBrush.Get() : state.fieldBrush.Get());
            if (!primary)
            {
                state.renderTarget->DrawRoundedRectangle(hyperbrowse::render::ToD2DRoundedRect(bounds, scaleF(5.0f), scaleF(5.0f)),
                                                         hovered ? state.accentBrush.Get() : state.borderBrush.Get(), scaleF(1.0f));
            }
            RECT textBounds = bounds;
            textBounds.left += scale(12);
            textBounds.right -= scale(10);
            DrawExperimentalSettingsText(state.renderTarget.Get(), state.buttonFormat.Get(), text, textBounds,
                                         primary ? state.buttonTextBrush.Get() : state.textBrush.Get(),
                                         text[0] == L'A' ? L'A' : text[0] == L'O' ? L'O' : L'C');
            if (customTargetFocused(focusKind, -1))
            {
                RECT focusBounds = bounds;
                InflateRect(&focusBounds, -scale(2), -scale(2));
                state.renderTarget->DrawRoundedRectangle(
                    hyperbrowse::render::ToD2DRoundedRect(focusBounds, scaleF(5.0f), scaleF(5.0f)),
                    state.accentBrush.Get(),
                    scaleF(2.0f));
            }
        };
        drawButton(state.applyButtonRect, L"Apply", false, -10, ExperimentalSettingsFocusTargetKind::ApplyButton);
        drawButton(state.okButtonRect, L"OK", true, -11, ExperimentalSettingsFocusTargetKind::OkButton);
        drawButton(state.cancelButtonRect, L"Cancel", false, -12, ExperimentalSettingsFocusTargetKind::CancelButton);
        state.renderTarget->EndDraw();
    }

    bool CollectExperimentalSettings(HWND hwnd, ExperimentalSettingsDialogState& state)
    {
        std::wstring normalizedQuickSendShortcutOrder;
        if (!hyperbrowse::ui::QuickSendModel::TryNormalizeShortcutOrder(
                ReadWindowText(state.nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)]),
                &normalizedQuickSendShortcutOrder))
        {
            MessageBoxW(hwnd,
                        L"Enter each supported Quick Actions key at most once; unsupported characters are not allowed.",
                        state.settings->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            SetFocus(state.nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)]);
            return false;
        }

        UINT slideshowDuration = 0;
        if (!TryReadDialogUInt(state.numericEdits[0], kSlideshowMinimumDurationMs, kSlideshowMaximumDurationMs, &slideshowDuration))
        {
            MessageBoxW(hwnd, L"Slide duration must be between 250 and 60000 milliseconds.", state.settings->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(state.numericEdits[0]);
            return false;
        }
        UINT transitionDuration = 0;
        if (!TryReadDialogUInt(state.numericEdits[1], kSlideshowMinimumTransitionDurationMs, kSlideshowMaximumTransitionDurationMs, &transitionDuration))
        {
            MessageBoxW(hwnd, L"Transition duration must be between 100 and 5000 milliseconds.", state.settings->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(state.numericEdits[1]);
            return false;
        }
        const bool thumbnailAutomatic = state.settings->thumbnailCacheCapacityOverrideBytes == 0;
        const bool metadataAutomatic = state.settings->metadataCacheCapacityOverrideEntries == 0;
        std::size_t thumbnailMegabytes = 0;
        if (!thumbnailAutomatic && (!TryParsePositiveSizeValue(ReadWindowText(state.numericEdits[2]), &thumbnailMegabytes)
                                     || thumbnailMegabytes > std::numeric_limits<std::uint64_t>::max() / (1024ULL * 1024ULL)))
        {
            MessageBoxW(hwnd, L"Enter a positive thumbnail cache size in megabytes, or keep Follow profile enabled.", state.settings->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(state.numericEdits[2]);
            return false;
        }
        std::size_t metadataEntries = 0;
        if (!metadataAutomatic && !TryParsePositiveSizeValue(ReadWindowText(state.numericEdits[3]), &metadataEntries))
        {
            MessageBoxW(hwnd, L"Enter a positive metadata cache capacity in entries, or keep Follow profile enabled.", state.settings->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(state.numericEdits[3]);
            return false;
        }
        const bool prefetchAutomatic = state.settings->prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth;
        const bool persistentAutomatic = state.settings->persistentThumbnailCacheCapacityOverrideBytes == 0;
        UINT prefetchDepth = 0;
        if (!prefetchAutomatic
            && !TryReadDialogUInt(state.numericEdits[4],
                                  static_cast<UINT>(hyperbrowse::util::kMinimumPrefetchDepth),
                                  static_cast<UINT>(hyperbrowse::util::kMaximumPrefetchDepth),
                                  &prefetchDepth))
        {
            MessageBoxW(hwnd, L"Prefetch depth must be between 1 and 16, or keep Follow profile enabled.", state.settings->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(state.numericEdits[4]);
            return false;
        }
        std::size_t persistentMegabytes = 0;
        if (!persistentAutomatic && (!TryParsePositiveSizeValue(ReadWindowText(state.numericEdits[5]), &persistentMegabytes)
                                      || persistentMegabytes > std::numeric_limits<std::uint64_t>::max() / (1024ULL * 1024ULL)))
        {
            MessageBoxW(hwnd, L"Enter a positive persistent cache size in megabytes, or keep Follow profile enabled.", state.settings->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(state.numericEdits[5]);
            return false;
        }
        state.settings->slideshowIntervalMs = slideshowDuration;
        state.settings->slideshowTransitionDurationMs = transitionDuration;
        state.settings->thumbnailCacheCapacityOverrideBytes = thumbnailAutomatic ? 0 : hyperbrowse::util::SaturatingCastToSizeT(static_cast<std::uint64_t>(thumbnailMegabytes) * 1024ULL * 1024ULL);
        state.settings->metadataCacheCapacityOverrideEntries = metadataAutomatic ? 0 : metadataEntries;
        state.settings->prefetchDepthOverride = prefetchAutomatic
            ? hyperbrowse::util::kAutomaticPrefetchDepth
            : static_cast<int>(prefetchDepth);
        state.settings->persistentThumbnailCacheCapacityOverrideBytes = persistentAutomatic
            ? 0
            : hyperbrowse::util::SaturatingCastToSizeT(static_cast<std::uint64_t>(persistentMegabytes) * 1024ULL * 1024ULL);
        state.settings->quickSendShortcutOrder = std::move(normalizedQuickSendShortcutOrder);
        return true;
    }

    void ApplyExperimentalSettingsTheme(ExperimentalSettingsDialogState& state)
    {
        const bool dark = state.settings && state.settings->darkTheme;
        const hyperbrowse::ui::DialogTheme theme = hyperbrowse::ui::MakeDialogTheme(dark);
        auto createBrush = [&](Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush, COLORREF color)
        {
            if (state.renderTarget)
            {
                state.renderTarget->CreateSolidColorBrush(hyperbrowse::render::ToD2DColor(color), brush.ReleaseAndGetAddressOf());
            }
        };
        createBrush(state.panelBrush, theme.surfaceBackground);
        createBrush(state.fieldBrush, theme.fieldBackground);
        createBrush(state.borderBrush, theme.border);
        createBrush(state.textBrush, theme.text);
        createBrush(state.mutedTextBrush, theme.mutedText);
        createBrush(state.accentBrush, theme.accent);
        createBrush(state.accentFillBrush, theme.accentFill);
        createBrush(state.buttonTextBrush, theme.accentText);
        if (state.editBackgroundBrush)
        {
            DeleteObject(state.editBackgroundBrush);
        }
        state.editBackgroundBrush = CreateSolidBrush(theme.fieldBackground);
        for (const HWND edit : state.numericEdits)
        {
            if (edit)
            {
                SetWindowTheme(edit, dark ? L"" : nullptr, dark ? L"" : nullptr);
            }
        }
        const HWND shortcutOrderEdit = state.nativeControls[
            static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)];
        if (shortcutOrderEdit)
        {
            SetWindowTheme(shortcutOrderEdit, dark ? L"" : nullptr, dark ? L"" : nullptr);
        }
        InvalidateRect(state.dialogWindow, nullptr, FALSE);
    }

    void DrawExperimentalSettingsComboItem(const ExperimentalSettingsDialogState& state,
                                           const DRAWITEMSTRUCT& drawItem)
    {
        if (!state.settings || !drawItem.hDC)
        {
            return;
        }

        const bool editField = (drawItem.itemState & ODS_COMBOBOXEDIT) != 0;
        const bool selected = (drawItem.itemState & ODS_SELECTED) != 0 && !editField;
        const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
        const auto theme = hyperbrowse::ui::MakeDialogTheme(state.settings->darkTheme);
        const COLORREF fieldBackground = theme.fieldBackground;
        const COLORREF selectedBackground = theme.accentFill;
        const COLORREF text = disabled ? theme.mutedText : (selected ? theme.accentText : theme.text);
        const COLORREF border = theme.border;
        const RECT itemRect = drawItem.rcItem;
        HBRUSH backgroundBrush = CreateSolidBrush(selected ? selectedBackground : fieldBackground);
        if (backgroundBrush)
        {
            FillRect(drawItem.hDC, &itemRect, backgroundBrush);
            DeleteObject(backgroundBrush);
        }

        wchar_t itemText[256]{};
        if (drawItem.itemID != static_cast<UINT>(-1))
        {
            SendMessageW(drawItem.hwndItem,
                         CB_GETLBTEXT,
                         drawItem.itemID,
                         reinterpret_cast<LPARAM>(itemText));
        }
        RECT textRect = itemRect;
        const int dpi = static_cast<int>(std::max<UINT>(96, state.dpi));
        textRect.left += ScaleDialogAppTextDimension(8, state.settings->appTextSize, static_cast<UINT>(dpi));
        textRect.right -= ScaleDialogAppTextDimension(8, state.settings->appTextSize, static_cast<UINT>(dpi));
        SetBkMode(drawItem.hDC, TRANSPARENT);
        SetTextColor(drawItem.hDC, text);
        HFONT itemFont = state.choiceFont
            ? state.choiceFont
            : state.controlFont
                ? state.controlFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ previousFont = SelectObject(drawItem.hDC, itemFont);
        DrawTextW(drawItem.hDC,
                  itemText,
                  -1,
                  &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (previousFont && previousFont != HGDI_ERROR)
        {
            SelectObject(drawItem.hDC, previousFont);
        }

        if (editField && (drawItem.itemState & ODS_FOCUS) != 0)
        {
            HBRUSH borderBrush = CreateSolidBrush(border);
            if (borderBrush)
            {
                RECT focusRect = itemRect;
                const int focusInset = ScaleDialogDimension(1, static_cast<UINT>(dpi));
                InflateRect(&focusRect, -focusInset, -focusInset);
                FrameRect(drawItem.hDC, &focusRect, borderBrush);
                DeleteObject(borderBrush);
            }
        }
    }

    LRESULT CALLBACK ExperimentalSettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ExperimentalSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<ExperimentalSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state || !state->settings)
            {
                return -1;
            }
            state->dialogWindow = hwnd;
            auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
            state->renderTarget = renderer.CreateHwndRenderTarget(hwnd, true);
            if (!state->renderTarget)
            {
                return -1;
            }
            const auto size = state->settings->appTextSize;
            state->bodyFormat = renderer.CreateTextFormat(L"Segoe UI", hyperbrowse::util::ScaleAppTextPointSize(16.0f, size));
            state->smallFormat = renderer.CreateTextFormat(L"Segoe UI", hyperbrowse::util::ScaleAppTextPointSize(13.0f, size));
            state->buttonFormat = renderer.CreateTextFormat(L"Segoe UI", hyperbrowse::util::ScaleAppTextPointSize(15.0f, size), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            state->controlFont = CreateSystemUiFont(size, state->dpi);
            if (!state->controlFont)
            {
                state->controlFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            }
            state->numericFont = CreateDialogUiFont(16, FW_NORMAL, size, state->dpi);
            state->choiceFont = CreateDialogUiFont(16, FW_NORMAL, size, state->dpi);
            for (IDWriteTextFormat* format : {state->bodyFormat.Get(), state->smallFormat.Get(), state->buttonFormat.Get()})
            {
                if (format)
                {
                    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                }
            }
            state->editBackgroundBrush = CreateSolidBrush(
                hyperbrowse::ui::MakeDialogTheme(state->settings->darkTheme).fieldBackground);
            const auto profile = state->settings->resourceProfile;
            const auto thumbnailCacheMegabytes = state->settings->thumbnailCacheCapacityOverrideBytes == 0
                ? hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(0, profile) / (1024ULL * 1024ULL)
                : state->settings->thumbnailCacheCapacityOverrideBytes / (1024ULL * 1024ULL);
            const auto metadataCacheEntries = state->settings->metadataCacheCapacityOverrideEntries == 0
                ? hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(0, profile)
                : state->settings->metadataCacheCapacityOverrideEntries;
            const auto prefetchDepth = state->settings->prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth
                ? hyperbrowse::util::ResolvePrefetchDepth(profile, hyperbrowse::util::kAutomaticPrefetchDepth)
                : state->settings->prefetchDepthOverride;
            const auto persistentCacheMegabytes = state->settings->persistentThumbnailCacheCapacityOverrideBytes == 0
                ? hyperbrowse::services::ThumbnailScheduler::ResolvePersistentCacheCapacityBytes(0, profile) / (1024ULL * 1024ULL)
                : state->settings->persistentThumbnailCacheCapacityOverrideBytes / (1024ULL * 1024ULL);
            const std::array<std::pair<int, std::wstring>, 6> editValues{
                std::pair{ES_NUMBER, std::to_wstring(state->settings->slideshowIntervalMs)},
                std::pair{ES_NUMBER, std::to_wstring(state->settings->slideshowTransitionDurationMs)},
                std::pair{ES_NUMBER, std::to_wstring(thumbnailCacheMegabytes)},
                std::pair{ES_NUMBER, std::to_wstring(metadataCacheEntries)},
                std::pair{ES_NUMBER, std::to_wstring(prefetchDepth)},
                std::pair{ES_NUMBER, std::to_wstring(persistentCacheMegabytes)}};
            for (std::size_t index = 0; index < state->numericEdits.size(); ++index)
            {
                state->numericEdits[index] = CreateWindowExW(0, L"EDIT", editValues[index].second.c_str(),
                    WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | editValues[index].first, 0, 0, 0, 0, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(5700 + index)), state->instance, nullptr);
                if (state->numericEdits[index])
                {
                    const HFONT font = state->numericFont
                        ? state->numericFont
                        : state->controlFont;
                    SendMessageW(state->numericEdits[index], WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)] = CreateWindowExW(
                0,
                L"EDIT",
                state->settings->quickSendShortcutOrder.c_str(),
                WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(5900)),
                state->instance,
                nullptr);
            if (const HWND shortcutOrderEdit = state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)])
            {
                const HFONT font = state->numericFont
                    ? state->numericFont
                    : state->controlFont;
                SendMessageW(shortcutOrderEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            const auto createChoice = [&](ConsolidatedSettingsControl control,
                                          const std::vector<std::wstring>& values,
                                          int selectedIndex)
            {
                const int controlIndex = static_cast<int>(control);
                HWND choice = CreateWindowExW(
                    0,
                    WC_COMBOBOXW,
                    nullptr,
                    WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                    0,
                    0,
                    0,
                    0,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(5800 + controlIndex)),
                    state->instance,
                    nullptr);
                if (!choice)
                {
                    return;
                }
                state->nativeControls[static_cast<std::size_t>(control)] = choice;
                const HFONT choiceFont = ExperimentalSettingsControlIsChoice(control) && state->choiceFont
                    ? state->choiceFont
                    : state->controlFont;
                SendMessageW(choice, WM_SETFONT, reinterpret_cast<WPARAM>(choiceFont), TRUE);
                for (const std::wstring& value : values)
                {
                    SendMessageW(choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
                }
                SendMessageW(choice, CB_SETMINVISIBLE, static_cast<WPARAM>(std::min<std::size_t>(values.size(), 8)), 0);
                SendMessageW(choice, CB_SETCURSEL, selectedIndex, 0);
            };
            std::vector<std::wstring> transitionValues;
            transitionValues.reserve(kSlideshowTransitionOptions.size());
            for (const SlideshowTransitionOption& option : kSlideshowTransitionOptions)
            {
                transitionValues.emplace_back(option.label);
            }
            createChoice(ConsolidatedSettingsControl::TransitionStyle,
                         transitionValues,
                         SlideshowTransitionComboIndex(state->settings->slideshowTransitionStyle));
            createChoice(ConsolidatedSettingsControl::OverlayTextSize,
                         {L"Small", L"Medium", L"Large"},
                         static_cast<int>(state->settings->overlayTextSize));
            std::vector<std::wstring> escapeKeyBehaviorValues;
            escapeKeyBehaviorValues.reserve(kEscapeKeyBehaviorOptions.size());
            for (const EscapeKeyBehaviorOption& option : kEscapeKeyBehaviorOptions)
            {
                escapeKeyBehaviorValues.emplace_back(option.label);
            }
            createChoice(ConsolidatedSettingsControl::EscapeKeyBehavior,
                         escapeKeyBehaviorValues,
                         EscapeKeyBehaviorComboIndex(state->settings->viewerEscapeKeyBehavior));
            createChoice(ConsolidatedSettingsControl::AppTextSize,
                         {L"Small", L"Medium", L"Large"},
                         static_cast<int>(state->settings->appTextSize));
            int thumbnailIndex = 0;
            const auto thumbnailIterator = std::find(kThumbnailSizePresets.begin(), kThumbnailSizePresets.end(), state->settings->thumbnailSizePreset);
            if (thumbnailIterator != kThumbnailSizePresets.end())
            {
                thumbnailIndex = static_cast<int>(thumbnailIterator - kThumbnailSizePresets.begin());
            }
            std::vector<std::wstring> thumbnailValues;
            for (const auto preset : kThumbnailSizePresets)
            {
                thumbnailValues.push_back(std::to_wstring(static_cast<int>(preset)) + L" px");
            }
            createChoice(ConsolidatedSettingsControl::ThumbnailSize, thumbnailValues, thumbnailIndex);
            createChoice(ConsolidatedSettingsControl::ResourceProfile,
                         {L"Conservative", L"Balanced", L"Performance", L"Aggressive"},
                         static_cast<int>(state->settings->resourceProfile));
            for (std::size_t index = 0; index < state->numericSpins.size(); ++index)
            {
                state->numericSpins[index] = CreateWindowExW(
                    0,
                    UPDOWN_CLASSW,
                    nullptr,
                    WS_CHILD | WS_BORDER | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_NOTHOUSANDS,
                    0,
                    0,
                    0,
                    0,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(5850 + index)),
                    state->instance,
                    nullptr);
                if (state->numericSpins[index])
                {
                    SendMessageW(state->numericSpins[index], UDM_SETBUDDY,
                                 reinterpret_cast<WPARAM>(state->numericEdits[index]), 0);
                    const UINT minimum = index == 0 ? kSlideshowMinimumDurationMs : index == 1 ? kSlideshowMinimumTransitionDurationMs : 1;
                    const UINT maximum = index == 0 ? kSlideshowMaximumDurationMs
                        : index == 1 ? kSlideshowMaximumTransitionDurationMs
                        : index == 4 ? static_cast<UINT>(hyperbrowse::util::kMaximumPrefetchDepth)
                        : 2147483647U;
                    SendMessageW(state->numericSpins[index], UDM_SETRANGE32,
                                 minimum, maximum);
                    SendMessageW(state->numericSpins[index], UDM_SETPOS32, 0,
                                 index == 0 ? state->settings->slideshowIntervalMs
                                     : index == 1 ? state->settings->slideshowTransitionDurationMs
                                     : index == 2 ? static_cast<int>(thumbnailCacheMegabytes)
                                     : index == 3 ? static_cast<int>(metadataCacheEntries)
                                     : index == 4 ? prefetchDepth
                                     : static_cast<int>(persistentCacheMegabytes));
                }
            }
            for (std::size_t index = 2; index < state->numericSliders.size(); ++index)
            {
                state->numericSliders[index] = CreateWindowExW(
                    0,
                    TRACKBAR_CLASSW,
                    nullptr,
                    WS_CHILD,
                    0,
                    0,
                    0,
                    0,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(5870 + index)),
                    state->instance,
                    nullptr);
                if (state->numericSliders[index])
                {
                    SendMessageW(state->numericSliders[index],
                                 TBM_SETRANGE,
                                 TRUE,
                                 MAKELONG(kPerformanceSliderPositionMinimum,
                                          kPerformanceSliderPositionMaximum));
                }
            }
            const bool thumbnailCacheAutomatic = state->settings->thumbnailCacheCapacityOverrideBytes == 0;
            const bool metadataCacheAutomatic = state->settings->metadataCacheCapacityOverrideEntries == 0;
            const bool prefetchDepthAutomatic = state->settings->prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth;
            const bool persistentCacheAutomatic = state->settings->persistentThumbnailCacheCapacityOverrideBytes == 0;
            EnableWindow(state->numericEdits[2], thumbnailCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSpins[2], thumbnailCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericEdits[3], metadataCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSpins[3], metadataCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericEdits[4], prefetchDepthAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSpins[4], prefetchDepthAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSliders[2], thumbnailCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSliders[3], metadataCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSliders[4], prefetchDepthAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericEdits[5], persistentCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSpins[5], persistentCacheAutomatic ? FALSE : TRUE);
            EnableWindow(state->numericSliders[5], persistentCacheAutomatic ? FALSE : TRUE);
            UpdateExperimentalSettingsCacheValues(*state);
            ApplyExperimentalSettingsTheme(*state);
            LayoutExperimentalSettings(*state);
            hyperbrowse::ui::InitializeExperimentalSettingsAccessibility(*state);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                hyperbrowse::render::D2DRenderer::Instance().ResizeRenderTarget(state->renderTarget.Get(), hwnd, true);
                LayoutExperimentalSettings(*state);
            }
            return 0;
        case WM_HSCROLL:
            if (state)
            {
                HandleExperimentalSettingsSliderChange(*state, reinterpret_cast<HWND>(lParam));
                return 0;
            }
            break;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = std::max<UINT>(96, HIWORD(wParam));
                const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
                ResizeExperimentalSettingsToContent(*state, suggestedRect);
                hyperbrowse::render::D2DRenderer::Instance().ResizeRenderTarget(state->renderTarget.Get(), hwnd, true);
                DeleteFontIfOwned(state->controlFont);
                state->controlFont = CreateSystemUiFont(state->settings->appTextSize, state->dpi);
                const HFONT font = state->controlFont
                    ? state->controlFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                DeleteFontIfOwned(state->numericFont);
                state->numericFont = CreateDialogUiFont(16, FW_NORMAL, state->settings->appTextSize, state->dpi);
                const HFONT numericFont = state->numericFont ? state->numericFont : font;
                DeleteFontIfOwned(state->choiceFont);
                state->choiceFont = CreateDialogUiFont(16, FW_NORMAL, state->settings->appTextSize, state->dpi);
                for (std::size_t index = 0; index < state->nativeControls.size(); ++index)
                {
                    const HWND control = state->nativeControls[index];
                    if (control)
                    {
                        const auto controlId = static_cast<ConsolidatedSettingsControl>(index);
                        HFONT controlFont = font;
                        if (controlId == ConsolidatedSettingsControl::QuickSendShortcutOrder && numericFont)
                        {
                            controlFont = numericFont;
                        }
                        else if (ExperimentalSettingsControlIsChoice(controlId) && state->choiceFont)
                        {
                            controlFont = state->choiceFont;
                        }
                        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
                    }
                }
                for (const HWND edit : state->numericEdits)
                {
                    if (edit)
                    {
                        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(numericFont), TRUE);
                    }
                }
                LayoutExperimentalSettings(*state);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (state)
            {
                PAINTSTRUCT paint{};
                BeginPaint(hwnd, &paint);
                PaintExperimentalSettings(*state);
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (state)
            {
                const int dpi = static_cast<int>(std::max<UINT>(96, state->dpi));
                const POINT point{MulDiv(GET_X_LPARAM(lParam), 96, dpi), MulDiv(GET_Y_LPARAM(lParam), 96, dpi)};
                int hovered = -1;
                if (PtInRect(&state->applyButtonRect, point))
                {
                    hovered = -10;
                }
                else if (PtInRect(&state->okButtonRect, point))
                {
                    hovered = -11;
                }
                else if (PtInRect(&state->cancelButtonRect, point))
                {
                    hovered = -12;
                }
                int hoveredTab = -1;
                for (std::size_t index = 0; index < state->tabRects.size(); ++index)
                {
                    if (PtInRect(&state->tabRects[index], point))
                    {
                        hoveredTab = static_cast<int>(index);
                        break;
                    }
                }
                for (std::size_t index = 0; index < state->controlRects.size(); ++index)
                {
                    if (hovered == -1
                        && PtInRect(&state->bodyViewport, point)
                        && PtInRect(&state->controlRects[index], point))
                    {
                        hovered = static_cast<int>(index);
                        break;
                    }
                }
                if (hovered != state->hoveredControl || hoveredTab != state->hoveredTab)
                {
                    state->hoveredControl = hovered;
                    state->hoveredTab = hoveredTab;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tracking);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (state)
            {
                state->hoveredControl = -1;
                state->hoveredTab = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (state && state->scrollExtent > 0)
            {
                const int dpi = static_cast<int>(std::max<UINT>(96, state->dpi));
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &point);
                point.x = MulDiv(point.x, 96, dpi);
                point.y = MulDiv(point.y, 96, dpi);
                if (PtInRect(&state->bodyViewport, point))
                {
                    const int wheelSteps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                    const int scrollStep = hyperbrowse::util::ScaleAppTextDimension(50, state->settings->appTextSize);
                    SetExperimentalSettingsScrollOffset(*state, state->scrollOffset - wheelSteps * scrollStep);
                    return 0;
                }
            }
            break;
        case WM_VSCROLL:
            if (state && state->scrollExtent > 0)
            {
                const int scrollStep = hyperbrowse::util::ScaleAppTextDimension(50, state->settings->appTextSize);
                int nextOffset = state->scrollOffset;
                switch (LOWORD(wParam))
                {
                case SB_LINEUP: nextOffset -= scrollStep; break;
                case SB_LINEDOWN: nextOffset += scrollStep; break;
                case SB_PAGEUP: nextOffset -= static_cast<int>(state->bodyViewport.bottom - state->bodyViewport.top); break;
                case SB_PAGEDOWN: nextOffset += static_cast<int>(state->bodyViewport.bottom - state->bodyViewport.top); break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK: nextOffset = static_cast<int>(HIWORD(wParam)); break;
                case SB_TOP: nextOffset = 0; break;
                case SB_BOTTOM: nextOffset = state->scrollExtent; break;
                default: break;
                }
                SetExperimentalSettingsScrollOffset(*state, nextOffset);
                return 0;
            }
            break;
        case WM_MEASUREITEM:
            if (state && state->settings)
            {
                auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
                if (measureItem && measureItem->CtlType == ODT_COMBOBOX)
                {
                    const UINT dpi = std::max<UINT>(96, state->dpi);
                    measureItem->itemHeight = static_cast<UINT>(std::max(
                        ScaleDialogAppTextDimension(28, state->settings->appTextSize, dpi),
                        GetSystemMetricsForDpi(SM_CYMENU, dpi)));
                    return TRUE;
                }
            }
            break;
        case WM_DRAWITEM:
            if (state)
            {
                const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                if (drawItem && drawItem->CtlType == ODT_COMBOBOX)
                {
                    DrawExperimentalSettingsComboItem(*state, *drawItem);
                    return TRUE;
                }
            }
            break;
        case WM_GETOBJECT:
            if (state && state->accessibility)
            {
                const LRESULT result = state->accessibility->HandleGetObject(wParam, lParam);
                if (result != 0)
                {
                    return result;
                }
            }
            break;
        case WM_LBUTTONDOWN:
            if (state)
            {
                const int dpi = static_cast<int>(std::max<UINT>(96, state->dpi));
                const POINT point{MulDiv(GET_X_LPARAM(lParam), 96, dpi), MulDiv(GET_Y_LPARAM(lParam), 96, dpi)};
                SetFocus(hwnd);
                if (PtInRect(&state->applyButtonRect, point))
                {
                    FocusExperimentalSettingsTarget(*state, {ExperimentalSettingsFocusTargetKind::ApplyButton});
                    state->pressedControl = -10;
                    SetCapture(hwnd);
                    return 0;
                }
                if (PtInRect(&state->okButtonRect, point))
                {
                    FocusExperimentalSettingsTarget(*state, {ExperimentalSettingsFocusTargetKind::OkButton});
                    state->pressedControl = -11;
                    SetCapture(hwnd);
                    return 0;
                }
                if (PtInRect(&state->cancelButtonRect, point))
                {
                    FocusExperimentalSettingsTarget(*state, {ExperimentalSettingsFocusTargetKind::CancelButton});
                    state->pressedControl = -12;
                    SetCapture(hwnd);
                    return 0;
                }
                for (std::size_t index = 0; index < state->tabRects.size(); ++index)
                {
                    if (PtInRect(&state->tabRects[index], point))
                    {
                        state->page = static_cast<ConsolidatedSettingsPage>(index);
                        state->scrollOffset = 0;
                        LayoutExperimentalSettings(*state);
                        FocusExperimentalSettingsTarget(
                            *state,
                            {ExperimentalSettingsFocusTargetKind::Tab, static_cast<int>(index)});
                        return 0;
                    }
                }
                for (std::size_t index = 0; index < state->controlRects.size(); ++index)
                {
                    if (PtInRect(&state->bodyViewport, point)
                        && PtInRect(&state->controlRects[index], point))
                    {
                        const auto control = static_cast<ConsolidatedSettingsControl>(index);
                        if (!ExperimentalSettingsControlAvailable(*state, control))
                        {
                            continue;
                        }
                        state->focusedTarget = {
                            ExperimentalSettingsFocusTargetKind::CustomControl,
                            static_cast<int>(index)};
                        state->pressedControl = static_cast<int>(index);
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (state && state->pressedControl >= 0)
            {
                const int pressed = state->pressedControl;
                state->pressedControl = -1;
                ReleaseCapture();
                const int dpi = static_cast<int>(std::max<UINT>(96, state->dpi));
                POINT point{MulDiv(GET_X_LPARAM(lParam), 96, dpi), MulDiv(GET_Y_LPARAM(lParam), 96, dpi)};
                const auto& bounds = state->controlRects[static_cast<std::size_t>(pressed)];
                if (PtInRect(&bounds, point))
                {
                    const auto control = static_cast<ConsolidatedSettingsControl>(pressed);
                    ExperimentalSettingsToggle(*state, control);
                    UpdateExperimentalSettingsCustomControlState(*state, control);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            else if (state && state->pressedControl <= -10)
            {
                const int pressed = state->pressedControl;
                state->pressedControl = -1;
                ReleaseCapture();
                const int dpi = static_cast<int>(std::max<UINT>(96, state->dpi));
                POINT point{MulDiv(GET_X_LPARAM(lParam), 96, dpi), MulDiv(GET_Y_LPARAM(lParam), 96, dpi)};
                const RECT& bounds = pressed == -10 ? state->applyButtonRect : pressed == -11 ? state->okButtonRect : state->cancelButtonRect;
                if (PtInRect(&bounds, point))
                {
                    if (pressed == -12)
                    {
                        DestroyWindow(hwnd);
                    }
                    else if (CollectExperimentalSettings(hwnd, *state))
                    {
                        if (state->settings->apply)
                        {
                            state->settings->apply(*state->settings);
                        }
                        if (pressed == -11)
                        {
                            state->accepted = true;
                            DestroyWindow(hwnd);
                        }
                    }
                }
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (state && HandleExperimentalSettingsKeyboardInput(*state, message, wParam))
            {
                return 0;
            }
            if (state && wParam == VK_ESCAPE)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if (state && wParam == VK_RETURN && GetFocus() == hwnd)
            {
                if (CollectExperimentalSettings(hwnd, *state))
                {
                    if (state->settings->apply)
                    {
                        state->settings->apply(*state->settings);
                    }
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }
            if (LOWORD(wParam) == kExperimentalSettingsApplyId || LOWORD(wParam) == kExperimentalSettingsOkId)
            {
                if (CollectExperimentalSettings(hwnd, *state))
                {
                    if (state->settings->apply)
                    {
                        state->settings->apply(*state->settings);
                    }
                    if (LOWORD(wParam) == kExperimentalSettingsOkId)
                    {
                        state->accepted = true;
                        DestroyWindow(hwnd);
                    }
                }
                return 0;
            }
            if (LOWORD(wParam) == kExperimentalSettingsCancelId)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                const HWND source = reinterpret_cast<HWND>(lParam);
                const int selected = static_cast<int>(SendMessageW(source, CB_GETCURSEL, 0, 0));
                if (selected >= 0 && source == state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::TransitionStyle)]
                    && selected < static_cast<int>(kSlideshowTransitionOptions.size()))
                {
                    state->settings->slideshowTransitionStyle = kSlideshowTransitionOptions[static_cast<std::size_t>(selected)].style;
                }
                else if (selected >= 0 && source == state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::OverlayTextSize)] && selected < 3)
                {
                    state->settings->overlayTextSize = static_cast<hyperbrowse::viewer::InfoOverlayTextSize>(selected);
                }
                else if (selected >= 0 && source == state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::EscapeKeyBehavior)]
                         && selected < static_cast<int>(kEscapeKeyBehaviorOptions.size()))
                {
                    state->settings->viewerEscapeKeyBehavior = kEscapeKeyBehaviorOptions[static_cast<std::size_t>(selected)].behavior;
                }
                else if (selected >= 0 && source == state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::AppTextSize)] && selected < 3)
                {
                    state->settings->appTextSize = static_cast<hyperbrowse::util::AppTextSize>(selected);
                    RebuildExperimentalSettingsTextResources(*state);
                    ResizeExperimentalSettingsToContent(*state);
                    LayoutExperimentalSettings(*state);
                }
                else if (selected >= 0 && source == state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::ThumbnailSize)]
                         && selected < static_cast<int>(kThumbnailSizePresets.size()))
                {
                    state->settings->thumbnailSizePreset = kThumbnailSizePresets[static_cast<std::size_t>(selected)];
                }
                else if (selected >= 0 && source == state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::ResourceProfile)] && selected < 4)
                {
                    state->settings->resourceProfile = static_cast<hyperbrowse::util::ResourceProfile>(selected);
                    UpdateExperimentalSettingsCacheValues(*state);
                }
                if (state->accessibility)
                {
                    state->accessibility->NotifyStateChanged(CHILDID_SELF);
                }
                return 0;
            }
            break;
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                const auto theme = hyperbrowse::ui::MakeDialogTheme(state->settings->darkTheme);
                SetTextColor(dc, theme.text);
                SetBkColor(dc, theme.fieldBackground);
                return reinterpret_cast<LRESULT>(state->editBackgroundBrush);
            }
            break;
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            if (state)
            {
                ApplyExperimentalSettingsTheme(*state);
                const auto theme = hyperbrowse::ui::MakeDialogTheme(state->settings->darkTheme);
                ApplyWindowFrameTheme(hwnd,
                                      state->settings->darkTheme && !hyperbrowse::ui::IsHighContrastEnabled(),
                                      theme.windowBackground,
                                      theme.text,
                                      theme.border);
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->accessibility.reset();
                for (HWND edit : state->numericEdits)
                {
                    if (edit)
                    {
                        DestroyWindow(edit);
                    }
                }
                for (HWND slider : state->numericSliders)
                {
                    if (slider)
                    {
                        DestroyWindow(slider);
                    }
                }
                if (const HWND shortcutOrderEdit = state->nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::QuickSendShortcutOrder)])
                {
                    DestroyWindow(shortcutOrderEdit);
                }
                if (state->editBackgroundBrush)
                {
                    DeleteObject(state->editBackgroundBrush);
                    state->editBackgroundBrush = nullptr;
                }
                DeleteFontIfOwned(state->controlFont);
                state->controlFont = nullptr;
                DeleteFontIfOwned(state->numericFont);
                state->numericFont = nullptr;
                DeleteFontIfOwned(state->choiceFont);
                state->choiceFont = nullptr;
                state->done = true;
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    ExperimentalSettingsDialogResult PromptForExperimentalSettings(HWND ownerWindow,
                                                                    HINSTANCE instance,
                                                                    ConsolidatedSettingsDialogState* settings)
    {
        if (!settings || !hyperbrowse::render::D2DRenderer::Instance().IsAvailable())
        {
            return ExperimentalSettingsDialogResult::Unavailable;
        }
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kExperimentalSettingsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &ExperimentalSettingsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kExperimentalSettingsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return ExperimentalSettingsDialogResult::Unavailable;
            }
        }
        ExperimentalSettingsDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.instance = instance;
        state.settings = settings;
        const RECT windowRect = ExperimentalSettingsInitialWindowRect(
            ownerWindow,
            settings->appTextSize,
            state.dpi);
        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }
        HWND dialogWindow = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                                            kExperimentalSettingsDialogClassName,
                                            settings->title.c_str(),
                                            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN | WS_VSCROLL,
                                            CW_USEDEFAULT, CW_USEDEFAULT,
                                            windowRect.right - windowRect.left,
                                            windowRect.bottom - windowRect.top,
                                            ownerWindow, nullptr, instance, &state);
        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            return ExperimentalSettingsDialogResult::Unavailable;
        }
        SetWindowTextW(dialogWindow, settings->title.c_str());
        const auto dialogTheme = hyperbrowse::ui::MakeDialogTheme(settings->darkTheme);
        ApplyWindowFrameTheme(dialogWindow,
                              settings->darkTheme && !hyperbrowse::ui::IsHighContrastEnabled(),
                              dialogTheme.windowBackground,
                              dialogTheme.text,
                              dialogTheme.border);
        RefreshWindowNonClientArea(dialogWindow);
        CenterExperimentalSettingsOnWorkArea(dialogWindow, ownerWindow, settings->appTextSize);
        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);
        SetForegroundWindow(dialogWindow);
        SetActiveWindow(dialogWindow);
        const HWND initialFocus = state.nativeControls[static_cast<std::size_t>(ConsolidatedSettingsControl::TransitionStyle)];
        if (initialFocus)
        {
            FocusExperimentalSettingsTarget(
                state,
                {ExperimentalSettingsFocusTargetKind::NativeControl,
                 static_cast<int>(ConsolidatedSettingsControl::TransitionStyle)});
        }
        else
        {
            FocusFirstExperimentalSettingsPageControl(state);
        }
        const auto focusFirstControlOnPage = [&state]()
        {
            FocusFirstExperimentalSettingsPageControl(state);
        };
        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            const bool keyMessage = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
            const bool dialogMessage = message.hwnd == dialogWindow || IsChild(dialogWindow, message.hwnd) != FALSE;
            if (message.message == WM_MOUSEWHEEL
                && dialogMessage
                && state.scrollExtent > 0)
            {
                POINT point{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
                ScreenToClient(dialogWindow, &point);
                point.x = MulDiv(point.x, 96, static_cast<int>(std::max<UINT>(96, state.dpi)));
                point.y = MulDiv(point.y, 96, static_cast<int>(std::max<UINT>(96, state.dpi)));
                if (PtInRect(&state.bodyViewport, point))
                {
                    SendMessageW(dialogWindow, message.message, message.wParam, message.lParam);
                    continue;
                }
            }
            if (keyMessage && dialogMessage && HandleExperimentalSettingsKeyboardInput(state, message.message, message.wParam))
            {
                continue;
            }
            if (keyMessage && dialogMessage && message.wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                const int pageCount = static_cast<int>(ConsolidatedSettingsPage::Count);
                int pageIndex = static_cast<int>(state.page);
                const int direction = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1;
                pageIndex = (pageIndex + direction + pageCount) % pageCount;
                state.page = static_cast<ConsolidatedSettingsPage>(pageIndex);
                state.scrollOffset = 0;
                LayoutExperimentalSettings(state);
                focusFirstControlOnPage();
                continue;
            }
            if (keyMessage && dialogMessage && message.wParam == VK_ESCAPE && !ExperimentalSettingsChoiceDropped(state))
            {
                SendMessageW(dialogWindow, WM_CLOSE, 0, 0);
                continue;
            }
            if (keyMessage && dialogMessage && message.wParam == VK_RETURN && !ExperimentalSettingsChoiceDropped(state))
            {
                SendMessageW(dialogWindow, WM_COMMAND, MAKEWPARAM(kExperimentalSettingsOkId, BN_CLICKED), 0);
                continue;
            }
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }
        return state.accepted ? ExperimentalSettingsDialogResult::Accepted : ExperimentalSettingsDialogResult::Cancelled;
    }
}

namespace hyperbrowse::ui
{

    const wchar_t* ExperimentalSettingsAccessibleControlName(ConsolidatedSettingsControl control)
    {
        switch (control)
        {
        case ConsolidatedSettingsControl::TransitionEnabled: return L"Use slideshow transitions";
        case ConsolidatedSettingsControl::TransitionStyle: return L"Transition style";
        case ConsolidatedSettingsControl::SlideshowDuration: return L"Slide duration in milliseconds";
        case ConsolidatedSettingsControl::TransitionDuration: return L"Transition duration in milliseconds";
        case ConsolidatedSettingsControl::ViewerWheelZoom: return L"Mouse wheel zoom";
        case ConsolidatedSettingsControl::ViewerWheelNavigate: return L"Mouse wheel navigate";
        case ConsolidatedSettingsControl::InvertKeyboardPanning: return L"Invert keyboard panning";
        case ConsolidatedSettingsControl::RawPairingEnabled: return L"Treat paired RAW and JPEG files as one operation";
        case ConsolidatedSettingsControl::RawPreferJpeg: return L"Prefer JPEG";
        case ConsolidatedSettingsControl::RawPreferRaw: return L"Prefer RAW";
        case ConsolidatedSettingsControl::SecondaryMonitor: return L"Open viewers on a secondary monitor when available";
        case ConsolidatedSettingsControl::InfoOverlays: return L"Show viewer detail overlays";
        case ConsolidatedSettingsControl::OverlayTextSize: return L"Overlay text size";
        case ConsolidatedSettingsControl::ThemeLight: return L"Light theme";
        case ConsolidatedSettingsControl::ThemeDark: return L"Dark theme";
        case ConsolidatedSettingsControl::AppTextSize: return L"Application text size";
        case ConsolidatedSettingsControl::ThumbnailSize: return L"Thumbnail size";
        case ConsolidatedSettingsControl::ThumbnailDetails: return L"Show thumbnail details";
        case ConsolidatedSettingsControl::CompactLayout: return L"Use compact thumbnail layout";
        case ConsolidatedSettingsControl::DetailsPanel: return L"Show the details panel";
        case ConsolidatedSettingsControl::ResourceProfile: return L"Resource profile";
        case ConsolidatedSettingsControl::PersistentCache: return L"Keep the persistent thumbnail cache enabled";
        case ConsolidatedSettingsControl::ThumbnailCache: return L"Thumbnail cache cap in megabytes";
        case ConsolidatedSettingsControl::ThumbnailCacheAutomatic: return L"Follow profile for thumbnail cache";
        case ConsolidatedSettingsControl::MetadataCache: return L"Metadata cache cap in entries";
        case ConsolidatedSettingsControl::MetadataCacheAutomatic: return L"Follow profile for metadata cache";
        case ConsolidatedSettingsControl::PressureStatus: return L"Show memory pressure state in the status bar";
        case ConsolidatedSettingsControl::NvJpeg: return L"Use NVIDIA JPEG acceleration when available";
        case ConsolidatedSettingsControl::LibRawOutOfProcess: return L"Use out-of-process LibRaw fallback";
        case ConsolidatedSettingsControl::RecursiveBrowsing: return L"Browse folders recursively";
        case ConsolidatedSettingsControl::ShowSubfolders: return L"Show subfolders in the browser";
        case ConsolidatedSettingsControl::CloseOnEscape: return L"Close the main window when Escape is pressed";
        case ConsolidatedSettingsControl::SingleInstance: return L"Use a single application instance";
        case ConsolidatedSettingsControl::EscapeKeyBehavior: return L"Escape key behavior in full screen";
        case ConsolidatedSettingsControl::WindowedFullMetadata: return L"Show full metadata in windowed mode";
        case ConsolidatedSettingsControl::FullScreenFullMetadata: return L"Show full metadata in full-screen mode";
        case ConsolidatedSettingsControl::PrefetchDepth: return L"Prefetch depth in items";
        case ConsolidatedSettingsControl::PrefetchDepthAutomatic: return L"Follow profile for prefetch depth";
        case ConsolidatedSettingsControl::PersistentCacheCapacity: return L"Persistent thumbnail cache cap in megabytes";
        case ConsolidatedSettingsControl::PersistentCacheCapacityAutomatic: return L"Follow profile for persistent thumbnail cache";
        case ConsolidatedSettingsControl::TrimPersistentCache: return L"Trim persistent thumbnail cache now";
        case ConsolidatedSettingsControl::QuickSendShortcutOrder: return L"Quick Actions shortcut order";
        default: return L"Setting";
        }
    }

    bool ExperimentalSettingsAccessibleControlIsRadio(ConsolidatedSettingsControl control)
    {
        return control == ConsolidatedSettingsControl::ViewerWheelZoom
            || control == ConsolidatedSettingsControl::ViewerWheelNavigate
            || control == ConsolidatedSettingsControl::RawPreferRaw
            || control == ConsolidatedSettingsControl::RawPreferJpeg
            || control == ConsolidatedSettingsControl::ThemeLight
            || control == ConsolidatedSettingsControl::ThemeDark;
    }

    std::vector<ExperimentalSettingsFocusTarget> ExperimentalSettingsAccessibilityTargets(
        const ExperimentalSettingsDialogState& state)
    {
        std::vector<ExperimentalSettingsFocusTarget> targets;
        targets.reserve(state.tabRects.size()
                        + ExperimentalSettingsPageControlOrder(state.page).size()
                        + 3);
        for (int index = 0; index < static_cast<int>(state.tabRects.size()); ++index)
        {
            targets.push_back({ExperimentalSettingsFocusTargetKind::Tab, index});
        }
        for (const ConsolidatedSettingsControl control : ExperimentalSettingsPageControlOrder(state.page))
        {
            targets.push_back({ExperimentalSettingsNativeWindow(state, control)
                                   ? ExperimentalSettingsFocusTargetKind::NativeControl
                                   : ExperimentalSettingsFocusTargetKind::CustomControl,
                               static_cast<int>(control)});
        }
        targets.push_back({ExperimentalSettingsFocusTargetKind::ApplyButton});
        targets.push_back({ExperimentalSettingsFocusTargetKind::OkButton});
        targets.push_back({ExperimentalSettingsFocusTargetKind::CancelButton});
        return targets;
    }

    RECT ExperimentalSettingsAccessibleBounds(const ExperimentalSettingsDialogState& state,
                                               const ExperimentalSettingsFocusTarget& target)
    {
        const UINT dpi = std::max<UINT>(96, state.dpi);
        const auto toPhysical = [dpi](const RECT& logical)
        {
            return RECT{
                ScaleDialogDimension(logical.left, dpi),
                ScaleDialogDimension(logical.top, dpi),
                ScaleDialogDimension(logical.right, dpi),
                ScaleDialogDimension(logical.bottom, dpi)};
        };
        const RECT physicalBodyViewport = toPhysical(state.bodyViewport);
        RECT viewport = physicalBodyViewport;
        POINT viewportTopLeft{viewport.left, viewport.top};
        POINT viewportBottomRight{viewport.right, viewport.bottom};
        ClientToScreen(state.dialogWindow, &viewportTopLeft);
        ClientToScreen(state.dialogWindow, &viewportBottomRight);
        viewport = {viewportTopLeft.x, viewportTopLeft.y, viewportBottomRight.x, viewportBottomRight.y};
        if (target.kind == ExperimentalSettingsFocusTargetKind::NativeControl)
        {
            RECT bounds{};
            GetWindowRect(ExperimentalSettingsNativeWindow(
                              state,
                              static_cast<ConsolidatedSettingsControl>(target.index)),
                          &bounds);
            RECT visibleBounds{};
            IntersectRect(&visibleBounds, &bounds, &viewport);
            return visibleBounds;
        }

        RECT bounds{};
        if (target.kind == ExperimentalSettingsFocusTargetKind::Tab)
        {
            bounds = toPhysical(state.tabRects[static_cast<std::size_t>(target.index)]);
        }
        else if (target.kind == ExperimentalSettingsFocusTargetKind::CustomControl)
        {
            bounds = toPhysical(state.controlRects[static_cast<std::size_t>(target.index)]);
            RECT visibleBounds{};
            IntersectRect(&visibleBounds, &bounds, &physicalBodyViewport);
            bounds = visibleBounds;
        }
        else if (target.kind == ExperimentalSettingsFocusTargetKind::ApplyButton)
        {
            bounds = toPhysical(state.applyButtonRect);
        }
        else if (target.kind == ExperimentalSettingsFocusTargetKind::OkButton)
        {
            bounds = toPhysical(state.okButtonRect);
        }
        else if (target.kind == ExperimentalSettingsFocusTargetKind::CancelButton)
        {
            bounds = toPhysical(state.cancelButtonRect);
        }
        POINT topLeft{bounds.left, bounds.top};
        POINT bottomRight{bounds.right, bounds.bottom};
        ClientToScreen(state.dialogWindow, &topLeft);
        ClientToScreen(state.dialogWindow, &bottomRight);
        return RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    }

    void InitializeExperimentalSettingsAccessibility(ExperimentalSettingsDialogState& state)
    {
        auto snapshotProvider = [&state]()
        {
            std::vector<hyperbrowse::ui::MainWindowAccessibility::Item> items;
            const auto targets = ExperimentalSettingsAccessibilityTargets(state);
            items.reserve(targets.size());
            for (const ExperimentalSettingsFocusTarget& target : targets)
            {
                hyperbrowse::ui::MainWindowAccessibility::Item item;
                item.bounds = ExperimentalSettingsAccessibleBounds(state, target);
                item.state = STATE_SYSTEM_FOCUSABLE;
                switch (target.kind)
                {
                case ExperimentalSettingsFocusTargetKind::Tab:
                    item.name = std::wstring(ExperimentalSettingsPageTitle(
                        static_cast<ConsolidatedSettingsPage>(target.index))) + L" tab";
                    item.role = ROLE_SYSTEM_PAGETAB;
                    item.defaultAction = L"Select";
                    item.state |= STATE_SYSTEM_SELECTABLE;
                    if (static_cast<int>(state.page) == target.index)
                    {
                        item.state |= STATE_SYSTEM_SELECTED;
                    }
                    break;
                case ExperimentalSettingsFocusTargetKind::NativeControl:
                case ExperimentalSettingsFocusTargetKind::CustomControl:
                {
                    const auto control = static_cast<ConsolidatedSettingsControl>(target.index);
                    item.name = ExperimentalSettingsAccessibleControlName(control);
                    item.keyboardShortcut.assign(ExperimentalSettingsControlMnemonic(control)
                                                     ? std::wstring(L"Alt+") + ExperimentalSettingsControlMnemonic(control)
                                                     : std::wstring{});
                    const HWND nativeWindow = ExperimentalSettingsNativeWindow(state, control);
                    if (nativeWindow)
                    {
                        item.role = ExperimentalSettingsControlIsChoice(control)
                            ? ROLE_SYSTEM_COMBOBOX
                            : ROLE_SYSTEM_TEXT;
                        item.value = ReadWindowText(nativeWindow);
                        item.defaultAction = ExperimentalSettingsControlIsChoice(control) ? L"Open" : L"Edit";
                    }
                    else
                    {
                        item.role = ExperimentalSettingsAccessibleControlIsRadio(control)
                            ? ROLE_SYSTEM_RADIOBUTTON
                            : ROLE_SYSTEM_CHECKBUTTON;
                        const bool checked = ExperimentalSettingsChecked(*state.settings, control);
                        item.value = checked ? L"Checked" : L"Not checked";
                        item.defaultAction = ExperimentalSettingsAccessibleControlIsRadio(control)
                            ? L"Select"
                            : L"Toggle";
                        if (checked)
                        {
                            item.state |= STATE_SYSTEM_CHECKED;
                        }
                    }
                    if (!ExperimentalSettingsControlAvailable(state, control))
                    {
                        item.state |= STATE_SYSTEM_UNAVAILABLE;
                    }
                    break;
                }
                case ExperimentalSettingsFocusTargetKind::ApplyButton:
                case ExperimentalSettingsFocusTargetKind::OkButton:
                case ExperimentalSettingsFocusTargetKind::CancelButton:
                    item.name = target.kind == ExperimentalSettingsFocusTargetKind::ApplyButton
                        ? L"Apply"
                        : target.kind == ExperimentalSettingsFocusTargetKind::OkButton ? L"OK" : L"Cancel";
                    item.role = ROLE_SYSTEM_PUSHBUTTON;
                    item.defaultAction = L"Press";
                    break;
                default:
                    break;
                }
                item.description = item.value;
                items.push_back(std::move(item));
            }
            return items;
        };
        auto focusedChildProvider = [&state]() -> long
        {
            const ExperimentalSettingsFocusTarget focused = ExperimentalSettingsCurrentFocusTarget(state);
            const auto targets = ExperimentalSettingsAccessibilityTargets(state);
            const auto iterator = std::find(targets.begin(), targets.end(), focused);
            return iterator == targets.end()
                ? CHILDID_SELF
                : static_cast<long>(std::distance(targets.begin(), iterator) + 1);
        };
        auto focusProvider = [&state](long childId)
        {
            const auto targets = ExperimentalSettingsAccessibilityTargets(state);
            return childId > 0 && childId <= static_cast<long>(targets.size())
                && FocusExperimentalSettingsTarget(state, targets[static_cast<std::size_t>(childId - 1)]);
        };
        auto defaultActionProvider = [&state](long childId)
        {
            const auto targets = ExperimentalSettingsAccessibilityTargets(state);
            if (childId <= 0 || childId > static_cast<long>(targets.size()))
            {
                return false;
            }
            const ExperimentalSettingsFocusTarget target = targets[static_cast<std::size_t>(childId - 1)];
            if (!ExperimentalSettingsFocusTargetAvailable(state, target))
            {
                return false;
            }
            if (target.kind == ExperimentalSettingsFocusTargetKind::NativeControl)
            {
                return FocusExperimentalSettingsTarget(state, target);
            }
            ActivateExperimentalSettingsTarget(state, target);
            return true;
        };
        state.accessibility = std::make_shared<hyperbrowse::ui::MainWindowAccessibility>(
            state.dialogWindow,
            std::move(snapshotProvider),
            std::move(focusedChildProvider),
            std::move(focusProvider),
            std::move(defaultActionProvider),
            L"HyperBrowse Settings",
            L"HyperBrowse application settings",
            ROLE_SYSTEM_DIALOG);
    }

    void MainWindow::ShowConsolidatedSettingsDialog()
    {
        ConsolidatedSettingsDialogState state;
        state.ownerWindow = hwnd_;
        state.dpi = DialogDpiForWindow(hwnd_);
        state.instance = instance_;
        state.title = L"Settings";
        state.appTextSize = appTextSize_;
        state.darkTheme = themeMode_ == ThemeMode::Dark;
        state.resourceProfile = resourceProfile_;
        state.prefetchDepthOverride = prefetchDepthOverride_;
        state.thumbnailSizePreset = thumbnailSizePreset_;
        state.viewerMouseWheelBehavior = viewerMouseWheelBehavior_;
        state.viewerEscapeKeyBehavior = viewerEscapeKeyBehavior_;
        state.invertKeyboardPanning = invertKeyboardPanning_;
        state.slideshowTransitionStyle = slideshowTransitionStyle_;
        state.slideshowIntervalMs = NormalizeSlideshowDuration(slideshowIntervalMs_);
        state.slideshowTransitionDurationMs = NormalizeSlideshowTransitionDuration(slideshowTransitionDurationMs_);
        state.useSlideshowTransition = useSlideshowTransition_;
        state.compactThumbnailLayout = compactThumbnailLayout_;
        state.thumbnailDetailsVisible = thumbnailDetailsVisible_;
        state.detailsStripVisible = detailsStripVisible_;
        state.recursiveBrowsingEnabled = recursiveBrowsingEnabled_;
        state.showSubfoldersInBrowser = showSubfoldersInBrowser_;
        state.rawJpegPairedOperationsEnabled = rawJpegPairedOperationsEnabled_;
        state.pairedRawJpegViewerPreference = pairedRawJpegViewerPreference_;
        state.defaultViewerToSecondaryMonitor = defaultViewerToSecondaryMonitor_;
        state.persistentThumbnailCacheEnabled = persistentThumbnailCacheEnabled_;
        state.thumbnailCacheCapacityOverrideBytes = thumbnailCacheCapacityOverrideBytes_;
        state.metadataCacheCapacityOverrideEntries = metadataCacheCapacityOverrideEntries_;
        state.persistentThumbnailCacheCapacityOverrideBytes = persistentThumbnailCacheCapacityOverrideBytes_;
        state.showPressureStateInStatusBar = showPressureStateInStatusBar_;
        state.nvJpegEnabled = nvJpegEnabled_;
        state.libRawOutOfProcessEnabled = libRawOutOfProcessEnabled_;
        state.closeMainWindowOnEscape = closeMainWindowOnEscape_;
        state.singleInstanceEnabled = app::Application::IsSingleInstanceEnabled();
        state.keepInNotificationAreaEnabled = app::Application::IsKeepInNotificationAreaEnabled();
        state.quickSendShortcutOrder = quickSendShortcutOrder_;
        state.secondaryMonitorAvailable = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        state.nvJpegAvailable = HasNvJpegCapability();
        state.libRawAvailable = decode::IsLibRawBuildEnabled();
        state.infoOverlaysVisible = viewerWindow_ && viewerWindow_->IsOpen()
            ? viewerWindow_->AreInfoOverlaysVisible()
            : viewer::ViewerWindow::DefaultInfoOverlaysVisible();
        state.overlayTextSize = viewerWindow_ && viewerWindow_->IsOpen()
            ? viewerWindow_->OverlayTextSize()
            : viewer::ViewerWindow::DefaultOverlayTextSize();
        state.windowedFullMetadataVisible = viewer::ViewerWindow::DefaultWindowedFullMetadataVisible();
        state.fullScreenFullMetadataVisible = viewer::ViewerWindow::DefaultFullScreenFullMetadataVisible();
        if (viewerWindow_ && viewerWindow_->IsOpen())
        {
            if (viewerWindow_->IsFullScreen())
            {
                state.fullScreenFullMetadataVisible = viewerWindow_->IsFullMetadataVisible();
            }
            else
            {
                state.windowedFullMetadataVisible = viewerWindow_->IsFullMetadataVisible();
            }
        }

        state.trimPersistentCache = [this]()
        {
            StartPersistentThumbnailCacheMaintenance(false, false);
        };

        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize, 96);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        state.apply = [this](const ConsolidatedSettingsDialogState& draft)
        {
            const std::vector<viewer::ViewerWindow*> openViewerWindows = OpenViewerWindows();
            const bool slideshowDurationChanged = slideshowIntervalMs_ != draft.slideshowIntervalMs;
            const bool slideshowTransitionChanged = slideshowTransitionStyle_ != draft.slideshowTransitionStyle
                || slideshowTransitionDurationMs_ != draft.slideshowTransitionDurationMs
                || useSlideshowTransition_ != draft.useSlideshowTransition;
            const bool thumbnailDisplayChanged = thumbnailSizePreset_ != draft.thumbnailSizePreset
                || compactThumbnailLayout_ != draft.compactThumbnailLayout
                || thumbnailDetailsVisible_ != draft.thumbnailDetailsVisible
                || appTextSize_ != draft.appTextSize;
            const bool folderScopeChanged = recursiveBrowsingEnabled_ != draft.recursiveBrowsingEnabled
                || showSubfoldersInBrowser_ != draft.showSubfoldersInBrowser;
            const bool viewerOverlayChanged = openViewerWindows.empty()
                || std::any_of(openViewerWindows.begin(), openViewerWindows.end(), [&draft](const viewer::ViewerWindow* viewer)
                {
                    const bool expectedFullMetadataVisible = viewer->IsFullScreen()
                        ? draft.fullScreenFullMetadataVisible
                        : draft.windowedFullMetadataVisible;
                    return viewer->AreInfoOverlaysVisible() != draft.infoOverlaysVisible
                        || viewer->OverlayTextSize() != draft.overlayTextSize
                        || viewer->IsFullMetadataVisible() != expectedFullMetadataVisible;
                })
                || viewer::ViewerWindow::DefaultWindowedFullMetadataVisible() != draft.windowedFullMetadataVisible
                || viewer::ViewerWindow::DefaultFullScreenFullMetadataVisible() != draft.fullScreenFullMetadataVisible;
            const bool quickSendShortcutOrderChanged = quickSendShortcutOrder_ != draft.quickSendShortcutOrder;
            const int nextPrefetchDepthOverride = hyperbrowse::util::ResolvePrefetchDepth(
                draft.resourceProfile,
                draft.prefetchDepthOverride) == draft.prefetchDepthOverride
                    ? draft.prefetchDepthOverride
                    : hyperbrowse::util::kAutomaticPrefetchDepth;
            const bool resourceProfileChanged = resourceProfile_ != draft.resourceProfile
                || prefetchDepthOverride_ != nextPrefetchDepthOverride;
            const bool cacheCapacityChanged = thumbnailCacheCapacityOverrideBytes_ != draft.thumbnailCacheCapacityOverrideBytes
                || metadataCacheCapacityOverrideEntries_ != draft.metadataCacheCapacityOverrideEntries
                || persistentThumbnailCacheCapacityOverrideBytes_ != draft.persistentThumbnailCacheCapacityOverrideBytes;
            const bool persistentCacheEnabledChanged = persistentThumbnailCacheEnabled_ != draft.persistentThumbnailCacheEnabled;

            slideshowIntervalMs_ = NormalizeSlideshowDuration(draft.slideshowIntervalMs);
            slideshowTransitionStyle_ = draft.slideshowTransitionStyle;
            slideshowTransitionDurationMs_ = NormalizeSlideshowTransitionDuration(draft.slideshowTransitionDurationMs);
            useSlideshowTransition_ = draft.useSlideshowTransition;
            viewerMouseWheelBehavior_ = draft.viewerMouseWheelBehavior;
            viewerEscapeKeyBehavior_ = draft.viewerEscapeKeyBehavior;
            invertKeyboardPanning_ = draft.invertKeyboardPanning;
            rawJpegPairedOperationsEnabled_ = draft.rawJpegPairedOperationsEnabled;
            pairedRawJpegViewerPreference_ = draft.pairedRawJpegViewerPreference;
            defaultViewerToSecondaryMonitor_ = draft.defaultViewerToSecondaryMonitor;
            themeMode_ = draft.darkTheme ? ThemeMode::Dark : ThemeMode::Light;
            appTextSize_ = draft.appTextSize;
            thumbnailSizePreset_ = draft.thumbnailSizePreset;
            thumbnailDetailsVisible_ = draft.thumbnailDetailsVisible;
            compactThumbnailLayout_ = draft.compactThumbnailLayout;
            detailsStripVisible_ = draft.detailsStripVisible;
            resourceProfile_ = draft.resourceProfile;
            prefetchDepthOverride_ = nextPrefetchDepthOverride;
            persistentThumbnailCacheEnabled_ = draft.persistentThumbnailCacheEnabled;
            thumbnailCacheCapacityOverrideBytes_ = draft.thumbnailCacheCapacityOverrideBytes;
            metadataCacheCapacityOverrideEntries_ = draft.metadataCacheCapacityOverrideEntries;
            persistentThumbnailCacheCapacityOverrideBytes_ = draft.persistentThumbnailCacheCapacityOverrideBytes;
            showPressureStateInStatusBar_ = draft.showPressureStateInStatusBar;
            nvJpegEnabled_ = draft.nvJpegEnabled;
            libRawOutOfProcessEnabled_ = draft.libRawOutOfProcessEnabled;
            recursiveBrowsingEnabled_ = draft.recursiveBrowsingEnabled;
            showSubfoldersInBrowser_ = draft.showSubfoldersInBrowser;
            closeMainWindowOnEscape_ = draft.closeMainWindowOnEscape;
            if (quickSendShortcutOrderChanged)
            {
                quickSendShortcutOrder_ = draft.quickSendShortcutOrder;
                quickSendModel_.SetShortcutAssignmentOrder(quickSendShortcutOrder_);
                SaveQuickSendStateToRegistry();
            }

            app::Application::SetSingleInstanceEnabled(draft.singleInstanceEnabled);
            app::Application::SetKeepInNotificationAreaEnabled(draft.keepInNotificationAreaEnabled);
            decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_ && HasNvJpegCapability());
            ApplyTheme();
            if (thumbnailDisplayChanged)
            {
                ApplyAppTextSize();
                ApplyThumbnailDisplaySettings();
            }
            if (viewerOverlayChanged)
            {
                viewer::ViewerWindow::SetDefaultWindowedFullMetadataVisible(draft.windowedFullMetadataVisible);
                viewer::ViewerWindow::SetDefaultFullScreenFullMetadataVisible(draft.fullScreenFullMetadataVisible);
                if (!viewerWindow_ || !viewerWindow_->IsOpen())
                {
                    viewer::ViewerWindow::SetDefaultInfoOverlaysVisible(draft.infoOverlaysVisible);
                    viewer::ViewerWindow::SetDefaultOverlayTextSize(draft.overlayTextSize);
                }
                for (viewer::ViewerWindow* viewer : openViewerWindows)
                {
                    viewer->SetInfoOverlaysVisible(draft.infoOverlaysVisible);
                    viewer->SetOverlayTextSize(draft.overlayTextSize);
                    viewer->SetFullMetadataVisible(viewer->IsFullScreen()
                        ? draft.fullScreenFullMetadataVisible
                        : draft.windowedFullMetadataVisible);
                }
            }
            ApplyViewerMouseWheelSetting();
            ApplyViewerEscapeKeyBehavior();
            if (slideshowTransitionChanged)
            {
                ApplyViewerTransitionSettings();
            }
            ApplyRawJpegPairingSettings();
            if (resourceProfileChanged)
            {
                ApplyResourceProfileSetting(false, false);
            }
            if (cacheCapacityChanged)
            {
                ApplyCacheCapacityOverrideSettings(false, false);
            }
            if (resourceProfileChanged || cacheCapacityChanged)
            {
                if (browserPaneController_)
                {
                    browserPaneController_->SetResourceAndCacheSettings(
                        resourceProfile_,
                        prefetchDepthOverride_,
                        thumbnailCacheCapacityOverrideBytes_,
                        metadataCacheCapacityOverrideEntries_,
                        persistentThumbnailCacheCapacityOverrideBytes_);
                }
                RecreateDetailsPanelThumbnailScheduler();
            }
            if (persistentCacheEnabledChanged)
            {
                ApplyPersistentThumbnailCacheSetting();
            }

            if (detailsStripVisible_)
            {
                UpdateDetailsPanel();
            }
            else if (detailsPanelText_)
            {
                ShowWindow(detailsPanelText_, SW_HIDE);
            }
            LayoutChildren();

            if (folderScopeChanged && browserModel_ && !browserModel_->FolderPath().empty())
            {
                LoadFolderAsync(browserModel_->FolderPath());
            }
            else if (folderScopeChanged)
            {
                RefreshBrowserPane();
            }

            if (slideshowDurationChanged)
            {
                for (viewer::ViewerWindow* viewer : openViewerWindows)
                {
                    if (viewer->IsSlideshowActive())
                    {
                        viewer->StartSlideshow(slideshowIntervalMs_);
                    }
                }
            }
            UpdateStatusText();
            UpdateMenuState();
            UpdateWindowTitle();
            SaveWindowState();
        };

        const ExperimentalSettingsDialogResult result = PromptForExperimentalSettings(hwnd_, instance_, &state);
        DeleteFontIfOwned(state.bodyFont);
        state.bodyFont = nullptr;
        if (result == ExperimentalSettingsDialogResult::Unavailable)
        {
            return;
        }
    }
}
