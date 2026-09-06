#include "ui/DetailsPanelHistogramPainter.h"

#include <d2d1.h>

#include <algorithm>
#include <cwchar>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr COLORREF kRedChannelColor = RGB(224, 98, 92);
        constexpr COLORREF kGreenChannelColor = RGB(112, 188, 102);
        constexpr COLORREF kBlueChannelColor = RGB(92, 150, 232);
    }

    void DetailsPanelHistogramPainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                                 const State& state,
                                                 const Palette& palette,
                                                 ID2D1Brush* borderBrush,
                                                 ID2D1Brush* mutedBrush,
                                                 IDWriteTextFormat* textFormat)
    {
        if (!renderTarget || !borderBrush || !mutedBrush || !textFormat || IsRectEmpty(&state.rect))
        {
            return;
        }

        const auto createBrush = [renderTarget](COLORREF color)
        {
            render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
            return brush;
        };
        const auto histogramBrush = createBrush(palette.background);
        if (histogramBrush)
        {
            renderTarget->FillRectangle(render::ToD2DRect(state.rect), histogramBrush.Get());
        }
        renderTarget->DrawRectangle(render::ToD2DRect(state.rect), borderBrush, 1.0f);

        RECT textRect = state.rect;
        InflateRect(&textRect, -8, -8);
        const auto drawText = [renderTarget, textFormat, mutedBrush](const wchar_t* text, const RECT& rect)
        {
            if (text && *text && rect.right > rect.left && rect.bottom > rect.top)
            {
                renderTarget->DrawText(text,
                                       static_cast<UINT32>(std::wcslen(text)),
                                       textFormat,
                                       render::ToD2DRect(rect),
                                       mutedBrush);
            }
        };
        if (state.loading)
        {
            drawText(L"Loading histogram...", textRect);
            return;
        }
        if (!state.visible || state.peak == 0)
        {
            drawText(L"Histogram unavailable", textRect);
            return;
        }

        const int chartLeft = state.rect.left + 6;
        const int chartTop = state.rect.top + 6;
        const int chartRight = state.rect.right - 6;
        const int chartBottom = state.rect.bottom - 6;
        const int chartWidth = (std::max)(1, chartRight - chartLeft);
        const int chartHeight = (std::max)(1, chartBottom - chartTop);
        const auto drawChannel = [&](const auto& values, COLORREF color)
        {
            const auto channelBrush = createBrush(color);
            if (!channelBrush)
            {
                return;
            }
            for (std::size_t index = 1; index < DetailsPanelHistogram::kBinCount; ++index)
            {
                const int previousX = chartLeft + MulDiv(static_cast<int>(index - 1),
                                                         chartWidth - 1,
                                                         static_cast<int>(DetailsPanelHistogram::kBinCount - 1));
                const int currentX = chartLeft + MulDiv(static_cast<int>(index),
                                                        chartWidth - 1,
                                                        static_cast<int>(DetailsPanelHistogram::kBinCount - 1));
                const int previousHeight = MulDiv(
                    static_cast<int>(values[index - 1]),
                    chartHeight - 1,
                    static_cast<int>(state.peak));
                const int currentHeight = MulDiv(
                    static_cast<int>(values[index]),
                    chartHeight - 1,
                    static_cast<int>(state.peak));
                renderTarget->DrawLine(
                    render::ToD2DPoint(static_cast<float>(previousX), static_cast<float>(chartBottom - previousHeight)),
                    render::ToD2DPoint(static_cast<float>(currentX), static_cast<float>(chartBottom - currentHeight)),
                    channelBrush.Get(),
                    1.0f);
            }
        };
        drawChannel(state.red, kRedChannelColor);
        drawChannel(state.green, kGreenChannelColor);
        drawChannel(state.blue, kBlueChannelColor);
    }

    void DetailsPanelHistogramPainter::PaintGdi(HDC hdc,
                                                 const State& state,
                                                 const Palette& palette,
                                                 HFONT textFont)
    {
        if (!hdc || IsRectEmpty(&state.rect))
        {
            return;
        }

        const HBRUSH histogramBrush = CreateSolidBrush(palette.background);
        FillRect(hdc, &state.rect, histogramBrush);
        DeleteObject(histogramBrush);

        const HPEN histogramBorderPen = CreatePen(PS_SOLID, 1, palette.border);
        const HGDIOBJ oldPen = SelectObject(hdc, histogramBorderPen);
        MoveToEx(hdc, state.rect.left, state.rect.top, nullptr);
        LineTo(hdc, state.rect.right, state.rect.top);
        LineTo(hdc, state.rect.right, state.rect.bottom);
        LineTo(hdc, state.rect.left, state.rect.bottom);
        LineTo(hdc, state.rect.left, state.rect.top);
        SelectObject(hdc, oldPen);
        DeleteObject(histogramBorderPen);

        const HFONT resolvedFont = textFont
            ? textFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        RECT textRect = state.rect;
        InflateRect(&textRect, -8, -8);
        if (state.loading)
        {
            render::DrawGdiText(hdc,
                                resolvedFont,
                                L"Loading histogram...",
                                -1,
                                textRect,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                palette.mutedText,
                                palette.background);
            return;
        }
        if (!state.visible || state.peak == 0)
        {
            render::DrawGdiText(hdc,
                                resolvedFont,
                                L"Histogram unavailable",
                                -1,
                                textRect,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                palette.mutedText,
                                palette.background);
            return;
        }

        const int chartLeft = state.rect.left + 6;
        const int chartTop = state.rect.top + 6;
        const int chartRight = state.rect.right - 6;
        const int chartBottom = state.rect.bottom - 6;
        const int chartWidth = (std::max)(1, chartRight - chartLeft);
        const int chartHeight = (std::max)(1, chartBottom - chartTop);
        const auto drawChannel = [&](const auto& values, COLORREF color)
        {
            const HPEN channelPen = CreatePen(PS_SOLID, 1, color);
            const HGDIOBJ oldChannelPen = SelectObject(hdc, channelPen);
            for (std::size_t index = 0; index < DetailsPanelHistogram::kBinCount; ++index)
            {
                const int x = chartLeft + MulDiv(static_cast<int>(index),
                                                 chartWidth - 1,
                                                 static_cast<int>(DetailsPanelHistogram::kBinCount - 1));
                const int valueHeight = MulDiv(static_cast<int>(values[index]),
                                               chartHeight - 1,
                                               static_cast<int>(state.peak));
                const int y = chartBottom - valueHeight;
                if (index == 0)
                {
                    MoveToEx(hdc, x, y, nullptr);
                }
                else
                {
                    LineTo(hdc, x, y);
                }
            }
            SelectObject(hdc, oldChannelPen);
            DeleteObject(channelPen);
        };
        drawChannel(state.red, kRedChannelColor);
        drawChannel(state.green, kGreenChannelColor);
        drawChannel(state.blue, kBlueChannelColor);
    }
}
