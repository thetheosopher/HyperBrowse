#include "ui/DialogShell.h"

#include <algorithm>
#include <cstdint>

#include "ui/DialogDpi.h"

namespace hyperbrowse::ui
{
    DialogShellMetrics MeasureDialogShellMetrics(HWND ownerWindow,
                                                  hyperbrowse::util::AppTextSize appTextSize)
    {
        DialogShellMetrics metrics;
        metrics.dpi = DialogDpiForWindow(ownerWindow);
        metrics.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        const HWND referenceWindow = ownerWindow ? ownerWindow : GetDesktopWindow();
        const HMONITOR monitor = MonitorFromWindow(referenceWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (monitor && GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
        {
            metrics.workArea = monitorInfo.rcWork;
        }
        else
        {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &metrics.workArea, 0);
        }
        metrics.workAreaWidth = std::max(1, static_cast<int>(metrics.workArea.right - metrics.workArea.left));
        metrics.workAreaHeight = std::max(1, static_cast<int>(metrics.workArea.bottom - metrics.workArea.top));
        metrics.workAreaWidthDip = MulDiv(metrics.workAreaWidth, 96, static_cast<int>(metrics.dpi));
        metrics.workAreaHeightDip = MulDiv(metrics.workAreaHeight, 96, static_cast<int>(metrics.dpi));
        metrics.marginDip = metrics.Scale(28);
        metrics.rowGapDip = metrics.Scale(12);
        metrics.controlHeightDip = metrics.Scale(38);
        metrics.buttonHeightDip = metrics.Scale(38);
        metrics.buttonGapDip = metrics.Scale(12);
        return metrics;
    }

    RECT ClampDialogFrameToWorkArea(const RECT& frame,
                                    const RECT& workArea) noexcept
    {
        const int workWidth = std::max(1, static_cast<int>(workArea.right - workArea.left));
        const int workHeight = std::max(1, static_cast<int>(workArea.bottom - workArea.top));
        const int width = std::min(workWidth, std::max(1, static_cast<int>(frame.right - frame.left)));
        const int height = std::min(workHeight, std::max(1, static_cast<int>(frame.bottom - frame.top)));
        RECT result{frame.left, frame.top, frame.left + width, frame.top + height};
        result.left = std::clamp(result.left, workArea.left, workArea.right - width);
        result.top = std::clamp(result.top, workArea.top, workArea.bottom - height);
        result.right = result.left + width;
        result.bottom = result.top + height;
        return result;
    }

    void CenterDialogInWorkArea(HWND window,
                                const RECT& workArea) noexcept
    {
        if (!window)
        {
            return;
        }
        RECT dialogRect{};
        GetWindowRect(window, &dialogRect);
        const int width = dialogRect.right - dialogRect.left;
        const int height = dialogRect.bottom - dialogRect.top;
        const int x = workArea.left + std::max(0L, (workArea.right - workArea.left - width) / 2);
        const int y = workArea.top + std::max(0L, (workArea.bottom - workArea.top - height) / 2);
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
