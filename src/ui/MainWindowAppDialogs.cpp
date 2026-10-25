#include "ui/MainWindowInternal.h"

namespace
{
    using hyperbrowse::ui::dialog_detail::AboutDialogState;
    using hyperbrowse::ui::dialog_detail::AboutDialogLinkPalette;
    using hyperbrowse::ui::dialog_detail::ImageInformationDialogState;
    using hyperbrowse::ui::dialog_detail::ShortcutReferenceState;

    std::wstring ToLowercaseCopy(std::wstring value);
    constexpr wchar_t kAboutDialogClassName[] = L"HyperBrowseAboutDialog";
    constexpr UINT kAboutAdapterVendorMessage = WM_APP + 80;
    enum class AboutAdapterVendor : UINT_PTR
    {
        Unavailable,
        Nvidia,
        Amd,
        Intel,
        Qualcomm,
        Microsoft,
        Other,
    };
    constexpr wchar_t kShortcutReferenceClassName[] = L"HyperBrowseShortcutReference";
    constexpr int kShortcutReferenceWidth = 820;
    constexpr int kShortcutReferenceHeight = 620;
    constexpr int kShortcutReferenceMinimumWidth = 560;
    constexpr int kShortcutReferenceMinimumHeight = 360;
    constexpr int kShortcutReferenceMargin = 18;
    constexpr int kShortcutReferenceSubtitleHeight = 24;
    constexpr int kShortcutReferenceControlGap = 8;
    constexpr int kShortcutReferenceButtonWidth = 88;
    constexpr int kShortcutReferenceButtonHeight = 28;
    constexpr int kShortcutReferenceListControlId = 401;
    constexpr wchar_t kAboutDialogGitHubLabel[] = L"GitHub Project";
    constexpr wchar_t kAboutDialogSupportLabel[] = L"Buy Me A Coffee";

    constexpr wchar_t kAboutDialogGitHubUrl[] = L"https://github.com/thetheosopher/HyperBrowse";
    constexpr wchar_t kAboutDialogSupportUrl[] = L"https://buymeacoffee.com/theosopher";
    constexpr int kAboutDialogWidth = 1180;
    constexpr int kAboutDialogHeight = 720;
    constexpr int kAboutDialogMargin = 36;
    constexpr int kAboutDialogHeaderHeight = 224;
    constexpr int kAboutDialogFooterHeight = 96;
    constexpr int kAboutDialogButtonWidth = 104;
    constexpr int kAboutDialogButtonHeight = 38;
    constexpr int kAboutDialogButtonGap = 12;
    constexpr int kAboutDialogBrandArtSize = 152;
    constexpr int kAboutDialogIconSize = 64;

