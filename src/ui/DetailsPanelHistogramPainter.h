#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

#include "ui/DetailsPanelHistogram.h"

struct ID2D1Brush;
struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class DetailsPanelHistogramPainter final
    {
    public:
        struct State
        {
            RECT rect{};
            std::array<std::uint32_t, DetailsPanelHistogram::kBinCount> red{};
            std::array<std::uint32_t, DetailsPanelHistogram::kBinCount> green{};
            std::array<std::uint32_t, DetailsPanelHistogram::kBinCount> blue{};
            std::uint32_t peak{};
            bool visible{};
            bool loading{};
        };

        struct Palette
        {
            COLORREF background{};
            COLORREF border{};
            COLORREF mutedText{};
        };

        static void PaintD2D(ID2D1RenderTarget* renderTarget,
                             const State& state,
                             const Palette& palette,
                             ID2D1Brush* borderBrush,
                             ID2D1Brush* mutedBrush,
                             IDWriteTextFormat* textFormat);
        static void PaintGdi(HDC hdc,
                             const State& state,
                             const Palette& palette,
                             HFONT textFont);
    };
}
