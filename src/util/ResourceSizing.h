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
    };

    inline const wchar_t* ResourceProfileToDisplayName(ResourceProfile profile) noexcept
    {
        switch (profile)
        {
        case ResourceProfile::Conservative:
            return L"Conservative";
        case ResourceProfile::Performance:
            return L"Performance";
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
}