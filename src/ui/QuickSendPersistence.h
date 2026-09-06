#pragma once

#include "ui/QuickSend.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    struct QuickSendPersistedState
    {
        std::vector<std::wstring> favoriteDestinationFolders;
        std::wstring lastQuickSendDestination;
        QuickSendModel::ShortcutAssignments shortcutAssignments{};
    };

    class QuickSendPersistence
    {
    public:
        using ReadValue = std::function<bool(std::wstring_view valueName, std::wstring* value)>;
        using WriteValue = std::function<void(std::wstring_view valueName, std::wstring_view value)>;
        using DeleteValue = std::function<void(std::wstring_view valueName)>;

        static QuickSendPersistedState Load(const ReadValue& readValue,
                                            std::size_t favoriteDestinationLimit);
        static void Save(const QuickSendPersistedState& state,
                          const WriteValue& writeValue,
                          const DeleteValue& deleteValue);
    };
}
