#include "ui/ShellPainter.h"

#include <d2d1.h>

#include <algorithm>

#include "render/D2DRenderer.h"

namespace hyperbrowse::ui
{
    namespace
    {
        template <typename PaintSplitter>
        void ForEachSplitter(const RECT& clientRect,
                             const ShellPainterGeometry& geometry,
                             PaintSplitter&& paintSplitter)
        {
            paintSplitter(RECT{geometry.leftPaneWidth,
                               geometry.actionStripHeight,
                               geometry.leftPaneWidth + geometry.splitterWidth,
                               clientRect.bottom});
            if (geometry.detailsPanelVisible && !IsRectEmpty(&geometry.detailsPanelRect))
            {
                paintSplitter(RECT{geometry.detailsPanelRect.left - geometry.splitterWidth,
                                   geometry.actionStripHeight,
                                   geometry.detailsPanelRect.left,
                                   clientRect.bottom});
            }
        }
    }

    void ShellPainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                const RECT& clientRect,
                                const ShellPainterPalette& palette,
                                const ShellPainterGeometry& geometry)
    {
        if (!renderTarget)
        {
            return;
        }

        renderTarget->Clear(render::ToD2DColor(palette.windowBackground));
        const auto createBrush = [renderTarget](COLORREF color)
        {
            render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
            return brush;
        };
        const auto splitterBrush = createBrush(palette.splitter);
        const auto gripBrush = createBrush(palette.actionStripBorder);
        ForEachSplitter(clientRect, geometry, [&](const RECT& splitterRect)
        {
            if (splitterBrush)
            {
                renderTarget->FillRectangle(render::ToD2DRect(splitterRect), splitterBrush.Get());
            }
            if (gripBrush)
            {
                const float gripX = static_cast<float>((splitterRect.left + splitterRect.right) / 2);
                const float gripTop = static_cast<float>(splitterRect.top + 20);
                const float gripBottom = static_cast<float>((std::max)(
                    static_cast<int>(splitterRect.top) + 32,
                    static_cast<int>(splitterRect.bottom) - 20));
                renderTarget->DrawLine(render::ToD2DPoint(gripX, gripTop),
                                       render::ToD2DPoint(gripX, gripBottom),
                                       gripBrush.Get());
            }
        });
    }

    void ShellPainter::PaintGdi(HDC hdc,
                                const RECT& clientRect,
                                const ShellPainterPalette& palette,
                                const ShellPainterGeometry& geometry)
    {
        if (!hdc)
        {
            return;
        }

        const HBRUSH backgroundBrush = CreateSolidBrush(palette.windowBackground);
        FillRect(hdc, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        ForEachSplitter(clientRect, geometry, [&](const RECT& splitterRect)
        {
            const HBRUSH splitterBrush = CreateSolidBrush(palette.splitter);
            FillRect(hdc, &splitterRect, splitterBrush);
            DeleteObject(splitterBrush);

            const HPEN gripPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
            const HGDIOBJ oldPen = SelectObject(hdc, gripPen);
            const int gripX = (splitterRect.left + splitterRect.right) / 2;
            const int gripTop = splitterRect.top + 20;
            const int gripBottom = (std::max)(gripTop + 12, static_cast<int>(splitterRect.bottom) - 20);
            MoveToEx(hdc, gripX, gripTop, nullptr);
            LineTo(hdc, gripX, gripBottom);
            SelectObject(hdc, oldPen);
            DeleteObject(gripPen);
        });
    }
}
