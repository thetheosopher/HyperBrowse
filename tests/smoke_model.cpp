#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "ui/QuickSend.h"
#include "ui/QuickSendPersistence.h"
#include "util/PathUtils.h"

#include "smoke_model.h"

namespace hyperbrowse::tests
{
    namespace
    {
        void Expect(bool condition, const std::string& message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }

        void RunBrowserModelBulkRemovalScenario()
        {
            hyperbrowse::browser::BrowserModel model;
            std::vector<hyperbrowse::browser::BrowserItem> items;
            items.push_back(hyperbrowse::browser::BrowserItem{L"alpha.jpg", L"C:\\Alpha\\alpha.jpg", L"JPG", L"", 1, 10});
            items.push_back(hyperbrowse::browser::BrowserItem{L"beta.jpg", L"C:\\Alpha\\beta.jpg", L"JPG", L"", 2, 20});
            items.push_back(hyperbrowse::browser::BrowserItem{L"gamma.jpg", L"C:\\Alpha\\gamma.jpg", L"JPG", L"", 3, 30});
            model.Reset(L"C:\\Alpha", false);
            model.AppendItems(std::move(items), 3, 60);
            model.Complete();

            Expect(model.RemoveItemsByPath({L"c:/alpha/BETA.jpg", L"C:\\Alpha\\missing.jpg"}),
                   "Bulk model removal did not match normalized file paths");
            Expect(model.Items().size() == 2, "Bulk model removal removed the wrong number of items");
            Expect(model.TotalCount() == 2, "Bulk model removal did not update the item count");
            Expect(model.TotalBytes() == 40, "Bulk model removal did not update total bytes");
            Expect(model.FindItemIndexByPath(L"C:\\Alpha\\beta.jpg") < 0,
                   "Bulk model removal left the requested item in the model");
        }

