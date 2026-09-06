#include "ui/FolderTreeDragController.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "ui/FolderTreeDropPolicy.h"

namespace fs = std::filesystem;

namespace hyperbrowse::ui
{
    FolderTreeDragController::~FolderTreeDragController()
    {
        Finish(false);
    }

    void FolderTreeDragController::Configure(HWND ownerWindow, HWND treeWindow, Handlers handlers)
    {
        if (active_)
        {
            Finish(false);
        }

        ownerWindow_ = ownerWindow;
        treeWindow_ = treeWindow;
        handlers_ = std::move(handlers);
    }

    bool FolderTreeDragController::Begin(const NMTREEVIEWW& treeView)
    {
        if (!ownerWindow_ || !treeWindow_ || !handlers_.nodePath
            || (handlers_.isFileOperationActive && handlers_.isFileOperationActive()))
        {
            return false;
        }

        const HTREEITEM sourceItem = treeView.itemNew.hItem;
        const std::wstring nodePath = handlers_.nodePath(sourceItem);
        if (nodePath.empty() || !TreeView_GetParent(treeWindow_, sourceItem))
        {
            return false;
        }

        if (active_)
        {
            Finish(false);
        }

        active_ = true;
        dropAllowed_ = false;
        sourceItem_ = sourceItem;
        hoverItem_ = nullptr;
        sourcePath_ = handlers_.normalizePath ? handlers_.normalizePath(nodePath) : nodePath;
        destinationPath_.clear();

        TreeView_SelectDropTarget(treeWindow_, nullptr);

        int preferredHotspotX = 8;
        int preferredHotspotY = 8;
        dragImageList_ = nullptr;

        if (HIMAGELIST treeImageList = TreeView_GetImageList(treeWindow_, TVSIL_NORMAL))
        {
            int iconWidth = 0;
            int iconHeight = 0;
            if (ImageList_GetIconSize(treeImageList, &iconWidth, &iconHeight) != FALSE)
            {
                if (iconWidth > 0)
                {
                    preferredHotspotX = iconWidth / 2;
                }
                if (iconHeight > 0)
                {
                    preferredHotspotY = iconHeight / 2;
                }
            }

            TVITEMW dragItem{};
            dragItem.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            dragItem.hItem = sourceItem_;
            if (TreeView_GetItem(treeWindow_, &dragItem) != FALSE)
            {
                const int dragImageIndex = dragItem.iSelectedImage >= 0 ? dragItem.iSelectedImage : dragItem.iImage;
                if (dragImageIndex >= 0)
                {
                    if (HICON dragIcon = ImageList_GetIcon(treeImageList, dragImageIndex, ILD_NORMAL))
                    {
                        const int dragImageWidth = (std::max)(1, preferredHotspotX * 2);
                        const int dragImageHeight = (std::max)(1, preferredHotspotY * 2);
                        dragImageList_ = ImageList_Create(dragImageWidth, dragImageHeight, ILC_COLOR32 | ILC_MASK, 1, 1);
                        if (dragImageList_)
                        {
                            if (ImageList_AddIcon(dragImageList_, dragIcon) == -1)
                            {
                                ImageList_Destroy(dragImageList_);
                                dragImageList_ = nullptr;
                            }
                        }
                        DestroyIcon(dragIcon);
                    }
                }
            }
        }

        if (!dragImageList_)
        {
            dragImageList_ = TreeView_CreateDragImage(treeWindow_, sourceItem_);
        }

        POINT dragScreenPoint = treeView.ptDrag;
        ClientToScreen(treeWindow_, &dragScreenPoint);

        if (dragImageList_)
        {
            int hotspotX = preferredHotspotX;
            int hotspotY = preferredHotspotY;

            int imageWidth = 0;
            int imageHeight = 0;
            if (ImageList_GetIconSize(dragImageList_, &imageWidth, &imageHeight) != FALSE)
            {
                if (imageWidth > 0)
                {
                    hotspotX = (std::clamp)(hotspotX, 0, imageWidth - 1);
                }
                if (imageHeight > 0)
                {
                    hotspotY = (std::clamp)(hotspotY, 0, imageHeight - 1);
                }
            }

            ImageList_BeginDrag(dragImageList_, 0, hotspotX, hotspotY);
            // ImageList_DragEnter expects coordinates relative to the full window origin.
            RECT windowRect{};
            GetWindowRect(ownerWindow_, &windowRect);
            const POINT dragWindowPoint{
                dragScreenPoint.x - windowRect.left,
                dragScreenPoint.y - windowRect.top,
            };
            ImageList_DragEnter(ownerWindow_, dragWindowPoint.x, dragWindowPoint.y);
        }

        SetCapture(ownerWindow_);
        POINT ownerClientPoint = dragScreenPoint;
        ScreenToClient(ownerWindow_, &ownerClientPoint);
        Update(ownerClientPoint);
        return true;
    }

