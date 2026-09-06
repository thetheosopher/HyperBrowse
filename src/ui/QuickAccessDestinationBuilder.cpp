#include "ui/QuickAccessDestinationBuilder.h"

#include "ui/QuickAccessMenuBuilder.h"

namespace hyperbrowse::ui
{
    std::vector<QuickAccessLayout::Destination> QuickAccessDestinationBuilder::Build(
        const std::vector<std::wstring>& favoriteDestinations,
        const MetadataBuilder& metadataBuilder,
        const ShortcutLookup& shortcutLookup)
    {
        std::vector<QuickAccessLayout::Destination> destinations;
        destinations.reserve(favoriteDestinations.size());
        for (const std::wstring& favoritePath : favoriteDestinations)
        {
            QuickAccessLayout::Destination destination;
            destination.destinationPath = favoritePath;
            destination.displayLabel = QuickAccessMenuBuilder::FormatFolderShortcutMenuLabel(favoritePath);
            destination.metadataLabel = metadataBuilder(favoritePath);
            if (const std::optional<int> assignedShortcut = shortcutLookup(favoritePath))
            {
                destination.assignedShortcut = *assignedShortcut;
            }
            destination.favorite = true;
            destinations.push_back(std::move(destination));
        }

        return destinations;
    }
}
