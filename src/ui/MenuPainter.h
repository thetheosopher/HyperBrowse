#pragma once

#include <windows.h>

#include <memory>
#include <vector>

#include "ui/MenuMessageHandling.h"
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

        void MeasureOwnerDrawMenuItem(
            MEASUREITEMSTRUCT* measureItem,
            hyperbrowse::util::AppTextSize appTextSize,
            HFONT menuFont) const;

        void DrawOwnerDrawMenuItem(
            const DRAWITEMSTRUCT& drawItem,
            const MenuPainterPalette& palette,
            hyperbrowse::util::AppTextSize appTextSize,
            HFONT menuFont,
            bool darkTheme) const;
    };
}
