#include "ui/BrowserItemScopeCollector.h"

#include <cstddef>

namespace hyperbrowse::ui
{
    std::vector<browser::BrowserItem> BrowserItemScopeCollector::Collect(const Input& input)
    {
        std::vector<browser::BrowserItem> items;
        if (input.selectionScope && input.hasSelectionSource)
        {
            items.reserve(input.selectedModelIndices.size());
            for (const int modelIndex : input.selectedModelIndices)
            {
                if (modelIndex >= 0 && modelIndex < static_cast<int>(input.modelItems.size()))
                {
                    items.push_back(input.modelItems[static_cast<std::size_t>(modelIndex)]);
                }
            }
            return items;
        }

        std::vector<int> fallbackIndices;
        std::span<const int> orderedModelIndices = input.orderedModelIndices;
        if (orderedModelIndices.empty())
        {
            fallbackIndices.reserve(input.modelItems.size());
            for (std::size_t index = 0; index < input.modelItems.size(); ++index)
            {
                fallbackIndices.push_back(static_cast<int>(index));
            }
            orderedModelIndices = fallbackIndices;
        }

        items.reserve(orderedModelIndices.size());
        for (const int modelIndex : orderedModelIndices)
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(input.modelItems.size()))
            {
                items.push_back(input.modelItems[static_cast<std::size_t>(modelIndex)]);
            }
        }

        return items;
    }
}
