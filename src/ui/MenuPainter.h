#pragma once

#include <windows.h>

#include <memory>
#include <vector>

#include "ui/MenuMessageHandling.h"
#include "ui/MenuMetrics.h"
#include "util/UiTextSize.h"

namespace hyperbrowse::ui
{
    struct MenuPainterPalette
    {
        COLORREF windowBackground{};
        COLORREF paneBackground{};
        COLORREF text{};
        COLORREF mutedText{};
        COLORREF actionStripBackground{};
        COLORREF actionStripBorder{};
        COLORREF accent{};
        COLORREF accentFill{};
        COLORREF accentText{};
    };

    class MenuPainter final
    {
    public:
        void PrepareMenuForOwnerDraw(
            HMENU menu,
            std::vector<std::unique_ptr<MenuDrawItemData>>& storage,
            bool ownerDrawCurrentLevel) const;

        void RefreshMenuMeasurements(HMENU menu) const;

        void MeasureOwnerDrawMenuItem(
            MEASUREITEMSTRUCT* measureItem,
            const MenuMetrics& metrics,
            HFONT menuFont) const;

        void DrawOwnerDrawMenuItem(
            const DRAWITEMSTRUCT& drawItem,
            const MenuPainterPalette& palette,
            const MenuMetrics& metrics,
            HFONT menuFont,
            bool darkTheme) const;
    };
}
