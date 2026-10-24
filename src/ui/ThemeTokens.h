#pragma once

#include <windows.h>

#include "ui/SystemTheme.h"

namespace hyperbrowse::ui
{
    enum class ThemeVariant
    {
        Light,
        Dark,
        HighContrast,
    };

    // Colors shared by every window. Window-specific colors stay with their window, but any value
    // that is identical across windows should come from here so a palette change happens once.
    struct ThemeTokens
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

    inline ThemeVariant ResolveThemeVariant(bool darkTheme) noexcept
    {
        if (IsHighContrastEnabled())
        {
            return ThemeVariant::HighContrast;
        }

        return darkTheme ? ThemeVariant::Dark : ThemeVariant::Light;
    }

    inline ThemeTokens MakeThemeTokens(ThemeVariant variant) noexcept
    {
        switch (variant)
        {
        case ThemeVariant::HighContrast:
            return ThemeTokens{
                GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_WINDOWTEXT),
                GetSysColor(COLOR_GRAYTEXT),
                GetSysColor(COLOR_WINDOWTEXT),
                GetSysColor(COLOR_HIGHLIGHT),
                GetSysColor(COLOR_HIGHLIGHT),
                GetSysColor(COLOR_HIGHLIGHTTEXT),
            };
        case ThemeVariant::Dark:
            return ThemeTokens{
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
        case ThemeVariant::Light:
        default:
            return ThemeTokens{
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
}
