#include "ui/FolderHistory.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <utility>

namespace hyperbrowse::ui
{
    namespace
    {
        bool DefaultFolderPathComparer(std::wstring_view lhs, std::wstring_view rhs)
        {
            return _wcsicmp(std::wstring(lhs).c_str(), std::wstring(rhs).c_str()) == 0;
        }
        bool StartsWithInsensitive(std::wstring_view value, std::wstring_view prefix)
        {
            return value.size() >= prefix.size()
                && _wcsnicmp(value.data(), prefix.data(), prefix.size()) == 0;
        }
        std::size_t UncRootLength(std::wstring_view path, std::size_t serverStart)
        {
            const std::size_t serverEnd = path.find(L'\\', serverStart);
            if (serverEnd == std::wstring_view::npos)
            {
                return path.size();
            }
            const std::size_t shareEnd = path.find(L'\\', serverEnd + 1);
            return shareEnd == std::wstring_view::npos ? path.size() : shareEnd;
        }
    }
    std::vector<FolderBreadcrumbSegment> BuildFolderBreadcrumbSegments(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return {};
        }
        std::wstring normalized = std::filesystem::path(folderPath).lexically_normal().wstring();
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        if (normalized.empty())
        {
            return {};
        }
        std::wstring rootLabel;
        std::wstring currentPath;
        std::size_t componentStart = 0;
        const std::wstring_view normalizedView(normalized);
        if (StartsWithInsensitive(normalizedView, L"\\\\?\\UNC\\"))
        {
            const std::size_t rootLength = UncRootLength(normalizedView, 8);
            rootLabel = normalized.substr(0, rootLength);
            currentPath = rootLabel;
        }
        else if (normalizedView.starts_with(L"\\\\")
                 && !StartsWithInsensitive(normalizedView, L"\\\\?\\")
                 && !StartsWithInsensitive(normalizedView, L"\\\\.\\"))
        {
            const std::size_t rootLength = UncRootLength(normalizedView, 2);
            rootLabel = normalized.substr(0, rootLength);
            currentPath = rootLabel;
        }
        else if (StartsWithInsensitive(normalizedView, L"\\\\?\\")
                 && normalized.size() >= 7
                 && normalized[5] == L':'
                 && normalized[6] == L'\\')
        {
            rootLabel = normalized.substr(0, 7);
            currentPath = rootLabel;
        }
        else if (normalized.size() >= 3
                 && normalized[1] == L':'
                 && normalized[2] == L'\\')
        {
            rootLabel = normalized.substr(0, 3);
            currentPath = rootLabel;
        }
        else if (normalized.front() == L'\\')
        {
            rootLabel = L"\\";
            currentPath = rootLabel;
        }
        if (!rootLabel.empty())
        {
            componentStart = rootLabel.size();
        }
        std::vector<FolderBreadcrumbSegment> segments;
        if (!rootLabel.empty())
        {
            std::wstring targetPath = currentPath;
            if (targetPath.back() != L'\\')
            {
                targetPath.push_back(L'\\');
            }
            segments.push_back(FolderBreadcrumbSegment{rootLabel, std::move(targetPath)});
        }
        while (componentStart < normalized.size())
        {
            while (componentStart < normalized.size() && normalized[componentStart] == L'\\')
            {
                ++componentStart;
            }
            if (componentStart >= normalized.size())
            {
                break;
            }
            const std::size_t componentEnd = normalized.find(L'\\', componentStart);
            const std::size_t end = componentEnd == std::wstring::npos ? normalized.size() : componentEnd;
            std::wstring label = normalized.substr(componentStart, end - componentStart);
            if (!label.empty() && label != L".")
            {
                if (!currentPath.empty() && currentPath.back() != L'\\')
                {
                    currentPath.push_back(L'\\');
                }
                currentPath.append(label);
                segments.push_back(FolderBreadcrumbSegment{std::move(label), currentPath});
            }
            componentStart = end;
        }
        if (segments.empty())
        {
            segments.push_back(FolderBreadcrumbSegment{normalized, normalized});
        }
        return segments;
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
