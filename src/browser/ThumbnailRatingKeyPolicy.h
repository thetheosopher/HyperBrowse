#pragma once

#include <windows.h>

#include <optional>

namespace hyperbrowse::browser
{
    class ThumbnailRatingKeyPolicy final
    {
    public:
        static constexpr std::optional<int> RatingFromVirtualKey(UINT virtualKey,
                                                                  bool controlPressed,
                                                                  bool shiftPressed,
                                                                  bool altPressed) noexcept
        {
            if (controlPressed || shiftPressed || altPressed)
            {
                return std::nullopt;
            }

            if (virtualKey >= static_cast<UINT>('0') && virtualKey <= static_cast<UINT>('5'))
            {
                return static_cast<int>(virtualKey - static_cast<UINT>('0'));
            }

            if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD5)
            {
                return static_cast<int>(virtualKey - VK_NUMPAD0);
            }

            return std::nullopt;
        }
    };
}
