#include "ui/CommandBarPainter.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <string_view>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"
#include "ui/ToolbarIconLibrary.h"

namespace hyperbrowse::ui
{
    namespace
    {
        COLORREF BlendColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
        {
            const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
            return RGB(
                (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
                (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
                (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
        }

        void AlphaBlendBitmap(HDC targetDC, HDC scratchDC, HBITMAP bitmap, int x, int y, int width, int height)
        {
            if (!targetDC || !scratchDC || !bitmap || width <= 0 || height <= 0)
            {
                return;
            }

            const HGDIOBJ oldBitmap = SelectObject(scratchDC, bitmap);
            BLENDFUNCTION blend{};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;
            AlphaBlend(targetDC, x, y, width, height, scratchDC, 0, 0, width, height, blend);
            SelectObject(scratchDC, oldBitmap);
        }
    }

    void CommandBarPainter::PaintD2D(
        ID2D1RenderTarget* renderTarget,
        const RECT& stripRect,
        const std::array<CommandBarController::CommandBarMenuButton, 5>& menuButtons,
        const std::vector<CommandBarController::ToolbarItem>& toolbarItems,
        const CommandBarPalette& palette,
        const MenuMetrics& metrics,
        IDWriteTextFormat* textFormat,
        ToolbarIconLibrary* iconLibrary,
        const CommandBarPaintState& state) const
    {
        if (!renderTarget || !textFormat)
        {
            return;
        }

        const int menuButtonPadding = metrics.ScaleDip(metrics.commandBarMenuButtonPaddingDip);
        const int menuChevronWidth = metrics.ScaleDip(metrics.commandBarMenuChevronWidthDip);
        const int toolbarIconSize = metrics.ScaleDip(metrics.commandBarToolbarIconSizeDip);
        const int dropdownChevronSize = metrics.ScaleDip(metrics.commandBarDropdownChevronSizeDip);
        const int buttonRadius = metrics.ScaleDip(metrics.commandBarButtonRadiusDip);
        const int focusRingRadius = metrics.ScaleDip(metrics.commandBarFocusRingRadiusDip);

        auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
        const auto createBrush = [renderTarget](COLORREF color)
        {
            hyperbrowse::render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(
                hyperbrowse::render::ToD2DColor(color),
                brush.GetAddressOf());
            return brush;
        };
        const auto stripBrush = createBrush(palette.actionStripBackground);
        const auto borderBrush = createBrush(palette.actionStripBorder);

        if (stripBrush)
        {
            renderTarget->FillRectangle(
                hyperbrowse::render::ToD2DRect(stripRect),
                stripBrush.Get());
        }
        if (borderBrush)
        {
            renderTarget->DrawLine(
                hyperbrowse::render::ToD2DPoint(static_cast<float>(stripRect.left), static_cast<float>(stripRect.bottom - 0.5f)),
                hyperbrowse::render::ToD2DPoint(static_cast<float>(stripRect.right), static_cast<float>(stripRect.bottom - 0.5f)),
                borderBrush.Get());
        }

        const auto drawText = [renderTarget, textFormat](std::wstring_view text,
                                                           const RECT& rect,
                                                           COLORREF color)
        {
            if (text.empty())
            {
                return;
            }

            hyperbrowse::render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(
                hyperbrowse::render::ToD2DColor(color),
                brush.GetAddressOf());
            if (brush)
            {
                renderTarget->DrawText(
                    text.data(),
                    static_cast<UINT32>(text.size()),
                    textFormat,
                    hyperbrowse::render::ToD2DRect(rect),
                    brush.Get());
            }
        };

        const auto drawRoundedButton = [&](const RECT& sourceRect, COLORREF fillColor, COLORREF borderColor)
        {
            RECT buttonRect = sourceRect;
            InflateRect(&buttonRect, -1, -1);
            const auto fillBrush = createBrush(fillColor);
            const auto buttonBorderBrush = createBrush(borderColor);
            const D2D1_ROUNDED_RECT roundedRect = hyperbrowse::render::ToD2DRoundedRect(
                buttonRect, static_cast<float>(buttonRadius), static_cast<float>(buttonRadius));
            if (fillBrush)
            {
                renderTarget->FillRoundedRectangle(&roundedRect, fillBrush.Get());
            }
            if (buttonBorderBrush)
            {
                renderTarget->DrawRoundedRectangle(&roundedRect, buttonBorderBrush.Get(), 1.0f);
            }
        };
        const auto drawFocusRing = [&](const RECT& sourceRect)
        {
            RECT focusRect = sourceRect;
            InflateRect(&focusRect, -1, -1);
            const auto focusBrush = createBrush(palette.accent);
            if (focusBrush)
            {
                const auto roundedRect = hyperbrowse::render::ToD2DRoundedRect(
                    focusRect, static_cast<float>(focusRingRadius), static_cast<float>(focusRingRadius));
                renderTarget->DrawRoundedRectangle(&roundedRect, focusBrush.Get(), 2.0f);
            }
        };

        for (int index = 0; index < static_cast<int>(menuButtons.size()); ++index)
        {
            const auto& button = menuButtons[static_cast<std::size_t>(index)];
            if (IsRectEmpty(&button.rect))
            {
                continue;
            }

            const bool hot = index == state.hotMenuIndex;
            const bool pressed = index == state.pressedMenuIndex;
            const COLORREF fillColor = pressed
                ? BlendColor(palette.actionStripBackground, palette.accent, 48)
                : (hot
                    ? BlendColor(palette.actionStripBackground, palette.text, 20)
                    : palette.actionStripBackground);
            const COLORREF borderColor = hot || pressed
                ? BlendColor(palette.actionStripBorder, palette.accent, 28)
                : fillColor;
            drawRoundedButton(button.rect, fillColor, borderColor);

            RECT textRect = button.rect;
            InflateRect(&textRect, -menuButtonPadding, 0);
            textRect.right -= menuChevronWidth + metrics.ScaleDip(4);
            if (state.keyboardActive && button.mnemonic != L'\0')
            {
                const auto mnemonicIt = std::find_if(button.label.begin(), button.label.end(), [&button](wchar_t character)
                {
                    return towupper(character) == towupper(button.mnemonic);
                });
                const auto labelLayout = mnemonicIt == button.label.end()
                    ? hyperbrowse::render::ComPtr<IDWriteTextLayout>{}
                    : renderer.CreateTextLayout(
                        button.label,
                        textFormat,
                        static_cast<float>(std::max<LONG>(1, textRect.right - textRect.left)),
                        static_cast<float>(std::max<LONG>(1, textRect.bottom - textRect.top)));
                if (labelLayout)
                {
                    labelLayout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{
                        static_cast<UINT32>(std::distance(button.label.begin(), mnemonicIt)),
                        1});
                    const auto labelBrush = createBrush(palette.text);
                    if (labelBrush)
                    {
                        renderTarget->DrawTextLayout(
                            hyperbrowse::render::ToD2DPoint(static_cast<float>(textRect.left), static_cast<float>(textRect.top)),
                            labelLayout.Get(),
                            labelBrush.Get());
                    }
                }
                else
                {
                    drawText(button.label, textRect, palette.text);
                }
            }
            else
            {
                drawText(button.label, textRect, palette.text);
            }

            const int chevronX = button.rect.right - menuButtonPadding - menuChevronWidth;
            const int chevronY = button.rect.top + ((button.rect.bottom - button.rect.top) - menuChevronWidth) / 2;
            const auto chevronBrush = createBrush(palette.mutedText);
            if (chevronBrush)
            {
                renderTarget->DrawLine(
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX), static_cast<float>(chevronY + metrics.ScaleDip(2))),
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + (menuChevronWidth / 2)), static_cast<float>(chevronY + metrics.ScaleDip(6))),
                    chevronBrush.Get(),
                    2.0f);
                renderTarget->DrawLine(
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + (menuChevronWidth / 2)), static_cast<float>(chevronY + metrics.ScaleDip(6))),
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + menuChevronWidth), static_cast<float>(chevronY + metrics.ScaleDip(2))),
                    chevronBrush.Get(),
                    2.0f);
            }
        }

        for (int index = 0; index < static_cast<int>(toolbarItems.size()); ++index)
        {
            const auto& item = toolbarItems[static_cast<std::size_t>(index)];
            if (item.kind == CommandBarController::ToolbarItemKind::Separator)
            {
                if (borderBrush)
                {
                        renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(item.rect.left), static_cast<float>(item.rect.top + metrics.ScaleDip(4))),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(item.rect.left), static_cast<float>(item.rect.bottom - metrics.ScaleDip(4))),
                        borderBrush.Get());
                }
                continue;
            }

            if (item.kind == CommandBarController::ToolbarItemKind::FilterEdit)
            {
                if (state.filterEditPresent)
                {
                    RECT filterRect = item.rect;
                    InflateRect(&filterRect, 0, -metrics.ScaleDip(2));
                    const COLORREF borderColor = state.filterFocused ? palette.accent : palette.actionStripBorder;
                    drawRoundedButton(filterRect, palette.actionFieldBackground, borderColor);

                    if (iconLibrary)
                    {
                        const int filterIconSize = metrics.ScaleDip(14);
                        const HBITMAP bitmap = iconLibrary->GetBitmap("search", filterIconSize, palette.mutedText);
                        BITMAP bitmapInfo{};
                        if (bitmap && GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) == sizeof(bitmapInfo))
                        {
                            const auto icon = hyperbrowse::render::D2DRenderer::Instance().CreateBitmapFromHBITMAP(
                                renderTarget,
                                bitmap,
                                bitmapInfo.bmWidth,
                                std::abs(bitmapInfo.bmHeight));
                            if (icon)
                            {
                                const int iconLeft = filterRect.left + metrics.ScaleDip(7);
                                const int iconTop = filterRect.top + metrics.ScaleDip(5);
                                hyperbrowse::render::DrawBitmapHighQuality(
                                    renderTarget,
                                    icon.Get(),
                                    D2D1::RectF(static_cast<float>(iconLeft),
                                                static_cast<float>(iconTop),
                                                static_cast<float>(iconLeft + filterIconSize),
                                                static_cast<float>(iconTop + filterIconSize)));
                            }
                        }
                    }
                }
                continue;
            }

            const bool isHot = index == state.hotToolbarIndex;
            const bool isPressed = index == state.pressedToolbarIndex;
            const bool isFocused = index == state.focusedToolbarIndex;
            const bool isChecked = item.checked;
            const bool isEnabled = item.enabled;
            COLORREF iconColor = palette.mutedText;
            if (isChecked)
            {
                iconColor = palette.accentText;
            }
            else if (!isEnabled)
            {
                iconColor = BlendColor(palette.mutedText, palette.actionStripBackground, 140);
            }

            if (isEnabled && (isHot || isPressed || isChecked))
            {
                COLORREF backgroundColor = palette.actionStripBackground;
                if (isChecked)
                {
                    backgroundColor = palette.accentFill;
                    if (isPressed)
                    {
                        backgroundColor = BlendColor(backgroundColor, palette.accent, 48);
                    }
                    else if (isHot)
                    {
                        backgroundColor = BlendColor(backgroundColor, palette.accent, 24);
                    }
                }
                else if (isPressed)
                {
                    backgroundColor = BlendColor(palette.actionStripBackground, palette.accent, 48);
                }
                else
                {
                    backgroundColor = BlendColor(palette.actionStripBackground, palette.text, 20);
                }
                drawRoundedButton(item.rect, backgroundColor, backgroundColor);
            }

            if (!item.iconName.empty() && iconLibrary)
            {
                RECT iconRect = item.rect;
                if (item.kind == CommandBarController::ToolbarItemKind::IconDropdown)
                {
                    iconRect.right -= dropdownChevronSize + metrics.ScaleDip(2);
                }
                const int iconLeft = iconRect.left + ((iconRect.right - iconRect.left) - toolbarIconSize) / 2;
                const int iconTop = iconRect.top + ((iconRect.bottom - iconRect.top) - toolbarIconSize) / 2;
                const HBITMAP bitmap = iconLibrary->GetBitmap(item.iconName, toolbarIconSize, iconColor);
                BITMAP bitmapInfo{};
                if (bitmap && GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) == sizeof(bitmapInfo))
                {
                    const auto icon = hyperbrowse::render::D2DRenderer::Instance().CreateBitmapFromHBITMAP(
                        renderTarget,
                        bitmap,
                        bitmapInfo.bmWidth,
                        std::abs(bitmapInfo.bmHeight));
                    if (icon)
                    {
                        hyperbrowse::render::DrawBitmapHighQuality(
                            renderTarget,
                            icon.Get(),
                            D2D1::RectF(static_cast<float>(iconLeft),
                                        static_cast<float>(iconTop),
                                        static_cast<float>(iconLeft + toolbarIconSize),
                                        static_cast<float>(iconTop + toolbarIconSize)));
                    }
                }
            }

            if (item.kind == CommandBarController::ToolbarItemKind::IconDropdown && isEnabled)
            {
                const int chevronX = item.rect.right - dropdownChevronSize - metrics.ScaleDip(6);
                const int chevronY = item.rect.top + ((item.rect.bottom - item.rect.top) - dropdownChevronSize) / 2;
                const auto chevronBrush = createBrush(palette.mutedText);
                if (chevronBrush)
                {
                    renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX), static_cast<float>(chevronY + metrics.ScaleDip(3))),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + metrics.ScaleDip(5)), static_cast<float>(chevronY + metrics.ScaleDip(7))),
                        chevronBrush.Get(),
                        1.5f);
                    renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + metrics.ScaleDip(5)), static_cast<float>(chevronY + metrics.ScaleDip(7))),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + dropdownChevronSize), static_cast<float>(chevronY + metrics.ScaleDip(3))),
                        chevronBrush.Get(),
                        1.5f);
                }
            }

            if (isEnabled && isFocused)
            {
                drawFocusRing(item.rect);
            }
        }
    }

    void CommandBarPainter::PaintGdi(
        HDC hdc,
        const RECT& stripRect,
        const std::array<CommandBarController::CommandBarMenuButton, 5>& menuButtons,
        const std::vector<CommandBarController::ToolbarItem>& toolbarItems,
        const CommandBarPalette& palette,
        const MenuMetrics& metrics,
        HFONT menuFont,
        ToolbarIconLibrary* iconLibrary,
        const CommandBarPaintState& state) const
    {
        const int menuButtonPadding = metrics.ScaleDip(metrics.commandBarMenuButtonPaddingDip);
        const int menuChevronWidth = metrics.ScaleDip(metrics.commandBarMenuChevronWidthDip);
        const int toolbarIconSize = metrics.ScaleDip(metrics.commandBarToolbarIconSizeDip);
        const int dropdownChevronSize = metrics.ScaleDip(metrics.commandBarDropdownChevronSizeDip);
        const int buttonRadius = metrics.ScaleDip(metrics.commandBarButtonRadiusDip);
        const int focusRingRadius = metrics.ScaleDip(metrics.commandBarFocusRingRadiusDip);
        const int filterRadius = metrics.ScaleDip(metrics.commandBarFilterRadiusDip);
        HDC iconDC = iconLibrary ? CreateCompatibleDC(hdc) : nullptr;

        const HBRUSH stripBrush = CreateSolidBrush(palette.actionStripBackground);
        FillRect(hdc, &stripRect, stripBrush);
        DeleteObject(stripBrush);

        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        const HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, stripRect.left, stripRect.bottom - 1, nullptr);
        LineTo(hdc, stripRect.right, stripRect.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        const auto drawFocusRing = [&](const RECT& sourceRect)
        {
            RECT focusRect = sourceRect;
            InflateRect(&focusRect, -1, -1);
            const HPEN focusPen = CreatePen(PS_SOLID, 2, palette.accent);
            const HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            const HGDIOBJ oldPen = SelectObject(hdc, focusPen);
            RoundRect(hdc, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                      focusRingRadius, focusRingRadius);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(focusPen);
        };

        for (int index = 0; index < static_cast<int>(menuButtons.size()); ++index)
        {
            const auto& button = menuButtons[static_cast<std::size_t>(index)];
            if (IsRectEmpty(&button.rect))
            {
                continue;
            }

            const bool hot = index == state.hotMenuIndex;
            const bool pressed = index == state.pressedMenuIndex;
            RECT buttonRect = button.rect;
            InflateRect(&buttonRect, -1, -1);

            const COLORREF fillColor = pressed
                ? BlendColor(palette.actionStripBackground, palette.accent, 48)
                : (hot
                    ? BlendColor(palette.actionStripBackground, palette.text, 20)
                    : palette.actionStripBackground);
            const COLORREF borderColor = (hot || pressed)
                ? BlendColor(palette.actionStripBorder, palette.accent, 28)
                : fillColor;

            const HBRUSH buttonBrush = CreateSolidBrush(fillColor);
            const HPEN buttonPen = CreatePen(PS_SOLID, 1, borderColor);
            const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
            const HGDIOBJ oldButtonPen = SelectObject(hdc, buttonPen);
            RoundRect(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom, buttonRadius, buttonRadius);
            SelectObject(hdc, oldButtonPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(buttonPen);
            DeleteObject(buttonBrush);

            RECT textRect = buttonRect;
            textRect.left += menuButtonPadding;
            textRect.right -= menuButtonPadding + menuChevronWidth + metrics.ScaleDip(4);
            std::wstring buttonLabel = button.label;
            if (state.keyboardActive && button.mnemonic != L'\0')
            {
                const auto mnemonicIt = std::find_if(buttonLabel.begin(), buttonLabel.end(), [&button](wchar_t character)
                {
                    return towupper(character) == towupper(button.mnemonic);
                });
                if (mnemonicIt != buttonLabel.end())
                {
                    buttonLabel.insert(
                        static_cast<std::wstring::size_type>(std::distance(buttonLabel.begin(), mnemonicIt)),
                        1,
                        L'&');
                }
            }
            render::DrawGdiText(hdc,
                                menuFont,
                                buttonLabel.c_str(),
                                -1,
                                textRect,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                palette.text,
                                fillColor);

            const int chevronX = buttonRect.right - menuButtonPadding - menuChevronWidth;
            const int chevronY = buttonRect.top + ((buttonRect.bottom - buttonRect.top) - menuChevronWidth) / 2;
            const HPEN chevronPen = CreatePen(PS_SOLID, 2, palette.mutedText);
            const HGDIOBJ oldChevronPen = SelectObject(hdc, chevronPen);
            MoveToEx(hdc, chevronX, chevronY + metrics.ScaleDip(2), nullptr);
            LineTo(hdc, chevronX + (menuChevronWidth / 2), chevronY + metrics.ScaleDip(6));
            LineTo(hdc, chevronX + menuChevronWidth, chevronY + metrics.ScaleDip(2));
            SelectObject(hdc, oldChevronPen);
            DeleteObject(chevronPen);
        }

        for (int index = 0; index < static_cast<int>(toolbarItems.size()); ++index)
        {
            const auto& item = toolbarItems[static_cast<std::size_t>(index)];

            if (item.kind == CommandBarController::ToolbarItemKind::Separator)
            {
                const HPEN sepPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
                const HGDIOBJ savedPen = SelectObject(hdc, sepPen);
                MoveToEx(hdc, item.rect.left, item.rect.top + metrics.ScaleDip(4), nullptr);
                LineTo(hdc, item.rect.left, item.rect.bottom - metrics.ScaleDip(4));
                SelectObject(hdc, savedPen);
                DeleteObject(sepPen);
                continue;
            }

            if (item.kind == CommandBarController::ToolbarItemKind::FilterEdit)
            {
                if (state.filterEditPresent)
                {
                    RECT filterBg = item.rect;
                    InflateRect(&filterBg, 0, -metrics.ScaleDip(2));
                    const HBRUSH fieldBrush = CreateSolidBrush(palette.actionFieldBackground);
                    const HPEN fieldPen = CreatePen(PS_SOLID, 1,
                                                    state.filterFocused ? palette.accent : palette.actionStripBorder);
                    const HGDIOBJ oldb = SelectObject(hdc, fieldBrush);
                    const HGDIOBJ oldp = SelectObject(hdc, fieldPen);
                    RoundRect(hdc, filterBg.left, filterBg.top, filterBg.right, filterBg.bottom, filterRadius, filterRadius);
                    SelectObject(hdc, oldp);
                    SelectObject(hdc, oldb);
                    DeleteObject(fieldPen);
                    DeleteObject(fieldBrush);

                    if (iconLibrary && iconDC)
                    {
                        const int filterIconSize = metrics.ScaleDip(14);
                        const HBITMAP searchBitmap = iconLibrary->GetBitmap("search", filterIconSize, palette.mutedText);
                        AlphaBlendBitmap(hdc,
                                         iconDC,
                                         searchBitmap,
                                         filterBg.left + metrics.ScaleDip(7),
                                         filterBg.top + metrics.ScaleDip(7),
                                         filterIconSize,
                                         filterIconSize);
                    }
                }
                continue;
            }

            const bool isHot = index == state.hotToolbarIndex;
            const bool isPressed = index == state.pressedToolbarIndex;
            const bool isFocused = index == state.focusedToolbarIndex;
            const bool isChecked = item.checked;
            const bool isEnabled = item.enabled;

            COLORREF iconColor = palette.mutedText;
            if (isChecked)
            {
                iconColor = palette.accentText;
            }
            else if (!isEnabled)
            {
                iconColor = BlendColor(palette.mutedText, palette.actionStripBackground, 140);
            }

            if (isEnabled && (isHot || isPressed || isChecked))
            {
                RECT bgRect = item.rect;
                InflateRect(&bgRect, -1, -1);

                COLORREF bgColor;
                if (isChecked)
                {
                    bgColor = palette.accentFill;
                    if (isPressed)
                    {
                        bgColor = BlendColor(bgColor, palette.accent, 48);
                    }
                    else if (isHot)
                    {
                        bgColor = BlendColor(bgColor, palette.accent, 24);
                    }
                }
                else if (isPressed)
                {
                    bgColor = BlendColor(palette.actionStripBackground, palette.accent, 48);
                }
                else
                {
                    bgColor = BlendColor(palette.actionStripBackground, palette.text, 20);
                }

                const HBRUSH bgBrush = CreateSolidBrush(bgColor);
                const HPEN bgPen = CreatePen(PS_SOLID, 1, bgColor);
                const HGDIOBJ oldb = SelectObject(hdc, bgBrush);
                const HGDIOBJ oldp = SelectObject(hdc, bgPen);
                const int backgroundRadius = metrics.ScaleDip(10);
                RoundRect(hdc, bgRect.left, bgRect.top, bgRect.right, bgRect.bottom, backgroundRadius, backgroundRadius);
                SelectObject(hdc, oldp);
                SelectObject(hdc, oldb);
                DeleteObject(bgPen);
                DeleteObject(bgBrush);
            }

            if (!item.iconName.empty() && iconLibrary && iconDC)
            {
                RECT iconRect = item.rect;
                if (item.kind == CommandBarController::ToolbarItemKind::IconDropdown)
                {
                    iconRect.right -= dropdownChevronSize + metrics.ScaleDip(2);
                }

                const int iconX = iconRect.left + ((iconRect.right - iconRect.left) - toolbarIconSize) / 2;
                const int iconY = iconRect.top + ((iconRect.bottom - iconRect.top) - toolbarIconSize) / 2;
                const HBITMAP iconBitmap = iconLibrary->GetBitmap(item.iconName, toolbarIconSize, iconColor);
                AlphaBlendBitmap(hdc, iconDC, iconBitmap, iconX, iconY, toolbarIconSize, toolbarIconSize);
            }

            if (item.kind == CommandBarController::ToolbarItemKind::IconDropdown && isEnabled && iconLibrary && iconDC)
            {
                const int chevronX = item.rect.right - dropdownChevronSize - metrics.ScaleDip(6);
                const int chevronY = item.rect.top + ((item.rect.bottom - item.rect.top) - dropdownChevronSize) / 2;
                const HBITMAP chevronBitmap = iconLibrary->GetBitmap("chevron-down",
                                                                       dropdownChevronSize,
                                                                       palette.mutedText);
                AlphaBlendBitmap(hdc,
                                 iconDC,
                                 chevronBitmap,
                                 chevronX,
                                 chevronY,
                                 dropdownChevronSize,
                                 dropdownChevronSize);
            }

            if (isEnabled && isFocused)
            {
                drawFocusRing(item.rect);
            }
        }

        if (iconDC)
        {
            DeleteDC(iconDC);
        }
    }
}
