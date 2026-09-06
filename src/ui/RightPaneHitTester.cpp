#include "ui/RightPaneHitTester.h"

namespace hyperbrowse::ui
{
    int RightPaneHitTester::Tab(bool panelVisible,
                                 const RECT& tabStripRect,
                                 const std::array<RECT, 2>& tabRects,
                                 int x,
                                 int y)
    {
        if (!panelVisible || IsRectEmpty(&tabStripRect))
        {
            return -1;
        }

        const POINT point{x, y};
        for (std::size_t index = 0; index < tabRects.size(); ++index)
        {
            if (!IsRectEmpty(&tabRects[index]) && PtInRect(&tabRects[index], point) != FALSE)
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    int RightPaneHitTester::CloseButton(bool panelVisible, const RECT& closeButtonRect, int x, int y)
    {
        if (!panelVisible || IsRectEmpty(&closeButtonRect))
        {
            return -1;
        }

        const POINT point{x, y};
        return PtInRect(&closeButtonRect, point) != FALSE ? 0 : -1;
    }

    int RightPaneHitTester::SortButton(const RECT& sortButtonRect, int x, int y)
    {
        if (IsRectEmpty(&sortButtonRect))
        {
            return -1;
        }

        const POINT point{x, y};
        return PtInRect(&sortButtonRect, point) != FALSE ? 0 : -1;
    }
}
