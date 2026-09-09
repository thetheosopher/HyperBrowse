#pragma once

#include <windows.h>

namespace hyperbrowse::ui
{
    inline bool IsHighContrastEnabled() noexcept
    {
        HIGHCONTRASTW highContrast{};
        highContrast.cbSize = sizeof(highContrast);
        return SystemParametersInfoW(
                   SPI_GETHIGHCONTRAST,
                   sizeof(highContrast),
                   &highContrast,
                   0) != FALSE
            && (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
}
