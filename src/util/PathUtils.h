#pragma once

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>

namespace hyperbrowse::util
{
    inline std::wstring NormalizePathForComparison(std::wstring_view value)
    {
        std::wstring normalized(value);
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        while (normalized.size() > 3 && !normalized.empty() && normalized.back() == L'\\')
        {
            normalized.pop_back();
        }
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return normalized;
    }

    // Allocation-free helpers that compute the same canonical form as
    // NormalizePathForComparison() on the fly. Used on hot paths (thumbnail cache key
    // hashing/equality) where building a temporary normalized std::wstring per call is
    // measurable. The canonical form treats '/' and '\\' as equivalent, is
    // case-insensitive, and trims trailing separators while preserving a 3-character
    // minimum (e.g. drive roots such as "C:\\").

    // Length of value after trimming trailing separators (matching the size>3 rule).
    inline std::size_t NormalizedPathLength(std::wstring_view value) noexcept
    {
        std::size_t length = value.size();
        while (length > 3 && (value[length - 1] == L'\\' || value[length - 1] == L'/'))
        {
            --length;
        }
        return length;
    }

    // Canonical character at index: separators fold to '\\', everything is lowercased.
    inline wchar_t NormalizedPathCharAt(std::wstring_view value, std::size_t index) noexcept
    {
        const wchar_t character = value[index];
        if (character == L'/')
        {
            return L'\\';
        }
        return static_cast<wchar_t>(towlower(character));
    }

    inline bool NormalizedPathEquals(std::wstring_view lhs, std::wstring_view rhs) noexcept
    {
        const std::size_t lhsLength = NormalizedPathLength(lhs);
        const std::size_t rhsLength = NormalizedPathLength(rhs);
        if (lhsLength != rhsLength)
        {
            return false;
        }
        for (std::size_t index = 0; index < lhsLength; ++index)
        {
            if (NormalizedPathCharAt(lhs, index) != NormalizedPathCharAt(rhs, index))
            {
                return false;
            }
        }
        return true;
    }

    // FNV-1a over the canonical character sequence. Only required to be self-consistent
    // with NormalizedPathEquals(); it is not persisted, so the constants may change.
    inline std::size_t NormalizedPathHash(std::wstring_view value) noexcept
    {
        std::size_t hash = 1469598103934665603ULL;
        const std::size_t length = NormalizedPathLength(value);
        for (std::size_t index = 0; index < length; ++index)
        {
            hash ^= static_cast<std::size_t>(NormalizedPathCharAt(value, index));
            hash *= 1099511628211ULL;
        }
        return hash;
    }
}