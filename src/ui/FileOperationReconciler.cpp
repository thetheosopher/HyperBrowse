#include "ui/FileOperationReconciler.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "services/FileOperationService.h"
#include "util/PathUtils.h"

namespace fs = std::filesystem;

namespace hyperbrowse::ui
{
    namespace
    {
    constexpr std::size_t kIncrementalFileOperationPathLimit = 64;

        std::wstring NormalizeFolderPath(std::wstring path)
        {
            std::replace(path.begin(), path.end(), L'/', L'\\');
            while (path.size() > 3 && !path.empty() && path.back() == L'\\')
            {
                path.pop_back();
            }

            if (path.size() == 2 && path[1] == L':')
            {
                path.push_back(L'\\');
            }

            return path;
        }

        bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
        {
            const std::wstring normalizedLeft = NormalizeFolderPath(std::wstring(lhs));
            const std::wstring normalizedRight = NormalizeFolderPath(std::wstring(rhs));
            return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
        }

        std::wstring RewritePathPrefix(std::wstring_view path,
                                       std::wstring_view oldPrefix,
                                       std::wstring_view newPrefix)
        {
            if (!browser::PathHasPrefix(path, oldPrefix))
            {
                return std::wstring(path);
            }

            std::wstring rewrittenPath(newPrefix);
            std::wstring suffix(std::wstring(path).substr(oldPrefix.size()));
            if (!rewrittenPath.empty() && !suffix.empty() && rewrittenPath.back() == L'\\' && suffix.front() == L'\\')
            {
                suffix.erase(suffix.begin());
            }

            rewrittenPath.append(suffix);
            return NormalizeFolderPath(std::move(rewrittenPath));
        }

        std::wstring FindExistingFolderAncestor(fs::path candidate)
        {
            std::error_code error;
            while (!candidate.empty())
            {
                if (fs::is_directory(candidate, error) && !error)
                {
                    return NormalizeFolderPath(candidate.wstring());
                }

                error.clear();
                const fs::path parent = candidate.parent_path();
                if (parent == candidate)
                {
                    break;
                }

                candidate = parent;
            }

            return {};
        }
    }

    FileOperationTreeEffects BuildFileOperationTreeEffects(
        const services::FileOperationUpdate& update,
        std::wstring_view treeFolderOperationPath,
        std::wstring_view treeFolderRenamePath,
        std::wstring_view treeFolderMoveSourcePath,
        std::wstring_view treeFolderMoveDestinationFolder,
        const browser::BrowserModel* browserModel)
    {
        FileOperationTreeEffects effects;
        effects.treeFolderDeleteOperation = !treeFolderOperationPath.empty()
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);
        effects.treeFolderMoveOperation = !treeFolderMoveSourcePath.empty()
            && update.type == services::FileOperationType::Move;
        effects.treeFolderDeleteSucceeded = effects.treeFolderDeleteOperation
            && std::any_of(update.succeededSourcePaths.begin(), update.succeededSourcePaths.end(), [&](const std::wstring& sourcePath)
            {
                return FolderPathsEqual(sourcePath, treeFolderOperationPath);
            });

        if (effects.treeFolderMoveOperation)
        {
            const std::size_t movePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
            for (std::size_t index = 0; index < movePairCount; ++index)
            {
                if (!FolderPathsEqual(update.succeededSourcePaths[index], treeFolderMoveSourcePath))
                {
                    continue;
                }

                effects.treeFolderMoveCreatedPath = NormalizeFolderPath(update.createdPaths[index]);
                break;
            }

            if (effects.treeFolderMoveCreatedPath.empty())
            {
                const bool sourcePathReported = std::any_of(update.succeededSourcePaths.begin(), update.succeededSourcePaths.end(), [&](const std::wstring& sourcePath)
                {
                    return FolderPathsEqual(sourcePath, treeFolderMoveSourcePath);
                });
                if (sourcePathReported && !treeFolderMoveDestinationFolder.empty())
                {
                    effects.treeFolderMoveCreatedPath = NormalizeFolderPath(
                        (fs::path(treeFolderMoveDestinationFolder) / fs::path(treeFolderMoveSourcePath).filename()).wstring());
                }
            }
        }
        effects.treeFolderMoveSucceeded = effects.treeFolderMoveOperation
            && !effects.treeFolderMoveCreatedPath.empty();

        effects.refreshFolderTree = effects.treeFolderDeleteSucceeded || effects.treeFolderMoveSucceeded;
        if (effects.treeFolderDeleteSucceeded
            && browserModel
            && !browserModel->FolderPath().empty()
            && browser::PathHasPrefix(browserModel->FolderPath(), treeFolderOperationPath))
        {
            effects.fallbackFolderPath = FindExistingFolderAncestor(fs::path(treeFolderOperationPath).parent_path());
        }

