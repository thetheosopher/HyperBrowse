#include "ui/DetailsPanelTextPainter.h"

#include <d2d1.h>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"

namespace hyperbrowse::ui
{
    void DetailsPanelTextPainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                           const State& state,
                                           const Palette& palette,
                                           IDWriteTextFormat* titleFormat,
                                           IDWriteTextFormat* summaryFormat)
    {
        if (!renderTarget || !titleFormat || !summaryFormat)
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

        const auto textBrush = createBrush(palette.text);
        drawText(state.titleText, titleFormat, state.titleRect, textBrush.Get());

        const auto mutedBrush = createBrush(palette.mutedText);
        drawText(state.summaryText, summaryFormat, state.summaryRect, mutedBrush.Get());
        drawText(state.emptyStateText, summaryFormat, state.emptyStateRect, mutedBrush.Get());
    }

    void DetailsPanelTextPainter::PaintGdi(HDC hdc,
                                           const State& state,
                                           const Palette& palette,
                                           HFONT titleFont,
                                           HFONT summaryFont)
    {
        if (!hdc)
        {
            return;
        }

        const HFONT resolvedTitleFont = titleFont
            ? titleFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HFONT resolvedSummaryFont = summaryFont
            ? summaryFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const auto drawText = [hdc, palette](HFONT font,
                                             std::wstring_view text,
                                             const RECT& rect,
                                             COLORREF textColor)
        {
            if (!text.empty() && rect.right > rect.left && rect.bottom > rect.top)
            {
                render::DrawGdiText(hdc,
                                    font,
                                    text.data(),
                                    static_cast<int>(text.size()),
                                    rect,
                                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                    textColor,
                                    palette.paneBackground);
            }
        };
        drawText(resolvedTitleFont, state.titleText, state.titleRect, palette.text);
        drawText(resolvedSummaryFont, state.summaryText, state.summaryRect, palette.mutedText);
        drawText(resolvedSummaryFont, state.emptyStateText, state.emptyStateRect, palette.mutedText);
    }
}
