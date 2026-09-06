#pragma once

#include <span>

namespace hyperbrowse::ui
{
    class SelectionRatingPolicy final
    {
    public:
        static int CommonRating(std::span<const int> ratings);
    };
}
