#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    inline constexpr std::size_t kQuickSendDigitShortcutCount = 10;
    inline constexpr std::size_t kQuickSendLetterShortcutCount = 26;
    inline constexpr std::size_t kQuickSendShortcutCount = kQuickSendDigitShortcutCount + kQuickSendLetterShortcutCount;

    enum class QuickSendAssignmentResult
    {
        Accepted,
        DestinationNotFavorite,
        InvalidShortcut,
        DuplicateShortcut,
    };

    class QuickSendModel
    {
    public:
        using ShortcutAssignments = std::array<std::wstring, kQuickSendShortcutCount>;

        static std::vector<std::wstring> BuildFavoriteDestinations(
            const std::vector<std::wstring>& favoritePaths);

        void SetFavoriteDestinations(std::vector<std::wstring> favoritePaths);
        void SortFavoriteDestinationsByShortcut();
        const std::vector<std::wstring>& FavoriteDestinations() const noexcept;

        void SetShortcutAssignments(const ShortcutAssignments& assignments);
        const ShortcutAssignments& ShortcutAssignmentsByKey() const noexcept;

        static std::optional<int> ShortcutIndexFromText(std::wstring_view shortcutText);
        static wchar_t ShortcutCharacter(int shortcut);

        QuickSendAssignmentResult SetShortcutForDestination(
            std::wstring_view destinationPath,
            std::wstring_view shortcutText);
        std::optional<int> AssignNextAvailableShortcut(std::wstring_view destinationPath);
        std::optional<int> ShortcutForDestination(std::wstring_view destinationPath) const;
        std::optional<std::wstring> DestinationForShortcut(int shortcut) const;

        void PruneShortcutAssignments();

    private:
        int FindFavoriteIndex(std::wstring_view destinationPath) const;
        static std::wstring NormalizeStoredPath(std::wstring_view path);

        std::vector<std::wstring> favoriteDestinations_;
        ShortcutAssignments shortcutAssignments_{};
    };
}