#pragma once

#include <windows.h>

#include "ui/SystemTheme.h"
#include "ui/ThemeTokens.h"

namespace hyperbrowse::ui
{
    using DialogTheme = ThemeTokens;

    inline DialogTheme MakeDialogTheme(bool darkTheme) noexcept
    {
        return MakeThemeTokens(ResolveThemeVariant(darkTheme));
    }
}
