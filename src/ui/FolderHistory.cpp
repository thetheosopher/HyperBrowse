#include "ui/FolderHistory.h"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace hyperbrowse::ui
{
    namespace
    {
        bool DefaultFolderPathComparer(std::wstring_view lhs, std::wstring_view rhs)
        {
            return _wcsicmp(std::wstring(lhs).c_str(), std::wstring(rhs).c_str()) == 0;
        }
    }

    FolderHistory::FolderHistory(std::size_t historyLimit)
        : historyLimit_(std::max<std::size_t>(historyLimit, 1))
    {
    }

    void FolderHistory::RecordOpenedFolder(std::wstring normalizedFolderPath)
    {
        if (normalizedFolderPath.empty())
        {
            return;
        }

        if (pendingNavigation_ != FolderHistoryNavigationDirection::None)
        {
            const std::size_t targetIndex = pendingTargetIndex_;
            CancelPendingNavigation();
            if (targetIndex < openedFolders_.size())
            {
                openedFolders_[targetIndex] = std::move(normalizedFolderPath);
                currentIndex_ = targetIndex;
                return;
            }
        }

        if (currentIndex_ != kInvalidIndex && currentIndex_ + 1 < openedFolders_.size())
        {
            openedFolders_.erase(openedFolders_.begin() + static_cast<std::ptrdiff_t>(currentIndex_ + 1),
                                 openedFolders_.end());
        }

        if (openedFolders_.empty()
            || !DefaultFolderPathComparer(openedFolders_.back(), normalizedFolderPath))
        {
            openedFolders_.push_back(std::move(normalizedFolderPath));
        }

        while (openedFolders_.size() > historyLimit_)
        {
            openedFolders_.erase(openedFolders_.begin());
        }

        currentIndex_ = openedFolders_.empty() ? kInvalidIndex : openedFolders_.size() - 1;
    }

    std::optional<FolderHistoryNavigation> FolderHistory::FindBack(
        std::wstring_view currentFolderPath,
        const ExistingFolderResolver& resolveExistingFolder,
        const FolderPathComparer& compareFolderPaths) const
    {
        return FindNavigation(FolderHistoryNavigationDirection::Back,
                              currentFolderPath,
                              resolveExistingFolder,
                              compareFolderPaths);
    }

    std::optional<FolderHistoryNavigation> FolderHistory::FindForward(
        std::wstring_view currentFolderPath,
        const ExistingFolderResolver& resolveExistingFolder,
        const FolderPathComparer& compareFolderPaths) const
    {
        return FindNavigation(FolderHistoryNavigationDirection::Forward,
                              currentFolderPath,
                              resolveExistingFolder,
                              compareFolderPaths);
    }

    bool FolderHistory::CanNavigateBack() const noexcept
    {
        return currentIndex_ != kInvalidIndex && currentIndex_ > 0;
    }

    bool FolderHistory::CanNavigateForward() const noexcept
    {
        return currentIndex_ != kInvalidIndex && currentIndex_ + 1 < openedFolders_.size();
    }

    bool FolderHistory::HasPendingNavigation() const noexcept
    {
        return pendingNavigation_ != FolderHistoryNavigationDirection::None;
    }

    void FolderHistory::BeginNavigation(FolderHistoryNavigationDirection direction, std::size_t targetIndex)
    {
        if (direction == FolderHistoryNavigationDirection::None || targetIndex >= openedFolders_.size())
        {
            CancelPendingNavigation();
            return;
        }

        pendingNavigation_ = direction;
        pendingTargetIndex_ = targetIndex;
    }

    void FolderHistory::CancelPendingNavigation()
    {
        pendingNavigation_ = FolderHistoryNavigationDirection::None;
        pendingTargetIndex_ = kInvalidIndex;
    }

    std::optional<FolderHistoryNavigation> FolderHistory::FindNavigation(
        FolderHistoryNavigationDirection direction,
        std::wstring_view currentFolderPath,
        const ExistingFolderResolver& resolveExistingFolder,
        const FolderPathComparer& compareFolderPaths) const
    {
        if (openedFolders_.empty() || currentIndex_ == kInvalidIndex)
        {
            return std::nullopt;
        }

        std::size_t candidateIndex = currentIndex_;
        while ((direction == FolderHistoryNavigationDirection::Back && candidateIndex > 0)
               || (direction == FolderHistoryNavigationDirection::Forward
                   && candidateIndex + 1 < openedFolders_.size()))
        {
            if (direction == FolderHistoryNavigationDirection::Back)
            {
                --candidateIndex;
            }
            else
            {
                ++candidateIndex;
            }

            const std::wstring resolvedFolderPath = resolveExistingFolder
                ? resolveExistingFolder(openedFolders_[candidateIndex])
                : openedFolders_[candidateIndex];
            if (resolvedFolderPath.empty())
            {
                continue;
            }

            const bool isCurrentFolder = !currentFolderPath.empty()
                && (compareFolderPaths
                    ? compareFolderPaths(currentFolderPath, resolvedFolderPath)
                    : DefaultFolderPathComparer(currentFolderPath, resolvedFolderPath));
            if (isCurrentFolder)
            {
                continue;
            }

            return FolderHistoryNavigation{direction, candidateIndex, resolvedFolderPath};
        }

        return std::nullopt;
    }
}
