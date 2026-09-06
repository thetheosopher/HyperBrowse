#include "ui/SelectionRatingPolicy.h"

#include <algorithm>

namespace hyperbrowse::ui
{
    int SelectionRatingPolicy::CommonRating(std::span<const int> ratings)
    {
        if (ratings.empty())
        {
            return -1;
        }

        const int commonRating = std::clamp(ratings.front(), 0, 5);
        for (const int rating : ratings.subspan(1))
        {
            if (std::clamp(rating, 0, 5) != commonRating)
            {
                return -1;
            }
        }

        return commonRating;
    }
}
