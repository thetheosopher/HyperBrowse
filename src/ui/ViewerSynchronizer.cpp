#include "ui/ViewerSynchronizer.h"

#include "ui/ViewerItemSelectionPolicy.h"

#include <utility>

namespace hyperbrowse::ui
{
    ViewerSynchronizer::Result ViewerSynchronizer::Build(
        const std::vector<browser::BrowserItem>& modelItems,
        const std::vector<int>& orderedModelIndices,
        std::wstring_view preferredPath,
        std::wstring_view currentPath,
        int currentIndex,
        bool slideshowActive,
        PairedItemResolver resolvePairedItems)
    {
        Result result;
        if (modelItems.empty())
        {
            result.closeRequested = true;
            return result;
        }

        ViewerItemSelectionPolicy::Result selection = ViewerItemSelectionPolicy::Build({
            modelItems,
            orderedModelIndices,
            -1,
            preferredPath,
            currentPath,
            currentIndex});
        if (selection.items.empty())
        {
            result.closeRequested = true;
            return result;
        }

        result.items = resolvePairedItems
            ? resolvePairedItems(std::move(selection.items), slideshowActive)
            : std::move(selection.items);
        result.selectedIndex = selection.selectedIndex;
        return result;
    }
}
