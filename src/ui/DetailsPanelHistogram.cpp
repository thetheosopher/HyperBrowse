#include "ui/DetailsPanelHistogram.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace hyperbrowse::ui
{
    bool DetailsPanelHistogram::Compute(HBITMAP bitmap, Result* result)
    {
        if (!result)
        {
            return false;
        }

        *result = Result{};
        if (!bitmap)
        {
            return false;
        }

        BITMAP bitmapObject{};
        if (GetObjectW(bitmap, sizeof(bitmapObject), &bitmapObject) == 0)
        {
            return false;
        }

        const int bitmapWidth = bitmapObject.bmWidth;
        const int bitmapHeight = std::abs(bitmapObject.bmHeight);
        if (bitmapWidth <= 0 || bitmapHeight <= 0)
        {
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = bitmapWidth;
        bitmapInfo.bmiHeader.biHeight = -bitmapHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        std::vector<RGBQUAD> pixels(static_cast<std::size_t>(bitmapWidth) * static_cast<std::size_t>(bitmapHeight));
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return false;
        }

        const int copiedScanLines = GetDIBits(screenDc,
                                              bitmap,
                                              0,
                                              static_cast<UINT>(bitmapHeight),
                                              pixels.data(),
                                              &bitmapInfo,
                                              DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);
        if (copiedScanLines == 0)
        {
            return false;
        }

        for (const RGBQUAD& pixel : pixels)
        {
            const std::size_t redIndex = (std::min)(kBinCount - 1, (pixel.rgbRed * kBinCount) / 256);
            const std::size_t greenIndex = (std::min)(kBinCount - 1, (pixel.rgbGreen * kBinCount) / 256);
            const std::size_t blueIndex = (std::min)(kBinCount - 1, (pixel.rgbBlue * kBinCount) / 256);

            result->red[redIndex] += 1;
            result->green[greenIndex] += 1;
            result->blue[blueIndex] += 1;
        }

        for (std::size_t index = 0; index < kBinCount; ++index)
        {
            result->peak = (std::max)(result->peak, result->red[index]);
            result->peak = (std::max)(result->peak, result->green[index]);
            result->peak = (std::max)(result->peak, result->blue[index]);
        }

        result->visible = result->peak > 0;
        return true;
    }
}
