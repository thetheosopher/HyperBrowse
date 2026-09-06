#include "ui/QuickAccessPainter.h"

#include <d2d1.h>

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

        void AlphaBlendBitmap(HDC targetDc, HDC scratchDc, HBITMAP bitmap, int x, int y, int width, int height)
        {
            if (!targetDc || !scratchDc || !bitmap || width <= 0 || height <= 0)
            {
                return;
            }

            const HGDIOBJ oldBitmap = SelectObject(scratchDc, bitmap);
            BLENDFUNCTION blend{};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;
            AlphaBlend(targetDc, x, y, width, height, scratchDc, 0, 0, width, height, blend);
            SelectObject(scratchDc, oldBitmap);
        }
    }

    void QuickAccessPainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                      HDC clipDc,
                                      const State& state,
                                      const Palette& palette,
                                      IDWriteTextFormat* summaryFormat,
                                      IDWriteTextFormat* bodyFormat)
    {
        if (!renderTarget || !summaryFormat || !bodyFormat)
        {
            return;
        }

        const auto createBrush = [renderTarget](COLORREF color)
        {
            render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
            return brush;
        };
        const auto drawText = [renderTarget](std::wstring_view text,
                                             IDWriteTextFormat* format,
                                             const RECT& rect,
                                             ID2D1Brush* brush)
        {
            if (!text.empty() && format && brush && rect.right > rect.left && rect.bottom > rect.top)
            {
                renderTarget->DrawText(text.data(),
                                       static_cast<UINT32>(text.size()),
                                       format,
                                       render::ToD2DRect(rect),
                                       brush);
            }
        };
        const auto drawRounded = [renderTarget](const RECT& rect,
                                                COLORREF fillColor,
                                                COLORREF borderColor,
                                                float radius)
        {
            render::ComPtr<ID2D1SolidColorBrush> fillBrush;
            render::ComPtr<ID2D1SolidColorBrush> outlineBrush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(fillColor), fillBrush.GetAddressOf());
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(borderColor), outlineBrush.GetAddressOf());
            RECT insetRect = rect;
            InflateRect(&insetRect, -1, -1);
            const auto roundedRect = render::ToD2DRoundedRect(insetRect, radius, radius);
            if (fillBrush)
            {
                renderTarget->FillRoundedRectangle(&roundedRect, fillBrush.Get());
            }
            if (outlineBrush)
            {
                renderTarget->DrawRoundedRectangle(&roundedRect, outlineBrush.Get(), 1.0f);
            }
        };

        const auto mutedBrush = createBrush(palette.mutedText);
        drawText(L"Quick Actions", summaryFormat, state.headerRect, mutedBrush.Get());

        if (!IsRectEmpty(&state.sortButtonRect))
        {
            const bool hot = state.sortButtonHot;
            const bool pressed = state.sortButtonPressed;
            if (hot || pressed)
            {
                drawRounded(state.sortButtonRect,
                            pressed
                                ? BlendColor(palette.accentFill, palette.accent, palette.darkTheme ? 40 : 18)
                                : BlendColor(palette.actionFieldBackground, palette.accentFill, palette.darkTheme ? 28 : 16),
                            palette.accent,
                            4.0f);
            }
            const auto sortBrush = createBrush(hot || pressed ? palette.accentText : palette.mutedText);
            if (sortBrush)
            {
                const int centerX = (state.sortButtonRect.left + state.sortButtonRect.right) / 2;
                renderTarget->DrawLine(render::ToD2DPoint(static_cast<float>(centerX - 6), static_cast<float>(state.sortButtonRect.top + 6)),
                                       render::ToD2DPoint(static_cast<float>(centerX + 6), static_cast<float>(state.sortButtonRect.top + 6)),
                                       sortBrush.Get(),
                                       1.5f);
                renderTarget->DrawLine(render::ToD2DPoint(static_cast<float>(centerX - 4), static_cast<float>(state.sortButtonRect.top + 10)),
                                       render::ToD2DPoint(static_cast<float>(centerX + 4), static_cast<float>(state.sortButtonRect.top + 10)),
                                       sortBrush.Get(),
                                       1.5f);
                renderTarget->DrawLine(render::ToD2DPoint(static_cast<float>(centerX - 2), static_cast<float>(state.sortButtonRect.top + 14)),
                                       render::ToD2DPoint(static_cast<float>(centerX + 2), static_cast<float>(state.sortButtonRect.top + 14)),
                                       sortBrush.Get(),
                                       1.5f);
            }
        }

        const COLORREF rowBackground = BlendColor(palette.actionFieldBackground,
                                                  palette.paneBackground,
                                                  palette.darkTheme ? 24 : 12);
        const COLORREF disabledButtonFill = BlendColor(palette.actionFieldBackground,
                                                       palette.paneBackground,
                                                       palette.darkTheme ? 10 : 20);
        const auto drawActionButton = [&](const RECT& rect,
                                          std::wstring_view label,
                                          int buttonIndex,
                                          bool enabled,
                                          COLORREF baseFill,
                                          COLORREF baseText,
                                          COLORREF enabledBorder)
        {
            const bool hot = buttonIndex == state.hotButtonIndex;
            const bool pressed = buttonIndex == state.pressedButtonIndex;
            const COLORREF fillColor = enabled
                ? (pressed ? palette.accent : (hot ? BlendColor(baseFill, palette.accent, 48) : baseFill))
                : disabledButtonFill;
            const COLORREF textColor = enabled ? baseText : palette.mutedText;
            drawRounded(rect, fillColor, enabled ? enabledBorder : palette.actionStripBorder, 5.0f);
            const auto buttonBrush = createBrush(textColor);
            drawText(label, bodyFormat, rect, buttonBrush.Get());
        };

        const int savedDc = clipDc ? SaveDC(clipDc) : 0;
        const bool validViewport = !IsRectEmpty(&state.viewportRect);
        if (clipDc && validViewport)
        {
            IntersectClipRect(clipDc,
                              state.viewportRect.left,
                              state.viewportRect.top,
                              state.viewportRect.right,
                              state.viewportRect.bottom);
        }
        if (validViewport)
        {
            renderTarget->PushAxisAlignedClip(render::ToD2DRect(state.viewportRect),
                                               D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        for (std::size_t rowIndex = 0; rowIndex < state.rows.size(); ++rowIndex)
        {
            const RowState& rowState = state.rows[rowIndex];
            if (!rowState.row)
            {
                continue;
            }

            const QuickAccessLayout::Row& row = *rowState.row;
            const bool rowHot = rowState.navigationEnabled && static_cast<int>(rowIndex) == state.hotRowIndex;
            const bool rowPressed = rowState.navigationEnabled && static_cast<int>(rowIndex) == state.pressedRowIndex;
            const COLORREF currentRowBackground = rowPressed
                ? BlendColor(rowBackground, palette.accentFill, palette.darkTheme ? 28 : 18)
                : (rowHot
                    ? BlendColor(rowBackground, palette.accentFill, palette.darkTheme ? 20 : 12)
                    : rowBackground);
            drawRounded(row.rowRect,
                        currentRowBackground,
                        rowHot ? palette.accent : palette.actionStripBorder,
                        6.0f);

            RECT labelRect = row.rowRect;
            labelRect.left += 10;
            labelRect.top += state.metrics.labelTopInset;
            labelRect.right = row.shortcutRect.left - 10;
            labelRect.bottom = labelRect.top + state.metrics.labelHeight;
            const auto rowTextBrush = createBrush(rowState.navigationEnabled ? palette.text : palette.mutedText);
            drawText(row.displayLabel, summaryFormat, labelRect, rowTextBrush.Get());

            RECT metadataRect = row.rowRect;
            metadataRect.left += 10;
            metadataRect.top += state.metrics.metadataTopInset;
            metadataRect.right = row.copyRect.left - 10;
            metadataRect.bottom -= state.metrics.metadataBottomInset;
            drawText(row.metadataLabel, bodyFormat, metadataRect, mutedBrush.Get());

            drawActionButton(row.copyRect,
                             L"Copy",
                             static_cast<int>(rowIndex * 3),
                             rowState.actionsEnabled,
                             palette.accentFill,
                             palette.accentText,
                             palette.accent);
            drawActionButton(row.moveRect,
                             L"Move",
                             static_cast<int>(rowIndex * 3 + 1),
                             rowState.actionsEnabled,
                             palette.accentFill,
                             palette.accentText,
                             palette.accent);
            drawActionButton(row.removeRect,
                             L"x",
                             static_cast<int>(rowIndex * 3 + 2),
                             true,
                             BlendColor(rowBackground,
                                        palette.actionFieldBackground,
                                        palette.darkTheme ? 12 : 20),
                             palette.mutedText,
                             palette.actionStripBorder);
        }

        if (validViewport)
        {
            renderTarget->PopAxisAlignedClip();
        }
        if (clipDc && savedDc != 0)
        {
            RestoreDC(clipDc, savedDc);
        }
    }

    void QuickAccessPainter::PaintGdi(HDC hdc,
                                      const State& state,
                                      const Palette& palette,
                                      HFONT summaryFont,
                                      HFONT bodyFont,
                                      ToolbarIconLibrary* iconLibrary)
    {
        if (!hdc)
        {
            return;
        }

        const HFONT resolvedSummaryFont = summaryFont
            ? summaryFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HFONT resolvedBodyFont = bodyFont
            ? bodyFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        render::DrawGdiText(hdc,
                            resolvedSummaryFont,
                            L"Quick Actions",
                            -1,
                            state.headerRect,
                            DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE,
                            palette.mutedText,
                            palette.paneBackground);

        if (!IsRectEmpty(&state.sortButtonRect))
        {
            const bool hot = state.sortButtonHot;
            const bool pressed = state.sortButtonPressed;
            if (hot || pressed)
            {
                const COLORREF fillColor = pressed
                    ? BlendColor(palette.accentFill, palette.accent, palette.darkTheme ? 40 : 18)
                    : BlendColor(palette.actionFieldBackground, palette.accentFill, palette.darkTheme ? 28 : 16);
                RECT buttonRect = state.sortButtonRect;
                InflateRect(&buttonRect, -1, -1);
                HBRUSH buttonBrush = CreateSolidBrush(fillColor);
                HPEN buttonPen = CreatePen(PS_SOLID, 1, palette.accent);
                const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
                const HGDIOBJ oldPen = SelectObject(hdc, buttonPen);
                RoundRect(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom, 8, 8);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(buttonPen);
                DeleteObject(buttonBrush);
            }

            HDC iconDc = iconLibrary ? CreateCompatibleDC(hdc) : nullptr;
            if (iconLibrary && iconDc)
            {
                const COLORREF iconColor = hot || pressed ? palette.accentText : palette.mutedText;
                const int iconSize = 14;
                const int iconX = state.sortButtonRect.left
                    + ((state.sortButtonRect.right - state.sortButtonRect.left) - iconSize) / 2;
                const int iconY = state.sortButtonRect.top
                    + ((state.sortButtonRect.bottom - state.sortButtonRect.top) - iconSize) / 2;
                const HBITMAP sortBitmap = iconLibrary->GetBitmap("sort", iconSize, iconColor);
                AlphaBlendBitmap(hdc, iconDc, sortBitmap, iconX, iconY, iconSize, iconSize);
            }
            if (iconDc)
            {
                DeleteDC(iconDc);
            }
        }

        const COLORREF rowBackground = BlendColor(palette.actionFieldBackground,
                                                  palette.paneBackground,
                                                  palette.darkTheme ? 24 : 12);
        const COLORREF disabledButtonFill = BlendColor(palette.actionFieldBackground,
                                                       palette.paneBackground,
                                                       palette.darkTheme ? 10 : 20);
        const auto drawActionButton = [&](const RECT& rect,
                                          const wchar_t* label,
                                          int buttonIndex,
                                          bool enabled,
                                          COLORREF baseFill,
                                          COLORREF baseText,
                                          COLORREF enabledBorder)
        {
            const bool hot = buttonIndex == state.hotButtonIndex;
            const bool pressed = buttonIndex == state.pressedButtonIndex;
            const COLORREF fillColor = enabled
                ? (pressed ? palette.accent : (hot ? BlendColor(baseFill, palette.accent, 48) : baseFill))
                : disabledButtonFill;
            const COLORREF textColor = enabled ? baseText : palette.mutedText;

            HBRUSH buttonBrush = CreateSolidBrush(fillColor);
            HPEN buttonPen = CreatePen(PS_SOLID, 1, enabled ? enabledBorder : palette.actionStripBorder);
            const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
            const HGDIOBJ oldButtonPen = SelectObject(hdc, buttonPen);
            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 10, 10);
            SelectObject(hdc, oldButtonPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(buttonPen);
            DeleteObject(buttonBrush);

            render::DrawGdiText(hdc,
                                resolvedBodyFont,
                                label,
                                -1,
                                rect,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                textColor,
                                fillColor);
        };

        const int savedDc = SaveDC(hdc);
        IntersectClipRect(hdc,
                          state.viewportRect.left,
                          state.viewportRect.top,
                          state.viewportRect.right,
                          state.viewportRect.bottom);
        for (std::size_t rowIndex = 0; rowIndex < state.rows.size(); ++rowIndex)
        {
            const RowState& rowState = state.rows[rowIndex];
            if (!rowState.row)
            {
                continue;
            }

            const QuickAccessLayout::Row& row = *rowState.row;
            const bool rowHot = rowState.navigationEnabled && static_cast<int>(rowIndex) == state.hotRowIndex;
            const bool rowPressed = rowState.navigationEnabled && static_cast<int>(rowIndex) == state.pressedRowIndex;
            const COLORREF currentRowBackground = rowPressed
                ? BlendColor(rowBackground, palette.accentFill, palette.darkTheme ? 28 : 18)
                : (rowHot
                    ? BlendColor(rowBackground, palette.accentFill, palette.darkTheme ? 20 : 12)
                    : rowBackground);
            HBRUSH rowBrush = CreateSolidBrush(currentRowBackground);
            HPEN rowPen = CreatePen(PS_SOLID, 1, rowHot ? palette.accent : palette.actionStripBorder);
            const HGDIOBJ oldBrush = SelectObject(hdc, rowBrush);
            const HGDIOBJ oldRowPen = SelectObject(hdc, rowPen);
            RoundRect(hdc, row.rowRect.left, row.rowRect.top, row.rowRect.right, row.rowRect.bottom, 12, 12);
            SelectObject(hdc, oldRowPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(rowPen);
            DeleteObject(rowBrush);

            RECT labelRect = row.rowRect;
            labelRect.left += 10;
            labelRect.top += state.metrics.labelTopInset;
            labelRect.right = row.shortcutRect.left - 10;
            labelRect.bottom = labelRect.top + state.metrics.labelHeight;
            render::DrawGdiText(hdc,
                                resolvedSummaryFont,
                                row.displayLabel.c_str(),
                                -1,
                                labelRect,
                                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                rowState.navigationEnabled ? palette.text : palette.mutedText,
                                currentRowBackground);

            RECT metadataRect = row.rowRect;
            metadataRect.left += 10;
            metadataRect.top += state.metrics.metadataTopInset;
            metadataRect.right = row.copyRect.left - 10;
            metadataRect.bottom -= state.metrics.metadataBottomInset;
            render::DrawGdiText(hdc,
                                resolvedBodyFont,
                                row.metadataLabel.c_str(),
                                -1,
                                metadataRect,
                                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                palette.mutedText,
                                currentRowBackground);

            drawActionButton(row.copyRect,
                             L"Copy",
                             static_cast<int>(rowIndex * 3),
                             rowState.actionsEnabled,
                             palette.accentFill,
                             palette.accentText,
                             palette.accent);
            drawActionButton(row.moveRect,
                             L"Move",
                             static_cast<int>(rowIndex * 3 + 1),
                             rowState.actionsEnabled,
                             palette.accentFill,
                             palette.accentText,
                             palette.accent);
            drawActionButton(row.removeRect,
                             L"x",
                             static_cast<int>(rowIndex * 3 + 2),
                             true,
                             BlendColor(rowBackground,
                                        palette.actionFieldBackground,
                                        palette.darkTheme ? 12 : 20),
                             palette.mutedText,
                             palette.actionStripBorder);
        }
        if (savedDc != 0)
        {
            RestoreDC(hdc, savedDc);
        }
    }
}
