#pragma once

#include <algorithm>
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
}