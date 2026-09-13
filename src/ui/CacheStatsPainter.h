#pragma once

#include <windows.h>

#include <string_view>

struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class CacheStatsPainter final
    {
    public:
        struct State
        {
            RECT bodyRect{};
            std::wstring_view text{};
            int lineHeight{20};
            int headingLineHeight{24};
        };

        struct Palette
        {
            COLORREF mutedText{};
            COLORREF headingText{};
            COLORREF paneBackground{};
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
