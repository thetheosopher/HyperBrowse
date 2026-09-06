#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"

namespace hyperbrowse::ui
{
    class ViewerSynchronizer final
    {
    public:
        struct Result
        {
            std::vector<browser::BrowserItem> items;
            int selectedIndex{-1};
            bool closeRequested{};
        };

        using PairedItemResolver = std::function<std::vector<browser::BrowserItem>(
            std::vector<browser::BrowserItem>,
            bool startSlideshow)>;

        static Result Build(const std::vector<browser::BrowserItem>& modelItems,
                            const std::vector<int>& orderedModelIndices,
                            std::wstring_view preferredPath,
                            std::wstring_view currentPath,
                            int currentIndex,
                            bool slideshowActive,
                            PairedItemResolver resolvePairedItems);
    };
}
