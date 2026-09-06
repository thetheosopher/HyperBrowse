#include "ui/ViewerItemSelectionPolicy.h"

#include <cstddef>

namespace hyperbrowse::ui
{
    ViewerItemSelectionPolicy::Result ViewerItemSelectionPolicy::Build(const Input& input)
    {
        Result result;
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

        result.items.reserve(orderedModelIndices.size());
        int selectedModelIndexPosition = -1;
        int preferredPathPosition = -1;
        int currentPathPosition = -1;
        for (const int modelIndex : orderedModelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(input.modelItems.size()))
            {
                continue;
            }

            result.items.push_back(input.modelItems[static_cast<std::size_t>(modelIndex)]);
            const int viewerIndex = static_cast<int>(result.items.size()) - 1;
            const std::wstring& path = result.items.back().filePath;
            if (selectedModelIndexPosition < 0 && modelIndex == input.selectedModelIndex)
            {
                selectedModelIndexPosition = viewerIndex;
            }
            if (preferredPathPosition < 0
                && !input.preferredPath.empty()
                && browser::FilePathsEqual(path, input.preferredPath))
            {
                preferredPathPosition = viewerIndex;
            }
            if (currentPathPosition < 0
                && !input.currentPath.empty()
                && browser::FilePathsEqual(path, input.currentPath))
            {
                currentPathPosition = viewerIndex;
            }
        }

        if (selectedModelIndexPosition >= 0)
        {
            result.selectedIndex = selectedModelIndexPosition;
        }
        else if (preferredPathPosition >= 0)
        {
            result.selectedIndex = preferredPathPosition;
        }
        else if (currentPathPosition >= 0)
        {
            result.selectedIndex = currentPathPosition;
        }
        else if (input.currentIndex >= 0
            && input.currentIndex < static_cast<int>(result.items.size()))
        {
            result.selectedIndex = input.currentIndex;
        }
        if (result.selectedIndex < 0 && !result.items.empty())
        {
            result.selectedIndex = 0;
        }

        return result;
    }
}
