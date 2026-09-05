#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    enum class FolderHistoryNavigationDirection
    {
        None,
        Back,
        Forward,
    };

    struct FolderHistoryNavigation
    {
        FolderHistoryNavigationDirection direction{FolderHistoryNavigationDirection::None};
        std::size_t targetIndex{};
        std::wstring folderPath;
    };

    class FolderHistory
    {
    public:
        using ExistingFolderResolver = std::function<std::wstring(std::wstring_view)>;
        using FolderPathComparer = std::function<bool(std::wstring_view, std::wstring_view)>;

        explicit FolderHistory(std::size_t historyLimit = 256);

        void RecordOpenedFolder(std::wstring normalizedFolderPath);
        std::optional<FolderHistoryNavigation> FindBack(
            std::wstring_view currentFolderPath,
            const ExistingFolderResolver& resolveExistingFolder,
            const FolderPathComparer& compareFolderPaths) const;
        std::optional<FolderHistoryNavigation> FindForward(
            std::wstring_view currentFolderPath,
            const ExistingFolderResolver& resolveExistingFolder,
            const FolderPathComparer& compareFolderPaths) const;
        bool CanNavigateBack() const noexcept;
        bool CanNavigateForward() const noexcept;
        bool HasPendingNavigation() const noexcept;
        void BeginNavigation(FolderHistoryNavigationDirection direction, std::size_t targetIndex);
        void CancelPendingNavigation();

    private:
        std::optional<FolderHistoryNavigation> FindNavigation(
            FolderHistoryNavigationDirection direction,
            std::wstring_view currentFolderPath,
            const ExistingFolderResolver& resolveExistingFolder,
            const FolderPathComparer& compareFolderPaths) const;

        static constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

        std::vector<std::wstring> openedFolders_;
        std::size_t historyLimit_{};
        std::size_t currentIndex_{kInvalidIndex};
        FolderHistoryNavigationDirection pendingNavigation_{FolderHistoryNavigationDirection::None};
        std::size_t pendingTargetIndex_{kInvalidIndex};
    };
}
