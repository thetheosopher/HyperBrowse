#pragma once

#include <string_view>

namespace hyperbrowse::ui
{
    bool TryParseItemNumber(std::wstring_view text,
                            int itemCount,
                            int* zeroBasedIndex) noexcept;
}
