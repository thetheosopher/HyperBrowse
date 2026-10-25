#include "ui/FolderTreeController.h"

#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <system_error>
#include <utility>

#include "util/Log.h"

namespace fs = std::filesystem;

namespace hyperbrowse::ui
{
    namespace
    {
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

        bool ShouldShowFolder(std::wstring_view folderPath)
        {
            if (folderPath.size() == 3 && folderPath[1] == L':' && folderPath[2] == L'\\')
            {
                return true;
            }

            const DWORD attributes = GetFileAttributesW(std::wstring(folderPath).c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_HIDDEN) == 0)
            {
                return true;
            }

            SHELLFLAGSTATE shellState{};
            SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
            return shellState.fShowAllObjects != FALSE;
        }

        std::wstring GetFolderDisplayName(std::wstring_view folderPath)
        {
            if (folderPath.empty())
            {
                return L"No Folder";
            }

            const fs::path path(folderPath);
            const std::wstring leaf = path.filename().wstring();
            return leaf.empty() ? std::wstring(folderPath) : leaf;
        }

        std::wstring FormatDriveDisplayName(const std::wstring& rootPath)
        {
            wchar_t volumeName[MAX_PATH]{};
            if (GetVolumeInformationW(
                    rootPath.c_str(),
                    volumeName,
                    static_cast<DWORD>(std::size(volumeName)),
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    0) != FALSE
                && volumeName[0] != L'\0')
            {
                std::wstring label = volumeName;
                label.append(L" (");
                label.append(rootPath.substr(0, 2));
                label.append(L")");
                return label;
            }

            return rootPath;
        }

        std::wstring TryGetKnownFolderPath(REFKNOWNFOLDERID folderId)
        {
            PWSTR rawPath = nullptr;
            const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
            if (FAILED(result) || !rawPath)
            {
                return {};
            }

            std::wstring path = rawPath;
            CoTaskMemFree(rawPath);
            return NormalizeFolderPath(std::move(path));
        }

        struct ShellItemInfo
        {
            std::wstring displayName;
            int iconIndex{};
            int openIconIndex{};
        };

