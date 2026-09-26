#pragma once

#include <windows.h>

#include <cstddef>
#include <span>
#include <vector>

namespace hyperbrowse::viewer
{
    struct NormalizedImageCenter
    {
        double x{0.5};
        double y{0.5};
    };

    std::vector<RECT> CompareTileBounds(const RECT& clientRect, std::size_t tileCount, LONG requestedGap);
    int HitTestCompareTile(std::span<const RECT> tileBounds, POINT point) noexcept;
    int NextAvailableCompareCandidate(int currentIndex,
                                     std::span<const int> visibleIndices,
                                     int candidateCount,
                                     int direction) noexcept;
    NormalizedImageCenter ImageCenterFromPan(int imageWidth,
                                            int imageHeight,
                                            double scale,
                                            double panX,
                                            double panY) noexcept;
    POINT PanFromImageCenter(int imageWidth,
                             int imageHeight,
                             double scale,
                             const RECT& clientRect,
                             NormalizedImageCenter center) noexcept;
}
