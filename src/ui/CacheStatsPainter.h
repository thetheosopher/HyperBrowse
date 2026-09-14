#pragma once

#include <windows.h>

#include <array>
#include <string>
#include <string_view>

struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class CacheStatsPainter final
    {
    public:
        struct Gauge
        {
            std::wstring label;
            std::wstring value;
            float fraction{};
        };

        struct VisualState
        {
            RECT rect{};
            std::array<Gauge, 5> gauges{};
            float hitRate{};
            bool pressureActive{};
        };

        struct State
        {
            RECT bodyRect{};
            std::wstring_view text{};
            int lineHeight{20};
            int headingLineHeight{24};
            VisualState visual{};
        };

        struct Palette
        {
            COLORREF mutedText{};
            COLORREF headingText{};
            COLORREF paneBackground{};
            COLORREF visualBackground{};
            COLORREF meterTrack{};
            COLORREF accent{};
            COLORREF pressure{};
        };

        static void PaintD2D(ID2D1RenderTarget* renderTarget,
                             const State& state,
                             const Palette& palette,
                             IDWriteTextFormat* bodyFormat,
                             IDWriteTextFormat* headingFormat);
        static void PaintGdi(HDC hdc,
                             const State& state,
                             const Palette& palette,
                             HFONT bodyFont,
                             HFONT headingFont);
    };
}