        ShellItemInfo QueryShellItemInfo(const std::wstring& folderPath)
        {
            ShellItemInfo info;
            SHFILEINFOW shellInfo{};
            if (SHGetFileInfoW(
                    folderPath.c_str(),
                    FILE_ATTRIBUTE_DIRECTORY,
                    &shellInfo,
                    sizeof(shellInfo),
                    SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
            {
                info.displayName = shellInfo.szDisplayName;
                info.iconIndex = shellInfo.iIcon;
            }

            if (SHGetFileInfoW(
                    folderPath.c_str(),
                    FILE_ATTRIBUTE_DIRECTORY,
                    &shellInfo,
                    sizeof(shellInfo),
                    SHGFI_OPENICON | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
            {
                info.openIconIndex = shellInfo.iIcon;
            }
            else
            {
                info.openIconIndex = info.iconIndex;
            }

            if (info.displayName.empty())
            {
                const fs::path path(folderPath);
                const std::wstring normalizedRoot = NormalizeFolderPath(path.root_path().wstring());
                info.displayName = FolderPathsEqual(normalizedRoot, folderPath)
                    ? FormatDriveDisplayName(folderPath)
                    : GetFolderDisplayName(folderPath);
            }

            return info;
        }

        struct GenericFolderIcons
        {
            int iconIndex{};
            int openIconIndex{};
        };

        // The generic folder icons never change within a session, so cache them.
        const GenericFolderIcons& GetGenericFolderIcons()
        {
            static const GenericFolderIcons icons = []
            {
                GenericFolderIcons result;
                SHFILEINFOW shellInfo{};
                constexpr UINT baseFlags = SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
                if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &shellInfo, sizeof(shellInfo), baseFlags) != 0)
                {
                    result.iconIndex = shellInfo.iIcon;
                }
                result.openIconIndex = result.iconIndex;
                if (SHGetFileInfoW(
                        L"folder",
                        FILE_ATTRIBUTE_DIRECTORY,
                        &shellInfo,
                        sizeof(shellInfo),
                        baseFlags | SHGFI_OPENICON) != 0)
                {
                    result.openIconIndex = shellInfo.iIcon;
                }
                return result;
            }();
            return icons;
        }

        std::wstring PathKey(std::wstring_view path)
        {
            std::wstring key = NormalizeFolderPath(std::wstring(path));
            if (!key.empty())
            {
                CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
            }
            return key;
        }
    }

    FolderTreeController::~FolderTreeController()
    {
        StopSliceTimer();
    }
    void FolderTreeController::Configure(HWND ownerWindow, HWND treeWindow, Handlers handlers)
    {
        ownerWindow_ = ownerWindow;
        treeWindow_ = treeWindow;
        handlers_ = std::move(handlers);
    }

    void FolderTreeController::Cancel()
    {
        service_.CancelAll();
        StopSliceTimer();
        fills_.clear();
        shellInfoQueue_.clear();
        pendingEnumerationItems_.clear();
        pendingChildPresenceItems_.clear();
        pendingSelectionPath_.clear();
        NotifyStatusChanged();
    }

    bool FolderTreeController::IsBusy() const noexcept
    {
        return !pendingEnumerationItems_.empty() || !pendingChildPresenceItems_.empty() || !fills_.empty();
    }

    void FolderTreeController::Initialize(std::wstring selectedFolderPath)
    {
        Cancel();
        pendingSelectionPath_.clear();
        SetSelectionSuppressed(true);
        if (treeWindow_)
        {
            TreeView_DeleteAllItems(treeWindow_);
        }
        nodes_.clear();
        PopulateSpecialFolderRoots();
        PopulateDriveRoots();
        SetSelectionSuppressed(false);
        SelectFolder(std::move(selectedFolderPath));
    }

    void FolderTreeController::Refresh(std::wstring selectedFolderPath)
    {
        selectionRestorePath_.clear();
        selectionRestoreY_ = 0;
        if (!selectedFolderPath.empty())
        {
            const HTREEITEM selectedItem = FindItemByPath(selectedFolderPath);
            RECT itemRect{};
            if (selectedItem && treeWindow_ && TreeView_GetItemRect(treeWindow_, selectedItem, &itemRect, TRUE))
            {
                selectionRestorePath_ = selectedFolderPath;
                selectionRestoreY_ = itemRect.top;
            }
        }

        Initialize(selectedFolderPath);
    }

    void FolderTreeController::PopulateSpecialFolderRoots()
    {
        const KNOWNFOLDERID specialFolderIds[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Pictures,
        };

        for (const KNOWNFOLDERID& specialFolderId : specialFolderIds)
        {
            const std::wstring folderPath = TryGetKnownFolderPath(specialFolderId);
            if (folderPath.empty())
            {
                continue;
            }

            std::error_code error;
            if (!fs::is_directory(fs::path(folderPath), error) || error)
            {
                continue;
            }

            if (!FindChildItem(nullptr, folderPath))
            {
                InsertItem(TVI_ROOT, folderPath);
            }
        }
    }

    void FolderTreeController::PopulateDriveRoots()
    {
        const DWORD driveMask = GetLogicalDrives();
        for (wchar_t driveLetter = L'A'; driveLetter <= L'Z'; ++driveLetter)
        {
            const DWORD driveBit = 1UL << (driveLetter - L'A');
            if ((driveMask & driveBit) == 0)
            {
                continue;
            }

            std::wstring drivePath;
            drivePath.push_back(driveLetter);
            drivePath.append(L":\\");
            const UINT driveType = GetDriveTypeW(drivePath.c_str());
            if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN)
            {
                continue;
            }

            if (!FindChildItem(nullptr, drivePath))
            {
                InsertItem(TVI_ROOT, drivePath);
            }
        }
    }

    HTREEITEM FolderTreeController::InsertItem(HTREEITEM parentItem,
                                                const std::wstring& folderPath,
                                                bool childrenKnown,
                                                bool hasChildren,
                                                bool requestPresence,
                                                bool deferShellInfo)
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        if (!ShouldShowFolder(normalizedPath))
        {
            return nullptr;
        }

        if (!childrenKnown && requestPresence)
        {
            bool cachedHasChildren = false;
            if (TryGetCachedChildPresence(normalizedPath, &cachedHasChildren))
            {
                childrenKnown = true;
                hasChildren = cachedHasChildren;
            }
        }

