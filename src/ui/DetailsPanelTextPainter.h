#pragma once

#include <windows.h>

#include <string_view>

struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class DetailsPanelTextPainter final
    {
    public:
        struct State
        {
            RECT titleRect{};
            RECT summaryRect{};
            RECT emptyStateRect{};
            std::wstring_view titleText{};
            std::wstring_view summaryText{};
            std::wstring_view emptyStateText{};
        };

        struct Palette
        {
            COLORREF text{};
            COLORREF mutedText{};
            COLORREF paneBackground{};
        };

        static void PaintD2D(ID2D1RenderTarget* renderTarget,
                             const State& state,
                             const Palette& palette,
                             IDWriteTextFormat* titleFormat,
                             IDWriteTextFormat* summaryFormat);
        static void PaintGdi(HDC hdc,
                             const State& state,
                             const Palette& palette,
                             HFONT titleFont,
                             HFONT summaryFont);
    };
}
