#pragma once

#include <windows.h>

#include <array>

namespace hyperbrowse::ui
{
    class RightPaneHitTester final
    {
    public:
        static int Tab(bool panelVisible,
                       const RECT& tabStripRect,
                       const std::array<RECT, 2>& tabRects,
                       int x,
                       int y);
        static int CloseButton(bool panelVisible, const RECT& closeButtonRect, int x, int y);
        static int SortButton(const RECT& sortButtonRect, int x, int y);
    };
}
