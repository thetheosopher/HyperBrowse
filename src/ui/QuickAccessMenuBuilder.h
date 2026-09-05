#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    class QuickAccessMenuBuilder final
    {
    public:
        static std::wstring FolderDisplayName(std::wstring_view folderPath);
        static std::wstring FormatFolderShortcutMenuLabel(std::wstring_view folderPath);

        void Refresh(
            HMENU fileMenu,
            HMENU openRecentFolderMenu,
            HMENU copySelectionToMenu,
            HMENU moveSelectionToMenu,
            bool hasCurrentFolder,
            bool currentFolderIsFavorite,
            bool allowMutatingFileCommands,
            const std::vector<std::wstring>& recentFolders,
            const std::vector<std::wstring>& favoriteDestinationFolders,
            const std::vector<std::wstring>& recentDestinationPaths) const;
    };
}
