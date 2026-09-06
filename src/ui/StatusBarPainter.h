#pragma once

#include <windows.h>

#include <string>

namespace hyperbrowse::ui
{
    struct StatusBarPainterPalette
    {
        COLORREF background{};
        COLORREF border{};
        COLORREF text{};
        COLORREF mutedText{};
    };

    class StatusBarPainter final
    {
    public:
        static bool PaintD2D(HDC hdc,
                             const RECT& itemRect,
                             const StatusBarPainterPalette& palette,
                             HFONT textFont,
                             const std::wstring& primaryText,
                             const std::wstring& secondaryText);
        static void PaintGdi(HDC hdc,
                             const RECT& itemRect,
                             const StatusBarPainterPalette& palette,
                             HFONT textFont,
                             const std::wstring& primaryText,
                             const std::wstring& secondaryText);
    };
}
