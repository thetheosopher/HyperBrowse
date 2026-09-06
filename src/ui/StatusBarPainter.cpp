#include "ui/StatusBarPainter.h"

#include <d2d1.h>

#include <algorithm>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr int kHorizontalPadding = 12;
    }

    bool StatusBarPainter::PaintD2D(HDC hdc,
                                    const RECT& itemRect,
                                    const StatusBarPainterPalette& palette,
                                    HFONT textFont,
                                    const std::wstring& primaryText,
                                    const std::wstring& secondaryText)
    {
        auto& renderer = render::D2DRenderer::Instance();
        if (!renderer.IsAvailable() || !hdc)
        {
            return false;
        }

        const int width = itemRect.right - itemRect.left;
        const int height = itemRect.bottom - itemRect.top;
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        const auto renderTarget = renderer.CreateDCRenderTarget();
        if (!renderTarget || FAILED(renderTarget->BindDC(hdc, &itemRect)))
        {
            return false;
        }

        const int firstPartWidth = width > 0 ? width / 2 : 420;
        const auto createBrush = [renderTarget](COLORREF color)
        {
            render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
            return brush;
        };

        const auto backgroundBrush = createBrush(palette.background);
        const auto borderBrush = createBrush(palette.border);
        const auto textBrush = createBrush(palette.text);
        const auto mutedTextBrush = createBrush(palette.mutedText);
        const auto textFormat = renderer.CreateTextFormatFromFont(textFont);
        if (!backgroundBrush || !borderBrush || !textBrush || !mutedTextBrush || !textFormat)
        {
            return false;
        }

        renderTarget->BeginDraw();
        renderTarget->Clear(render::ToD2DColor(palette.background));
        renderTarget->FillRectangle(
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
            backgroundBrush.Get());
        renderTarget->DrawLine(
            render::ToD2DPoint(0.0f, 0.5f),
            render::ToD2DPoint(static_cast<float>(width), 0.5f),
            borderBrush.Get());
        renderTarget->DrawLine(
            render::ToD2DPoint(static_cast<float>(firstPartWidth), 5.0f),
            render::ToD2DPoint(static_cast<float>(firstPartWidth), static_cast<float>((std::max)(5, height - 5))),
            borderBrush.Get());

        const RECT primaryRect{kHorizontalPadding,
                              0,
                              (std::max)(kHorizontalPadding, firstPartWidth - kHorizontalPadding),
                              height};
        const RECT secondaryRect{firstPartWidth + kHorizontalPadding,
                                0,
                                (std::max)(firstPartWidth + kHorizontalPadding, width - kHorizontalPadding),
                                height};
        renderTarget->DrawText(
            primaryText.c_str(),
            static_cast<UINT32>(primaryText.size()),
            textFormat.Get(),
            render::ToD2DRect(primaryRect),
            textBrush.Get());
        renderTarget->DrawText(
            secondaryText.c_str(),
            static_cast<UINT32>(secondaryText.size()),
            textFormat.Get(),
            render::ToD2DRect(secondaryRect),
            mutedTextBrush.Get());

        return SUCCEEDED(renderTarget->EndDraw());
    }

    void StatusBarPainter::PaintGdi(HDC hdc,
                                    const RECT& itemRect,
                                    const StatusBarPainterPalette& palette,
                                    HFONT textFont,
                                    const std::wstring& primaryText,
                                    const std::wstring& secondaryText)
    {
        if (!hdc)
        {
            return;
        }

        const int width = itemRect.right - itemRect.left;
        const int firstPartWidth = width > 0 ? width / 2 : 420;
        const HBRUSH backgroundBrush = CreateSolidBrush(palette.background);
        FillRect(hdc, &itemRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.border);
        const HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, itemRect.left, itemRect.top, nullptr);
        LineTo(hdc, itemRect.right, itemRect.top);
        MoveToEx(hdc, itemRect.left + firstPartWidth, itemRect.top + 5, nullptr);
        LineTo(hdc, itemRect.left + firstPartWidth, itemRect.bottom - 5);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        const HFONT resolvedFont = textFont
            ? textFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const RECT primaryRect{itemRect.left + kHorizontalPadding,
                               itemRect.top,
                               itemRect.left + firstPartWidth - kHorizontalPadding,
                               itemRect.bottom};
        render::DrawGdiText(hdc,
                            resolvedFont,
                            primaryText.c_str(),
                            -1,
                            primaryRect,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
                            palette.text,
                            palette.background);

        const RECT secondaryRect{itemRect.left + firstPartWidth + kHorizontalPadding,
                                 itemRect.top,
                                 itemRect.right - kHorizontalPadding,
                                 itemRect.bottom};
        render::DrawGdiText(hdc,
                            resolvedFont,
                            secondaryText.c_str(),
                            -1,
                            secondaryRect,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
                            palette.mutedText,
                            palette.background);
    }
}