        if (!treeFolderRenamePath.empty() && update.type == services::FileOperationType::Rename)
        {
            const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
            for (std::size_t index = 0; index < renamePairCount; ++index)
            {
                if (!FolderPathsEqual(update.succeededSourcePaths[index], treeFolderRenamePath))
                {
                    continue;
                }

                effects.refreshFolderTree = true;
                effects.treeFolderRenameCreatedPath = NormalizeFolderPath(update.createdPaths[index]);
                if (browserModel && !browserModel->FolderPath().empty())
                {
                    if (browser::PathHasPrefix(browserModel->FolderPath(), treeFolderRenamePath))
                    {
                        effects.treeFolderReloadPath = RewritePathPrefix(
                            browserModel->FolderPath(),
                            treeFolderRenamePath,
                            update.createdPaths[index]);
                    }
                    else if (browserModel->IsRecursive()
                        && browser::PathHasPrefix(treeFolderRenamePath, browserModel->FolderPath()))
                    {
                        effects.treeFolderReloadPath = browserModel->FolderPath();
                    }
                }
                effects.treeFolderRenameSucceeded = true;
                break;
            }
        }

        return effects;
    }

    bool ShouldReloadCurrentFolderForFileOperation(
        const services::FileOperationUpdate& update,
        const browser::BrowserModel* browserModel,
        std::wstring_view deferredFolderWatchReloadPath,
        const FileOperationTreeEffects& treeEffects,
        bool viewerDeleteOperation,
        bool browserItemDeleteOperation,
        const std::function<bool(std::wstring_view)>& isPathInCurrentScope)
    {
        bool reloadCurrentFolder = browserModel
            && !browserModel->FolderPath().empty()
            && !deferredFolderWatchReloadPath.empty()
            && FolderPathsEqual(browserModel->FolderPath(), deferredFolderWatchReloadPath);

        if (!treeEffects.treeFolderReloadPath.empty())
        {
            reloadCurrentFolder = true;
        }
        else if (treeEffects.treeFolderMoveSucceeded && browserModel && !browserModel->FolderPath().empty())
        {
            reloadCurrentFolder = true;
        }

        if (!reloadCurrentFolder && !browserItemDeleteOperation && browserModel && !browserModel->FolderPath().empty())
        {
            const std::size_t affectedCount = update.succeededSourcePaths.size() + update.createdPaths.size();
            if (affectedCount >= kIncrementalFileOperationPathLimit && isPathInCurrentScope)
            {
                const auto pathAffectsCurrentScope = [&](const std::wstring& path)
                {
                    return isPathInCurrentScope(path)
                        || browser::PathHasPrefix(browserModel->FolderPath(), path);
                };

                reloadCurrentFolder = std::any_of(
                    update.createdPaths.begin(),
                    update.createdPaths.end(),
                    pathAffectsCurrentScope)
                    || std::any_of(
                        update.succeededSourcePaths.begin(),
                        update.succeededSourcePaths.end(),
                        pathAffectsCurrentScope);
            }
        }

        if (viewerDeleteOperation)
        {
            reloadCurrentFolder = false;
        }
        else if (browserItemDeleteOperation && !treeEffects.treeFolderDeleteOperation)
        {
            reloadCurrentFolder = false;
        }

        return reloadCurrentFolder;
    }

    std::wstring FindDeleteFallbackFocusPath(
        const services::FileOperationUpdate& update,
        bool viewerDeleteOperation,
        const browser::BrowserModel* browserModel,
        const browser::BrowserPane* browserPaneController)
    {
        if (viewerDeleteOperation
            || (update.type != services::FileOperationType::DeleteRecycleBin
                && update.type != services::FileOperationType::DeletePermanent)
            || !browserModel
            || !browserPaneController)
        {
            return {};
        }

        const auto& itemsBeforeDelete = browserModel->Items();
        const std::vector<int> orderedModelIndices = browserPaneController->OrderedModelIndicesSnapshot();
        std::unordered_set<std::wstring> deletedPaths;
        deletedPaths.reserve(update.succeededSourcePaths.size());
        for (const std::wstring& deletedPath : update.succeededSourcePaths)
        {
            deletedPaths.insert(util::NormalizePathForComparison(deletedPath));
        }
        const auto pathAtOrdinal = [&](int ordinal) -> const std::wstring*
        {
            if (ordinal < 0 || ordinal >= static_cast<int>(orderedModelIndices.size()))
            {
                return nullptr;
            }

            const int modelIndex = orderedModelIndices[static_cast<std::size_t>(ordinal)];
            if (modelIndex < 0 || modelIndex >= static_cast<int>(itemsBeforeDelete.size()))
            {
                return nullptr;
            }

            return &itemsBeforeDelete[static_cast<std::size_t>(modelIndex)].filePath;
        };
        const auto isDeletedPath = [&](const std::wstring& path)
        {
            return deletedPaths.contains(util::NormalizePathForComparison(path));
        };

        const int ordinalCount = static_cast<int>(orderedModelIndices.size());
        int lastDeletedOrdinal = -1;
        for (int ordinal = 0; ordinal < ordinalCount; ++ordinal)
        {
            const std::wstring* path = pathAtOrdinal(ordinal);
            if (path && isDeletedPath(*path))
            {
                lastDeletedOrdinal = ordinal;
            }
        }

        for (int ordinal = lastDeletedOrdinal + 1; lastDeletedOrdinal >= 0 && ordinal < ordinalCount; ++ordinal)
        {
            const std::wstring* path = pathAtOrdinal(ordinal);
            if (path && !isDeletedPath(*path))
            {
                return *path;
            }
        }

        for (int ordinal = lastDeletedOrdinal - 1; ordinal >= 0; --ordinal)
        {
            const std::wstring* path = pathAtOrdinal(ordinal);
            if (path && !isDeletedPath(*path))
            {
                return *path;
            }
        }

        return {};
    }
}
