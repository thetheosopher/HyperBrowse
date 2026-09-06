#pragma once

#include "browser/BrowserModel.h"

#include <span>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    class ViewerItemSelectionPolicy final
    {
    public:
        struct Input
        {
            std::span<const browser::BrowserItem> modelItems{};
            std::span<const int> orderedModelIndices{};
            int selectedModelIndex{-1};
            std::wstring_view preferredPath{};
            std::wstring_view currentPath{};
            int currentIndex{-1};
        };

        struct Result
        {
            std::vector<browser::BrowserItem> items;
            int selectedIndex{-1};
        };

        static Result Build(const Input& input);
    };
}
