#include "ui/QuickAccessMenuBuilder.h"

#include <algorithm>
#include <filesystem>

#include "ui/CommandIds.h"

namespace hyperbrowse::ui
{
    using namespace command_ids;

    namespace
    {
        namespace fs = std::filesystem;

        void RemoveAllMenuItems(HMENU menu)
        {
            if (!menu)
            {
                return;
            }

            while (GetMenuItemCount(menu) > 0)
            {
                DeleteMenu(menu, 0, MF_BYPOSITION);
            }
        }
    }

    std::wstring QuickAccessMenuBuilder::FolderDisplayName(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return L"No Folder";
        }

        const fs::path path(folderPath);
        const std::wstring leaf = path.filename().wstring();
        return leaf.empty() ? std::wstring(folderPath) : leaf;
    }

    std::wstring QuickAccessMenuBuilder::FormatFolderShortcutMenuLabel(std::wstring_view folderPath)
    {
        const std::wstring displayName = FolderDisplayName(folderPath);
        if (displayName.empty() || displayName == folderPath)
        {
            return std::wstring(folderPath);
        }

        return displayName + L" (" + std::wstring(folderPath) + L")";
    }

    void QuickAccessMenuBuilder::Refresh(
        HMENU fileMenu,
        HMENU openRecentFolderMenu,
        HMENU copySelectionToMenu,
        HMENU moveSelectionToMenu,
        bool hasCurrentFolder,
        bool currentFolderIsFavorite,
        bool allowMutatingFileCommands,
        const std::vector<std::wstring>& recentFolders,
        const std::vector<std::wstring>& favoriteDestinationFolders,
        const std::vector<std::wstring>& recentDestinationPaths) const
    {
        if (!fileMenu || !openRecentFolderMenu || !copySelectionToMenu || !moveSelectionToMenu)
        {
            return;
        }

        const wchar_t* toggleLabel = hasCurrentFolder && currentFolderIsFavorite
            ? L"Remove Current Folder from Favorite &Destinations"
            : L"Add Current Folder to Favorite &Destinations";
        ModifyMenuW(fileMenu,
                    ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                    MF_BYCOMMAND | MF_STRING,
                    ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                    toggleLabel);

        RemoveAllMenuItems(openRecentFolderMenu);
        if (recentFolders.empty())
        {
            AppendMenuW(openRecentFolderMenu, MF_STRING | MF_GRAYED, 0, L"(No recent folders)");
        }
        else
        {
            const std::size_t recentCount = (std::min)(
                recentFolders.size(),
                static_cast<std::size_t>(ID_FILE_OPEN_RECENT_FOLDER_LAST - ID_FILE_OPEN_RECENT_FOLDER_BASE + 1));
            for (std::size_t index = 0; index < recentCount; ++index)
            {
                const std::wstring label = FormatFolderShortcutMenuLabel(recentFolders[index]);
                AppendMenuW(openRecentFolderMenu,
                            MF_STRING,
                            ID_FILE_OPEN_RECENT_FOLDER_BASE + static_cast<UINT>(index),
                            label.c_str());
            }

            AppendMenuW(openRecentFolderMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(openRecentFolderMenu, MF_STRING, ID_FILE_CLEAR_RECENT_FOLDERS, L"C&lear All Recent Folders");
        }

        const auto populateDestinationMenu = [&](HMENU menu,
                                                 UINT browseCommandId,
                                                 UINT favoriteBaseCommandId,
                                                 UINT favoriteLastCommandId,
                                                 UINT recentBaseCommandId,
                                                 UINT recentLastCommandId)
        {
            RemoveAllMenuItems(menu);
            AppendMenuW(menu,
                        MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                        browseCommandId,
                        L"Choose &Folder...");

            const std::size_t favoriteCapacity = favoriteLastCommandId - favoriteBaseCommandId + 1;
            const std::size_t recentCapacity = recentLastCommandId - recentBaseCommandId + 1;
            const std::size_t favoriteCount = (std::min)(favoriteDestinationFolders.size(), favoriteCapacity);
            const std::size_t recentCount = (std::min)(recentDestinationPaths.size(), recentCapacity);
            if (favoriteCount == 0 && recentCount == 0)
            {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(No favorite or recent destinations)");
                return;
            }

            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            if (favoriteCount > 0)
            {
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Quick Actions");
                for (std::size_t index = 0; index < favoriteCount; ++index)
                {
                    const std::wstring label = FormatFolderShortcutMenuLabel(favoriteDestinationFolders[index]);
                    AppendMenuW(menu,
                                MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                                favoriteBaseCommandId + static_cast<UINT>(index),
                                label.c_str());
                }
            }

            if (recentCount > 0)
            {
                if (favoriteCount > 0)
                {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                }

                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Recent Destinations");
                for (std::size_t index = 0; index < recentCount; ++index)
                {
                    const std::wstring label = FormatFolderShortcutMenuLabel(recentDestinationPaths[index]);
                    AppendMenuW(menu,
                                MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                                recentBaseCommandId + static_cast<UINT>(index),
                                label.c_str());
                }

                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, ID_FILE_CLEAR_RECENT_DESTINATIONS, L"Clear All Recent &Destinations");
            }
        };

        populateDestinationMenu(copySelectionToMenu,
                                ID_FILE_COPY_SELECTION_BROWSE,
                                ID_FILE_COPY_SELECTION_FAVORITE_BASE,
                                ID_FILE_COPY_SELECTION_FAVORITE_LAST,
                                ID_FILE_COPY_SELECTION_RECENT_BASE,
                                ID_FILE_COPY_SELECTION_RECENT_LAST);
        populateDestinationMenu(moveSelectionToMenu,
                                ID_FILE_MOVE_SELECTION_BROWSE,
                                ID_FILE_MOVE_SELECTION_FAVORITE_BASE,
                                ID_FILE_MOVE_SELECTION_FAVORITE_LAST,
                                ID_FILE_MOVE_SELECTION_RECENT_BASE,
                                ID_FILE_MOVE_SELECTION_RECENT_LAST);
    }
}
