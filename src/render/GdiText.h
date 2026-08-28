#pragma once

#include <windows.h>

namespace hyperbrowse::render
{
    inline void DrawGdiText(HDC hdc,
                            HFONT font,
                            const wchar_t* text,
                            int textLength,
                            RECT rect,
                            UINT format,
                            COLORREF textColor,
                            COLORREF backgroundColor)
    {
        if (!hdc || !text)
        {
            return;
        }

        const int savedDc = SaveDC(hdc);
        if (savedDc == 0)
        {
            return;
        }

        if (font)
        {
            SelectObject(hdc, font);
        }
        SetTextColor(hdc, textColor);
        SetBkColor(hdc, backgroundColor);
        SetBkMode(hdc, OPAQUE);
        DrawTextW(hdc, text, textLength, &rect, format);
        RestoreDC(hdc, savedDc);
    }
}