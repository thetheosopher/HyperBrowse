#pragma once

#include "browser/BrowserModel.h"

#include <span>
#include <vector>

namespace hyperbrowse::ui
{
    class BrowserItemScopeCollector final
    {
    public:
        struct Input
        {
            std::span<const browser::BrowserItem> modelItems{};
            std::span<const int> orderedModelIndices{};
            std::span<const int> selectedModelIndices{};
            bool selectionScope{};
            bool hasSelectionSource{};
        };

        static std::vector<browser::BrowserItem> Collect(const Input& input);
    };
}
