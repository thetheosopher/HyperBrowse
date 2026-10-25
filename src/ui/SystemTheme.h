#pragma once

#include <windows.h>

#include <optional>

namespace hyperbrowse::ui
{
    // Tests cannot toggle the Windows high contrast mode, so they force the answer here.
    inline std::optional<bool>& HighContrastOverrideForTesting() noexcept
    {
        static std::optional<bool> override;
        return override;
    }

    inline bool IsHighContrastEnabled() noexcept
    {
        if (const std::optional<bool>& forced = HighContrastOverrideForTesting())
        {
            return *forced;
        }

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
