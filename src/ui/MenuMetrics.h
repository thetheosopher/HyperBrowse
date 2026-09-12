#pragma once

#include <windows.h>

#include "util/UiTextSize.h"

namespace hyperbrowse::ui
{
    struct MenuMetrics
    {
        UINT dpi{96};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};

        int popupItemHeightDip{28};
        int popupSeparatorHeightDip{10};
        int popupCheckColumnWidthDip{24};
        int popupTextPaddingDip{12};
        int popupShortcutGapDip{24};
        int popupMeasurementAllowanceDip{8};
        int popupArrowWidthDip{12};

        int commandBarPaddingXDip{8};
        int commandBarItemSizeDip{32};
        int commandBarSeparatorWidthDip{9};
        int commandBarSeparatorGapDip{4};
        int commandBarMenuButtonGapDip{4};
        int commandBarMenuButtonPaddingDip{12};
        int commandBarMenuButtonMinWidthDip{56};
        int commandBarMenuChevronWidthDip{8};
        int commandBarToolbarIconSizeDip{18};
        int commandBarDropdownChevronSizeDip{10};
        int commandBarButtonRadiusDip{10};
        int commandBarFocusRingRadiusDip{10};
        int commandBarFilterRadiusDip{14};

        int ScaleDip(int value) const noexcept
        {
            const int appScaled = hyperbrowse::util::ScaleAppTextDimension(value, appTextSize);
            return MulDiv(appScaled, static_cast<int>(dpi), 96);
        }
    };

    inline MenuMetrics MakeMenuMetrics(
        hyperbrowse::util::AppTextSize appTextSize,
        UINT dpi = 96) noexcept
    {
        MenuMetrics metrics;
        metrics.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        metrics.dpi = (std::max)(96u, dpi);
        return metrics;
    }
}
