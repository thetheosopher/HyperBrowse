#include "ui/ItemNumberNavigationPolicy.h"

#include <cstdint>

namespace hyperbrowse::ui
{
    bool TryParseItemNumber(std::wstring_view text,
                            int itemCount,
                            int* zeroBasedIndex) noexcept
    {
        if (!zeroBasedIndex || itemCount <= 0 || text.empty())
        {
            return false;
        }

        std::uint64_t itemNumber = 0;
        for (const wchar_t character : text)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }

            const std::uint64_t digit = static_cast<std::uint64_t>(character - L'0');
            const std::uint64_t maximumItemNumber = static_cast<std::uint64_t>(itemCount);
            if (digit > maximumItemNumber
                || itemNumber > (maximumItemNumber - digit) / 10)
            {
                return false;
            }
            itemNumber = itemNumber * 10 + digit;
        }

        if (itemNumber == 0 || itemNumber > static_cast<std::uint64_t>(itemCount))
        {
            return false;
        }

        *zeroBasedIndex = static_cast<int>(itemNumber - 1);
        return true;
    }
}