    int CurrentCalendarYear()
    {
        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);
        return static_cast<int>(localTime.wYear);
    }

    void RebuildAboutDialogFonts(AboutDialogState& state)
    {
        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.subtitleFont);
        DeleteFontIfOwned(state.bodyFont);
        DeleteFontIfOwned(state.footerFont);
        state.titleFont = CreateDialogUiFont(21, FW_BOLD, state.appTextSize, state.dpi);
        state.subtitleFont = CreateDialogUiFont(11, FW_SEMIBOLD, state.appTextSize, state.dpi);
        state.bodyFont = CreateDialogUiFont(10, FW_NORMAL, state.appTextSize, state.dpi);
        state.footerFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize, state.dpi);

        if (!state.titleFont) state.titleFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.subtitleFont) state.subtitleFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.bodyFont) state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.footerFont) state.footerFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    int ScaleAboutDialogDimension(int dimension, const AboutDialogState& state)
    {
        return ScaleDialogAppTextDimension(dimension, state.appTextSize, state.dpi);
    }

    int MeasureAboutDialogLinkButtonWidth(HFONT font,
                                          std::wstring_view firstLabel,
                                          std::wstring_view secondLabel,
                                          const AboutDialogState& state)
    {
        const int textWidth = std::max(MeasureTextWidth(font, firstLabel), MeasureTextWidth(font, secondLabel));
        return std::max(ScaleAboutDialogDimension(180, state), textWidth + ScaleAboutDialogDimension(40, state));
    }

    int MeasureAboutDialogFooterActionWidth(const AboutDialogState& state);

    int MeasureAboutDialogClientWidth(const AboutDialogState& state)
    {
        const int margin = ScaleAboutDialogDimension(kAboutDialogMargin, state);
        const int minimumFooterTextWidth = ScaleAboutDialogDimension(200, state);
        const int minimumWidth = margin * 2
            + MeasureAboutDialogFooterActionWidth(state)
            + ScaleAboutDialogDimension(20, state)
            + minimumFooterTextWidth;
        return std::max(ScaleAboutDialogDimension(kAboutDialogWidth, state), minimumWidth);
    }

    std::wstring BuildAboutDialogBodyContent(const AboutDialogState& state)
    {
        std::wstring content = L"Version: ";
        content.append(state.version);
        content.append(L"\r\nBuild configuration: ");
        content.append(state.buildConfiguration);
        content.append(L"\r\nGPU vendor: ");
        content.append(state.gpuVendor.empty() ? L"Not available" : state.gpuVendor);
        content.append(L"\r\n\r\n");
        content.append(state.bodyContent);
        return content;
    }

    std::wstring DisplayDeviceNameForWindow(HWND window)
    {
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!monitor)
        {
            return {};
        }

        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&monitorInfo)) == FALSE)
        {
            return {};
        }
        return monitorInfo.szDevice;
    }

    AboutAdapterVendor QueryAboutAdapterVendor(std::wstring_view displayDeviceName)
    {
        if (displayDeviceName.empty())
        {
            return AboutAdapterVendor::Unavailable;
        }

        const std::wstring targetName(displayDeviceName);
        DISPLAY_DEVICEW displayDevice{};
        displayDevice.cb = sizeof(displayDevice);
        for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &displayDevice, 0) != FALSE; ++index)
        {
            if (_wcsicmp(displayDevice.DeviceName, targetName.c_str()) == 0)
            {
                const std::wstring deviceId = ToLowercaseCopy(displayDevice.DeviceID);
                const std::wstring deviceDescription = ToLowercaseCopy(displayDevice.DeviceString);
                if (deviceId.find(L"ven_10de") != std::wstring::npos || deviceDescription.find(L"nvidia") != std::wstring::npos)
                {
                    return AboutAdapterVendor::Nvidia;
                }
                if (deviceId.find(L"ven_1002") != std::wstring::npos
                    || deviceId.find(L"ven_1022") != std::wstring::npos
                    || deviceDescription.find(L"amd") != std::wstring::npos
                    || deviceDescription.find(L"radeon") != std::wstring::npos)
                {
                    return AboutAdapterVendor::Amd;
                }
                if (deviceId.find(L"ven_8086") != std::wstring::npos || deviceDescription.find(L"intel") != std::wstring::npos)
                {
                    return AboutAdapterVendor::Intel;
                }
                if (deviceId.find(L"ven_5143") != std::wstring::npos || deviceDescription.find(L"qualcomm") != std::wstring::npos)
                {
                    return AboutAdapterVendor::Qualcomm;
                }
                if (deviceId.find(L"ven_1414") != std::wstring::npos || deviceDescription.find(L"microsoft") != std::wstring::npos)
                {
                    return AboutAdapterVendor::Microsoft;
                }
                return AboutAdapterVendor::Other;
            }

            displayDevice = {};
            displayDevice.cb = sizeof(displayDevice);
        }
        return AboutAdapterVendor::Unavailable;
    }

    std::wstring AboutAdapterVendorLabel(WPARAM vendorValue)
    {
        switch (static_cast<AboutAdapterVendor>(vendorValue))
        {
        case AboutAdapterVendor::Nvidia: return L"NVIDIA";
        case AboutAdapterVendor::Amd: return L"AMD";
        case AboutAdapterVendor::Intel: return L"Intel";
        case AboutAdapterVendor::Qualcomm: return L"Qualcomm";
        case AboutAdapterVendor::Microsoft: return L"Microsoft";
        case AboutAdapterVendor::Other: return L"Other";
        default: return L"Not available";
        }
    }

    int MeasureAboutDialogClientHeight(const AboutDialogState& state, int clientWidth)
    {
        const int margin = ScaleAboutDialogDimension(kAboutDialogMargin, state);
        const int brandArtSize = ScaleAboutDialogDimension(kAboutDialogBrandArtSize, state);
        const int contentRight = clientWidth - margin;
        const int artLeft = contentRight - brandArtSize;
        const int iconLeft = margin;
        const int iconSize = ScaleAboutDialogDimension(kAboutDialogIconSize, state);
        const int textLeft = iconLeft + iconSize + ScaleAboutDialogDimension(20, state);
        const int textRight = artLeft - ScaleAboutDialogDimension(28, state);
        const int textWidth = std::max(1, textRight - textLeft);

        const int titleTop = margin - ScaleAboutDialogDimension(2, state);
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(44, state));
        const int subtitleTop = titleTop + titleHeight + ScaleAboutDialogDimension(10, state);
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(28, state));
        const int introTop = subtitleTop + subtitleHeight + ScaleAboutDialogDimension(10, state);
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, ScaleAboutDialogDimension(40, state));
        const int artFrameBottom = (margin - ScaleAboutDialogDimension(4, state)) + brandArtSize + ScaleAboutDialogDimension(10, state);
        const int headerHeight = std::max(ScaleAboutDialogDimension(kAboutDialogHeaderHeight, state),
                                          std::max(artFrameBottom + margin - ScaleAboutDialogDimension(8, state),
                                                   introTop + introHeight + margin - ScaleAboutDialogDimension(8, state)));

        const int bodyWidth = std::max(1, clientWidth - (margin * 2));
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(28, state));
        const std::wstring bodyContent = BuildAboutDialogBodyContent(state);
        const int bodyTextHeight = MeasureTextBlockHeight(state.bodyFont, bodyContent, bodyWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int bodyHeight = ScaleAboutDialogDimension(24, state) + headingHeight + ScaleAboutDialogDimension(12, state) + bodyTextHeight + ScaleAboutDialogDimension(26, state);

        const int footerActionWidth = state.githubButtonWidth
            + state.supportButtonWidth
            + ScaleAboutDialogDimension(kAboutDialogButtonWidth, state)
            + ScaleAboutDialogDimension(kAboutDialogButtonGap * 2, state);
        const int footerTextWidth = std::max(ScaleAboutDialogDimension(200, state),
                             clientWidth - (margin * 2) - footerActionWidth - ScaleAboutDialogDimension(20, state));
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(ScaleAboutDialogDimension(kAboutDialogFooterHeight, state),
                                          std::max(footerTextHeight + ScaleAboutDialogDimension(34, state),
                                                   ScaleAboutDialogDimension(kAboutDialogButtonHeight + 36, state)));

        return headerHeight + bodyHeight + footerHeight;
    }

    int MeasureAboutDialogFooterActionWidth(const AboutDialogState& state)
    {
        return state.githubButtonWidth
            + state.supportButtonWidth
            + ScaleAboutDialogDimension(kAboutDialogButtonWidth, state)
            + ScaleAboutDialogDimension(kAboutDialogButtonGap * 2, state);
    }

    int MeasureAboutDialogFooterTextWidth(const AboutDialogState& state, int clientWidth)
    {
        const int margin = ScaleAboutDialogDimension(kAboutDialogMargin, state);
        return std::max(ScaleAboutDialogDimension(200, state),
                        clientWidth - (margin * 2) - MeasureAboutDialogFooterActionWidth(state) - ScaleAboutDialogDimension(20, state));
    }

    int AboutDialogFooterButtonsLeft(const RECT& clientRect, const AboutDialogState& state)
    {
        const int margin = ScaleAboutDialogDimension(kAboutDialogMargin, state);
        const int buttonsLeft = static_cast<int>(clientRect.right) - margin - MeasureAboutDialogFooterActionWidth(state);
        return std::max(margin, buttonsLeft);
    }

    int AboutDialogFooterButtonsTop(const RECT& clientRect, const AboutDialogState& state)
    {
        return clientRect.bottom
            - ScaleAboutDialogDimension(kAboutDialogMargin, state)
            - ScaleAboutDialogDimension(kAboutDialogButtonHeight, state);
    }

    const wchar_t* GetAboutDialogLinkLabel(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return kAboutDialogGitHubLabel;
        case ID_ABOUT_OPEN_SUPPORT:
            return kAboutDialogSupportLabel;
        default:
            return L"";
        }
    }

    const wchar_t* GetAboutDialogLinkTarget(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return kAboutDialogGitHubUrl;
        case ID_ABOUT_OPEN_SUPPORT:
            return kAboutDialogSupportUrl;
        default:
            return nullptr;
        }
    }

    const wchar_t* GetAboutDialogLinkFailureMessage(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return L"Failed to open the HyperBrowse GitHub project.";
        case ID_ABOUT_OPEN_SUPPORT:
            return L"Failed to open the Buy Me A Coffee page.";
        default:
            return L"Failed to open the selected link.";
        }
    }

    COLORREF GetAboutDialogSupportAccent(bool darkMode)
    {
        return darkMode ? RGB(255, 214, 126) : RGB(145, 78, 16);
    }

    AboutDialogLinkPalette BuildAboutDialogLinkPalette(UINT controlId, const AboutDialogState& state, UINT itemState)
    {
        const bool supportButton = controlId == ID_ABOUT_OPEN_SUPPORT;
        const bool pressed = (itemState & ODS_SELECTED) != 0;
        const bool hot = (itemState & ODS_HOTLIGHT) != 0;
        const bool disabled = (itemState & ODS_DISABLED) != 0;

        // High contrast uses system colors only; blended brand colors would break the user's palette.
        if (hyperbrowse::ui::IsHighContrastEnabled())
        {
            AboutDialogLinkPalette highContrastPalette;
            const bool highlighted = pressed || hot;
            highContrastPalette.fill = GetSysColor(highlighted ? COLOR_HIGHLIGHT : COLOR_WINDOW);
            highContrastPalette.text = GetSysColor(
                disabled ? COLOR_GRAYTEXT : (highlighted ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            highContrastPalette.border = GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT);
            return highContrastPalette;
        }

        const COLORREF accent = supportButton ? GetAboutDialogSupportAccent(state.darkMode) : state.accent;
        int fillMixAmount = supportButton
            ? (state.darkMode ? 34 : 18)
            : (state.darkMode ? 28 : 10);
        if (hot)
        {
            fillMixAmount += supportButton ? 10 : 8;
        }
        if (pressed)
        {
            fillMixAmount += supportButton ? 18 : 14;
        }
        fillMixAmount = std::min(fillMixAmount, 96);

        AboutDialogLinkPalette palette;
        if (supportButton)
        {
            const COLORREF baseFill = state.darkMode ? RGB(88, 61, 19) : RGB(255, 245, 219);
            palette.fill = BlendColor(baseFill, accent, static_cast<BYTE>(fillMixAmount));
            palette.text = state.darkMode ? RGB(255, 236, 194) : RGB(112, 62, 15);
        }
        else
        {
            const COLORREF baseFill = state.darkMode ? state.footerBackground : RGB(255, 255, 255);
            palette.fill = BlendColor(baseFill, accent, static_cast<BYTE>(fillMixAmount));
            palette.text = accent;
        }

        palette.border = BlendColor(accent, state.border, supportButton ? 22 : 28);

        if (disabled)
        {
            palette.fill = BlendColor(palette.fill, state.footerBackground, 120);
            palette.border = BlendColor(palette.border, state.border, 120);
            palette.text = state.mutedText;
        }

        return palette;
    }

    void OpenAboutDialogLink(HWND hwnd, UINT controlId)
    {
        const wchar_t* target = GetAboutDialogLinkTarget(controlId);
        if (!target)
        {
            return;
        }

        if (!LaunchShellTarget(hwnd, L"open", target))
        {
            MessageBoxW(hwnd, GetAboutDialogLinkFailureMessage(controlId), L"About HyperBrowse", MB_OK | MB_ICONERROR);
        }
    }

    void DrawAboutDialogLinkButton(const DRAWITEMSTRUCT& drawItem, const AboutDialogState& state)
    {
        RECT buttonRect{};
        GetClientRect(drawItem.hwndItem, &buttonRect);

        const HBRUSH footerBrush = CreateSolidBrush(state.footerBackground);
        FillRect(drawItem.hDC, &buttonRect, footerBrush);
        DeleteObject(footerBrush);

        const AboutDialogLinkPalette palette = BuildAboutDialogLinkPalette(drawItem.CtlID, state, drawItem.itemState);
        RECT pillRect = buttonRect;
        InflateRect(&pillRect, -1, -1);

        const HBRUSH fillBrush = CreateSolidBrush(palette.fill);
        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.border);
        const HGDIOBJ oldBrush = SelectObject(drawItem.hDC, fillBrush);
        const HGDIOBJ oldPen = SelectObject(drawItem.hDC, borderPen);
        const int cornerRadius = ScaleAboutDialogDimension(16, state);
        RoundRect(drawItem.hDC, pillRect.left, pillRect.top, pillRect.right, pillRect.bottom, cornerRadius, cornerRadius);
        SelectObject(drawItem.hDC, oldPen);
        SelectObject(drawItem.hDC, oldBrush);
        DeleteObject(borderPen);
        DeleteObject(fillBrush);

        RECT textRect = pillRect;
        InflateRect(&textRect,
                -ScaleAboutDialogDimension(12, state),
                -ScaleAboutDialogDimension(6, state));
        hyperbrowse::render::DrawGdiText(drawItem.hDC,
                    state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    GetAboutDialogLinkLabel(drawItem.CtlID),
                    -1,
                    textRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    palette.text,
                    palette.fill);

        if ((drawItem.itemState & ODS_FOCUS) != 0)
        {
            RECT focusRect = pillRect;
            InflateRect(&focusRect,
                        -ScaleAboutDialogDimension(4, state),
                        -ScaleAboutDialogDimension(4, state));
            DrawFocusRect(drawItem.hDC, &focusRect);
        }
    }

    void LayoutAboutDialogControls(HWND hwnd, const AboutDialogState& state)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int buttonTop = AboutDialogFooterButtonsTop(client, state);
        const int buttonHeight = ScaleAboutDialogDimension(kAboutDialogButtonHeight, state);
        const int buttonGap = ScaleAboutDialogDimension(kAboutDialogButtonGap, state);
        int buttonLeft = AboutDialogFooterButtonsLeft(client, state);

        if (state.githubButton)
        {
            MoveWindow(state.githubButton,
                       buttonLeft,
                       buttonTop,
                       state.githubButtonWidth,
                       buttonHeight,
                       TRUE);
            buttonLeft += state.githubButtonWidth + buttonGap;
        }

        if (state.supportButton)
        {
            MoveWindow(state.supportButton,
                       buttonLeft,
                       buttonTop,
                       state.supportButtonWidth,
                       buttonHeight,
                       TRUE);
            buttonLeft += state.supportButtonWidth + buttonGap;
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton,
                       buttonLeft,
                       buttonTop,
                       ScaleAboutDialogDimension(kAboutDialogButtonWidth, state),
                       buttonHeight,
                       TRUE);
        }
    }

    void PaintAboutDialog(HDC hdc, const RECT& clientRect, const AboutDialogState& state)
    {
        const std::wstring bodyContent = BuildAboutDialogBodyContent(state);
        const int margin = ScaleAboutDialogDimension(kAboutDialogMargin, state);
        const int brandArtSize = ScaleAboutDialogDimension(kAboutDialogBrandArtSize, state);
        const int contentRight = clientRect.right - margin;
        const int artLeft = contentRight - brandArtSize;
        const int artTop = margin - ScaleAboutDialogDimension(4, state);
        const int iconLeft = margin;
        const int iconTop = margin + ScaleAboutDialogDimension(2, state);
        const int iconSize = ScaleAboutDialogDimension(kAboutDialogIconSize, state);
        const int textLeft = iconLeft + iconSize + ScaleAboutDialogDimension(20, state);
        const int textRight = artLeft - ScaleAboutDialogDimension(28, state);
        const int textWidth = std::max(ScaleAboutDialogDimension(320, state), textRight - textLeft);

        const int titleTop = margin - ScaleAboutDialogDimension(2, state);
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(44, state));
        const int subtitleTop = titleTop + titleHeight + ScaleAboutDialogDimension(10, state);
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(28, state));
        const int introTop = subtitleTop + subtitleHeight + ScaleAboutDialogDimension(10, state);
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, ScaleAboutDialogDimension(40, state));
        const int artFrameBottom = artTop + brandArtSize + ScaleAboutDialogDimension(10, state);
        const int headerHeight = std::max(ScaleAboutDialogDimension(kAboutDialogHeaderHeight, state),
                                          std::max(artFrameBottom + margin - ScaleAboutDialogDimension(8, state),
                                                   introTop + introHeight + margin - ScaleAboutDialogDimension(8, state)));

        const int bodyWidth = clientRect.right - (margin * 2);
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(28, state));
        const int footerTextWidth = MeasureAboutDialogFooterTextWidth(state, clientRect.right);
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(ScaleAboutDialogDimension(kAboutDialogFooterHeight, state),
                                          std::max(footerTextHeight + ScaleAboutDialogDimension(34, state),
                                                   ScaleAboutDialogDimension(kAboutDialogButtonHeight + 36, state)));

        const RECT headerRect{clientRect.left, clientRect.top, clientRect.right, clientRect.top + headerHeight};
        const RECT footerRect{clientRect.left, clientRect.bottom - footerHeight, clientRect.right, clientRect.bottom};
        const RECT bodyRect{clientRect.left, headerRect.bottom, clientRect.right, footerRect.top};

        HBRUSH backgroundBrush = CreateSolidBrush(state.background);
        FillRect(hdc, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        HBRUSH headerBrush = CreateSolidBrush(state.headerBackground);
        FillRect(hdc, &headerRect, headerBrush);
        DeleteObject(headerBrush);

        HBRUSH footerBrush = CreateSolidBrush(state.footerBackground);
        FillRect(hdc, &footerRect, footerBrush);
        DeleteObject(footerBrush);

        HBRUSH bodyBrush = CreateSolidBrush(state.panelBackground);
        FillRect(hdc, &bodyRect, bodyBrush);
        DeleteObject(bodyBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, state.border);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, clientRect.left, headerRect.bottom - 1, nullptr);
        LineTo(hdc, clientRect.right, headerRect.bottom - 1);
        MoveToEx(hdc, clientRect.left, footerRect.top, nullptr);
        LineTo(hdc, clientRect.right, footerRect.top);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        if (state.heroIcon)
        {
            DrawIconEx(hdc, iconLeft, iconTop, state.heroIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        }

        if (state.brandArt)
        {
            const int artInset = ScaleAboutDialogDimension(10, state);
            RECT artFrame{artLeft - artInset, artTop - artInset, artLeft + brandArtSize + artInset, artTop + brandArtSize + artInset};
            HBRUSH artPanelBrush = CreateSolidBrush(BlendColor(state.headerBackground, state.background, state.darkMode ? 42 : 18));
            HPEN artBorderPen = CreatePen(PS_SOLID, 1, state.border);
            HGDIOBJ oldBrush = SelectObject(hdc, artPanelBrush);
            HGDIOBJ oldArtPen = SelectObject(hdc, artBorderPen);
            RoundRect(hdc, artFrame.left, artFrame.top, artFrame.right, artFrame.bottom, 18, 18);
            SelectObject(hdc, oldArtPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(artBorderPen);
            DeleteObject(artPanelBrush);

            hyperbrowse::util::DrawBitmapWithAlpha(hdc, *state.brandArt, artLeft, artTop, brandArtSize, brandArtSize);
        }

        RECT titleRect{textLeft, titleTop, textRight, titleTop + titleHeight};
        RECT subtitleRect{textLeft, subtitleTop, textRight, subtitleTop + subtitleHeight};
        RECT introRect{textLeft, introTop, textRight, introTop + introHeight};

        hyperbrowse::render::DrawGdiText(hdc,
                    state.titleFont ? state.titleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.title.c_str(),
                    -1,
                    titleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    state.text,
                    state.headerBackground);

        hyperbrowse::render::DrawGdiText(hdc,
                    state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.subtitle.c_str(),
                    -1,
                    subtitleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    state.accent,
                    state.headerBackground);

        hyperbrowse::render::DrawGdiText(hdc,
                    state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.intro.c_str(),
                    -1,
                    introRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                    state.mutedText,
                    state.headerBackground);

        const int bodyTopInset = ScaleAboutDialogDimension(24, state);
        RECT headingRect{margin, bodyRect.top + bodyTopInset, clientRect.right - margin, bodyRect.top + bodyTopInset + headingHeight};
        hyperbrowse::render::DrawGdiText(hdc,
                    state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.bodyHeading.c_str(),
                    -1,
                    headingRect,
                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX,
                    state.accent,
                    state.panelBackground);

        RECT bodyTextRect{margin, headingRect.bottom + ScaleAboutDialogDimension(12, state), clientRect.right - margin, footerRect.top - ScaleAboutDialogDimension(18, state)};
        hyperbrowse::render::DrawGdiText(hdc,
                    state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    bodyContent.c_str(),
                    -1,
                    bodyTextRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                    state.text,
                    state.panelBackground);

        RECT footerTextRect{margin,
                footerRect.top + ScaleAboutDialogDimension(18, state),
                std::max(margin + ScaleAboutDialogDimension(200, state), AboutDialogFooterButtonsLeft(clientRect, state) - ScaleAboutDialogDimension(20, state)),
                footerRect.bottom - ScaleAboutDialogDimension(16, state)};
        hyperbrowse::render::DrawGdiText(hdc,
                    state.footerFont ? state.footerFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.footer.c_str(),
                    -1,
                    footerTextRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                    state.mutedText,
                    state.footerBackground);
    }

    bool EnsureAboutDialogD2DResources(HWND hwnd, AboutDialogState& state)
    {
        if (state.d2dRenderTarget)
        {
            return true;
        }

        auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return false;
        }

        state.d2dRenderTarget = renderer.CreateHwndRenderTarget(hwnd);
        if (!state.d2dRenderTarget)
        {
            return false;
        }

        state.d2dTitleFormat = renderer.CreateTextFormatFromFont(state.titleFont);
        state.d2dSubtitleFormat = renderer.CreateTextFormatFromFont(state.subtitleFont);
        state.d2dBodyFormat = renderer.CreateTextFormatFromFont(state.bodyFont);
        state.d2dFooterFormat = renderer.CreateTextFormatFromFont(state.footerFont);

        if (!state.d2dTitleFormat
            || !state.d2dSubtitleFormat
            || !state.d2dBodyFormat
            || !state.d2dFooterFormat)
        {
            state.d2dRenderTarget.Reset();
            state.d2dBrandArtBitmap.Reset();
            state.d2dTitleFormat.Reset();
            state.d2dSubtitleFormat.Reset();
            state.d2dBodyFormat.Reset();
            state.d2dFooterFormat.Reset();
            return false;
        }

        state.d2dTitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        state.d2dSubtitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        state.d2dBodyFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        state.d2dFooterFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

        if (state.brandArt)
        {
            state.d2dBrandArtBitmap = renderer.CreateBitmapFromCachedThumbnail(
                state.d2dRenderTarget.Get(),
                *state.brandArt);
        }

        return true;
    }

    void PaintAboutDialogD2D(ID2D1RenderTarget* renderTarget,
                            const D2D1_SIZE_F& size,
                            const AboutDialogState& state)
    {
        if (!renderTarget)
        {
            return;
        }

        const std::wstring bodyContent = BuildAboutDialogBodyContent(state);
        const int clientWidth = static_cast<int>(size.width);
        const int clientHeight = static_cast<int>(size.height);
        const int margin = ScaleAboutDialogDimension(kAboutDialogMargin, state);
        const int brandArtSize = ScaleAboutDialogDimension(kAboutDialogBrandArtSize, state);
        const int contentRight = clientWidth - margin;
        const int artLeft = contentRight - brandArtSize;
        const int artTop = margin - ScaleAboutDialogDimension(4, state);
        const int iconLeft = margin;
        const int iconTop = margin + ScaleAboutDialogDimension(2, state);
        const int iconSize = ScaleAboutDialogDimension(kAboutDialogIconSize, state);
        const int textLeft = iconLeft + iconSize + ScaleAboutDialogDimension(20, state);
        const int textRight = artLeft - ScaleAboutDialogDimension(28, state);
        const int textWidth = std::max(ScaleAboutDialogDimension(320, state), textRight - textLeft);

        const int titleTop = margin - ScaleAboutDialogDimension(2, state);
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(44, state));
        const int subtitleTop = titleTop + titleHeight + ScaleAboutDialogDimension(10, state);
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(28, state));
        const int introTop = subtitleTop + subtitleHeight + ScaleAboutDialogDimension(10, state);
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, ScaleAboutDialogDimension(40, state));
        const int artFrameBottom = artTop + brandArtSize + ScaleAboutDialogDimension(10, state);
        const int headerHeight = std::max(ScaleAboutDialogDimension(kAboutDialogHeaderHeight, state),
                          std::max(artFrameBottom + margin - ScaleAboutDialogDimension(8, state),
                               introTop + introHeight + margin - ScaleAboutDialogDimension(8, state)));

        const int bodyWidth = clientWidth - (margin * 2);
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, ScaleAboutDialogDimension(28, state));
        const int footerTextWidth = MeasureAboutDialogFooterTextWidth(state, clientWidth);
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(ScaleAboutDialogDimension(kAboutDialogFooterHeight, state),
                          std::max(footerTextHeight + ScaleAboutDialogDimension(34, state),
                               ScaleAboutDialogDimension(kAboutDialogButtonHeight + 36, state)));

        const D2D1_RECT_F headerRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(clientWidth), static_cast<float>(headerHeight));
        const D2D1_RECT_F footerRect = D2D1::RectF(0.0f, static_cast<float>(clientHeight - footerHeight), static_cast<float>(clientWidth), static_cast<float>(clientHeight));
        const D2D1_RECT_F bodyRect = D2D1::RectF(0.0f, static_cast<float>(headerHeight), static_cast<float>(clientWidth), static_cast<float>(clientHeight - footerHeight));

        auto createBrush = [renderTarget](COLORREF color)
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(
                hyperbrowse::render::ToD2DColor(color),
                brush.GetAddressOf());
            return brush;
        };

        const auto backgroundBrush = createBrush(state.background);
        const auto headerBrush = createBrush(state.headerBackground);
        const auto footerBrush = createBrush(state.footerBackground);
        const auto panelBrush = createBrush(state.panelBackground);
        const auto borderBrush = createBrush(state.border);
        const auto textBrush = createBrush(state.text);
        const auto mutedTextBrush = createBrush(state.mutedText);
        const auto accentBrush = createBrush(state.accent);

        renderTarget->Clear(hyperbrowse::render::ToD2DColor(state.background));
        if (headerBrush)
        {
            renderTarget->FillRectangle(&headerRect, headerBrush.Get());
        }
        if (footerBrush)
        {
            renderTarget->FillRectangle(&footerRect, footerBrush.Get());
        }
        if (panelBrush)
        {
            renderTarget->FillRectangle(&bodyRect, panelBrush.Get());
        }
        if (borderBrush)
        {
            renderTarget->DrawLine(
                D2D1::Point2F(0.0f, static_cast<float>(headerHeight - 1)),
                D2D1::Point2F(static_cast<float>(clientWidth), static_cast<float>(headerHeight - 1)),
                borderBrush.Get());
            renderTarget->DrawLine(
                D2D1::Point2F(0.0f, static_cast<float>(clientHeight - footerHeight)),
                D2D1::Point2F(static_cast<float>(clientWidth), static_cast<float>(clientHeight - footerHeight)),
                borderBrush.Get());
        }

        if (state.d2dBrandArtBitmap)
        {
            const D2D1_RECT_F artFrame = D2D1::RectF(
                static_cast<float>(artLeft - ScaleAboutDialogDimension(10, state)),
                static_cast<float>(artTop - ScaleAboutDialogDimension(10, state)),
                static_cast<float>(artLeft + brandArtSize + ScaleAboutDialogDimension(10, state)),
                static_cast<float>(artTop + brandArtSize + ScaleAboutDialogDimension(10, state)));
            if (panelBrush)
            {
                renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(artFrame, 9.0f, 9.0f),
                    panelBrush.Get());
            }
            if (borderBrush)
            {
                renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(artFrame, 9.0f, 9.0f),
                    borderBrush.Get(),
                    1.0f);
            }
            renderTarget->DrawBitmap(
                state.d2dBrandArtBitmap.Get(),
                D2D1::RectF(static_cast<float>(artLeft),
                            static_cast<float>(artTop),
                            static_cast<float>(artLeft + brandArtSize),
                            static_cast<float>(artTop + brandArtSize)),
                1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }

        const auto drawText = [renderTarget](std::wstring_view text,
                                             IDWriteTextFormat* format,
                                             const RECT& rect,
                                             ID2D1Brush* brush)
        {
            if (text.empty() || !format || !brush)
            {
                return;
            }

            renderTarget->DrawText(
                text.data(),
                static_cast<UINT32>(text.size()),
                format,
                hyperbrowse::render::ToD2DRect(rect),
                brush);
        };

        const RECT titleRect{textLeft, titleTop, textRight, titleTop + titleHeight};
        const RECT subtitleRect{textLeft, subtitleTop, textRight, subtitleTop + subtitleHeight};
        const RECT introRect{textLeft, introTop, textRight, introTop + introHeight};
        drawText(state.title, state.d2dTitleFormat.Get(), titleRect, textBrush.Get());
        drawText(state.subtitle, state.d2dSubtitleFormat.Get(), subtitleRect, accentBrush.Get());
        drawText(state.intro, state.d2dBodyFormat.Get(), introRect, mutedTextBrush.Get());

        const int bodyTopInset = ScaleAboutDialogDimension(24, state);
        const RECT headingRect{margin, headerHeight + bodyTopInset, clientWidth - margin, headerHeight + bodyTopInset + headingHeight};
        const RECT bodyTextRect{margin, headingRect.bottom + ScaleAboutDialogDimension(12, state), clientWidth - margin, clientHeight - footerHeight - ScaleAboutDialogDimension(18, state)};
        drawText(state.bodyHeading, state.d2dSubtitleFormat.Get(), headingRect, accentBrush.Get());
        drawText(bodyContent, state.d2dBodyFormat.Get(), bodyTextRect, textBrush.Get());

        const RECT footerTextRect{margin,
                      clientHeight - footerHeight + ScaleAboutDialogDimension(18, state),
                      std::max(margin + ScaleAboutDialogDimension(200, state), AboutDialogFooterButtonsLeft(RECT{0, 0, clientWidth, clientHeight}, state) - ScaleAboutDialogDimension(20, state)),
                      clientHeight - ScaleAboutDialogDimension(16, state)};
        drawText(state.footer, state.d2dFooterFormat.Get(), footerTextRect, mutedTextBrush.Get());

        (void)backgroundBrush;
        (void)iconLeft;
        (void)iconTop;
        (void)iconSize;
    }

    LRESULT CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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
            state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            state->githubButton = CreateWindowExW(
                0,
                L"BUTTON",
                kAboutDialogGitHubLabel,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0,
                0,
                state->githubButtonWidth,
                ScaleAboutDialogDimension(kAboutDialogButtonHeight, *state),
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ABOUT_OPEN_GITHUB)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->supportButton = CreateWindowExW(
                0,
                L"BUTTON",
                kAboutDialogSupportLabel,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0,
                0,
                state->supportButtonWidth,
                ScaleAboutDialogDimension(kAboutDialogButtonHeight, *state),
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ABOUT_OPEN_SUPPORT)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                ScaleAboutDialogDimension(kAboutDialogButtonWidth, *state),
                ScaleAboutDialogDimension(kAboutDialogButtonHeight, *state),
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            if (state->githubButton)
            {
                SendMessageW(state->githubButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->subtitleFont ? state->subtitleFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            if (state->supportButton)
            {
                SendMessageW(state->supportButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->subtitleFont ? state->subtitleFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            if (state->okButton)
            {
                SendMessageW(state->okButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->bodyFont ? state->bodyFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }

            if (state->windowIcon)
            {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(state->windowIcon));
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(state->windowIcon));
            }

            ApplyWindowFrameTheme(hwnd, state->darkMode);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            LayoutAboutDialogControls(hwnd, *state);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutAboutDialogControls(hwnd, *state);
            }
            return 0;
        case kAboutAdapterVendorMessage:
            if (state)
            {
                state->gpuVendor = AboutAdapterVendorLabel(wParam);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = std::max<UINT>(96, HIWORD(wParam));
                RebuildAboutDialogFonts(*state);
                state->githubButtonWidth = MeasureAboutDialogLinkButtonWidth(state->subtitleFont,
                                                                               kAboutDialogGitHubLabel,
                                                                               kAboutDialogSupportLabel,
                                                                               *state);
                state->supportButtonWidth = state->githubButtonWidth;
                if (state->githubButton)
                {
                    SendMessageW(state->githubButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->subtitleFont), TRUE);
                }
                if (state->supportButton)
                {
                    SendMessageW(state->supportButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->subtitleFont), TRUE);
                }
                if (state->okButton)
                {
                    SendMessageW(state->okButton, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
                }
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                const int aboutClientWidth = MeasureAboutDialogClientWidth(*state);
                const int aboutClientHeight = std::max(ScaleAboutDialogDimension(kAboutDialogHeight, *state),
                                                        MeasureAboutDialogClientHeight(*state, aboutClientWidth));
                RECT desiredFrame{0, 0, aboutClientWidth, aboutClientHeight};
                AdjustDialogWindowRectForDpi(
                    &desiredFrame,
                    WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                    FALSE,
                    WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                    state->dpi);

                RECT adjusted = suggested ? *suggested : RECT{};
                if (!suggested)
                {
                    GetWindowRect(hwnd, &adjusted);
                }
                adjusted.right = adjusted.left + (desiredFrame.right - desiredFrame.left);
                adjusted.bottom = adjusted.top + (desiredFrame.bottom - desiredFrame.top);
                adjusted = ClampDialogFrameToWorkArea(
                    adjusted,
                    MeasureDialogShellMetrics(hwnd, state->appTextSize).workArea);
                if (adjusted.right > adjusted.left && adjusted.bottom > adjusted.top)
                {
                    SetWindowPos(hwnd,
                                 nullptr,
                                 adjusted.left,
                                 adjusted.top,
                                 adjusted.right - adjusted.left,
                                 adjusted.bottom - adjusted.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                state->d2dRenderTarget.Reset();
                state->d2dBrandArtBitmap.Reset();
                state->d2dTitleFormat.Reset();
                state->d2dSubtitleFormat.Reset();
                state->d2dBodyFormat.Reset();
                state->d2dFooterFormat.Reset();
                LayoutAboutDialogControls(hwnd, *state);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state && state->okButton)
            {
                SetFocus(state->okButton);
                return FALSE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            if (!state)
            {
                break;
            }

            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);

            bool paintedWithD2D = false;
            if (EnsureAboutDialogD2DResources(hwnd, *state))
            {
                hyperbrowse::render::D2DRenderer::Instance().ResizeRenderTarget(
                    state->d2dRenderTarget.Get(),
                    hwnd);
                state->d2dRenderTarget->BeginDraw();
                PaintAboutDialogD2D(
                    state->d2dRenderTarget.Get(),
                    state->d2dRenderTarget->GetSize(),
                    *state);
                const HRESULT drawResult = state->d2dRenderTarget->EndDraw();
                paintedWithD2D = SUCCEEDED(drawResult);
                if (drawResult == D2DERR_RECREATE_TARGET)
                {
                    state->d2dRenderTarget.Reset();
                    state->d2dBrandArtBitmap.Reset();
                    state->d2dTitleFormat.Reset();
                    state->d2dSubtitleFormat.Reset();
                    state->d2dBodyFormat.Reset();
                    state->d2dFooterFormat.Reset();
                }
            }

            if (!paintedWithD2D)
            {
                PaintAboutDialog(hdc, clientRect, *state);
            }
            if (paintedWithD2D && state->heroIcon)
            {
                const int iconInset = ScaleAboutDialogDimension(2, *state);
                const int iconSize = ScaleAboutDialogDimension(kAboutDialogIconSize, *state);
                DrawIconEx(hdc,
                           ScaleAboutDialogDimension(kAboutDialogMargin, *state),
                           ScaleAboutDialogDimension(kAboutDialogMargin, *state) + iconInset,
                           state->heroIcon,
                           iconSize,
                           iconSize,
                           0,
                           nullptr,
                           DI_NORMAL);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM:
            if (state && (wParam == ID_ABOUT_OPEN_GITHUB || wParam == ID_ABOUT_OPEN_SUPPORT))
            {
                DrawAboutDialogLinkButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), *state);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_ABOUT_OPEN_GITHUB || LOWORD(wParam) == ID_ABOUT_OPEN_SUPPORT)
            {
                OpenAboutDialogLink(hwnd, LOWORD(wParam));
                return 0;
            }
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void LayoutShortcutReferenceControls(HWND hwnd, const ShortcutReferenceState& state)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientWidth = static_cast<int>(client.right);
        const int clientHeight = static_cast<int>(client.bottom);
        const int margin = ScaleDialogAppTextDimension(kShortcutReferenceMargin, state.appTextSize, state.dpi);
        const int gap = ScaleDialogAppTextDimension(kShortcutReferenceControlGap, state.appTextSize, state.dpi);
        const int subtitleHeight = ScaleDialogAppTextDimension(kShortcutReferenceSubtitleHeight, state.appTextSize, state.dpi);
        const int buttonWidth = ScaleDialogAppTextDimension(kShortcutReferenceButtonWidth, state.appTextSize, state.dpi);
        const int buttonHeight = ScaleDialogAppTextDimension(kShortcutReferenceButtonHeight, state.appTextSize, state.dpi);
        const int listTop = margin + subtitleHeight + gap;
        const int buttonTop = std::max(listTop + 1, clientHeight - margin - buttonHeight);
        const int listBottom = std::max(listTop + 1, buttonTop - gap);

        if (state.subtitleWindow)
        {
            SetWindowPos(state.subtitleWindow, nullptr, margin, margin,
                         std::max(1, clientWidth - margin * 2), subtitleHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.listWindow)
        {
            SetWindowPos(state.listWindow, nullptr, margin, listTop,
                         std::max(1, clientWidth - margin * 2), std::max(1, listBottom - listTop),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.closeButton)
        {
            SetWindowPos(state.closeButton, nullptr, clientWidth - margin - buttonWidth, buttonTop,
                         buttonWidth, buttonHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

    }

    void ReflowShortcutReferenceWindow(HWND hwnd, ShortcutReferenceState& state)
    {
        constexpr DWORD windowStyle =
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
        constexpr DWORD windowExStyle = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
        RECT clientRect{0,
                        0,
                        ScaleDialogAppTextDimension(kShortcutReferenceWidth, state.appTextSize, state.dpi),
                        ScaleDialogAppTextDimension(kShortcutReferenceHeight, state.appTextSize, state.dpi)};
        AdjustDialogWindowRectForDpi(
            &clientRect, windowStyle, FALSE, windowExStyle, state.dpi);

        RECT currentRect{};
        GetWindowRect(hwnd, &currentRect);
        const int width = clientRect.right - clientRect.left;
        const int height = clientRect.bottom - clientRect.top;
        const int centerX = (currentRect.left + currentRect.right) / 2;
        const int centerY = (currentRect.top + currentRect.bottom) / 2;
        RECT desiredRect{
            centerX - width / 2,
            centerY - height / 2,
            centerX - width / 2 + width,
            centerY - height / 2 + height};
        desiredRect = ClampDialogFrameToWorkArea(
            desiredRect, MeasureDialogShellMetrics(hwnd, state.appTextSize).workArea);
        SetWindowPos(hwnd,
                     nullptr,
                     desiredRect.left,
                     desiredRect.top,
                     desiredRect.right - desiredRect.left,
                     desiredRect.bottom - desiredRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutShortcutReferenceControls(hwnd, state);
    }

    void AutoSizeShortcutReferenceColumns(HWND listWindow)
    {
        if (!listWindow)
        {
            return;
        }

        const int columnCount = Header_GetItemCount(ListView_GetHeader(listWindow));
        for (int column = 0; column < columnCount; ++column)
        {
            ListView_SetColumnWidth(listWindow, column, LVSCW_AUTOSIZE_USEHEADER);
        }
    }

    void AddShortcutReferenceRows(HWND listWindow, const ShortcutReferenceState& state)
    {
        if (!listWindow)
        {
            return;
        }

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = ScaleDialogAppTextDimension(170, state.appTextSize, state.dpi);
        column.pszText = const_cast<LPWSTR>(L"Category");
        ListView_InsertColumn(listWindow, 0, &column);
        column.cx = ScaleDialogAppTextDimension(130, state.appTextSize, state.dpi);
        column.pszText = const_cast<LPWSTR>(L"Shortcut");
        ListView_InsertColumn(listWindow, 1, &column);
        column.cx = ScaleDialogAppTextDimension(430, state.appTextSize, state.dpi);
        column.pszText = const_cast<LPWSTR>(L"Action");
        ListView_InsertColumn(listWindow, 2, &column);
        column.cx = ScaleDialogAppTextDimension(110, state.appTextSize, state.dpi);
        column.pszText = const_cast<LPWSTR>(L"Context");
        ListView_InsertColumn(listWindow, 3, &column);

        std::unordered_set<std::wstring> seenRows;
        const auto appendCatalog = [&](std::span<const hyperbrowse::ui::ShortcutDefinition> catalog)
        {
            for (const hyperbrowse::ui::ShortcutDefinition& shortcut : catalog)
            {
                std::wstring rowKey(shortcut.group);
                rowKey.push_back(L'\x1f');
                rowKey.append(shortcut.displayChord);
                rowKey.push_back(L'\x1f');
                rowKey.append(shortcut.action);
                rowKey.push_back(L'\x1f');
                std::wstring_view contextKey;
                std::wstring_view contextLabel;
                switch (shortcut.context)
                {
                case hyperbrowse::ui::ShortcutContext::MainWindow:
                    contextKey = L"main";
                    contextLabel = L"Main window";
                    break;
                case hyperbrowse::ui::ShortcutContext::Browser:
                    contextKey = L"browser";
                    contextLabel = L"Thumbnail browser";
                    break;
                case hyperbrowse::ui::ShortcutContext::Viewer:
                    contextKey = L"viewer";
                    contextLabel = L"Viewer";
                    break;
                }
                rowKey.append(contextKey);
                if (!seenRows.insert(std::move(rowKey)).second)
                {
                    continue;
                }

                const std::wstring category(shortcut.group);
                const std::wstring chord(shortcut.displayChord);
                const std::wstring action(shortcut.action);
                const std::wstring context(contextLabel);
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = ListView_GetItemCount(listWindow);
                item.pszText = const_cast<LPWSTR>(category.c_str());
                const int row = ListView_InsertItem(listWindow, &item);
                if (row < 0)
                {
                    continue;
                }
                ListView_SetItemText(listWindow, row, 1, const_cast<LPWSTR>(chord.c_str()));
                ListView_SetItemText(listWindow, row, 2, const_cast<LPWSTR>(action.c_str()));
                ListView_SetItemText(listWindow, row, 3, const_cast<LPWSTR>(context.c_str()));
            }
        };

        appendCatalog(hyperbrowse::ui::MainWindowShortcuts());
        appendCatalog(hyperbrowse::ui::BrowserShortcuts());
        appendCatalog(hyperbrowse::ui::ViewerShortcuts());
        const auto appendGamepadRow = [listWindow](std::wstring_view controls,
                                                   std::wstring_view action,
                                                   std::wstring_view context)
        {
            const std::wstring category = L"Gamepad controls";
            const std::wstring controlText(controls);
            const std::wstring actionText(action);
            const std::wstring contextText(context);
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = ListView_GetItemCount(listWindow);
            item.pszText = const_cast<LPWSTR>(category.c_str());
            const int row = ListView_InsertItem(listWindow, &item);
            if (row >= 0)
            {
                ListView_SetItemText(listWindow, row, 1, const_cast<LPWSTR>(controlText.c_str()));
                ListView_SetItemText(listWindow, row, 2, const_cast<LPWSTR>(actionText.c_str()));
                ListView_SetItemText(listWindow, row, 3, const_cast<LPWSTR>(contextText.c_str()));
            }
        };
        appendGamepadRow(L"D-pad / left stick", L"Move the selected image", L"Browser");
        appendGamepadRow(L"A", L"Open the selected image", L"Browser");
        appendGamepadRow(L"B", L"Use the Escape key behavior", L"Browser");
        appendGamepadRow(L"X", L"Move selected images to the Recycle Bin", L"Browser");
        appendGamepadRow(L"D-pad / left stick; LB / RB",
                         L"Previous / next image (up/left or LB: previous)",
                         L"Viewer");
        appendGamepadRow(L"LT / RT", L"Zoom out / in", L"Viewer");
        appendGamepadRow(L"Right stick",
                         L"Pan the image faster the longer it is held",
                         L"Viewer");
        appendGamepadRow(L"A", L"Toggle between fit and actual size", L"Viewer");
        appendGamepadRow(L"Y", L"Toggle overlays (Tab)", L"Viewer");
        appendGamepadRow(L"B", L"Use the configured Escape action", L"Viewer");
        appendGamepadRow(L"X", L"Move the displayed image to the Recycle Bin", L"Viewer");
        AutoSizeShortcutReferenceColumns(listWindow);
    }

    LRESULT CALLBACK ShortcutReferenceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ShortcutReferenceState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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
            state = reinterpret_cast<ShortcutReferenceState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            state->subtitleWindow = CreateWindowExW(
                0, L"STATIC", L"Main-window and viewer commands, grouped by task.",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0, 0, 0, 0, hwnd, nullptr,
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);
            state->listWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutReferenceListControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);
            state->closeButton = CreateWindowExW(
                0, L"BUTTON", L"Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);

            const HFONT bodyFont = state->bodyFont ? state->bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            if (state->subtitleWindow)
            {
                SendMessageW(state->subtitleWindow, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
            }
            if (state->listWindow)
            {
                SendMessageW(state->listWindow, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
                ListView_SetExtendedListViewStyle(state->listWindow, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                ListView_SetBkColor(state->listWindow, state->listBackground);
                ListView_SetTextBkColor(state->listWindow, state->listBackground);
                ListView_SetTextColor(state->listWindow, state->text);
                AddShortcutReferenceRows(state->listWindow, *state);
            }
            if (state->closeButton)
            {
                SendMessageW(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
            }

            state->backgroundBrush = CreateSolidBrush(state->background);
            state->listBackgroundBrush = CreateSolidBrush(state->listBackground);
            ApplyWindowFrameTheme(hwnd, state->darkMode, state->background, state->text, state->border);
            LayoutShortcutReferenceControls(hwnd, *state);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutShortcutReferenceControls(hwnd, *state);
            }
            return 0;
        case WM_GETMINMAXINFO:
            if (state)
            {
                auto* sizeInfo = reinterpret_cast<MINMAXINFO*>(lParam);
                if (sizeInfo)
                {
                    const int dpi = static_cast<int>(state->dpi == 0 ? 96 : state->dpi);
                    sizeInfo->ptMinTrackSize.x = MulDiv(kShortcutReferenceMinimumWidth, dpi, 96);
                    sizeInfo->ptMinTrackSize.y = MulDiv(kShortcutReferenceMinimumHeight, dpi, 96);
                    return 0;
                }
            }
            break;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = HIWORD(wParam) == 0 ? 96 : HIWORD(wParam);
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested)
                {
                    const RECT adjusted = ClampDialogFrameToWorkArea(
                        *suggested,
                        MeasureDialogShellMetrics(hwnd, state->appTextSize).workArea);
                    SetWindowPos(hwnd, nullptr,
                                 adjusted.left, adjusted.top,
                                 adjusted.right - adjusted.left,
                                 adjusted.bottom - adjusted.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                DeleteFontIfOwned(state->bodyFont);
                state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
                const HFONT bodyFont = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                for (HWND control : {state->subtitleWindow, state->listWindow, state->closeButton})
                {
                    if (control)
                    {
                        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
                    }
                }
                AutoSizeShortcutReferenceColumns(state->listWindow);
                LayoutShortcutReferenceControls(hwnd, *state);
            }
            return 0;
        case kAppTextSizeChangedMessage:
            if (state)
            {
                state->appTextSize = hyperbrowse::util::NormalizeAppTextSize(
                    static_cast<std::uint32_t>(wParam));
                state->dpi = DialogDpiForWindow(hwnd);
                DeleteFontIfOwned(state->bodyFont);
                state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
                const HFONT bodyFont = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                for (HWND control : {state->subtitleWindow, state->listWindow, state->closeButton})
                {
                    if (control)
                    {
                        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
                    }
                }
                AutoSizeShortcutReferenceColumns(state->listWindow);
                ReflowShortcutReferenceWindow(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->text);
                SetBkColor(dc, state->background);
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (state)
            {
                PAINTSTRUCT paint{};
                const HDC dc = BeginPaint(hwnd, &paint);
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(dc, &client, state->backgroundBrush);
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                if (state->windowSlot && *state->windowSlot == hwnd)
                {
                    *state->windowSlot = nullptr;
                }
                if (state->backgroundBrush)
                {
                    DeleteObject(state->backgroundBrush);
                    state->backgroundBrush = nullptr;
                }
                if (state->listBackgroundBrush)
                {
                    DeleteObject(state->listBackgroundBrush);
                    state->listBackgroundBrush = nullptr;
                }
                DeleteFontIfOwned(state->bodyFont);
                state->bodyFont = nullptr;
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    std::wstring FindUserGuidePath()
    {
        std::vector<wchar_t> modulePath(512);
        DWORD length = 0;
        while (true)
        {
            length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < modulePath.size() - 1)
            {
                break;
            }
            if (modulePath.size() >= 32768)
            {
                return {};
            }
            modulePath.resize(modulePath.size() * 2);
        }

        const fs::path moduleDirectory = fs::path(std::wstring(modulePath.data(), length)).parent_path();
        const fs::path candidates[] = {
            moduleDirectory / L"docs" / L"user-guide.html",
            moduleDirectory.parent_path() / L"docs" / L"user-guide.html",
        };

        for (const fs::path& candidate : candidates)
        {
            std::error_code error;
            if (fs::is_regular_file(candidate, error))
            {
                return candidate.lexically_normal().wstring();
            }
        }

        return {};
    }

    std::wstring ToLowercaseCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return value;
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::ShowDiagnosticsSnapshot()
    {
        if (!diagnosticsWindow_)
        {
            return;
        }

        util::RefreshWicCodecReadiness();
        diagnosticsWindow_->Show(
            hwnd_,
            decode::DescribeJpegAccelerationState(),
            decode::DescribeRawDecodingState(),
            browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"),
            util::CaptureDiagnosticsSnapshot(),
            themeMode_ == ThemeMode::Dark);
        util::LogInfo(L"Opened diagnostics snapshot window.");
    }

    void MainWindow::ExportRedactedDiagnosticsSnapshot()
    {
        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
        const HRESULT createResult = CoCreateInstance(CLSID_FileSaveDialog,
                                                       nullptr,
                                                       CLSCTX_INPROC_SERVER,
                                                       IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(createResult) || !dialog)
        {
            MessageBoxW(hwnd_, L"Windows could not open the diagnostics export dialog.", L"Diagnostics", MB_OK | MB_ICONERROR);
            return;
        }

        constexpr COMDLG_FILTERSPEC filters[] = {
            {L"JSON diagnostics", L"*.json"},
            {L"All files", L"*.*"},
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetDefaultExtension(L"json");
        dialog->SetFileName(L"HyperBrowse-diagnostics-redacted.json");
        dialog->SetTitle(L"Export Redacted Diagnostics Snapshot");
        if (FAILED(dialog->Show(hwnd_)))
        {
            return;
        }

        Microsoft::WRL::ComPtr<IShellItem> shellItem;
        if (FAILED(dialog->GetResult(&shellItem)) || !shellItem)
        {
            return;
        }

        PWSTR rawPath = nullptr;
        if (FAILED(shellItem->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath)
        {
            MessageBoxW(hwnd_, L"Windows did not return a writable diagnostics export path.", L"Diagnostics", MB_OK | MB_ICONERROR);
            return;
        }

        const std::wstring outputPath(rawPath);
        CoTaskMemFree(rawPath);
        if (!util::WriteRedactedDiagnosticsSnapshot(outputPath))
        {
            MessageBoxW(hwnd_, L"HyperBrowse could not write the diagnostics snapshot.", L"Diagnostics", MB_OK | MB_ICONERROR);
            return;
        }

        util::LogInfo(L"Wrote redacted diagnostics snapshot.");
        MessageBoxW(hwnd_, L"The redacted diagnostics snapshot was exported.", L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ShowUserGuide() const
    {
        const std::wstring guidePath = FindUserGuidePath();
        if (guidePath.empty())
        {
            MessageBoxW(hwnd_,
                        L"The local user guide could not be found. Make sure the application's docs folder is present.",
                        L"User Guide",
                        MB_OK | MB_ICONERROR);
            return;
        }

        if (!LaunchShellTarget(hwnd_, L"open", guidePath))
        {
            MessageBoxW(hwnd_,
                        L"Windows could not open the local user guide in the default browser.",
                        L"User Guide",
                        MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::ShowShortcutReference() const
    {
        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_))
        {
            ShowWindow(shortcutReferenceWindow_, SW_RESTORE);
            SetForegroundWindow(shortcutReferenceWindow_);
            return;
        }
        shortcutReferenceWindow_ = nullptr;

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kShortcutReferenceClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &ShortcutReferenceProc;
            windowClass.hInstance = instance_;
            windowClass.lpszClassName = kShortcutReferenceClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return;
            }
        }

        const ThemePalette palette = GetThemePalette();
        auto state = std::make_unique<ShortcutReferenceState>();
        state->ownerWindow = hwnd_;
        state->windowSlot = &shortcutReferenceWindow_;
        state->appTextSize = appTextSize_;
        state->darkMode = themeMode_ == ThemeMode::Dark;
        state->dpi = DialogDpiForWindow(hwnd_);
        state->background = palette.windowBackground;
        state->listBackground = palette.paneBackground;
        state->text = palette.text;
        state->mutedText = palette.mutedText;
        state->border = palette.actionStripBorder;
        state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
        if (!state->bodyFont)
        {
            state->bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        constexpr DWORD shortcutReferenceWindowStyle =
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
        constexpr DWORD shortcutReferenceWindowExStyle = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
        RECT windowRect{0, 0,
            ScaleDialogAppTextDimension(kShortcutReferenceWidth, state->appTextSize, state->dpi),
            ScaleDialogAppTextDimension(kShortcutReferenceHeight, state->appTextSize, state->dpi)};
        AdjustDialogWindowRectForDpi(&windowRect,
                         shortcutReferenceWindowStyle,
                         FALSE,
                         shortcutReferenceWindowExStyle,
                         state->dpi);
        windowRect = ClampDialogFrameToWorkArea(
            windowRect,
            MeasureDialogShellMetrics(hwnd_, state->appTextSize).workArea);

        HWND dialogWindow = CreateWindowExW(
            shortcutReferenceWindowExStyle,
            kShortcutReferenceClassName,
            L"Keyboard Shortcuts - HyperBrowse",
            shortcutReferenceWindowStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            hwnd_,
            nullptr,
            instance_,
            state.get());

        if (!dialogWindow)
        {
            DeleteFontIfOwned(state->bodyFont);
            return;
        }

        shortcutReferenceWindow_ = dialogWindow;
        state.release();
        SetWindowTextW(dialogWindow, L"Keyboard Shortcuts - HyperBrowse");
        CenterWindowOnOwner(dialogWindow, hwnd_);
        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);
    }

    void MainWindow::ShowAboutDialog() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kAboutDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &AboutDialogProc;
            windowClass.hInstance = instance_;
            windowClass.lpszClassName = kAboutDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return;
            }
        }

        const ThemePalette palette = GetThemePalette();
        const util::StartupDiagnosticsSnapshot startupDiagnostics = util::CaptureStartupDiagnosticsSnapshot();
        const auto formatStartupMilliseconds = [](const std::optional<double>& milliseconds)
        {
            if (!milliseconds)
            {
            return std::wstring(L"Not available");
            }
            return std::to_wstring(static_cast<std::uint64_t>(std::max(0.0, *milliseconds) + 0.5)) + L" ms";
        };
    #if defined(NDEBUG)
        constexpr const wchar_t* kBuildConfiguration = L"Release";
    #else
        constexpr const wchar_t* kBuildConfiguration = L"Debug";
    #endif

        AboutDialogState state;
        state.ownerWindow = hwnd_;
        state.instance = instance_;
        state.appTextSize = appTextSize_;
        state.dpi = hwnd_ ? std::max<UINT>(96, GetDpiForWindow(hwnd_)) : 96;
        state.darkMode = themeMode_ == ThemeMode::Dark;
        state.background = palette.windowBackground;
        state.headerBackground = BlendColor(palette.actionStripBackground, palette.accent, state.darkMode ? 28 : 18);
        state.footerBackground = BlendColor(palette.paneBackground, palette.windowBackground, state.darkMode ? 38 : 58);
        state.panelBackground = palette.paneBackground;
        state.border = palette.actionStripBorder;
        state.text = palette.text;
        state.mutedText = palette.mutedText;
        state.accent = palette.accent;
        state.title = hyperbrowse::build::kDisplayName;
        state.subtitle = L"High-performance native image browser for Windows";
        state.intro = L"Started in "
            + formatStartupMilliseconds(startupDiagnostics.processToFirstWindowVisibleMs)
            + L" / first thumbnail in "
            + formatStartupMilliseconds(startupDiagnostics.firstWindowVisibleToFirstThumbnailPaintedMs)
            + L" after window visibility.";
        state.version = hyperbrowse::build::kVersion;
        state.buildConfiguration = kBuildConfiguration;
        state.gpuVendor = L"Not available";
        state.bodyHeading = L"Build details and highlights";
        state.bodyContent =
            L"- Native Win32 shell tuned for fast startup, compact chrome, and direct file-system browsing.\r\n"
            L"- Async folder enumeration, incremental folder watching, and responsive refresh in large image collections.\r\n"
            L"- Virtualized thumbnail and details views with filtering, metadata-aware sorting, and efficient multi-selection workflows.\r\n"
            L"- Broad format coverage across JPEG, PNG, GIF, TIFF, and major RAW camera formats with graceful fallback behavior.\r\n"
            L"- Dedicated viewer window with slideshow playback, transition effects, overlays, full-screen viewing, and background prefetch.\r\n"
            L"- Practical photographer workflows including batch convert, lossless JPEG orientation adjustment, and Explorer-friendly file operations.\r\n"
            L"- Optional NVIDIA-accelerated JPEG decoding when available, while preserving correctness on systems without GPU acceleration.";
        state.footer =
            L"Copyright (c) "
            + std::to_wstring(CurrentCalendarYear())
            + L" Michael A. McCloskey\r\nLicensed under the MIT License.";
        state.brandArt = util::LoadPngResourceBitmap(instance_, IDB_HYPERBROWSE_BRAND_PNG, kAboutDialogBrandArtSize, kAboutDialogBrandArtSize);
        state.heroIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON, kAboutDialogIconSize, kAboutDialogIconSize, LR_DEFAULTCOLOR));
        state.windowIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        RebuildAboutDialogFonts(state);

        state.githubButtonWidth = MeasureAboutDialogLinkButtonWidth(state.subtitleFont,
                                         kAboutDialogGitHubLabel,
                                         kAboutDialogSupportLabel,
                                         state);
        state.supportButtonWidth = state.githubButtonWidth;

        const int aboutClientWidth = MeasureAboutDialogClientWidth(state);
        const int aboutClientHeight = std::max(ScaleAboutDialogDimension(kAboutDialogHeight, state),
                            MeasureAboutDialogClientHeight(state, aboutClientWidth));
        RECT windowRect{0, 0, aboutClientWidth, aboutClientHeight};
        AdjustDialogWindowRectForDpi(&windowRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);
        windowRect = ClampDialogFrameToWorkArea(
            windowRect,
            MeasureDialogShellMetrics(hwnd_, state.appTextSize).workArea);

        if (hwnd_)
        {
            EnableWindow(hwnd_, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kAboutDialogClassName,
            L"About HyperBrowse",
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            hwnd_,
            nullptr,
            instance_,
            &state);

        if (!dialogWindow)
        {
            if (hwnd_)
            {
                EnableWindow(hwnd_, TRUE);
            }
            DeleteFontIfOwned(state.titleFont);
            DeleteFontIfOwned(state.subtitleFont);
            DeleteFontIfOwned(state.bodyFont);
            DeleteFontIfOwned(state.footerFont);
            if (state.heroIcon) DestroyIcon(state.heroIcon);
            if (state.windowIcon) DestroyIcon(state.windowIcon);
            return;
        }

        const std::wstring displayDeviceName = DisplayDeviceNameForWindow(hwnd_);
        if (memoryPressureExecutor_ && !displayDeviceName.empty())
        {
            memoryPressureExecutor_->Post([dialogWindow, displayDeviceName]
            {
                const AboutAdapterVendor vendor = QueryAboutAdapterVendor(displayDeviceName);
                PostMessageW(dialogWindow,
                             kAboutAdapterVendorMessage,
                             static_cast<WPARAM>(vendor),
                             0);
            });
        }

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (hwnd_)
        {
            EnableWindow(hwnd_, TRUE);
            SetForegroundWindow(hwnd_);
            SetActiveWindow(hwnd_);
        }

        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.subtitleFont);
        DeleteFontIfOwned(state.bodyFont);
        DeleteFontIfOwned(state.footerFont);
        if (state.heroIcon) DestroyIcon(state.heroIcon);
        if (state.windowIcon) DestroyIcon(state.windowIcon);
    }

    void MainWindow::ResetDiagnosticsState()
    {
        util::ResetDiagnostics();
        util::LogInfo(L"Diagnostics timings and counters were reset.");

        if (diagnosticsWindow_ && diagnosticsWindow_->IsOpen())
        {
            diagnosticsWindow_->Show(
                hwnd_,
                decode::DescribeJpegAccelerationState(),
                decode::DescribeRawDecodingState(),
                browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"),
                util::CaptureDiagnosticsSnapshot(),
                themeMode_ == ThemeMode::Dark);
        }

        MessageBoxW(hwnd_, L"Diagnostics timings and counters were reset.", L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    }

    constexpr wchar_t kImageInformationDialogClassName[] = L"HyperBrowseImageInformationDialog";
    constexpr int kImageInformationDialogWidth = 640;
    constexpr int kImageInformationDialogMargin = 18;
    constexpr int kImageInformationDialogTitlePointSize = 16;
    constexpr int kImageInformationDialogBodyPointSize = 10;
    constexpr int kImageInformationDialogEditVerticalPadding = 10;
    constexpr int kImageInformationDialogButtonHeight = 30;
    constexpr int kImageInformationDialogButtonWidth = 96;
    constexpr int kImageInformationDialogCopyPromptWidth = 120;
    constexpr int kImageInformationDialogToggleWidth = 230;
    constexpr int kImageInformationDialogToggleId = 5703;
    constexpr int kImageInformationDialogCopyPromptId = 5704;
    constexpr int kImageInformationDialogGap = 10;

    RECT ImageInformationDialogWorkArea(HWND ownerWindow)
    {
        return MeasureDialogShellMetrics(ownerWindow, hyperbrowse::util::kDefaultAppTextSize).workArea;
    }

    void CenterImageInformationDialogOnWorkArea(HWND hwnd, const ImageInformationDialogState& state)
    {
        CenterDialogInWorkArea(hwnd, state.workArea);
    }

    void MeasureImageInformationDialogContent(ImageInformationDialogState& state)
    {
        const auto scale = [&state](int value)
        {
            return ScaleDialogAppTextDimension(value, state.appTextSize, state.dpi);
        };
        const int contentWidth = scale(kImageInformationDialogWidth - kImageInformationDialogMargin * 2);
        const int editVerticalPadding = scale(kImageInformationDialogEditVerticalPadding);
        const int contentTextHeight = MeasureTextBlockHeight(state.bodyFont,
                                                             state.content,
                                                             contentWidth,
                                                             DT_WORDBREAK | DT_EDITCONTROL,
                                                             0);
        const int metadataTextHeight = MeasureTextBlockHeight(state.bodyFont,
                                                              state.metadata,
                                                              contentWidth,
                                                              DT_WORDBREAK | DT_EDITCONTROL,
                                                              0);
        state.contentHeight = std::max(scale(40), contentTextHeight + editVerticalPadding);
        state.metadataHeight = std::max(scale(56), metadataTextHeight + editVerticalPadding);
        state.expandedWindowHeight = scale(kImageInformationDialogMargin + 34)
            + state.contentHeight
            + scale(kImageInformationDialogGap)
            + state.metadataHeight
            + scale(kImageInformationDialogGap + kImageInformationDialogButtonHeight
                   + kImageInformationDialogMargin);
    }

    void ApplyImageInformationEditMargins(const ImageInformationDialogState& state)
    {
        const int editMargin = ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi);
        for (const HWND editWindow : {state.contentWindow, state.metadataWindow})
        {
            if (editWindow)
            {
                SendMessageW(editWindow,
                             EM_SETMARGINS,
                             EC_LEFTMARGIN | EC_RIGHTMARGIN,
                             MAKELPARAM(editMargin, editMargin));
            }
        }
    }

    void LayoutImageInformationDialog(HWND hwnd, ImageInformationDialogState& state)
    {
        const auto scale = [&state](int value)
        {
            return ScaleDialogAppTextDimension(value, state.appTextSize, state.dpi);
        };
        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientWidth = client.right - client.left;
        const int clientHeight = client.bottom - client.top;
        const int contentLeft = scale(kImageInformationDialogMargin);
        const int contentWidth = clientWidth - scale(kImageInformationDialogMargin * 2);
        const int buttonsTop = clientHeight
            - scale(kImageInformationDialogMargin)
            - scale(kImageInformationDialogButtonHeight);
        const int contentTop = scale(kImageInformationDialogMargin + 34);
        const int availableReportHeight = std::max(1, buttonsTop - scale(kImageInformationDialogGap) - contentTop);
        const int metadataHeight = state.expanded
            ? std::min(state.metadataHeight, std::max(1, availableReportHeight - scale(40)))
            : 0;
        const int metadataTop = buttonsTop - scale(kImageInformationDialogGap) - metadataHeight;
        const int contentBottom = state.expanded
            ? metadataTop - scale(kImageInformationDialogGap)
            : buttonsTop - scale(kImageInformationDialogGap);
        const int contentHeight = std::min(state.contentHeight, std::max(1, contentBottom - contentTop));

        if (state.filenameWindow)
        {
            SetWindowPos(state.filenameWindow,
                         nullptr,
                         contentLeft,
                         scale(kImageInformationDialogMargin),
                         contentWidth,
                         scale(28),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.contentWindow)
        {
            SetWindowPos(state.contentWindow,
                         nullptr,
                         contentLeft,
                         contentTop,
                         contentWidth,
                         contentHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowScrollBar(state.contentWindow, SB_VERT, state.contentHeight > contentHeight);
        }
        if (state.metadataWindow)
        {
            SetWindowPos(state.metadataWindow,
                         nullptr,
                         contentLeft,
                         metadataTop,
                         contentWidth,
                         metadataHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowScrollBar(state.metadataWindow,
                          SB_VERT,
                          state.expanded && state.metadataHeight > metadataHeight);
        }
        if (state.metadataToggleButton)
        {
            SetWindowPos(state.metadataToggleButton,
                         nullptr,
                         contentLeft,
                         buttonsTop,
                         scale(kImageInformationDialogToggleWidth),
                         scale(kImageInformationDialogButtonHeight),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.copyPromptButton)
        {
            SetWindowPos(state.copyPromptButton,
                         nullptr,
                         clientWidth - scale(kImageInformationDialogMargin
                             + kImageInformationDialogButtonWidth
                             + kImageInformationDialogGap
                             + kImageInformationDialogCopyPromptWidth),
                         buttonsTop,
                         scale(kImageInformationDialogCopyPromptWidth),
                         scale(kImageInformationDialogButtonHeight),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.okButton)
        {
            SetWindowPos(state.okButton,
                         nullptr,
                         clientWidth - scale(kImageInformationDialogMargin + kImageInformationDialogButtonWidth),
                         buttonsTop,
                         scale(kImageInformationDialogButtonWidth),
                         scale(kImageInformationDialogButtonHeight),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void ResizeImageInformationDialog(HWND hwnd, ImageInformationDialogState& state)
    {
        const auto scale = [&state](int value)
        {
            return ScaleDialogAppTextDimension(value, state.appTextSize, state.dpi);
        };
        const int collapsedWindowHeight = scale(kImageInformationDialogMargin + 34)
            + state.contentHeight
            + scale(kImageInformationDialogGap + kImageInformationDialogButtonHeight
                   + kImageInformationDialogMargin);
        const int clientHeight = std::min(state.expanded
                                              ? state.expandedWindowHeight
                                              : collapsedWindowHeight,
                                          state.maximumWindowHeight);
        RECT windowRect{0,
                        0,
                        scale(kImageInformationDialogWidth),
                        clientHeight};
        AdjustDialogWindowRectForDpi(&windowRect,
                                     WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                     FALSE,
                                     WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                                     state.dpi);
        SetWindowPos(hwnd,
                     nullptr,
                     0,
                     0,
                     windowRect.right - windowRect.left,
                     windowRect.bottom - windowRect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        CenterImageInformationDialogOnWorkArea(hwnd, state);
        LayoutImageInformationDialog(hwnd, state);
    }

    LRESULT CALLBACK ImageInformationDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ImageInformationDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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
            state = reinterpret_cast<ImageInformationDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            state->backgroundBrush = CreateSolidBrush(state->theme.windowBackground);
            state->fieldBrush = CreateSolidBrush(state->theme.fieldBackground);
            state->filenameWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->filename.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
                0,
                0,
                100,
                28,
                hwnd,
                nullptr,
                state->instance,
                nullptr);
            state->contentWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->content.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL,
                0,
                0,
                100,
                100,
                hwnd,
                nullptr,
                state->instance,
                nullptr);
            state->metadataWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->metadata.c_str(),
                WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL,
                0,
                0,
                100,
                100,
                hwnd,
                nullptr,
                state->instance,
                nullptr);
            state->metadataToggleButton = CreateWindowExW(
                0,
                L"BUTTON",
                state->expanded ? L"Collapse Metadata Details" : L"Show Metadata Details",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kImageInformationDialogToggleWidth,
                kImageInformationDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kImageInformationDialogToggleId)),
                state->instance,
                nullptr);
            state->copyPromptButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Copy Prompt",
                WS_CHILD | WS_TABSTOP,
                0,
                0,
                kImageInformationDialogCopyPromptWidth,
                kImageInformationDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kImageInformationDialogCopyPromptId)),
                state->instance,
                nullptr);
            if (state->copyPromptButton && state->prompt.empty())
            {
                ShowWindow(state->copyPromptButton, SW_HIDE);
            }
            else if (state->copyPromptButton)
            {
                ShowWindow(state->copyPromptButton, SW_SHOW);
            }
            ShowWindow(state->metadataWindow, state->expanded ? SW_SHOW : SW_HIDE);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kImageInformationDialogButtonWidth,
                kImageInformationDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                state->instance,
                nullptr);

            const HWND windows[] = {
                state->filenameWindow,
                state->contentWindow,
                state->metadataWindow,
                state->metadataToggleButton,
                state->copyPromptButton,
                state->okButton,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
                }
            }
            if (state->filenameWindow && state->titleFont)
            {
                SendMessageW(state->filenameWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
            }
            for (HWND editWindow : {state->contentWindow, state->metadataWindow})
            {
                if (editWindow)
                {
                    SendMessageW(editWindow, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(state->theme.fieldBackground));
                }
            }
            ApplyImageInformationEditMargins(*state);

            LayoutImageInformationDialog(hwnd, *state);
            CenterImageInformationDialogOnWorkArea(hwnd, *state);
            SetFocus(state->okButton);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutImageInformationDialog(hwnd, *state);
            }
            return 0;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = std::max<UINT>(96, HIWORD(wParam));
                state->workArea = ImageInformationDialogWorkArea(hwnd);
                DeleteFontIfOwned(state->titleFont);
                DeleteFontIfOwned(state->bodyFont);
                state->titleFont = CreateDialogUiFont(kImageInformationDialogTitlePointSize,
                                                      FW_BOLD,
                                                      state->appTextSize,
                                                      state->dpi);
                state->bodyFont = CreateDialogUiFont(kImageInformationDialogBodyPointSize,
                                                     FW_NORMAL,
                                                     state->appTextSize,
                                                     state->dpi);
                MeasureImageInformationDialogContent(*state);
                const RECT frameRect{0, 0, 0, 0};
                RECT adjustedFrame = frameRect;
                AdjustDialogWindowRectForDpi(&adjustedFrame,
                                             WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                             FALSE,
                                             WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                                             state->dpi);
                const int frameHeight = adjustedFrame.bottom - adjustedFrame.top;
                state->maximumWindowHeight = static_cast<int>(std::max<LONG>(
                    1L,
                    state->workArea.bottom - state->workArea.top - frameHeight));
                state->expandedWindowHeight = std::min(state->expandedWindowHeight, state->maximumWindowHeight);
                const HFONT font = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                EnumChildWindows(hwnd,
                                 [](HWND child, LPARAM parameter) -> BOOL
                                 {
                                     SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), TRUE);
                                     return TRUE;
                                 },
                                 reinterpret_cast<LPARAM>(font));
                if (state->filenameWindow && state->titleFont)
                {
                    SendMessageW(state->filenameWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
                }
                ApplyImageInformationEditMargins(*state);
                ResizeImageInformationDialog(hwnd, *state);
            }
            return 0;
        case WM_CTLCOLORDLG:
            return state && state->backgroundBrush
                ? reinterpret_cast<INT_PTR>(state->backgroundBrush)
                : 0;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLOREDIT:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.fieldBackground);
                return reinterpret_cast<INT_PTR>(state->fieldBrush);
            }
            break;
        case WM_CTLCOLORBTN:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush)
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }
            if (LOWORD(wParam) == kImageInformationDialogToggleId && HIWORD(wParam) == BN_CLICKED)
            {
                state->expanded = !state->expanded;
                SetWindowTextW(state->metadataToggleButton,
                               state->expanded ? L"Collapse Metadata Details" : L"Show Metadata Details");
                ShowWindow(state->metadataWindow, state->expanded ? SW_SHOW : SW_HIDE);
                ResizeImageInformationDialog(hwnd, *state);
                return 0;
            }
            if (LOWORD(wParam) == kImageInformationDialogCopyPromptId && HIWORD(wParam) == BN_CLICKED)
            {
                if (!CopyTextToClipboard(hwnd, state->prompt))
                {
                    MessageBoxW(hwnd,
                                L"Failed to copy the image prompt to the clipboard.",
                                L"Copy Prompt",
                                MB_OK | MB_ICONERROR);
                }
                return 0;
            }
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                if (state->backgroundBrush)
                {
                    DeleteObject(state->backgroundBrush);
                    state->backgroundBrush = nullptr;
                }
                if (state->fieldBrush)
                {
                    DeleteObject(state->fieldBrush);
                    state->fieldBrush = nullptr;
                }
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void ShowImageInformationDialog(HWND ownerWindow,
                                    HINSTANCE instance,
                                    bool darkTheme,
                                    hyperbrowse::util::AppTextSize appTextSize,
                                    std::wstring filename,
                                    std::wstring content,
                                    std::wstring metadata,
                                    std::wstring prompt)
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kImageInformationDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &ImageInformationDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kImageInformationDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
            windowClass.hIconSm = windowClass.hIcon;
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return;
            }
        }

        ImageInformationDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.instance = instance;
        state.theme = hyperbrowse::ui::MakeDialogTheme(darkTheme);
        state.workArea = ImageInformationDialogWorkArea(ownerWindow);
        RECT frameRect{0, 0, 0, 0};
        AdjustDialogWindowRectForDpi(&frameRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);
        const LONG frameHeight = frameRect.bottom - frameRect.top;
        state.maximumWindowHeight = static_cast<int>(std::max<LONG>(1L,
                                                                     state.workArea.bottom
                                                                         - state.workArea.top
                                                                         - frameHeight));
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.titleFont = CreateDialogUiFont(kImageInformationDialogTitlePointSize,
                                             FW_BOLD,
                                             state.appTextSize,
                                             state.dpi);
        state.bodyFont = CreateDialogUiFont(kImageInformationDialogBodyPointSize,
                                            FW_NORMAL,
                                            state.appTextSize,
                                            state.dpi);
        state.filename = std::move(filename);
        state.content = std::move(content);
        state.metadata = std::move(metadata);
        state.prompt = std::move(prompt);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        MeasureImageInformationDialogContent(state);
        state.expandedWindowHeight = std::min(state.expandedWindowHeight, state.maximumWindowHeight);

        RECT windowRect{
            0,
            0,
            ScaleDialogAppTextDimension(kImageInformationDialogWidth, state.appTextSize, state.dpi),
            state.expandedWindowHeight};
        AdjustDialogWindowRectForDpi(&windowRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);
        windowRect = ClampDialogFrameToWorkArea(
            windowRect,
            MeasureDialogShellMetrics(ownerWindow, state.appTextSize).workArea);
        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }
        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kImageInformationDialogClassName,
            L"Image Information",
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);
        if (dialogWindow)
        {
            SetWindowTextW(dialogWindow, L"Image Information");
            ApplyWindowFrameTheme(dialogWindow,
                                  darkTheme,
                                  state.theme.windowBackground,
                                  state.theme.text,
                                  state.theme.border);
            RefreshWindowNonClientArea(dialogWindow);
            ShowWindow(dialogWindow, SW_SHOWNORMAL);
            UpdateWindow(dialogWindow);
            MSG message{};
            while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                if (!IsDialogMessageW(dialogWindow, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }
        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }
        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.bodyFont);
    }

    void MainWindow::ShowImageInformation(HWND ownerWindow)
    {
        if (!browserPaneController_ || !browserModel_ || !imageCommandService_)
        {
            return;
        }

        const HWND dialogOwner = ownerWindow && IsWindow(ownerWindow) ? ownerWindow : hwnd_;

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        if (modelIndex < 0)
        {
            MessageBoxW(dialogOwner, L"Select an image first.", L"Image Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& items = browserModel_->Items();
        if (modelIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        const browser::BrowserItem item = items[static_cast<std::size_t>(modelIndex)];
        if (const auto metadata = browserPaneController_->FindCachedMetadataForModelIndex(modelIndex))
        {
            PresentImageInformation(item, metadata, dialogOwner);
            return;
        }

        imageCommandActive_ = true;
        imageCommandCancelling_ = false;
        imageCommandActivityLabel_ = L"Reading image information";
        activeImageCommandRequestId_ = imageCommandService_->StartInformation(hwnd_, dialogOwner, item);
        UpdateStatusText();
    }

    void MainWindow::PresentImageInformation(
        const browser::BrowserItem& item,
        std::shared_ptr<const services::ImageMetadata> metadata,
        HWND ownerWindow)
    {
        const HWND dialogOwner = ownerWindow && IsWindow(ownerWindow) ? ownerWindow : hwnd_;
        if (!metadata)
        {
            MessageBoxW(dialogOwner,
                        L"No metadata is available for the selected image.",
                        L"Image Information",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }
        const std::wstring content = services::FormatImageInfoContent(item);
        const std::wstring expanded = services::FormatImageInfoExpanded(*metadata);
        const std::wstring metadataContent = expanded.empty()
            ? L"No embedded EXIF, IPTC, XMP, or other metadata is available."
            : expanded;
        const std::wstring prompt = services::ExtractImagePrompt(*metadata);
        const int currentModelIndex = browserModel_ ? browserModel_->FindItemIndexByPath(item.filePath) : -1;
        std::wstring diagnostic = browserPaneController_ && currentModelIndex >= 0
            ? browserPaneController_->ThumbnailDecodeFailureMessageForModelIndex(currentModelIndex)
            : std::wstring{};
        if (!diagnostic.empty())
        {
            diagnostic.insert(0, L"Decode diagnostic:\r\n");
        }
        const std::wstring contentWithDiagnostic = diagnostic.empty()
            ? content
            : content + L"\r\n\r\n" + diagnostic;

        ShowImageInformationDialog(dialogOwner,
                       instance_,
                       themeMode_ == ThemeMode::Dark,
                       appTextSize_,
                       item.fileName,
                       contentWithDiagnostic,
                       metadataContent,
                       prompt);
    }
}