        // Deferred rows start with the generic folder look and are refined later in time slices.
        ShellItemInfo shellInfo;
        if (deferShellInfo)
        {
            const GenericFolderIcons& genericIcons = GetGenericFolderIcons();
            shellInfo.displayName = GetFolderDisplayName(normalizedPath);
            shellInfo.iconIndex = genericIcons.iconIndex;
            shellInfo.openIconIndex = genericIcons.openIconIndex;
        }
        else
        {
            shellInfo = QueryShellItemInfo(normalizedPath);
        }

        auto nodeData = std::make_unique<NodeData>();
        nodeData->path = normalizedPath;
        nodeData->childrenKnown = childrenKnown;
        nodeData->hasChildren = hasChildren;
        nodeData->shellInfoPending = deferShellInfo;
        NodeData* rawNodeData = nodeData.get();
        nodes_.push_back(std::move(nodeData));

        TVINSERTSTRUCTW item{};
        item.hParent = parentItem;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
        item.item.pszText = const_cast<LPWSTR>(shellInfo.displayName.c_str());
        item.item.iImage = shellInfo.iconIndex;
        item.item.iSelectedImage = shellInfo.openIconIndex;
        item.item.lParam = reinterpret_cast<LPARAM>(rawNodeData);
        item.item.cChildren = childrenKnown && hasChildren ? 1 : 0;

        const HTREEITEM insertedItem = treeWindow_ ? TreeView_InsertItem(treeWindow_, &item) : nullptr;
        if (!insertedItem)
        {
            rawNodeData->shellInfoPending = false;
        }
        else if (deferShellInfo)
        {
            shellInfoQueue_.push_back(insertedItem);
        }

        if (insertedItem && childrenKnown && hasChildren)
        {
            AddPlaceholder(insertedItem);
        }
        else if (insertedItem && !childrenKnown && requestPresence)
        {
            RequestChildPresence({insertedItem});
        }

