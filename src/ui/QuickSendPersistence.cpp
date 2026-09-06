#include "ui/QuickSendPersistence.h"

#include "ui/QuickAccessPathList.h"

#include <utility>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kFavoriteDestinationFoldersValue = L"FavoriteDestinationFolders";
        constexpr std::wstring_view kLastQuickSendDestinationValue = L"LastQuickSendDestination";
        constexpr std::wstring_view kQuickSendShortcutPrefix = L"QuickSendShortcut";
    }

    QuickSendPersistedState QuickSendPersistence::Load(const ReadValue& readValue,
                                                       std::size_t favoriteDestinationLimit)
    {
        QuickSendPersistedState state;
        std::wstring serializedPaths;
        if (readValue(kFavoriteDestinationFoldersValue, &serializedPaths))
        {
            state.favoriteDestinationFolders = QuickAccessPathList::Deserialize(
                serializedPaths,
                favoriteDestinationLimit);
        }

        readValue(kLastQuickSendDestinationValue, &state.lastQuickSendDestination);

        for (std::size_t index = 0; index < state.shortcutAssignments.size(); ++index)
        {
            const std::wstring valueName = std::wstring(kQuickSendShortcutPrefix) + std::to_wstring(index);
            readValue(valueName, &state.shortcutAssignments[index]);
        }

        return state;
    }

    void QuickSendPersistence::Save(const QuickSendPersistedState& state,
                                    const WriteValue& writeValue,
                                    const DeleteValue& deleteValue)
    {
        writeValue(kFavoriteDestinationFoldersValue,
                   QuickAccessPathList::Serialize(state.favoriteDestinationFolders));
        if (!state.lastQuickSendDestination.empty())
        {
            writeValue(kLastQuickSendDestinationValue, state.lastQuickSendDestination);
        }
        else
        {
            deleteValue(kLastQuickSendDestinationValue);
        }

        for (std::size_t index = 0; index < state.shortcutAssignments.size(); ++index)
        {
            const std::wstring valueName = std::wstring(kQuickSendShortcutPrefix) + std::to_wstring(index);
            writeValue(valueName, state.shortcutAssignments[index]);
        }
    }
}
