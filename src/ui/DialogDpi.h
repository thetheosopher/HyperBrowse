#pragma once

#include <windows.h>

#include <algorithm>

#include "util/UiTextSize.h"

namespace hyperbrowse::ui
{
    constexpr UINT kDefaultDpi = 96;

    inline UINT DialogDpiForWindow(HWND window) noexcept
    {
        const UINT dpi = window ? GetDpiForWindow(window) : 0;
        return (std::max)(kDefaultDpi, dpi == 0 ? kDefaultDpi : dpi);
    }

    inline int ScaleDialogDimension(int dimension, UINT dpi) noexcept
    {
        return MulDiv(dimension, static_cast<int>(dpi == 0 ? kDefaultDpi : dpi), kDefaultDpi);
    }

    inline int ScaleDialogAppTextDimension(int dimension,
                                           hyperbrowse::util::AppTextSize textSize,
                                           UINT dpi) noexcept
    {
        return ScaleDialogDimension(hyperbrowse::util::ScaleAppTextDimension(dimension, textSize), dpi);
    }

    inline void AdjustDialogWindowRectForDpi(RECT* rect,
                                             DWORD style,
                                             BOOL hasMenu,
                                             DWORD exStyle,
                                             UINT dpi) noexcept
    {
        if (!rect)
        {
            return;
        }

        if (AdjustWindowRectExForDpi(rect,
                                     style,
                                     hasMenu,
                                     exStyle,
                                     dpi == 0 ? kDefaultDpi : dpi) == FALSE)
        {
            AdjustWindowRectEx(rect, style, hasMenu, exStyle);
        }
    }
}
