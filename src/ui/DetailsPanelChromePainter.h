#pragma once

#include <windows.h>

#include <array>

struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class DetailsPanelChromePainter final
    {
    public:
        struct State
        {
            RECT tabStripRect{};
            std::array<RECT, 2> tabRects{};
            int activeTabIndex{-1};
            int hotTabIndex{-1};
            int pressedTabIndex{-1};
            RECT closeButtonRect{};
            bool closeButtonHot{};
            bool closeButtonPressed{};
        };

        struct Palette
        {
            COLORREF actionFieldBackground{};
            COLORREF paneBackground{};
            COLORREF actionStripBorder{};
            COLORREF accent{};
            COLORREF accentFill{};
            COLORREF accentText{};
            COLORREF text{};
            COLORREF mutedText{};
            bool darkTheme{};
        };

        static void PaintD2D(ID2D1RenderTarget* renderTarget,
                             const State& state,
                             const Palette& palette,
                             IDWriteTextFormat* tabFormat);
        static void PaintGdi(HDC hdc,
                             const State& state,
                             const Palette& palette,
                             HFONT textFont);
    };
}
