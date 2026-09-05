#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace hyperbrowse::browser
{
    class BrowserModel;
    class BrowserPane;
}

namespace hyperbrowse::services
{
    struct FileOperationUpdate;
}

namespace hyperbrowse::ui
{
    struct FileOperationTreeEffects
    {
        bool treeFolderDeleteOperation{};
        bool treeFolderMoveOperation{};
        bool treeFolderDeleteSucceeded{};
        bool treeFolderMoveSucceeded{};
        bool treeFolderRenameSucceeded{};
        bool refreshFolderTree{};
        std::wstring treeFolderMoveCreatedPath;
        std::wstring fallbackFolderPath;
        std::wstring treeFolderReloadPath;
        std::wstring treeFolderRenameCreatedPath;
    };

    FileOperationTreeEffects BuildFileOperationTreeEffects(
        const services::FileOperationUpdate& update,
        std::wstring_view treeFolderOperationPath,
        std::wstring_view treeFolderRenamePath,
        std::wstring_view treeFolderMoveSourcePath,
        std::wstring_view treeFolderMoveDestinationFolder,
        const browser::BrowserModel* browserModel);

    bool ShouldReloadCurrentFolderForFileOperation(
        const services::FileOperationUpdate& update,
        const browser::BrowserModel* browserModel,
        std::wstring_view deferredFolderWatchReloadPath,
        const FileOperationTreeEffects& treeEffects,
        bool viewerDeleteOperation,
        bool browserItemDeleteOperation,
        const std::function<bool(std::wstring_view)>& isPathInCurrentScope);

    std::wstring FindDeleteFallbackFocusPath(
        const services::FileOperationUpdate& update,
        bool viewerDeleteOperation,
        const browser::BrowserModel* browserModel,
        const browser::BrowserPane* browserPaneController);
}
