#pragma once

#include <windows.h>

namespace hyperbrowse::ui
{
    struct DialogTheme
    {
        COLORREF windowBackground;
        COLORREF surfaceBackground;
        COLORREF fieldBackground;
        COLORREF text;
        COLORREF mutedText;
        COLORREF border;
        COLORREF accent;
        COLORREF accentFill;
        COLORREF accentText;
    };

    inline DialogTheme MakeDialogTheme(bool darkTheme) noexcept
    {
        if (darkTheme)
        {
            return DialogTheme{
                RGB(24, 28, 32),
                RGB(34, 38, 43),
                RGB(21, 25, 30),
                RGB(234, 238, 242),
                RGB(140, 148, 158),
                RGB(74, 82, 92),
                RGB(112, 169, 227),
                RGB(47, 68, 92),
                RGB(244, 248, 252),
            };
        }

        return DialogTheme{
            RGB(243, 245, 248),
            RGB(255, 255, 255),
            RGB(255, 255, 255),
            RGB(32, 36, 40),
            RGB(128, 136, 148),
            RGB(210, 215, 223),
            RGB(54, 114, 186),
            RGB(220, 233, 247),
            RGB(25, 35, 50),
        };
    }
}
