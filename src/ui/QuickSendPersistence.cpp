#include "ui/QuickSendPersistence.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "ui/QuickAccessPathList.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kFavoriteDestinationFoldersValue = L"FavoriteDestinationFolders";
        constexpr std::wstring_view kLastQuickSendDestinationValue = L"LastQuickSendDestination";
        constexpr std::wstring_view kDestinationUsageHistoryValue = L"QuickSendDestinationUsageHistory";
        constexpr std::wstring_view kQuickSendShortcutPrefix = L"QuickSendShortcut";
        constexpr std::wstring_view kQuickSendShortcutOrderValue = L"QuickSendShortcutOrder";

        bool TryParseUnsigned(std::wstring_view value, std::uint64_t* result)
        {
            if (!result || value.empty())
            {
                return false;
            }

            std::uint64_t parsed = 0;
            for (const wchar_t character : value)
            {
                if (character < L'0' || character > L'9')
                {
                    return false;
                }

                const std::uint64_t digit = static_cast<std::uint64_t>(character - L'0');
                if (parsed > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
                {
                    return false;
                }
                parsed = parsed * 10 + digit;
            }

            *result = parsed;
            return true;
        }

        std::vector<QuickSendDestinationUsage> DeserializeDestinationUsage(
            std::wstring_view serializedUsage,
            std::size_t destinationLimit)
        {
            std::vector<QuickSendDestinationUsage> usage;
            usage.reserve((std::min)(destinationLimit, std::size_t{16}));
            std::vector<std::wstring> seenPaths;
            std::size_t offset = 0;
            while (offset < serializedUsage.size() && usage.size() < destinationLimit)
            {
                const std::size_t lineEnd = serializedUsage.find(L'\n', offset);
                std::wstring_view line = serializedUsage.substr(
                    offset,
                    lineEnd == std::wstring_view::npos ? std::wstring_view::npos : lineEnd - offset);
                if (!line.empty() && line.back() == L'\r')
                {
                    line.remove_suffix(1);
                }

                const std::size_t separator = line.rfind(L'\t');
                if (separator != std::wstring_view::npos && separator > 0)
                {
                    std::uint64_t lastUsedFileTime = 0;
                    if (TryParseUnsigned(line.substr(separator + 1), &lastUsedFileTime)
                        && lastUsedFileTime != 0
                        && QuickAccessPathList::Insert(
                            &seenPaths,
                            std::wstring(line.substr(0, separator)),
                            destinationLimit,
                            false))
                    {
                        usage.push_back({seenPaths.back(), lastUsedFileTime});
                    }
                }

                if (lineEnd == std::wstring_view::npos)
                {
                    break;
                }
                offset = lineEnd + 1;
            }

            return usage;
        }

        std::wstring SerializeDestinationUsage(
            const std::vector<QuickSendDestinationUsage>& usage,
            std::size_t destinationLimit)
        {
            std::wstring serialized;
            std::vector<std::wstring> seenPaths;
            for (const QuickSendDestinationUsage& entry : usage)
            {
                if (seenPaths.size() >= destinationLimit || entry.lastUsedFileTime == 0
                    || !QuickAccessPathList::Insert(
                        &seenPaths,
                        entry.folderPath,
                        destinationLimit,
                        false))
                {
                    continue;
                }

                if (!serialized.empty())
                {
                    serialized.push_back(L'\n');
                }
                serialized.append(seenPaths.back());
                serialized.push_back(L'\t');
                serialized.append(std::to_wstring(entry.lastUsedFileTime));
            }
            return serialized;
        }
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

        std::wstring serializedUsage;
        if (readValue(kDestinationUsageHistoryValue, &serializedUsage))
        {
            state.destinationUsage = DeserializeDestinationUsage(serializedUsage, favoriteDestinationLimit);
        }

        for (std::size_t index = 0; index < state.shortcutAssignments.size(); ++index)
        {
            const std::wstring valueName = std::wstring(kQuickSendShortcutPrefix) + std::to_wstring(index);
            readValue(valueName, &state.shortcutAssignments[index]);
        }

        std::wstring shortcutOrder;
        if (readValue(kQuickSendShortcutOrderValue, &shortcutOrder))
        {
            std::wstring normalizedShortcutOrder;
            if (QuickSendModel::TryNormalizeShortcutOrder(shortcutOrder, &normalizedShortcutOrder))
            {
                state.shortcutAssignmentOrder = std::move(normalizedShortcutOrder);
            }
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

        const std::wstring serializedUsage = SerializeDestinationUsage(
            state.destinationUsage,
            kQuickSendShortcutCount);
        if (serializedUsage.empty())
        {
            deleteValue(kDestinationUsageHistoryValue);
        }
        else
        {
            writeValue(kDestinationUsageHistoryValue, serializedUsage);
        }

        for (std::size_t index = 0; index < state.shortcutAssignments.size(); ++index)
        {
            const std::wstring valueName = std::wstring(kQuickSendShortcutPrefix) + std::to_wstring(index);
            writeValue(valueName, state.shortcutAssignments[index]);
        }
        writeValue(kQuickSendShortcutOrderValue, state.shortcutAssignmentOrder);
    }
}
