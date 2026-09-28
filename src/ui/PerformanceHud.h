#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace hyperbrowse::ui
{
    struct PerformanceHudSnapshot
    {
        std::size_t activeDecodes{};
        std::optional<double> scaleAverageMs;
        std::optional<double> thumbnailCacheHitRatePercent;
        bool memoryPressureActive{};
        std::size_t pendingThumbnailJobs{};
    };

    std::wstring FormatPerformanceHudText(const PerformanceHudSnapshot& snapshot);
}