        void RunQuickSendModelScenario()
        {
            using hyperbrowse::ui::QuickSendAssignmentResult;
            using hyperbrowse::ui::QuickSendModel;

            QuickSendModel model;
            model.SetFavoriteDestinations({
                L"C:\\Favorites\\One\\",
                L"c:/favorites/one",
                L"D:\\Favorites\\Two",
                L"E:\\Favorites\\Three",
                L"F:\\Favorites\\Four",
                L"G:\\Favorites\\Five",
            });

            Expect(model.FavoriteDestinations().size() == 5,
                   "Quick Send did not deduplicate favorite destinations while preserving more than four entries");
            Expect(model.FavoriteDestinations().front() == L"C:\\Favorites\\One\\",
                   "Quick Send did not preserve the first favorite path for display");
            Expect(std::ranges::none_of(model.FavoriteDestinations(), [](const std::wstring& path)
            {
                return hyperbrowse::util::NormalizedPathEquals(path, L"H:\\RecentOnly");
            }),
                   "Recent-only destinations leaked into the favorite Quick Send list");
            Expect(QuickSendModel::ShortcutIndexFromText(L"0") == 0,
                   "Quick Send did not map the first digit shortcut");
            Expect(QuickSendModel::ShortcutIndexFromText(L"9") == 9,
                   "Quick Send did not map the last digit shortcut");
            Expect(QuickSendModel::ShortcutIndexFromText(L"A") == 10,
                   "Quick Send did not map the first letter shortcut");
            Expect(QuickSendModel::ShortcutIndexFromText(L"z") == 35,
                   "Quick Send did not normalize the last lowercase letter shortcut");
            Expect(QuickSendModel::ShortcutIndexFromText(L"AB") == std::nullopt,
                   "Quick Send accepted a multi-character shortcut key");
            for (const wchar_t character : hyperbrowse::ui::kQuickSendPunctuationShortcuts)
            {
                const std::wstring shortcutText(1, character);
                const std::optional<int> shortcutIndex = QuickSendModel::ShortcutIndexFromText(shortcutText);
                Expect(shortcutIndex.has_value()
                           && QuickSendModel::ShortcutCharacter(*shortcutIndex) == character,
                       "Quick Send did not round-trip a printable punctuation shortcut");
            }
            Expect(QuickSendModel::ShortcutIndexFromText(L" ") == std::nullopt,
                   "Quick Send accepted a whitespace shortcut key");
            Expect(QuickSendModel::ShortcutIndexFromText(L"\x00E9") == std::nullopt,
                   "Quick Send accepted an unsupported non-ASCII shortcut key");
            Expect(QuickSendModel::ShortcutIndexFromText(L"F1") == std::nullopt,
                   "Quick Send accepted a function-key name as a shortcut");
            Expect(QuickSendModel::ShortcutCharacter(0) == L'0'
                       && QuickSendModel::ShortcutCharacter(9) == L'9'
                       && QuickSendModel::ShortcutCharacter(10) == L'A'
                       && QuickSendModel::ShortcutCharacter(35) == L'Z'
                       && QuickSendModel::ShortcutCharacter(36) == L'`'
                       && QuickSendModel::ShortcutCharacter(static_cast<int>(hyperbrowse::ui::kQuickSendShortcutCount - 1)) == L'?'
                       && QuickSendModel::ShortcutCharacter(static_cast<int>(hyperbrowse::ui::kQuickSendShortcutCount)) == L'\0',
                   "Quick Send did not map shortcut indexes to display keys");

            Expect(model.SetShortcutForDestination(L"c:/FAVORITES/one/", L"2")
                       == QuickSendAssignmentResult::Accepted,
                   "Quick Send rejected a valid normalized favorite assignment");
            Expect(model.ShortcutForDestination(L"C:\\Favorites\\One") == 2,
                   "Quick Send did not resolve a destination assignment by normalized path");
            Expect(model.ShortcutAssignmentsByKey()[2] == L"c:\\favorites\\one",
                   "Quick Send did not persist assignments in normalized form");

            Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"2")
                       == QuickSendAssignmentResult::DuplicateShortcut,
                   "Quick Send allowed two favorite destinations to claim one digit");
            Expect(model.ShortcutForDestination(L"C:\\Favorites\\One") == 2,
                   "Quick Send duplicate rejection disturbed the existing assignment");
            Expect(model.AssignNextAvailableShortcut(L"D:\\Favorites\\Two") == 0,
                   "Quick Send did not assign the lowest available shortcut");
            Expect(model.AssignNextAvailableShortcut(L"D:\\Favorites\\Two") == 0,
                   "Quick Send changed an existing automatic shortcut assignment");
            Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"12")
                       == QuickSendAssignmentResult::InvalidShortcut,
                   "Quick Send accepted a multi-character shortcut");
            Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"x")
                       == QuickSendAssignmentResult::Accepted
                       && model.ShortcutForDestination(L"D:\\Favorites\\Two") == 33,
                   "Quick Send did not accept and normalize a lowercase letter shortcut");
            Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"!")
                       == QuickSendAssignmentResult::Accepted
                       && model.ShortcutForDestination(L"D:\\Favorites\\Two") == 38,
                   "Quick Send did not accept a printable punctuation shortcut");
            Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", {})
                       == QuickSendAssignmentResult::Accepted,
                   "Quick Send did not accept a blank shortcut to clear an assignment");

            QuickSendModel restoredModel;
            restoredModel.SetFavoriteDestinations(model.FavoriteDestinations());
            QuickSendModel::ShortcutAssignments persisted{};
            persisted[1] = L"C:\\FAVORITES\\ONE";
            persisted[2] = L"C:\\Removed\\Destination";
            persisted[3] = L"c:/favorites/one/";
            persisted[10] = L"E:\\Favorites\\Three";
            restoredModel.SetShortcutAssignments(persisted);
            Expect(restoredModel.ShortcutForDestination(L"C:\\Favorites\\One") == 1,
                   "Quick Send did not restore a persisted path assignment");
            Expect(restoredModel.ShortcutForDestination(L"E:\\Favorites\\Three") == 10,
                   "Quick Send did not restore a persisted letter assignment");
            Expect(restoredModel.DestinationForShortcut(2) == std::nullopt,
                   "Quick Send did not prune a persisted destination that is no longer favorited");
            Expect(restoredModel.DestinationForShortcut(3) == std::nullopt,
                   "Quick Send did not reject duplicate persisted assignments");

            restoredModel.SetFavoriteDestinations({
                L"G:\\Favorites\\Five",
                L"C:\\Favorites\\One",
                L"E:\\Favorites\\Three",
                L"D:\\Favorites\\Two",
                L"F:\\Favorites\\Four",
            });
            Expect(restoredModel.ShortcutForDestination(L"C:\\Favorites\\One") == 1,
                   "Quick Send assignment changed when favorite ordering changed");

            QuickSendModel sortedModel;
            sortedModel.SetFavoriteDestinations({
                L"C:\\Favorites\\Unassigned",
                L"C:\\Favorites\\Letter",
                L"C:\\Favorites\\Digit",
            });
            Expect(sortedModel.SetShortcutForDestination(L"C:\\Favorites\\Letter", L"A")
                       == QuickSendAssignmentResult::Accepted
                       && sortedModel.SetShortcutForDestination(L"C:\\Favorites\\Digit", L"2")
                           == QuickSendAssignmentResult::Accepted,
                   "Quick Send rejected valid shortcuts for sort coverage");
            sortedModel.SortFavoriteDestinationsByShortcut();
            Expect(sortedModel.FavoriteDestinations().size() == 3
                       && sortedModel.FavoriteDestinations()[0] == L"C:\\Favorites\\Digit"
                       && sortedModel.FavoriteDestinations()[1] == L"C:\\Favorites\\Letter"
                       && sortedModel.FavoriteDestinations()[2] == L"C:\\Favorites\\Unassigned",
                   "Quick Send did not sort favorites by digit-then-letter shortcuts with unassigned entries last");

            QuickSendModel fullModel;
            std::vector<std::wstring> fullFavorites;
            for (std::size_t index = 0; index < hyperbrowse::ui::kQuickSendShortcutCount; ++index)
            {
                fullFavorites.push_back(L"C:\\Favorites\\Shortcut" + std::to_wstring(index));
            }
            fullModel.SetFavoriteDestinations(fullFavorites);
            for (std::size_t index = 0; index < hyperbrowse::ui::kQuickSendShortcutCount; ++index)
            {
                Expect(fullModel.AssignNextAvailableShortcut(fullFavorites[index]) == static_cast<int>(index),
                       "Quick Send did not consume shortcuts in digit-then-letter order");
            }
            Expect(fullModel.ShortcutForDestination(L"C:\\Favorites\\Shortcut10") == 10,
                   "Quick Send did not assign A after the digit shortcuts");
            fullFavorites.push_back(L"C:\\Favorites\\New");
            fullModel.SetFavoriteDestinations(fullFavorites);
            Expect(fullModel.AssignNextAvailableShortcut(L"C:\\Favorites\\New") == std::nullopt,
                   "Quick Send assigned a shortcut when all supported keys were already occupied");
        }

              void RunQuickSendPersistenceScenario()
              {
                     using hyperbrowse::ui::QuickSendPersistedState;
                     using hyperbrowse::ui::QuickSendPersistence;

                     std::map<std::wstring, std::wstring> values;
                     QuickSendPersistedState state;
                     state.favoriteDestinationFolders = {
                            L"C:\\Favorites\\One",
                            L"D:\\Favorites\\Two",
                            L"E:\\Favorites\\Three",
                     };
                     state.lastQuickSendDestination = L"D:\\Favorites\\Two";
                     state.shortcutAssignments[3] = L"D:\\Favorites\\Two";

                     QuickSendPersistence::Save(
                            state,
                            [&values](std::wstring_view valueName, std::wstring_view value)
                            {
                                   values[std::wstring(valueName)] = value;
                            },
                            [&values](std::wstring_view valueName)
                            {
                                   values.erase(std::wstring(valueName));
                            });

                     Expect(values[L"FavoriteDestinationFolders"] == L"C:\\Favorites\\One\nD:\\Favorites\\Two\nE:\\Favorites\\Three"
                                      && values[L"LastQuickSendDestination"] == L"D:\\Favorites\\Two"
                                      && values[L"QuickSendShortcut3"] == L"D:\\Favorites\\Two",
                               "Quick Send persistence did not write the expected registry value contract");

                     const QuickSendPersistedState restored = QuickSendPersistence::Load(
                            [&values](std::wstring_view valueName, std::wstring* value)
                            {
                                   const auto found = values.find(std::wstring(valueName));
                                   if (found == values.end())
                                   {
                                          return false;
                                   }

                                   *value = found->second;
                                   return true;
                            },
                            2);
                     Expect(restored.favoriteDestinationFolders == std::vector<std::wstring>{
                                             L"C:\\Favorites\\One",
                                             L"D:\\Favorites\\Two"}
                                      && restored.lastQuickSendDestination == L"D:\\Favorites\\Two"
                                      && restored.shortcutAssignments[3] == L"D:\\Favorites\\Two",
                               "Quick Send persistence did not restore capped favorites, last destination, and shortcuts");

                     state.lastQuickSendDestination.clear();
                     QuickSendPersistence::Save(
                            state,
                            [&values](std::wstring_view valueName, std::wstring_view value)
                            {
                                   values[std::wstring(valueName)] = value;
                            },
                            [&values](std::wstring_view valueName)
                            {
                                   values.erase(std::wstring(valueName));
                            });
                     Expect(!values.contains(L"LastQuickSendDestination"),
                               "Quick Send persistence did not delete an empty last destination");
              }
    }

    void RunModelScenarios()
    {
        RunBrowserModelBulkRemovalScenario();
        RunQuickSendModelScenario();
              RunQuickSendPersistenceScenario();
    }
}
