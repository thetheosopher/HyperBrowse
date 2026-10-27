#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace hyperbrowse::ui
{
    class DetailsPanelHistogram final
    {
    public:
        static constexpr std::size_t kBinCount = 256;

        struct Result
        {
            std::array<std::uint32_t, kBinCount> red{};
            std::array<std::uint32_t, kBinCount> green{};
            std::array<std::uint32_t, kBinCount> blue{};
            std::uint32_t peak{};
            bool visible{};
        };

        using Cancelled = std::function<bool()>;
        static bool Compute(HBITMAP bitmap, Result* result, const Cancelled& cancelled = {});
        static bool ComputePixels(const unsigned char* bgra, int width, int height,
                                  std::size_t stride, Result* result, const Cancelled& cancelled = {});
    };
}
