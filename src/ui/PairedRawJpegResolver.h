#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"

namespace hyperbrowse::ui
{
    class PairedRawJpegResolver
    {
    public:
        using FolderPathEquals = std::function<bool(std::wstring_view lhs, std::wstring_view rhs)>;

        static browser::BrowserItem Resolve(
            const browser::BrowserItem& item,
            const std::vector<browser::BrowserItem>& candidates,
            browser::RawJpegDisplayPreference preference,
            const FolderPathEquals& folderPathEquals);
        static std::vector<browser::BrowserItem> ResolveItems(
            std::vector<browser::BrowserItem> items,
            const std::vector<browser::BrowserItem>& candidates,
            browser::RawJpegDisplayPreference preference,
            const FolderPathEquals& folderPathEquals);
        static std::vector<std::wstring> ExpandPaths(
            const std::vector<std::wstring>& paths,
            const std::vector<browser::BrowserItem>& candidates,
            const FolderPathEquals& folderPathEquals);
    };
}
