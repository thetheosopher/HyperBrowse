#include "ui/DetailsPanelHistogram.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace hyperbrowse::ui
{
    bool DetailsPanelHistogram::Compute(HBITMAP bitmap, Result* result, const Cancelled& cancelled)
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
        if (cancelled && cancelled()) return false;
        if (bitmapObject.bmBits && bitmapObject.bmBitsPixel == 32 && bitmapObject.bmWidthBytes > 0)
        {
            return ComputePixels(static_cast<const unsigned char*>(bitmapObject.bmBits), bitmapWidth,
                bitmapHeight, static_cast<std::size_t>(bitmapObject.bmWidthBytes), result, cancelled);
        }
        if (static_cast<std::size_t>(bitmapWidth) > (std::numeric_limits<std::size_t>::max)()
            / sizeof(RGBQUAD) / static_cast<std::size_t>(bitmapHeight)) return false;

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
        if (copiedScanLines != bitmapHeight)
        {
            return false;
        }

        return ComputePixels(reinterpret_cast<const unsigned char*>(pixels.data()), bitmapWidth, bitmapHeight,
            static_cast<std::size_t>(bitmapWidth) * sizeof(RGBQUAD), result, cancelled);
    }

    bool DetailsPanelHistogram::ComputePixels(const unsigned char* bgra, int width, int height,
                                              std::size_t stride, Result* result, const Cancelled& cancelled)
    {
        if (!result) return false;
        *result = {};
        if (!bgra || width <= 0 || height <= 0
            || static_cast<std::size_t>(width) > (std::numeric_limits<std::size_t>::max)() / 4
            || stride < static_cast<std::size_t>(width) * 4
            || stride > (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(height)) return false;
        for (int y = 0; y < height; ++y)
        {
            if (cancelled && cancelled())
            {
                *result = {};
                return false;
            }
            const auto* row = bgra + static_cast<std::size_t>(y) * stride;
            for (int x = 0; x < width; ++x)
            {
                const auto* pixel = row + static_cast<std::size_t>(x) * 4;
                ++result->blue[pixel[0]];
                ++result->green[pixel[1]];
                ++result->red[pixel[2]];
            }
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
