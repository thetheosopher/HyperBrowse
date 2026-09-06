#pragma once

#include <windows.h>

struct ID2D1Brush;
struct ID2D1RenderTarget;

namespace hyperbrowse::ui
{
    class DetailsPanelSurfacePainter final
    {
    public:
        static void PaintD2D(ID2D1RenderTarget* renderTarget,
                             const RECT& panelRect,
                             ID2D1Brush* panelBrush,
                             ID2D1Brush* borderBrush);
        static void PaintGdi(HDC hdc,
                             const RECT& panelRect,
                             HBRUSH panelBrush,
                             HBRUSH fallbackBrush,
                             COLORREF borderColor);
    };
}
