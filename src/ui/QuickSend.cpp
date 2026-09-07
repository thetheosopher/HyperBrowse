#include "ui/QuickSend.h"

#include "util/PathUtils.h"

#include <algorithm>
#include <utility>

namespace hyperbrowse::ui
{
    std::vector<std::wstring> QuickSendModel::BuildFavoriteDestinations(
        const std::vector<std::wstring>& favoritePaths)
    {
        std::vector<std::wstring> destinations;
        destinations.reserve(favoritePaths.size());

        for (const std::wstring& favoritePath : favoritePaths)
        {
            if (favoritePath.empty()
                || std::ranges::any_of(destinations, [&](const std::wstring& destination)
                {
                    return hyperbrowse::util::NormalizedPathEquals(destination, favoritePath);
                }))
            {
                continue;
            }

            destinations.push_back(favoritePath);
        }

        return destinations;
    }

    void QuickSendModel::SetFavoriteDestinations(std::vector<std::wstring> favoritePaths)
    {
        favoriteDestinations_ = BuildFavoriteDestinations(favoritePaths);
        PruneShortcutAssignments();
    }

    const std::vector<std::wstring>& QuickSendModel::FavoriteDestinations() const noexcept
    {
        return favoriteDestinations_;
    }

    void QuickSendModel::SortFavoriteDestinationsByShortcut()
    {
        std::stable_sort(favoriteDestinations_.begin(), favoriteDestinations_.end(), [&](const std::wstring& left,
                                                                                           const std::wstring& right)
        {
            const std::optional<int> leftShortcut = ShortcutForDestination(left);
            const std::optional<int> rightShortcut = ShortcutForDestination(right);
            if (leftShortcut && rightShortcut)
            {
                return *leftShortcut < *rightShortcut;
            }
            return leftShortcut.has_value() && !rightShortcut.has_value();
        });
    }

    void QuickSendModel::SetShortcutAssignments(const ShortcutAssignments& assignments)
    {
        shortcutAssignments_ = {};
        for (std::size_t index = 0; index < shortcutAssignments_.size(); ++index)
        {
            const std::wstring normalizedPath = NormalizeStoredPath(assignments[index]);
            if (normalizedPath.empty())
            {
                continue;
            }

            const bool alreadyAssigned = std::ranges::any_of(
                shortcutAssignments_.begin(),
                shortcutAssignments_.begin() + static_cast<std::ptrdiff_t>(index),
                [&](const std::wstring& existingPath)
                {
                    return !existingPath.empty()
                        && hyperbrowse::util::NormalizedPathEquals(existingPath, normalizedPath);
                });
            if (!alreadyAssigned)
            {
                shortcutAssignments_[index] = normalizedPath;
            }
        }

        PruneShortcutAssignments();
    }

    const QuickSendModel::ShortcutAssignments& QuickSendModel::ShortcutAssignmentsByKey() const noexcept
    {
        return shortcutAssignments_;
    }

    std::optional<int> QuickSendModel::ShortcutIndexFromText(std::wstring_view shortcutText)
    {
        if (shortcutText.size() != 1)
        {
            return std::nullopt;
        }

        const wchar_t character = shortcutText.front();
        if (character >= L'0' && character <= L'9')
        {
            return static_cast<int>(character - L'0');
        }
        if (character >= L'A' && character <= L'Z')
        {
            return static_cast<int>(kQuickSendDigitShortcutCount + character - L'A');
        }
        if (character >= L'a' && character <= L'z')
        {
            return static_cast<int>(kQuickSendDigitShortcutCount + character - L'a');
        }

        const std::size_t punctuationIndex = kQuickSendPunctuationShortcuts.find(character);
        if (punctuationIndex != std::wstring_view::npos)
        {
            return static_cast<int>(kQuickSendDigitShortcutCount
                                    + kQuickSendLetterShortcutCount
                                    + punctuationIndex);
        }

        return std::nullopt;
    }

    wchar_t QuickSendModel::ShortcutCharacter(int shortcut)
    {
        if (shortcut < 0 || shortcut >= static_cast<int>(kQuickSendShortcutCount))
        {
            return L'\0';
        }
        if (shortcut < static_cast<int>(kQuickSendDigitShortcutCount))
        {
            return static_cast<wchar_t>(L'0' + shortcut);
        }
        if (shortcut < static_cast<int>(kQuickSendDigitShortcutCount + kQuickSendLetterShortcutCount))
        {
            return static_cast<wchar_t>(L'A' + shortcut - static_cast<int>(kQuickSendDigitShortcutCount));
        }

        return kQuickSendPunctuationShortcuts[
            static_cast<std::size_t>(shortcut - static_cast<int>(kQuickSendDigitShortcutCount + kQuickSendLetterShortcutCount))];
    }

    bool QuickSendModel::TryNormalizeShortcutOrder(std::wstring_view shortcutOrder,
                                                   std::wstring* normalizedOrder)
    {
        if (!normalizedOrder)
        {
            return false;
        }

        std::array<bool, kQuickSendShortcutCount> seen{};
        std::wstring normalized;
        normalized.reserve(shortcutOrder.size());
        for (const wchar_t character : shortcutOrder)
        {
            const std::optional<int> shortcut = ShortcutIndexFromText(std::wstring_view(&character, 1));
            if (!shortcut || seen[static_cast<std::size_t>(*shortcut)])
            {
                return false;
            }

            seen[static_cast<std::size_t>(*shortcut)] = true;
            normalized.push_back(ShortcutCharacter(*shortcut));
        }

        *normalizedOrder = std::move(normalized);
        return true;
    }

