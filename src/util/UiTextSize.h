#pragma once

#include <cstdint>

namespace hyperbrowse::util
{
    enum class AppTextSize : int
    {
        Small = 0,
        Medium = 1,
        Large = 2,
    };

    constexpr AppTextSize kDefaultAppTextSize = AppTextSize::Medium;

    constexpr bool IsValidAppTextSizeValue(std::uint32_t value) noexcept
    {
        return value <= static_cast<std::uint32_t>(AppTextSize::Large);
    }

    constexpr AppTextSize NormalizeAppTextSize(std::uint32_t value) noexcept
    {
        return IsValidAppTextSizeValue(value)
            ? static_cast<AppTextSize>(value)
            : kDefaultAppTextSize;
    }

    constexpr float AppTextSizeScale(AppTextSize size) noexcept
    {
        switch (size)
        {
        case AppTextSize::Small:
            return 0.90f;
        case AppTextSize::Large:
            return 1.15f;
        case AppTextSize::Medium:
        default:
            return 1.0f;
        }
    }

    constexpr float ScaleAppTextPointSize(float pointSize, AppTextSize size) noexcept
    {
        return pointSize * AppTextSizeScale(size);
    }

    constexpr int ScaleAppTextDimension(int dimension, AppTextSize size) noexcept
    {
        return static_cast<int>(static_cast<float>(dimension) * AppTextSizeScale(size) + 0.5f);
    }
}