        return insertedItem;
    }

    void FolderTreeController::AddPlaceholder(HTREEITEM parentItem)
    {
        if (!treeWindow_ || !parentItem)
        {
            return;
        }

        HTREEITEM childItem = TreeView_GetChild(treeWindow_, parentItem);
        while (childItem)
        {
            if (!GetNodeData(childItem))
            {
                return;
            }
            childItem = TreeView_GetNextSibling(treeWindow_, childItem);
        }

        TVINSERTSTRUCTW placeholder{};
        placeholder.hParent = parentItem;
        placeholder.hInsertAfter = TVI_LAST;
        placeholder.item.mask = TVIF_TEXT;
        placeholder.item.pszText = const_cast<LPWSTR>(L"");
        TreeView_InsertItem(treeWindow_, &placeholder);
    }

    void FolderTreeController::RequestChildPresence(const std::vector<HTREEITEM>& items)
    {
        if (items.empty() || !ownerWindow_)
        {
            return;
        }

        std::vector<HTREEITEM> pendingItems;
        std::vector<std::wstring> folderPaths;
        pendingItems.reserve(items.size());
        folderPaths.reserve(items.size());
        for (HTREEITEM item : items)
        {
            NodeData* nodeData = GetNodeData(item);
            if (!nodeData || nodeData->childrenKnown || nodeData->childPresenceLoading || nodeData->childrenLoading)
            {
                continue;
            }

            nodeData->childPresenceLoading = true;
            pendingItems.push_back(item);
            folderPaths.push_back(nodeData->path);
        }

        if (pendingItems.empty())
        {
            return;
        }

        const std::uint64_t requestId = service_.QueryChildDirectoryPresenceAsync(
            ownerWindow_,
            std::move(folderPaths));
        for (HTREEITEM item : pendingItems)
        {
            if (NodeData* nodeData = GetNodeData(item))
            {
                nodeData->childPresenceRequestId = requestId;
            }
        }
        pendingChildPresenceItems_[requestId] = std::move(pendingItems);
        NotifyStatusChanged();
    }

    bool FolderTreeController::TryGetCachedChildPresence(std::wstring_view folderPath, bool* hasChildren) const
    {
        if (!hasChildren || folderPath.empty())
        {
            return false;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(std::wstring(folderPath));
        const auto iterator = childPresenceCache_.find(normalizedPath);
        if (iterator == childPresenceCache_.end()
            || static_cast<std::uint64_t>(GetTickCount64()) - iterator->second.checkedTickCount
                > kChildPresenceCacheTtlMs)
        {
            return false;
        }

        *hasChildren = iterator->second.hasChildren;
        return true;
    }

    void FolderTreeController::CacheChildPresence(std::wstring_view folderPath, bool hasChildren)
    {
        if (!folderPath.empty())
        {
            childPresenceCache_[NormalizeFolderPath(std::wstring(folderPath))] =
                ChildPresenceCacheEntry{hasChildren, static_cast<std::uint64_t>(GetTickCount64())};
        }
    }

    void FolderTreeController::InvalidateChildPresence(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(std::wstring(folderPath));
        childPresenceCache_.erase(normalizedPath);
        const std::wstring parentPath = NormalizeFolderPath(fs::path(normalizedPath).parent_path().wstring());
        if (!parentPath.empty())
        {
            childPresenceCache_.erase(parentPath);
        }
    }

    void FolderTreeController::ClearChildPresenceCache()
    {
        childPresenceCache_.clear();
    }

    void FolderTreeController::UpdateChildrenIndicator(HTREEITEM item)
    {
        NodeData* nodeData = GetNodeData(item);
        if (!nodeData || !treeWindow_)
        {
            return;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_CHILDREN;
        treeItem.hItem = item;
        treeItem.cChildren = nodeData->childrenKnown && nodeData->hasChildren ? 1 : 0;
        TreeView_SetItem(treeWindow_, &treeItem);
    }

    void FolderTreeController::RequestChildren(HTREEITEM item)
    {
        NodeData* nodeData = GetNodeData(item);
        if (!nodeData || nodeData->childrenLoaded || nodeData->childrenLoading || !ownerWindow_)
        {
            return;
        }

        nodeData->childPresenceLoading = false;
        nodeData->childPresenceRequestId = 0;
        nodeData->childrenLoading = true;
        const std::uint64_t requestId = service_.EnumerateChildDirectoriesAsync(ownerWindow_, nodeData->path);
        nodeData->childEnumerationRequestId = requestId;
        pendingEnumerationItems_[requestId] = item;
        NotifyStatusChanged();
    }

    void FolderTreeController::ApplyChildren(HTREEITEM item,
                                              std::vector<services::FolderTreeChild> childFolders)
    {
        NodeData* nodeData = GetNodeData(item);
        if (!nodeData || !treeWindow_)
        {
            return;
        }

        nodeData->childrenKnown = true;
        nodeData->hasChildren = !childFolders.empty();
        CacheChildPresence(nodeData->path, nodeData->hasChildren);
        nodeData->childPresenceLoading = false;
        nodeData->childPresenceRequestId = 0;
        nodeData->childrenLoaded = true;
        nodeData->childrenLoading = false;
        nodeData->childEnumerationRequestId = 0;
        UpdateChildrenIndicator(item);

        SendMessageW(treeWindow_, WM_SETREDRAW, FALSE, 0);
        HTREEITEM childItem = TreeView_GetChild(treeWindow_, item);
        while (childItem)
        {
            HTREEITEM nextSibling = TreeView_GetNextSibling(treeWindow_, childItem);
            TreeView_DeleteItem(treeWindow_, childItem);
            childItem = nextSibling;
        }
        SendMessageW(treeWindow_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(treeWindow_, nullptr, FALSE);

        if (childFolders.empty())
        {
            ContinueSelectingFolder();
            return;
        }

        FillState fill;
        fill.parent = item;
        fill.parentData = nodeData;
        fill.children = std::move(childFolders);
        fill.inserted.reserve(fill.children.size());
        nodeData->childrenFilling = true;
        const bool runsNow = fills_.empty();
        fills_.push_back(std::move(fill));
        NotifyStatusChanged();

        if (runsNow)
        {
            RunSlice(Clock::now() + kSliceBudget);
        }
        else
        {
            EnsureLoadingItem(fills_.back());
        }

        if (HasPendingSliceWork())
        {
            ScheduleSlices();
        }
    }

    bool FolderTreeController::HasPendingSliceWork() const noexcept
    {
        return !fills_.empty() || !shellInfoQueue_.empty();
    }

    void CALLBACK FolderTreeController::SliceTimerProc(HWND, UINT, UINT_PTR timerId, DWORD)
    {
        auto* controller = reinterpret_cast<FolderTreeController*>(timerId);
        controller->RunSlice(Clock::now() + kSliceBudget);
        if (!controller->HasPendingSliceWork())
        {
            controller->StopSliceTimer();
        }
    }

    void FolderTreeController::ScheduleSlices()
    {
        if (sliceTimerActive_ || !ownerWindow_)
        {
            return;
        }

        if (SetTimer(ownerWindow_, reinterpret_cast<UINT_PTR>(this), kSliceIntervalMs, &FolderTreeController::SliceTimerProc) != 0)
        {
            sliceTimerActive_ = true;
            return;
        }

        // Without a timer the work would never finish, so drain it synchronously.
        while (HasPendingSliceWork())
        {
            RunSlice(Clock::time_point::max());
        }
    }

    void FolderTreeController::StopSliceTimer()
    {
        if (sliceTimerActive_)
        {
            KillTimer(ownerWindow_, reinterpret_cast<UINT_PTR>(this));
            sliceTimerActive_ = false;
        }
    }

    void FolderTreeController::RunSlice(Clock::time_point deadline)
    {
        while (!fills_.empty() && Clock::now() < deadline)
        {
            FillState& fill = fills_.front();
            const Clock::time_point sliceStart = Clock::now();
            const bool finished = RunFillSlice(fill, deadline);
            fill.uiTime += Clock::now() - sliceStart;
            if (!finished)
            {
                EnsureLoadingItem(fill);
                break;
            }

            FillState completed = std::move(fill);
            fills_.pop_front();
            FinishFill(completed);
        }

        if (fills_.empty() && !shellInfoQueue_.empty())
        {
            RunShellInfoSlice(deadline);
        }
    }

    bool FolderTreeController::RunFillSlice(FillState& fill, Clock::time_point deadline)
    {
        if (!treeWindow_ || GetNodeData(fill.parent) != fill.parentData)
        {
            fill.stale = true;
            return true;
        }

        SendMessageW(treeWindow_, WM_SETREDRAW, FALSE, 0);
        do
        {
            const services::FolderTreeChild& child = fill.children[fill.next++];
            if (const HTREEITEM inserted = InsertItem(fill.parent, child.path, false, false, false, true))
            {
                fill.inserted.push_back(inserted);
            }
        } while (fill.next < fill.children.size() && Clock::now() < deadline);
        SendMessageW(treeWindow_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(treeWindow_, nullptr, FALSE);

        return fill.next >= fill.children.size();
    }

    void FolderTreeController::EnsureLoadingItem(FillState& fill)
    {
        if (fill.loadingItem || !treeWindow_ || GetNodeData(fill.parent) != fill.parentData)
        {
            return;
        }

        TVINSERTSTRUCTW loading{};
        loading.hParent = fill.parent;
        loading.hInsertAfter = TVI_FIRST;
        loading.item.mask = TVIF_TEXT;
        loading.item.pszText = const_cast<LPWSTR>(L"Loading...");
        fill.loadingItem = TreeView_InsertItem(treeWindow_, &loading);
    }

    void FolderTreeController::FinishFill(FillState& fill)
    {
        if (!fill.stale)
        {
            if (fill.loadingItem)
            {
                TreeView_DeleteItem(treeWindow_, fill.loadingItem);
            }

            fill.parentData->childrenFilling = false;
            RequestChildPresence(fill.inserted);

            if (fill.children.size() >= kLargeFillLogThreshold)
            {
                const auto uiMs = std::chrono::duration_cast<std::chrono::milliseconds>(fill.uiTime).count();
                util::LogInfo(L"Folder tree filled " + std::to_wstring(fill.children.size())
                    + L" children in " + std::to_wstring(uiMs) + L" ms of UI time.");
            }
        }

        NotifyStatusChanged();
        if (!fill.stale)
        {
            ContinueSelectingFolder();
        }
    }

    void FolderTreeController::RunShellInfoSlice(Clock::time_point deadline)
    {
        if (!treeWindow_)
        {
            shellInfoQueue_.clear();
            return;
        }

        // Rows the user can see are refined first; the rest follow in insertion order.
        HTREEITEM visibleItem = TreeView_GetFirstVisible(treeWindow_);
        int visibleRemaining = static_cast<int>(TreeView_GetVisibleCount(treeWindow_)) + 1;
        while (visibleItem && visibleRemaining-- > 0 && Clock::now() < deadline)
        {
            RefineShellInfo(visibleItem);
            visibleItem = TreeView_GetNextVisible(treeWindow_, visibleItem);
        }

        while (!shellInfoQueue_.empty() && Clock::now() < deadline)
        {
            const HTREEITEM item = shellInfoQueue_.front();
            shellInfoQueue_.pop_front();
            RefineShellInfo(item);
        }
    }

    void FolderTreeController::RefineShellInfo(HTREEITEM item)
    {
        NodeData* nodeData = GetNodeData(item);
        if (!nodeData || !nodeData->shellInfoPending)
        {
            return;
        }

        nodeData->shellInfoPending = false;
        const ShellItemInfo shellInfo = QueryShellItemInfo(nodeData->path);
        const GenericFolderIcons& genericIcons = GetGenericFolderIcons();
        if (shellInfo.iconIndex == genericIcons.iconIndex
            && shellInfo.openIconIndex == genericIcons.openIconIndex
            && shellInfo.displayName == GetFolderDisplayName(nodeData->path))
        {
            return;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        treeItem.hItem = item;
        treeItem.pszText = const_cast<LPWSTR>(shellInfo.displayName.c_str());
        treeItem.iImage = shellInfo.iconIndex;
        treeItem.iSelectedImage = shellInfo.openIconIndex;
        TreeView_SetItem(treeWindow_, &treeItem);
    }

    void FolderTreeController::SelectFolder(std::wstring folderPath)
    {
        if (!treeWindow_ || folderPath.empty())
        {
            return;
        }

        pendingSelectionPath_ = NormalizeFolderPath(std::move(folderPath));
        ContinueSelectingFolder();
    }

    void FolderTreeController::ContinueSelectingFolder()
    {
        if (!treeWindow_ || pendingSelectionPath_.empty())
        {
            return;
        }

        const std::wstring normalizedPath = pendingSelectionPath_;
        HTREEITEM currentItem = FindChildItem(nullptr, normalizedPath);
        if (currentItem)
        {
            SetSelectionSuppressed(true);
            TreeView_SelectItem(treeWindow_, currentItem);
            TreeView_EnsureVisible(treeWindow_, currentItem);
            SetSelectionSuppressed(false);
            RestoreItemVerticalPosition(currentItem, normalizedPath);
            pendingSelectionPath_.clear();
            return;
        }

        const fs::path targetPath(normalizedPath);
        const std::wstring rootPath = NormalizeFolderPath(targetPath.root_path().wstring());
        if (rootPath.empty())
        {
            return;
        }

        currentItem = FindChildItem(nullptr, rootPath);
        if (!currentItem)
        {
            return;
        }

        if (!FolderPathsEqual(normalizedPath, rootPath))
        {
            fs::path currentPath(rootPath);
            for (const auto& segment : targetPath.relative_path())
            {
                if (segment.empty())
                {
                    continue;
                }

                currentPath /= segment;
                TreeView_Expand(treeWindow_, currentItem, TVE_EXPAND);
                NodeData* nodeData = GetNodeData(currentItem);
                if (!nodeData)
                {
                    return;
                }

                if (!nodeData->childrenLoaded)
                {
                    RequestChildren(currentItem);
                    return;
                }

                // The slice that finishes this fill resumes the selection.
                if (nodeData->childrenFilling)
                {
                    return;
                }
                currentItem = FindChildItem(currentItem, currentPath.wstring());
                if (!currentItem)
                {
                    pendingSelectionPath_.clear();
                    if (FolderPathsEqual(selectionRestorePath_, normalizedPath))
                    {
                        selectionRestorePath_.clear();
                        selectionRestoreY_ = 0;
                    }
                    return;
                }
            }
        }

        SetSelectionSuppressed(true);
        TreeView_SelectItem(treeWindow_, currentItem);
        TreeView_EnsureVisible(treeWindow_, currentItem);
        SetSelectionSuppressed(false);
        RestoreItemVerticalPosition(currentItem, normalizedPath);
        pendingSelectionPath_.clear();
    }

    void FolderTreeController::RestoreItemVerticalPosition(HTREEITEM item,
                                                             const std::wstring& selectedPath)
    {
        if (!treeWindow_ || !item || selectionRestorePath_.empty()
            || !FolderPathsEqual(selectionRestorePath_, selectedPath))
        {
            return;
        }

        RECT itemRect{};
        if (TreeView_GetItemRect(treeWindow_, item, &itemRect, TRUE))
        {
            const int verticalDelta = itemRect.top - selectionRestoreY_;
            if (verticalDelta != 0)
            {
                SCROLLINFO scrollInfo{};
                scrollInfo.cbSize = sizeof(scrollInfo);
                scrollInfo.fMask = SIF_ALL;
                if (GetScrollInfo(treeWindow_, SB_VERT, &scrollInfo))
                {
                    const int pageSize = static_cast<int>(scrollInfo.nPage);
                    const int maxPosition = std::max(
                        scrollInfo.nMin,
                        scrollInfo.nMax - std::max(0, pageSize - 1));
                    const int targetPosition = std::clamp(
                        scrollInfo.nPos + verticalDelta,
                        scrollInfo.nMin,
                        maxPosition);
                    SendMessageW(
                        treeWindow_,
                        WM_VSCROLL,
                        MAKEWPARAM(SB_THUMBPOSITION, targetPosition),
                        0);
                }
            }
        }

        selectionRestorePath_.clear();
        selectionRestoreY_ = 0;
    }

    HTREEITEM FolderTreeController::FindChildItem(HTREEITEM parentItem,
                                                   const std::wstring& folderPath) const
    {
        if (!treeWindow_)
        {
            return nullptr;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        HTREEITEM currentItem = parentItem
            ? TreeView_GetChild(treeWindow_, parentItem)
            : TreeView_GetRoot(treeWindow_);
        while (currentItem)
        {
            NodeData* nodeData = GetNodeData(currentItem);
            if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
            {
                return currentItem;
            }
            currentItem = TreeView_GetNextSibling(treeWindow_, currentItem);
        }

        return nullptr;
    }

    HTREEITEM FolderTreeController::FindItemByPath(const std::wstring& folderPath) const
    {
        if (!treeWindow_ || folderPath.empty())
        {
            return nullptr;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM parentItem) -> HTREEITEM
        {
            HTREEITEM currentItem = parentItem
                ? TreeView_GetChild(treeWindow_, parentItem)
                : TreeView_GetRoot(treeWindow_);
            while (currentItem)
            {
                NodeData* nodeData = GetNodeData(currentItem);
                if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
                {
                    return currentItem;
                }

                if (HTREEITEM descendant = findItem(currentItem))
                {
                    return descendant;
                }
                currentItem = TreeView_GetNextSibling(treeWindow_, currentItem);
            }
            return nullptr;
        };

        return findItem(nullptr);
    }

    void FolderTreeController::InsertFolderIfParentLoaded(std::wstring folderPath)
    {
        if (!treeWindow_ || folderPath.empty())
        {
            return;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(std::move(folderPath));
        const std::wstring parentPath = NormalizeFolderPath(fs::path(normalizedPath).parent_path().wstring());
        if (parentPath.empty())
        {
            return;
        }

        const HTREEITEM parentItem = FindItemByPath(parentPath);
        NodeData* parentNodeData = GetNodeData(parentItem);
        if (!parentNodeData || !parentNodeData->childrenLoaded || parentNodeData->childrenFilling)
        {
            return;
        }

        if (FindChildItem(parentItem, normalizedPath))
        {
            return;
        }

        InsertItem(parentItem, normalizedPath);
    }

    FolderTreeController::NodeData* FolderTreeController::GetNodeData(HTREEITEM item) const
    {
        if (!treeWindow_ || !item)
        {
            return nullptr;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_PARAM;
        treeItem.hItem = item;
        if (TreeView_GetItem(treeWindow_, &treeItem) == FALSE)
        {
            return nullptr;
        }

        return reinterpret_cast<NodeData*>(treeItem.lParam);
    }

    std::wstring FolderTreeController::GetSelectedFolderPath() const
    {
        if (!treeWindow_)
        {
            return {};
        }

        const HTREEITEM selectedItem = TreeView_GetSelection(treeWindow_);
        const NodeData* nodeData = GetNodeData(selectedItem);
        return nodeData ? nodeData->path : std::wstring{};
    }

    LRESULT FolderTreeController::HandleEnumerationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderTreeEnumerationUpdate> update(
            reinterpret_cast<services::FolderTreeEnumerationUpdate*>(lParam));
        if (!update)
        {
            return 0;
        }

        const auto pendingEnumerationItem = pendingEnumerationItems_.find(update->requestId);
        if (pendingEnumerationItem != pendingEnumerationItems_.end())
        {
            const HTREEITEM item = pendingEnumerationItem->second;
            pendingEnumerationItems_.erase(pendingEnumerationItem);
            NotifyStatusChanged();

            NodeData* nodeData = GetNodeData(item);
            if (!nodeData)
            {
                return 0;
            }

            switch (update->kind)
            {
            case services::FolderTreeEnumerationUpdateKind::Completed:
                if (nodeData->childEnumerationRequestId != update->requestId)
                {
                    return 0;
                }
                ApplyChildren(item, std::move(update->childFolders));
                return 0;
            case services::FolderTreeEnumerationUpdateKind::Failed:
                if (nodeData->childEnumerationRequestId != update->requestId)
                {
                    return 0;
                }

                nodeData->childrenLoading = false;
                nodeData->childEnumerationRequestId = 0;
                nodeData->childrenLoaded = false;
                if (!nodeData->childrenKnown)
                {
                    nodeData->hasChildren = false;
                    UpdateChildrenIndicator(item);
                }
                util::LogError(update->message);
                return 0;
            default:
                return 0;
            }
        }

        const auto pendingPresenceItems = pendingChildPresenceItems_.find(update->requestId);
        if (pendingPresenceItems == pendingChildPresenceItems_.end())
        {
            return 0;
        }

        const std::vector<HTREEITEM> items = std::move(pendingPresenceItems->second);
        pendingChildPresenceItems_.erase(pendingPresenceItems);
        NotifyStatusChanged();

        switch (update->kind)
        {
        case services::FolderTreeEnumerationUpdateKind::ChildPresenceCompleted:
        {
            std::unordered_map<std::wstring, HTREEITEM> itemsByPath;
            itemsByPath.reserve(items.size());
            for (HTREEITEM item : items)
            {
                if (const NodeData* requestedData = GetNodeData(item))
                {
                    itemsByPath.emplace(PathKey(requestedData->path), item);
                }
            }

            for (const services::FolderTreeChild& childPresence : update->childPresenceResults)
            {
                const auto requestedItem = itemsByPath.find(PathKey(childPresence.path));
                if (requestedItem == itemsByPath.end())
                {
                    continue;
                }

                const HTREEITEM item = requestedItem->second;
                NodeData* nodeData = GetNodeData(item);
                if (!nodeData || nodeData->childPresenceRequestId != update->requestId)
                {
                    continue;
                }

                nodeData->childPresenceLoading = false;
                nodeData->childPresenceRequestId = 0;
                nodeData->childrenKnown = true;
                nodeData->hasChildren = childPresence.hasChildren;
                CacheChildPresence(childPresence.path, childPresence.hasChildren);
                if (nodeData->hasChildren)
                {
                    AddPlaceholder(item);
                }
                UpdateChildrenIndicator(item);
            }
            return 0;
        }
        case services::FolderTreeEnumerationUpdateKind::Failed:
            for (HTREEITEM item : items)
            {
                NodeData* nodeData = GetNodeData(item);
                if (nodeData && nodeData->childPresenceRequestId == update->requestId)
                {
                    nodeData->childPresenceLoading = false;
                    nodeData->childPresenceRequestId = 0;
                }
            }
            util::LogError(update->message);
            return 0;
        default:
            return 0;
        }
    }

    void FolderTreeController::NotifyStatusChanged() const
    {
        if (handlers_.onStatusChanged)
        {
            handlers_.onStatusChanged();
        }
    }

    void FolderTreeController::SetSelectionSuppressed(bool suppressed) const
    {
        if (handlers_.onSelectionSuppressionChanged)
        {
            handlers_.onSelectionSuppressionChanged(suppressed);
        }
    }
}
