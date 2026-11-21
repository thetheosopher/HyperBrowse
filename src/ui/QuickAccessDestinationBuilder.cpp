#include "ui/QuickAccessDestinationBuilder.h"

#include <limits>

#include "ui/QuickAccessMenuBuilder.h"
#include "ui/QuickAccessPathList.h"

namespace hyperbrowse::ui
{
    std::vector<QuickAccessLayout::Destination> QuickAccessDestinationBuilder::Build(
        const std::vector<std::wstring>& favoriteDestinations,
        const std::vector<std::wstring>& recentDestinations,
        const MetadataBuilder& metadataBuilder,
        const ShortcutLookup& shortcutLookup)
    {
        std::vector<QuickAccessLayout::Destination> destinations;
        destinations.reserve(favoriteDestinations.size() + recentDestinations.size());
        std::vector<std::wstring> seenPaths;
        const auto appendDestination = [&](std::wstring_view path, bool favorite)
        {
            if (!QuickAccessPathList::Insert(
                    &seenPaths,
                    std::wstring(path),
                    (std::numeric_limits<std::size_t>::max)(),
                    false))
            {
                return;
            }

            QuickAccessLayout::Destination destination;
            destination.destinationPath = seenPaths.back();
            destination.displayLabel = QuickAccessMenuBuilder::FormatFolderShortcutMenuLabel(destination.destinationPath);
            if (metadataBuilder)
            {
                destination.metadataLabel = metadataBuilder(destination.destinationPath);
            }
            if (favorite && shortcutLookup)
            {
                if (const std::optional<int> assignedShortcut = shortcutLookup(destination.destinationPath))
                {
                    destination.assignedShortcut = *assignedShortcut;
                }
            }
            destination.favorite = favorite;
            destinations.push_back(std::move(destination));
        };

        for (const std::wstring& favoritePath : favoriteDestinations)
        {
            appendDestination(favoritePath, true);
        }
        for (const std::wstring& recentPath : recentDestinations)
        {
            appendDestination(recentPath, false);
        }

        return destinations;
    }
}
