#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace hyperbrowse::ui
{
    class DetailsPanelHistogram final
    {
    public:
        static constexpr std::size_t kBinCount = 64;

        struct Result
        {
            std::array<std::uint32_t, kBinCount> red{};
            std::array<std::uint32_t, kBinCount> green{};
            std::array<std::uint32_t, kBinCount> blue{};
            std::uint32_t peak{};
            bool visible{};
        };

        static bool Compute(HBITMAP bitmap, Result* result);
    };
}
