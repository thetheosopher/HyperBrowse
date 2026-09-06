#pragma once

#include <windows.h>

struct ID2D1RenderTarget;

namespace hyperbrowse::ui
{
    struct ShellPainterPalette
    {
        COLORREF windowBackground{};
        COLORREF splitter{};
        COLORREF actionStripBorder{};
    };

    struct ShellPainterGeometry
    {
        int leftPaneWidth{};
        int actionStripHeight{};
        int splitterWidth{};
        bool detailsPanelVisible{};
        RECT detailsPanelRect{};
    };

    class ShellPainter final
    {
    public:
        static void PaintD2D(ID2D1RenderTarget* renderTarget,
                             const RECT& clientRect,
                             const ShellPainterPalette& palette,
                             const ShellPainterGeometry& geometry);
        static void PaintGdi(HDC hdc,
                             const RECT& clientRect,
                             const ShellPainterPalette& palette,
                             const ShellPainterGeometry& geometry);
    };
}
