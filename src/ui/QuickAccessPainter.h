#pragma once

#include <windows.h>

#include <span>

#include "ui/QuickAccessLayout.h"

struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class ToolbarIconLibrary;

    class QuickAccessPainter final
    {
    public:
        struct RowState
        {
            const QuickAccessLayout::Row* row{};
            bool navigationEnabled{};
            bool actionsEnabled{};
        };

        struct State
        {
            RECT headerRect{};
            RECT viewportRect{};
            RECT sortButtonRect{};
            std::span<const RowState> rows{};
            QuickAccessLayout::Metrics metrics{};
            bool sortButtonHot{};
            bool sortButtonPressed{};
            int hotRowIndex{-1};
            int hotButtonIndex{-1};
            int pressedRowIndex{-1};
            int pressedButtonIndex{-1};
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
                             HDC clipDc,
                             const State& state,
                             const Palette& palette,
                             IDWriteTextFormat* summaryFormat,
                             IDWriteTextFormat* bodyFormat);
        static void PaintGdi(HDC hdc,
                             const State& state,
                             const Palette& palette,
                             HFONT summaryFont,
                             HFONT bodyFont,
                             ToolbarIconLibrary* iconLibrary);
    };
}
