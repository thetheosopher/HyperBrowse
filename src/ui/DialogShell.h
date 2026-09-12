#pragma once

#include <windows.h>

#include "util/UiTextSize.h"

namespace hyperbrowse::ui
{
    struct DialogShellMetrics
    {
        UINT dpi{96};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        RECT workArea{};
        int workAreaWidth{};
        int workAreaHeight{};
        int workAreaWidthDip{};
        int workAreaHeightDip{};
        int marginDip{};
        int rowGapDip{};
        int controlHeightDip{};
        int buttonHeightDip{};
        int buttonGapDip{};

        int Scale(int value) const noexcept
        {
            return hyperbrowse::util::ScaleAppTextDimension(value, appTextSize);
        }

        int ToPhysical(int value) const noexcept
        {
            return MulDiv(value, static_cast<int>(dpi), 96);
        }
    };

    DialogShellMetrics MeasureDialogShellMetrics(HWND ownerWindow,
                                                  hyperbrowse::util::AppTextSize appTextSize);

    RECT ClampDialogFrameToWorkArea(const RECT& frame,
                                    const RECT& workArea) noexcept;

    void CenterDialogInWorkArea(HWND window,
                                const RECT& workArea) noexcept;
}
