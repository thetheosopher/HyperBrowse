#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace hyperbrowse::util
{
    enum class ResourceProfile : int
    {
        Conservative = 0,
        Balanced = 1,
        Performance = 2,
        Aggressive = 3,
    };

    inline const wchar_t* ResourceProfileToDisplayName(ResourceProfile profile) noexcept
    {
        switch (profile)
        {
        case ResourceProfile::Conservative:
            return L"Conservative";
        case ResourceProfile::Performance:
            return L"Performance";
        case ResourceProfile::Aggressive:
            return L"Aggressive";
        case ResourceProfile::Balanced:
        default:
            return L"Balanced";
        }
    }


    struct MemorySnapshot
    {
        std::uint64_t totalPhysicalBytes{};
        std::uint64_t availablePhysicalBytes{};

        bool IsValid() const noexcept
        {
            return totalPhysicalBytes != 0 || availablePhysicalBytes != 0;
        }
    };

    inline MemorySnapshot QueryMemorySnapshot() noexcept
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) == FALSE)
        {
            return {};
        }

        return MemorySnapshot{status.ullTotalPhys, status.ullAvailPhys};
    }

    inline std::size_t SaturatingCastToSizeT(std::uint64_t value) noexcept
    {
        const std::uint64_t maxValue = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
        return static_cast<std::size_t>(std::min(value, maxValue));
    }

    inline std::size_t ResolveViewerFullImageCacheCapacityBytes(ResourceProfile profile) noexcept
    {
        constexpr std::uint64_t kMegabyte = 1024ULL * 1024ULL;
        constexpr std::uint64_t kBalancedFallback = 256ULL * kMegabyte;
        constexpr std::uint64_t kPerformanceFallback = 512ULL * kMegabyte;
        constexpr std::uint64_t kAggressiveFallback = 1024ULL * kMegabyte;
        constexpr std::uint64_t kConservativeMinimum = 128ULL * kMegabyte;
        constexpr std::uint64_t kBalancedMinimum = 128ULL * kMegabyte;
        constexpr std::uint64_t kPerformanceMinimum = 256ULL * kMegabyte;
        constexpr std::uint64_t kAggressiveMinimum = 512ULL * kMegabyte;
        constexpr std::uint64_t kConservativeMaximum = 256ULL * kMegabyte;
        constexpr std::uint64_t kBalancedMaximum = 1024ULL * kMegabyte;
        constexpr std::uint64_t kPerformanceMaximum = 2048ULL * kMegabyte;
        constexpr std::uint64_t kAggressiveMaximum = 4096ULL * kMegabyte;

        const MemorySnapshot memorySnapshot = QueryMemorySnapshot();
        if (!memorySnapshot.IsValid()
            || memorySnapshot.availablePhysicalBytes == 0
            || memorySnapshot.totalPhysicalBytes == 0)
        {
            switch (profile)
            {
            case ResourceProfile::Conservative:
                return static_cast<std::size_t>(kConservativeMinimum);
            case ResourceProfile::Performance:
                return static_cast<std::size_t>(kPerformanceFallback);
            case ResourceProfile::Aggressive:
                return static_cast<std::size_t>(kAggressiveFallback);
            case ResourceProfile::Balanced:
            default:
                return static_cast<std::size_t>(kBalancedFallback);
            }
        }

        std::uint64_t availabilityBudget = memorySnapshot.availablePhysicalBytes / 8ULL;
        std::uint64_t totalBudget = memorySnapshot.totalPhysicalBytes / 16ULL;
        std::uint64_t minimumBudget = kBalancedMinimum;
        std::uint64_t maximumBudget = kBalancedMaximum;
        switch (profile)
        {
        case ResourceProfile::Conservative:
            availabilityBudget = memorySnapshot.availablePhysicalBytes / 12ULL;
            totalBudget = memorySnapshot.totalPhysicalBytes / 24ULL;
            minimumBudget = kConservativeMinimum;
            maximumBudget = kConservativeMaximum;
            break;
        case ResourceProfile::Performance:
            availabilityBudget = memorySnapshot.availablePhysicalBytes / 5ULL;
            totalBudget = memorySnapshot.totalPhysicalBytes / 8ULL;
            minimumBudget = kPerformanceMinimum;
            maximumBudget = kPerformanceMaximum;
            break;
        case ResourceProfile::Aggressive:
            availabilityBudget = memorySnapshot.availablePhysicalBytes / 3ULL;
            totalBudget = memorySnapshot.totalPhysicalBytes / 4ULL;
            minimumBudget = kAggressiveMinimum;
            maximumBudget = kAggressiveMaximum;
            break;
        case ResourceProfile::Balanced:
        default:
            break;
        }

        const std::uint64_t preferredBudget = std::min(availabilityBudget, totalBudget);
        return SaturatingCastToSizeT(std::clamp(preferredBudget, minimumBudget, maximumBudget));
    }
}