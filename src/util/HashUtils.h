#pragma once

#include <cstddef>
#include <functional>

namespace hyperbrowse::util
{
    template <typename TValue>
    inline void HashCombine(std::size_t* seed, const TValue& value)
    {
        const std::size_t hashedValue = std::hash<TValue>{}(value);
        *seed ^= hashedValue + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);
    }
}