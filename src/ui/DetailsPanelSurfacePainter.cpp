#include "ui/DetailsPanelSurfacePainter.h"

#include <d2d1.h>

#include "render/D2DRenderer.h"

namespace hyperbrowse::ui
{
    void DetailsPanelSurfacePainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                              const RECT& panelRect,
                                              ID2D1Brush* panelBrush,
                                              ID2D1Brush* borderBrush)
    {
        if (!renderTarget || !panelBrush || !borderBrush || IsRectEmpty(&panelRect))
        {
            return;
        }

        renderTarget->FillRectangle(render::ToD2DRect(panelRect), panelBrush);
        renderTarget->DrawLine(
            render::ToD2DPoint(static_cast<float>(panelRect.left) + 0.5f, static_cast<float>(panelRect.top)),
            render::ToD2DPoint(static_cast<float>(panelRect.left) + 0.5f, static_cast<float>(panelRect.bottom)),
            borderBrush);
    }

    void DetailsPanelSurfacePainter::PaintGdi(HDC hdc,
                                              const RECT& panelRect,
                                              HBRUSH panelBrush,
                                              HBRUSH fallbackBrush,
                                              COLORREF borderColor)
    {
        if (!hdc || IsRectEmpty(&panelRect))
        {
            return;
        }

        FillRect(hdc,
                 &panelRect,
                 panelBrush ? panelBrush : (fallbackBrush ? fallbackBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)));

        const HPEN borderPen = CreatePen(PS_SOLID, 1, borderColor);
        const HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, panelRect.left, panelRect.top, nullptr);
        LineTo(hdc, panelRect.left, panelRect.bottom);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);
    }
}
