#pragma once

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <string>
#include <string_view>

namespace hyperbrowse::ui
{
    class FolderTreeDragController final
    {
    public:
        using OperationActiveHandler = std::function<bool()>;
        using NodePathHandler = std::function<std::wstring(HTREEITEM)>;
        using NormalizePathHandler = std::function<std::wstring(std::wstring)>;
        using ExistingDirectoryHandler = std::function<bool(std::wstring_view)>;
        using SameDriveHandler = std::function<bool(std::wstring_view, std::wstring_view)>;
        using MoveHandler = std::function<void(std::wstring, std::wstring)>;

        struct Handlers
        {
            OperationActiveHandler isFileOperationActive;
            NodePathHandler nodePath;
            NormalizePathHandler normalizePath;
            ExistingDirectoryHandler isExistingDirectory;
            SameDriveHandler areFoldersOnSameDrive;
            MoveHandler onMove;
        };

        FolderTreeDragController() = default;
        FolderTreeDragController(const FolderTreeDragController&) = delete;
        FolderTreeDragController& operator=(const FolderTreeDragController&) = delete;
        ~FolderTreeDragController();

        void Configure(HWND ownerWindow, HWND treeWindow, Handlers handlers);
        bool Begin(const NMTREEVIEWW& treeView);
        void Update(POINT ownerClientPoint);
        void Finish(bool commitDrop);

        bool IsActive() const noexcept { return active_; }
        bool IsDropAllowed() const noexcept { return dropAllowed_; }

    private:
        void DestroyDragImage();

        HWND ownerWindow_{};
        HWND treeWindow_{};
        Handlers handlers_;
        bool active_{};
        bool dropAllowed_{};
        HIMAGELIST dragImageList_{};
        HTREEITEM sourceItem_{};
        HTREEITEM hoverItem_{};
        std::wstring sourcePath_;
        std::wstring destinationPath_;
    };
}
