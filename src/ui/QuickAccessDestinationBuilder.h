#pragma once

#include "ui/QuickAccessLayout.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    class QuickAccessDestinationBuilder final
    {
    public:
        using MetadataBuilder = std::function<std::wstring(std::wstring_view destinationPath)>;
        using ShortcutLookup = std::function<std::optional<int>(std::wstring_view destinationPath)>;

        static std::vector<QuickAccessLayout::Destination> Build(
            const std::vector<std::wstring>& favoriteDestinations,
            const MetadataBuilder& metadataBuilder,
            const ShortcutLookup& shortcutLookup);
    };
}
