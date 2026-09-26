#include "viewer/CompareSessionPolicy.h"

#include <algorithm>
#include <cmath>

namespace hyperbrowse::viewer
{
    namespace
    {
        LONG RectWidth(const RECT& rect) noexcept
        {
            return std::max<LONG>(0, rect.right - rect.left);
        }

        LONG RectHeight(const RECT& rect) noexcept
        {
            return std::max<LONG>(0, rect.bottom - rect.top);
        }

        LONG ClampGap(LONG requestedGap, LONG extent) noexcept
        {
            return std::clamp<LONG>(requestedGap, 0, std::max<LONG>(0, extent - 2));
        }

        double ClampNormalized(double value) noexcept
        {
            return std::clamp(value, 0.0, 1.0);
        }
    }

    std::vector<RECT> CompareTileBounds(const RECT& clientRect, std::size_t tileCount, LONG requestedGap)
    {
        if (tileCount < 2 || tileCount > 4 || clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top)
        {
            return {};
        }

        const LONG width = RectWidth(clientRect);
        const LONG height = RectHeight(clientRect);
        const LONG gapX = ClampGap(requestedGap, width);
        const LONG leftWidth = (width - gapX) / 2;
        const LONG rightStart = clientRect.left + leftWidth + gapX;
        if (tileCount == 2)
        {
            return {
                RECT{clientRect.left, clientRect.top, clientRect.left + leftWidth, clientRect.bottom},
                RECT{rightStart, clientRect.top, clientRect.right, clientRect.bottom}};
        }

        const LONG gapY = ClampGap(requestedGap, height);
        const LONG topHeight = (height - gapY) / 2;
        const LONG bottomStart = clientRect.top + topHeight + gapY;
        std::vector<RECT> bounds{
            RECT{clientRect.left, clientRect.top, clientRect.left + leftWidth, clientRect.top + topHeight},
            RECT{rightStart, clientRect.top, clientRect.right, clientRect.top + topHeight},
            RECT{clientRect.left, bottomStart, clientRect.left + leftWidth, clientRect.bottom}};
        if (tileCount == 4)
        {
            bounds.push_back(RECT{rightStart, bottomStart, clientRect.right, clientRect.bottom});
        }
        return bounds;
    }

    int HitTestCompareTile(std::span<const RECT> tileBounds, POINT point) noexcept
    {
        for (std::size_t index = 0; index < tileBounds.size(); ++index)
        {
            const RECT& bounds = tileBounds[index];
            if (point.x >= bounds.left && point.x < bounds.right
                && point.y >= bounds.top && point.y < bounds.bottom)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    int NextAvailableCompareCandidate(int currentIndex,
                                      std::span<const int> visibleIndices,
                                      int candidateCount,
                                      int direction) noexcept
    {
        if (candidateCount < 2 || currentIndex < 0 || currentIndex >= candidateCount || direction == 0)
        {
            return -1;
        }

        for (int step = 1; step < candidateCount; ++step)
        {
            int candidateIndex = (currentIndex + (direction > 0 ? step : -step)) % candidateCount;
            if (candidateIndex < 0)
            {
                candidateIndex += candidateCount;
            }
            if (std::find(visibleIndices.begin(), visibleIndices.end(), candidateIndex) == visibleIndices.end())
            {
                return candidateIndex;
            }
        }
        return -1;
    }

    NormalizedImageCenter ImageCenterFromPan(int imageWidth,
                                             int imageHeight,
                                             double scale,
                                             double panX,
                                             double panY) noexcept
    {
        if (imageWidth <= 0 || imageHeight <= 0 || !std::isfinite(scale) || scale <= 0.0)
        {
            return {};
        }

        return {
            ClampNormalized(0.5 - (panX / (static_cast<double>(imageWidth) * scale))),
            ClampNormalized(0.5 - (panY / (static_cast<double>(imageHeight) * scale)))};
    }

    POINT PanFromImageCenter(int imageWidth,
                             int imageHeight,
                             double scale,
                             const RECT& clientRect,
                             NormalizedImageCenter center) noexcept
    {
        if (imageWidth <= 0 || imageHeight <= 0 || !std::isfinite(scale) || scale <= 0.0)
        {
            return {};
        }

        const double scaledWidth = static_cast<double>(imageWidth) * scale;
        const double scaledHeight = static_cast<double>(imageHeight) * scale;
        const double maxPanX = std::max(0.0, (scaledWidth - static_cast<double>(RectWidth(clientRect))) / 2.0);
        const double maxPanY = std::max(0.0, (scaledHeight - static_cast<double>(RectHeight(clientRect))) / 2.0);
        const double panX = std::clamp((0.5 - ClampNormalized(center.x)) * scaledWidth, -maxPanX, maxPanX);
        const double panY = std::clamp((0.5 - ClampNormalized(center.y)) * scaledHeight, -maxPanY, maxPanY);
        return POINT{static_cast<LONG>(std::lround(panX)), static_cast<LONG>(std::lround(panY))};
    }
}