    bool QuickSendModel::SetShortcutAssignmentOrder(std::wstring_view shortcutOrder)
    {
        std::wstring normalizedOrder;
        if (!TryNormalizeShortcutOrder(shortcutOrder, &normalizedOrder))
        {
            return false;
        }

        shortcutAssignmentOrder_ = std::move(normalizedOrder);
        return true;
    }

    const std::wstring& QuickSendModel::ShortcutAssignmentOrder() const noexcept
    {
        return shortcutAssignmentOrder_;
    }

    QuickSendAssignmentResult QuickSendModel::SetShortcutForDestination(
        std::wstring_view destinationPath,
        std::wstring_view shortcutText)
    {
        if (FindFavoriteIndex(destinationPath) < 0)
        {
            return QuickSendAssignmentResult::DestinationNotFavorite;
        }

        int shortcut = -1;
        if (!shortcutText.empty())
        {
            const std::optional<int> parsedShortcut = ShortcutIndexFromText(shortcutText);
            if (!parsedShortcut)
            {
                return QuickSendAssignmentResult::InvalidShortcut;
            }

            shortcut = *parsedShortcut;
            const std::wstring normalizedDestination = NormalizeStoredPath(destinationPath);
            const std::wstring& existingPath = shortcutAssignments_[static_cast<std::size_t>(shortcut)];
            if (!existingPath.empty()
                && !hyperbrowse::util::NormalizedPathEquals(existingPath, normalizedDestination))
            {
                return QuickSendAssignmentResult::DuplicateShortcut;
            }
        }

        const std::wstring normalizedDestination = NormalizeStoredPath(destinationPath);
        for (std::wstring& assignedPath : shortcutAssignments_)
        {
            if (!assignedPath.empty()
                && hyperbrowse::util::NormalizedPathEquals(assignedPath, normalizedDestination))
            {
                assignedPath.clear();
            }
        }

        if (shortcut >= 0)
        {
            shortcutAssignments_[static_cast<std::size_t>(shortcut)] = normalizedDestination;
        }

        return QuickSendAssignmentResult::Accepted;
    }

    std::optional<int> QuickSendModel::AssignNextAvailableShortcut(std::wstring_view destinationPath)
    {
        if (FindFavoriteIndex(destinationPath) < 0)
        {
            return std::nullopt;
        }

        if (const std::optional<int> existingShortcut = ShortcutForDestination(destinationPath))
        {
            return existingShortcut;
        }

        const std::wstring normalizedDestination = NormalizeStoredPath(destinationPath);
        const auto assignIfAvailable = [&](int shortcut) -> std::optional<int>
        {
            const std::size_t index = static_cast<std::size_t>(shortcut);
            if (shortcutAssignments_[index].empty())
            {
                shortcutAssignments_[index] = normalizedDestination;
                return shortcut;
            }
            return std::nullopt;
        };

        for (const wchar_t character : shortcutAssignmentOrder_)
        {
            const std::optional<int> shortcut = ShortcutIndexFromText(std::wstring_view(&character, 1));
            if (shortcut)
            {
                if (const std::optional<int> assignedShortcut = assignIfAvailable(*shortcut))
                {
                    return assignedShortcut;
                }
            }
        }

        for (std::size_t index = 0; index < shortcutAssignments_.size(); ++index)
        {
            if (const std::optional<int> assignedShortcut = assignIfAvailable(static_cast<int>(index)))
            {
                return assignedShortcut;
            }
        }

        return std::nullopt;
    }

    std::optional<int> QuickSendModel::ShortcutForDestination(std::wstring_view destinationPath) const
    {
        const std::wstring normalizedDestination = NormalizeStoredPath(destinationPath);
        for (std::size_t index = 0; index < shortcutAssignments_.size(); ++index)
        {
            if (!shortcutAssignments_[index].empty()
                && hyperbrowse::util::NormalizedPathEquals(shortcutAssignments_[index], normalizedDestination))
            {
                return static_cast<int>(index);
            }
        }

        return std::nullopt;
    }

    std::optional<std::wstring> QuickSendModel::DestinationForShortcut(int shortcut) const
    {
        if (shortcut < 0 || shortcut >= static_cast<int>(shortcutAssignments_.size()))
        {
            return std::nullopt;
        }

        const std::wstring& assignedPath = shortcutAssignments_[static_cast<std::size_t>(shortcut)];
        if (assignedPath.empty())
        {
            return std::nullopt;
        }

        const int favoriteIndex = FindFavoriteIndex(assignedPath);
        if (favoriteIndex < 0)
        {
            return std::nullopt;
        }

        return favoriteDestinations_[static_cast<std::size_t>(favoriteIndex)];
    }

    void QuickSendModel::PruneShortcutAssignments()
    {
        for (std::wstring& assignedPath : shortcutAssignments_)
        {
            if (!assignedPath.empty() && FindFavoriteIndex(assignedPath) < 0)
            {
                assignedPath.clear();
            }
        }
    }

    int QuickSendModel::FindFavoriteIndex(std::wstring_view destinationPath) const
    {
        for (std::size_t index = 0; index < favoriteDestinations_.size(); ++index)
        {
            if (hyperbrowse::util::NormalizedPathEquals(favoriteDestinations_[index], destinationPath))
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    std::wstring QuickSendModel::NormalizeStoredPath(std::wstring_view path)
    {
        return hyperbrowse::util::NormalizePathForComparison(path);
    }
}