    void FolderTreeDragController::Update(POINT ownerClientPoint)
    {
        if (!active_)
        {
            return;
        }

        bool dragImageTemporarilyHidden = false;

        POINT screenPoint = ownerClientPoint;
        ClientToScreen(ownerWindow_, &screenPoint);
        if (dragImageList_)
        {
            // ImageList_DragMove expects coordinates relative to the full window origin.
            RECT windowRect{};
            GetWindowRect(ownerWindow_, &windowRect);
            ImageList_DragMove(screenPoint.x - windowRect.left, screenPoint.y - windowRect.top);

            ImageList_DragShowNolock(FALSE);
            dragImageTemporarilyHidden = true;
        }

        HTREEITEM nextHoverItem = nullptr;
        std::wstring nextDestinationPath;
        bool nextDropAllowed = false;
        if (treeWindow_ && handlers_.nodePath && handlers_.normalizePath
            && handlers_.isExistingDirectory && handlers_.areFoldersOnSameDrive)
        {
            RECT treeRect{};
            GetClientRect(treeWindow_, &treeRect);

            POINT treePoint = screenPoint;
            ScreenToClient(treeWindow_, &treePoint);
            if (PtInRect(&treeRect, treePoint) != FALSE)
            {
                TVHITTESTINFO hitTest{};
                hitTest.pt = treePoint;
                const HTREEITEM item = TreeView_HitTest(treeWindow_, &hitTest);
                if (item && (hitTest.flags & TVHT_ONITEM) != 0 && item != sourceItem_)
                {
                    const std::wstring nodePath = handlers_.nodePath(item);
                    if (!nodePath.empty())
                    {
                        const std::wstring normalizedDestinationPath = handlers_.normalizePath(nodePath);
                        const std::wstring sourceParentPath = handlers_.normalizePath(
                            fs::path(sourcePath_).parent_path().wstring());
                        if (FolderTreeDropPolicy::IsValid({
                                sourcePath_,
                                normalizedDestinationPath,
                                sourceParentPath,
                                handlers_.isExistingDirectory(normalizedDestinationPath),
                                handlers_.areFoldersOnSameDrive(sourcePath_, normalizedDestinationPath)}))
                        {
                            nextHoverItem = item;
                            nextDestinationPath = normalizedDestinationPath;
                            nextDropAllowed = true;
                        }
                    }
                }
            }
        }

        if (nextHoverItem != hoverItem_)
        {
            hoverItem_ = nextHoverItem;
            TreeView_SelectDropTarget(treeWindow_, hoverItem_);
        }

        if (dragImageTemporarilyHidden)
        {
            ImageList_DragShowNolock(TRUE);
        }

        dropAllowed_ = nextDropAllowed;
        destinationPath_ = nextDropAllowed ? std::move(nextDestinationPath) : std::wstring{};
        SetCursor(LoadCursorW(nullptr, dropAllowed_ ? IDC_HAND : IDC_NO));
    }

    void FolderTreeDragController::Finish(bool commitDrop)
    {
        if (!active_)
        {
            return;
        }

        const std::wstring sourcePath = sourcePath_;
        const std::wstring destinationPath = (commitDrop && dropAllowed_)
            ? destinationPath_
            : std::wstring{};

        active_ = false;
        dropAllowed_ = false;
        sourceItem_ = nullptr;
        hoverItem_ = nullptr;
        sourcePath_.clear();
        destinationPath_.clear();

        if (treeWindow_)
        {
            if (dragImageList_)
            {
                ImageList_DragShowNolock(FALSE);
            }
            TreeView_SelectDropTarget(treeWindow_, nullptr);
        }

        DestroyDragImage();

        if (GetCapture() == ownerWindow_)
        {
            ReleaseCapture();
        }

        if (commitDrop && !sourcePath.empty() && !destinationPath.empty() && handlers_.onMove)
        {
            handlers_.onMove(sourcePath, destinationPath);
        }
    }

    void FolderTreeDragController::DestroyDragImage()
    {
        if (!dragImageList_)
        {
            return;
        }

        if (ownerWindow_)
        {
            ImageList_DragLeave(ownerWindow_);
        }
        ImageList_EndDrag();
        ImageList_Destroy(dragImageList_);
        dragImageList_ = nullptr;
    }
}
