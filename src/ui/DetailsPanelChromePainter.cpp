#include "ui/DetailsPanelChromePainter.h"

#include <d2d1.h>

#include <string_view>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"

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

        constexpr std::wstring_view kTabLabels[] = {L"File Details", L"Quick Actions"};
    }

    void DetailsPanelChromePainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                             const State& state,
                                             const Palette& palette,
                                             IDWriteTextFormat* tabFormat)
    {
        if (!renderTarget || !tabFormat)
        {
            return;
        }

        const auto createBrush = [renderTarget](COLORREF color)
        {
            render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
            return brush;
        };
        const auto drawText = [renderTarget, tabFormat](std::wstring_view text,
                                                        const RECT& rect,
                                                        ID2D1Brush* brush)
        {
            if (!text.empty() && brush && rect.right > rect.left && rect.bottom > rect.top)
            {
                renderTarget->DrawText(text.data(),
                                       static_cast<UINT32>(text.size()),
                                       tabFormat,
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

        if (!IsRectEmpty(&state.tabStripRect))
        {
            const COLORREF inactiveFill = BlendColor(palette.actionFieldBackground,
                                                      palette.paneBackground,
                                                      palette.darkTheme ? 24 : 12);
            const COLORREF inactiveBorder = BlendColor(palette.actionStripBorder,
                                                       palette.paneBackground,
                                                       palette.darkTheme ? 32 : 16);
            const COLORREF hoverFill = BlendColor(inactiveFill,
                                                  palette.accentFill,
                                                  palette.darkTheme ? 24 : 14);
            const COLORREF pressedFill = BlendColor(inactiveFill,
                                                    palette.accent,
                                                    palette.darkTheme ? 40 : 18);
            const COLORREF activePressedFill = BlendColor(palette.accentFill,
                                                          palette.accent,
                                                          palette.darkTheme ? 22 : 12);

            for (std::size_t index = 0; index < state.tabRects.size(); ++index)
            {
                const RECT& tabRect = state.tabRects[index];
                if (IsRectEmpty(&tabRect))
                {
                    continue;
                }

                const bool active = static_cast<int>(index) == state.activeTabIndex;
                const bool hot = static_cast<int>(index) == state.hotTabIndex;
                const bool pressed = static_cast<int>(index) == state.pressedTabIndex;
                const COLORREF fillColor = active
                    ? (pressed ? activePressedFill : palette.accentFill)
                    : (pressed ? pressedFill : (hot ? hoverFill : inactiveFill));
                const COLORREF borderColor = active
                    ? palette.accent
                    : ((hot || pressed)
                        ? BlendColor(inactiveBorder, palette.accent, palette.darkTheme ? 44 : 24)
                        : inactiveBorder);
                const COLORREF textColor = active
                    ? palette.accentText
                    : ((hot || pressed) ? palette.text : palette.mutedText);
                drawRounded(tabRect, fillColor, borderColor, 7.0f);
                const auto tabBrush = createBrush(textColor);
                if (tabBrush)
                {
                    drawText(kTabLabels[index], tabRect, tabBrush.Get());
                }
            }
        }

        if (!IsRectEmpty(&state.closeButtonRect))
        {
            const bool hot = state.closeButtonHot;
            const bool pressed = state.closeButtonPressed;
            const COLORREF fillColor = pressed
                ? BlendColor(palette.actionFieldBackground, palette.accentFill, palette.darkTheme ? 24 : 16)
                : (hot
                    ? BlendColor(palette.actionFieldBackground, palette.accentFill, palette.darkTheme ? 14 : 10)
                    : palette.actionFieldBackground);
            const COLORREF closeColor = hot || pressed ? palette.accentText : palette.mutedText;
            drawRounded(state.closeButtonRect,
                        fillColor,
                        hot || pressed ? palette.accent : palette.actionStripBorder,
                        4.0f);
            const auto closeBrush = createBrush(closeColor);
            if (closeBrush)
            {
                const RECT& closeRect = state.closeButtonRect;
                renderTarget->DrawLine(
                    render::ToD2DPoint(static_cast<float>(closeRect.left + 5), static_cast<float>(closeRect.top + 5)),
                    render::ToD2DPoint(static_cast<float>(closeRect.right - 5), static_cast<float>(closeRect.bottom - 5)),
                    closeBrush.Get(),
                    1.5f);
                renderTarget->DrawLine(
                    render::ToD2DPoint(static_cast<float>(closeRect.left + 5), static_cast<float>(closeRect.bottom - 5)),
                    render::ToD2DPoint(static_cast<float>(closeRect.right - 5), static_cast<float>(closeRect.top + 5)),
                    closeBrush.Get(),
                    1.5f);
            }
        }
    }

    void DetailsPanelChromePainter::PaintGdi(HDC hdc,
                                             const State& state,
                                             const Palette& palette,
                                             HFONT textFont)
    {
        if (!hdc)
        {
            return;
        }

        const HFONT resolvedFont = textFont
            ? textFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!IsRectEmpty(&state.tabStripRect))
        {
            const COLORREF inactiveFill = BlendColor(palette.actionFieldBackground,
                                                      palette.paneBackground,
                                                      palette.darkTheme ? 24 : 12);
            const COLORREF inactiveBorder = BlendColor(palette.actionStripBorder,
                                                       palette.paneBackground,
                                                       palette.darkTheme ? 32 : 16);
            const COLORREF hoverFill = BlendColor(inactiveFill,
                                                  palette.accentFill,
                                                  palette.darkTheme ? 24 : 14);
            const COLORREF pressedFill = BlendColor(inactiveFill,
                                                    palette.accent,
                                                    palette.darkTheme ? 40 : 18);
            const COLORREF activePressedFill = BlendColor(palette.accentFill,
                                                          palette.accent,
                                                          palette.darkTheme ? 22 : 12);

            for (std::size_t index = 0; index < state.tabRects.size(); ++index)
            {
                const RECT& tabRect = state.tabRects[index];
                if (IsRectEmpty(&tabRect))
                {
                    continue;
                }

                const bool active = static_cast<int>(index) == state.activeTabIndex;
                const bool hot = static_cast<int>(index) == state.hotTabIndex;
                const bool pressed = static_cast<int>(index) == state.pressedTabIndex;
                const COLORREF fillColor = active
                    ? (pressed ? activePressedFill : palette.accentFill)
                    : (pressed ? pressedFill : (hot ? hoverFill : inactiveFill));
                const COLORREF borderColor = active
                    ? palette.accent
                    : ((hot || pressed)
                        ? BlendColor(inactiveBorder, palette.accent, palette.darkTheme ? 44 : 24)
                        : inactiveBorder);
                const COLORREF textColor = active
                    ? palette.accentText
                    : ((hot || pressed) ? palette.text : palette.mutedText);

                const HBRUSH tabBrush = CreateSolidBrush(fillColor);
                const HPEN tabPen = CreatePen(PS_SOLID, 1, borderColor);
                const HGDIOBJ oldBrush = SelectObject(hdc, tabBrush);
                const HGDIOBJ oldPen = SelectObject(hdc, tabPen);
                RoundRect(hdc, tabRect.left, tabRect.top, tabRect.right, tabRect.bottom, 14, 14);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(tabPen);
                DeleteObject(tabBrush);

                render::DrawGdiText(hdc,
                                    resolvedFont,
                                    kTabLabels[index].data(),
                                    -1,
                                    tabRect,
                                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
                                    textColor,
                                    fillColor);
            }
        }

        if (!IsRectEmpty(&state.closeButtonRect))
        {
            const bool hot = state.closeButtonHot;
            const bool pressed = state.closeButtonPressed;
            const COLORREF fillColor = pressed
                ? BlendColor(palette.actionFieldBackground, palette.accentFill, palette.darkTheme ? 24 : 16)
                : (hot
                    ? BlendColor(palette.actionFieldBackground, palette.accentFill, palette.darkTheme ? 14 : 10)
                    : palette.actionFieldBackground);
            const COLORREF borderColor = hot || pressed ? palette.accent : palette.actionStripBorder;
            const COLORREF textColor = hot || pressed ? palette.accentText : palette.mutedText;

            const HBRUSH buttonBrush = CreateSolidBrush(fillColor);
            const HPEN buttonPen = CreatePen(PS_SOLID, 1, borderColor);
            const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
            const HGDIOBJ oldPen = SelectObject(hdc, buttonPen);
            RoundRect(hdc,
                      state.closeButtonRect.left,
                      state.closeButtonRect.top,
                      state.closeButtonRect.right,
                      state.closeButtonRect.bottom,
                      6,
                      6);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(buttonPen);
            DeleteObject(buttonBrush);

            const int inset = 5;
            const int left = state.closeButtonRect.left + inset;
            const int top = state.closeButtonRect.top + inset;
            const int right = state.closeButtonRect.right - inset;
            const int bottom = state.closeButtonRect.bottom - inset;
            const HPEN xPen = CreatePen(PS_SOLID, 1, textColor);
            const HGDIOBJ oldXPen = SelectObject(hdc, xPen);
            MoveToEx(hdc, left, top, nullptr);
            LineTo(hdc, right, bottom);
            MoveToEx(hdc, left, bottom, nullptr);
            LineTo(hdc, right, top);
            SelectObject(hdc, oldXPen);
            DeleteObject(xPen);
        }
    }
}
