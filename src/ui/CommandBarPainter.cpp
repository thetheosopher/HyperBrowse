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
        constexpr int kToolbarIconSize = 18;
        constexpr int kToolbarDropdownChevronSize = 10;
        constexpr int kCommandBarMenuButtonPadding = 12;
        constexpr int kCommandBarMenuChevronWidth = 8;

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
        const std::array<CommandBarController::CommandBarMenuButton, 4>& menuButtons,
        const std::vector<CommandBarController::ToolbarItem>& toolbarItems,
        const CommandBarPalette& palette,
        IDWriteTextFormat* textFormat,
        ToolbarIconLibrary* iconLibrary,
        const CommandBarPaintState& state) const
    {
        if (!renderTarget || !textFormat)
        {
            return;
        }

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
            const D2D1_ROUNDED_RECT roundedRect = hyperbrowse::render::ToD2DRoundedRect(buttonRect, 10.0f, 10.0f);
            if (fillBrush)
            {
                renderTarget->FillRoundedRectangle(&roundedRect, fillBrush.Get());
            }
            if (buttonBorderBrush)
            {
                renderTarget->DrawRoundedRectangle(&roundedRect, buttonBorderBrush.Get(), 1.0f);
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
            InflateRect(&textRect, -kCommandBarMenuButtonPadding, 0);
            textRect.right -= kCommandBarMenuChevronWidth + 4;
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

            const int chevronX = button.rect.right - kCommandBarMenuButtonPadding - kCommandBarMenuChevronWidth;
            const int chevronY = button.rect.top + ((button.rect.bottom - button.rect.top) - kCommandBarMenuChevronWidth) / 2;
            const auto chevronBrush = createBrush(palette.mutedText);
            if (chevronBrush)
            {
                renderTarget->DrawLine(
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX), static_cast<float>(chevronY + 2)),
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + (kCommandBarMenuChevronWidth / 2)), static_cast<float>(chevronY + 6)),
                    chevronBrush.Get(),
                    2.0f);
                renderTarget->DrawLine(
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + (kCommandBarMenuChevronWidth / 2)), static_cast<float>(chevronY + 6)),
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + kCommandBarMenuChevronWidth), static_cast<float>(chevronY + 2)),
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
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(item.rect.left), static_cast<float>(item.rect.top + 4)),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(item.rect.left), static_cast<float>(item.rect.bottom - 4)),
                        borderBrush.Get());
                }
                continue;
            }

            if (item.kind == CommandBarController::ToolbarItemKind::FilterEdit)
            {
                if (state.filterEditPresent)
                {
                    RECT filterRect = item.rect;
                    InflateRect(&filterRect, 0, -2);
                    const COLORREF borderColor = state.filterFocused ? palette.accent : palette.actionStripBorder;
                    drawRoundedButton(filterRect, palette.actionFieldBackground, borderColor);

                    if (iconLibrary)
                    {
                        const HBITMAP bitmap = iconLibrary->GetBitmap("search", 14, palette.mutedText);
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
                                const int iconLeft = filterRect.left + 7;
                                const int iconTop = filterRect.top + 5;
                                hyperbrowse::render::DrawBitmapHighQuality(
                                    renderTarget,
                                    icon.Get(),
                                    D2D1::RectF(static_cast<float>(iconLeft),
                                                static_cast<float>(iconTop),
                                                static_cast<float>(iconLeft + 14),
                                                static_cast<float>(iconTop + 14)));
                            }
                        }
                    }
                }
                continue;
            }

            const bool isHot = index == state.hotToolbarIndex;
            const bool isPressed = index == state.pressedToolbarIndex;
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
                    iconRect.right -= kToolbarDropdownChevronSize + 2;
                }
                const int iconLeft = iconRect.left + ((iconRect.right - iconRect.left) - kToolbarIconSize) / 2;
                const int iconTop = iconRect.top + ((iconRect.bottom - iconRect.top) - kToolbarIconSize) / 2;
                const HBITMAP bitmap = iconLibrary->GetBitmap(item.iconName, kToolbarIconSize, iconColor);
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
                                        static_cast<float>(iconLeft + kToolbarIconSize),
                                        static_cast<float>(iconTop + kToolbarIconSize)));
                    }
                }
            }

            if (item.kind == CommandBarController::ToolbarItemKind::IconDropdown && isEnabled)
            {
                const int chevronX = item.rect.right - kToolbarDropdownChevronSize - 6;
                const int chevronY = item.rect.top + ((item.rect.bottom - item.rect.top) - kToolbarDropdownChevronSize) / 2;
                const auto chevronBrush = createBrush(palette.mutedText);
                if (chevronBrush)
                {
                    renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX), static_cast<float>(chevronY + 3)),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + 5), static_cast<float>(chevronY + 7)),
                        chevronBrush.Get(),
                        1.5f);
                    renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + 5), static_cast<float>(chevronY + 7)),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(chevronX + 10), static_cast<float>(chevronY + 3)),
                        chevronBrush.Get(),
                        1.5f);
                }
            }
        }
    }

    void CommandBarPainter::PaintGdi(
        HDC hdc,
        const RECT& stripRect,
        const std::array<CommandBarController::CommandBarMenuButton, 4>& menuButtons,
        const std::vector<CommandBarController::ToolbarItem>& toolbarItems,
        const CommandBarPalette& palette,
        HFONT menuFont,
        ToolbarIconLibrary* iconLibrary,
        const CommandBarPaintState& state) const
    {
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
            RoundRect(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom, 10, 10);
            SelectObject(hdc, oldButtonPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(buttonPen);
            DeleteObject(buttonBrush);

            RECT textRect = buttonRect;
            textRect.left += kCommandBarMenuButtonPadding;
            textRect.right -= kCommandBarMenuButtonPadding + kCommandBarMenuChevronWidth + 4;
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

            const int chevronX = buttonRect.right - kCommandBarMenuButtonPadding - kCommandBarMenuChevronWidth;
            const int chevronY = buttonRect.top + ((buttonRect.bottom - buttonRect.top) - kCommandBarMenuChevronWidth) / 2;
            const HPEN chevronPen = CreatePen(PS_SOLID, 2, palette.mutedText);
            const HGDIOBJ oldChevronPen = SelectObject(hdc, chevronPen);
            MoveToEx(hdc, chevronX, chevronY + 2, nullptr);
            LineTo(hdc, chevronX + (kCommandBarMenuChevronWidth / 2), chevronY + 6);
            LineTo(hdc, chevronX + kCommandBarMenuChevronWidth, chevronY + 2);
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
                MoveToEx(hdc, item.rect.left, item.rect.top + 4, nullptr);
                LineTo(hdc, item.rect.left, item.rect.bottom - 4);
                SelectObject(hdc, savedPen);
                DeleteObject(sepPen);
                continue;
            }

            if (item.kind == CommandBarController::ToolbarItemKind::FilterEdit)
            {
                if (state.filterEditPresent)
                {
                    RECT filterBg = item.rect;
                    InflateRect(&filterBg, 0, -2);
                    const HBRUSH fieldBrush = CreateSolidBrush(palette.actionFieldBackground);
                    const HPEN fieldPen = CreatePen(PS_SOLID, 1,
                                                    state.filterFocused ? palette.accent : palette.actionStripBorder);
                    const HGDIOBJ oldb = SelectObject(hdc, fieldBrush);
                    const HGDIOBJ oldp = SelectObject(hdc, fieldPen);
                    RoundRect(hdc, filterBg.left, filterBg.top, filterBg.right, filterBg.bottom, 14, 14);
                    SelectObject(hdc, oldp);
                    SelectObject(hdc, oldb);
                    DeleteObject(fieldPen);
                    DeleteObject(fieldBrush);

                    if (iconLibrary && iconDC)
                    {
                        const HBITMAP searchBitmap = iconLibrary->GetBitmap("search", 14, palette.mutedText);
                        AlphaBlendBitmap(hdc, iconDC, searchBitmap, filterBg.left + 7, filterBg.top + 7, 14, 14);
                    }
                }
                continue;
            }

            const bool isHot = index == state.hotToolbarIndex;
            const bool isPressed = index == state.pressedToolbarIndex;
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
                RoundRect(hdc, bgRect.left, bgRect.top, bgRect.right, bgRect.bottom, 10, 10);
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
                    iconRect.right -= kToolbarDropdownChevronSize + 2;
                }

                const int iconX = iconRect.left + ((iconRect.right - iconRect.left) - kToolbarIconSize) / 2;
                const int iconY = iconRect.top + ((iconRect.bottom - iconRect.top) - kToolbarIconSize) / 2;
                const HBITMAP iconBitmap = iconLibrary->GetBitmap(item.iconName, kToolbarIconSize, iconColor);
                AlphaBlendBitmap(hdc, iconDC, iconBitmap, iconX, iconY, kToolbarIconSize, kToolbarIconSize);
            }

            if (item.kind == CommandBarController::ToolbarItemKind::IconDropdown && isEnabled && iconLibrary && iconDC)
            {
                const int chevronX = item.rect.right - kToolbarDropdownChevronSize - 6;
                const int chevronY = item.rect.top + ((item.rect.bottom - item.rect.top) - kToolbarDropdownChevronSize) / 2;
                const HBITMAP chevronBitmap = iconLibrary->GetBitmap("chevron-down",
                                                                       kToolbarDropdownChevronSize,
                                                                       palette.mutedText);
                AlphaBlendBitmap(hdc,
                                 iconDC,
                                 chevronBitmap,
                                 chevronX,
                                 chevronY,
                                 kToolbarDropdownChevronSize,
                                 kToolbarDropdownChevronSize);
            }
        }

        if (iconDC)
        {
            DeleteDC(iconDC);
        }
    }
}
