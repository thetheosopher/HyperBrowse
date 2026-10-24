#include "ui/MainWindowInternal.h"

namespace
{

    bool PromptForCrossDriveDropOperation(HWND ownerWindow,
                                          std::wstring_view destinationFolder,
                                          hyperbrowse::services::FileOperationType* operationType)
    {
        if (!operationType)
        {
            return false;
        }

        TASKDIALOG_BUTTON buttons[] = {
            {1001, L"Copy files\nLeave the original files where they are and copy them into the target folder."},
            {1002, L"Move files\nTransfer the files into the target folder and remove them from the original drive."},
        };

        const std::wstring destinationLabel = hyperbrowse::ui::QuickAccessMenuBuilder::FolderDisplayName(destinationFolder);
        const std::wstring content = L"The destination folder \""
            + destinationLabel
            + L"\" is on a different drive. Choose whether to copy the dropped files or move them.";

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = ownerWindow;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.pszWindowTitle = L"Drop Images";
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = L"Choose how to handle the dropped files.";
        config.pszContent = content.c_str();
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = 1001;

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        if (FAILED(dialogResult) || clickedButton == IDCANCEL)
        {
            return false;
        }

        *operationType = clickedButton == 1002
            ? hyperbrowse::services::FileOperationType::Move
            : hyperbrowse::services::FileOperationType::Copy;
        return true;
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::RefreshFolderTree()
    {
        if (!folderTreeController_)
        {
            return;
        }

        const std::wstring selectedFolderPath = pendingTreeMouseSelectionPath_.empty()
            ? GetSelectedFolderTreePath()
            : pendingTreeMouseSelectionPath_;
        pendingTreeMouseSelectionPath_.clear();
        folderTreeController_->Refresh(selectedFolderPath);
        EnsureActiveFolderVisible();
    }

    void MainWindow::EnsureActiveFolderVisible()
    {
        if (!folderTreeController_ || !browserModel_)
        {
            return;
        }

        const std::wstring activeFolderPath = browserModel_->FolderPath();
        if (!activeFolderPath.empty())
        {
            folderTreeController_->SelectFolder(activeFolderPath);
        }
    }

    void MainWindow::InvalidateFolderTreeChildPresence(std::wstring_view folderPath)
    {
        if (folderTreeController_)
        {
            folderTreeController_->InvalidateChildPresence(folderPath);
        }
    }

    HTREEITEM MainWindow::FindFolderTreeItemByPath(const std::wstring& folderPath) const
    {
        return folderTreeController_ ? folderTreeController_->FindItemByPath(folderPath) : nullptr;
    }

    void MainWindow::InsertFolderTreeFolderIfParentLoaded(const std::wstring& folderPath)
    {
        if (folderTreeController_)
        {
            folderTreeController_->InsertFolderIfParentLoaded(folderPath);
        }
    }

    MainWindow::FolderTreeNodeData* MainWindow::GetFolderTreeNodeData(HTREEITEM item) const
    {
        return folderTreeController_ ? folderTreeController_->GetNodeData(item) : nullptr;
    }

    std::wstring MainWindow::GetSelectedFolderTreePath() const
    {
        return folderTreeController_ ? folderTreeController_->GetSelectedFolderPath() : std::wstring{};
    }

    LRESULT MainWindow::OnFolderTreeNotify(LPARAM lParam)
    {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (!header || header->hwndFrom != treePane_)
        {
            return 0;
        }

        switch (header->code)
        {
        case NM_RCLICK:
            return OnFolderTreeRightClick();
        case NM_CUSTOMDRAW:
        {
            auto* customDraw = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
            if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
            {
                return CDRF_NOTIFYITEMDRAW;
            }

            if (customDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT
                && (customDraw->nmcd.uItemState & CDIS_SELECTED) == 0)
            {
                const HTREEITEM item = reinterpret_cast<HTREEITEM>(customDraw->nmcd.dwItemSpec);
                const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                if (nodeData && IsFavoriteDestination(nodeData->path))
                {
                    customDraw->clrText = GetThemePalette().accent;
                }
            }

            return CDRF_DODEFAULT;
        }
        case TVN_BEGINDRAGW:
            return OnFolderTreeBeginDrag(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_ITEMEXPANDINGW:
            return OnFolderTreeItemExpanding(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_SELCHANGEDW:
            return OnFolderTreeSelectionChanged(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_BEGINLABELEDITW:
            return OnFolderTreeBeginLabelEdit(*reinterpret_cast<const NMTVDISPINFOW*>(lParam));
        case TVN_ENDLABELEDITW:
            return OnFolderTreeEndLabelEdit(*reinterpret_cast<const NMTVDISPINFOW*>(lParam));
        default:
            return 0;
        }
    }

    void MainWindow::RelayFolderTreeTooltipEvent(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (!tooltipControl_ || !treePane_)
        {
            return;
        }

        if (message == WM_MOUSEMOVE)
        {
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = treePane_;
            TrackMouseEvent(&tracking);

            std::wstring hoveredPath;
            TVHITTESTINFO hitTest{};
            hitTest.pt = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
            if (item && (hitTest.flags & TVHT_ONITEM) != 0)
            {
                if (const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                    nodeData && IsFavoriteDestination(nodeData->path)
                    && quickSendModel_.ShortcutForDestination(nodeData->path).has_value())
                {
                    hoveredPath = nodeData->path;
                }
            }

            if (!FolderPathsEqual(treeTooltipPath_, hoveredPath))
            {
                treeTooltipPath_ = std::move(hoveredPath);
                treeFolderTooltipText_.clear();
                SendMessageW(tooltipControl_, TTM_POP, 0, 0);
            }
        }
        else if (message == WM_MOUSELEAVE)
        {
            treeTooltipPath_.clear();
            treeFolderTooltipText_.clear();
            SendMessageW(tooltipControl_, TTM_POP, 0, 0);
        }

        MSG relayMessage{};
        relayMessage.hwnd = treePane_;
        relayMessage.message = message;
        relayMessage.wParam = wParam;
        relayMessage.lParam = lParam;
        SendMessageW(tooltipControl_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }

    LRESULT CALLBACK MainWindow::FolderTreeTooltipSubclassProc(HWND hwnd,
                                                                 UINT message,
                                                                 WPARAM wParam,
                                                                 LPARAM lParam,
                                                                 UINT_PTR,
                                                                 DWORD_PTR refData)
    {
        auto* window = reinterpret_cast<MainWindow*>(refData);
        if (window && message == WM_LBUTTONDOWN)
        {
            window->pendingTreeMouseSelectionPath_.clear();

            if (window->pendingInlineRenameOriginalPath_.empty())
            {
                return DefSubclassProc(hwnd, message, wParam, lParam);
            }

            TVHITTESTINFO hitTest{};
            hitTest.pt = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const HTREEITEM item = TreeView_HitTest(hwnd, &hitTest);
            if (item && (hitTest.flags & TVHT_ONITEM) != 0)
            {
                if (const FolderTreeNodeData* nodeData = window->GetFolderTreeNodeData(item))
                {
                    window->pendingTreeMouseSelectionPath_ = nodeData->path;
                }
            }
        }
        if (window && (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE))
        {
            window->RelayFolderTreeTooltipEvent(message, wParam, lParam);
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::QuickAccessShortcutEditSubclassProc(HWND hwnd,
                                                                       UINT message,
                                                                       WPARAM wParam,
                                                                       LPARAM lParam,
                                                                       UINT_PTR,
                                                                       DWORD_PTR)
    {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (message == WM_LBUTTONUP)
        {
            PostMessageW(hwnd, EM_SETSEL, 0, static_cast<LPARAM>(-1));
        }

        return result;
    }

    LRESULT CALLBACK MainWindow::DetailsPanelTextSubclassProc(HWND hwnd,
                                                               UINT message,
                                                               WPARAM wParam,
                                                               LPARAM lParam,
                                                               UINT_PTR,
                                                               DWORD_PTR refData)
    {
        auto* window = reinterpret_cast<MainWindow*>(refData);
        if (window && message == WM_CONTEXTMENU && !window->detailsPanelPromptText_.empty())
        {
            POINT screenPoint{};
            if (lParam == static_cast<LPARAM>(-1))
            {
                GetCursorPos(&screenPoint);
            }
            else
            {
                screenPoint.x = GET_X_LPARAM(lParam);
                screenPoint.y = GET_Y_LPARAM(lParam);
            }
            window->ShowDetailsPanelContextMenu(screenPoint);
            return 0;
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT MainWindow::OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView)
    {
        if (suppressTreeSelectionChange_)
        {
            return 0;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(treeView.itemNew.hItem);
        if (!nodeData)
        {
            return 0;
        }

        if (pendingInlineRenameOriginalPath_.empty())
        {
            pendingTreeMouseSelectionPath_.clear();
        }

        if (!browserModel_ || !FolderPathsEqual(browserModel_->FolderPath(), nodeData->path))
        {
            LoadFolderAsync(nodeData->path);
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView)
    {
        if ((treeView.action & TVE_EXPAND) != 0)
        {
            if (folderTreeController_)
            {
                folderTreeController_->RequestChildren(treeView.itemNew.hItem);
            }
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeBeginLabelEdit(const NMTVDISPINFOW& dispInfo)
    {
        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(dispInfo.item.hItem);
        if (!nodeData || nodeData->path.empty())
        {
            return TRUE; // cancel edit
        }

        if (fileOperationActive_)
        {
            return TRUE; // cancel edit
        }

        // Roots (drives) cannot be renamed.
        if (!TreeView_GetParent(treePane_, dispInfo.item.hItem))
        {
            return TRUE;
        }

        pendingInlineRenameOriginalPath_ = nodeData->path;
        return FALSE; // allow edit
    }

    LRESULT MainWindow::OnFolderTreeEndLabelEdit(const NMTVDISPINFOW& dispInfo)
    {
        // pszText is null when the user cancelled (Esc).
        if (!dispInfo.item.pszText)
        {
            pendingInlineRenameOriginalPath_.clear();
            pendingTreeMouseSelectionPath_.clear();
            return 0;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(dispInfo.item.hItem);
        if (!nodeData || nodeData->path.empty())
        {
            pendingInlineRenameOriginalPath_.clear();
            pendingTreeMouseSelectionPath_.clear();
            return 0;
        }

        const std::wstring newLeafName = dispInfo.item.pszText;
        const std::wstring currentLeafName = fs::path(nodeData->path).filename().wstring();
        if (newLeafName.empty() || newLeafName == currentLeafName)
        {
            pendingInlineRenameOriginalPath_.clear();
            pendingTreeMouseSelectionPath_.clear();
            return 0; // no change; keep the old label
        }

        std::wstring validationError;
        if (!IsValidFolderName(newLeafName, &validationError))
        {
            pendingInlineRenameOriginalPath_.clear();
            pendingTreeMouseSelectionPath_.clear();
            MessageBoxW(hwnd_, validationError.c_str(), L"Rename Folder", MB_OK | MB_ICONWARNING);
            return 0;
        }

        const std::wstring folderPath = nodeData->path;
        activeTreeFolderRenamePath_ = folderPath;
        if (!StartFileOperation(services::FileOperationType::Rename,
                                {folderPath},
                                {},
                                services::FileConflictPolicy::PromptShell,
                                {newLeafName}))
        {
            activeTreeFolderRenamePath_.clear();
            pendingInlineRenameOriginalPath_.clear();
            pendingTreeMouseSelectionPath_.clear();
        }
        // Return 0 so the tree does not apply the label itself; the folder-watch /
        // operation-completion path will refresh the node with the real new name.
        return 0;
    }

    LRESULT MainWindow::OnFolderTreeRightClick()
    {
        if (!treePane_)
        {
            return 0;
        }

        POINT screenPoint{};
        if (!GetCursorPos(&screenPoint))
        {
            return 0;
        }

        POINT clientPoint = screenPoint;
        ScreenToClient(treePane_, &clientPoint);

        TVHITTESTINFO hitTest{};
        hitTest.pt = clientPoint;
        const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
        if (!item || (hitTest.flags & TVHT_ONITEM) == 0)
        {
            return 0;
        }

        if (!GetFolderTreeNodeData(item) || !TreeView_GetParent(treePane_, item))
        {
            return 0;
        }

        ShowFolderTreeContextMenu(screenPoint, item);
        return 0;
    }

    LRESULT MainWindow::OnFolderTreeBeginDrag(const NMTREEVIEWW& treeView)
    {
        folderTreeDragController_.Begin(treeView);
        return 0;
    }

    void MainWindow::UpdateFolderTreeDrag(POINT windowPoint)
    {
        folderTreeDragController_.Update(windowPoint);
    }

    void MainWindow::FinishFolderTreeDrag(bool commitDrop)
    {
        folderTreeDragController_.Finish(commitDrop);
    }

    void MainWindow::UpdateInternalSelectionDrag(POINT windowPoint)
    {
        if (dragMode_ != DragMode::QuickAccessInternal)
        {
            return;
        }

        const int previousQuickAccessRow = quickAccessHotRowIndex_;
        const int previousQuickAccessButton = quickAccessHotButtonIndex_;
        HTREEITEM nextTreeDropItem = nullptr;
        std::wstring nextTreeDropPath;

        quickAccessHotRowIndex_ = HitTestQuickAccessDestinationRow(windowPoint.x, windowPoint.y);
        quickAccessHotButtonIndex_ = -1;

        if (treePane_)
        {
            RECT treeRect{};
            GetWindowRect(treePane_, &treeRect);

            POINT screenPoint = windowPoint;
            ClientToScreen(hwnd_, &screenPoint);
            if (PtInRect(&treeRect, screenPoint) != FALSE)
            {
                POINT treePoint = screenPoint;
                ScreenToClient(treePane_, &treePoint);

                TVHITTESTINFO hitTest{};
                hitTest.pt = treePoint;
                const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
                if (item && (hitTest.flags & TVHT_ONITEM) != 0)
                {
                    const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                    if (nodeData && !nodeData->path.empty())
                    {
                        const std::wstring normalizedDestinationPath = NormalizeFolderPath(nodeData->path);
                        if (IsExistingDirectory(normalizedDestinationPath)
                            && !IsQuickAccessDestinationCurrentFolder(normalizedDestinationPath))
                        {
                            nextTreeDropItem = item;
                            nextTreeDropPath = normalizedDestinationPath;
                        }
                    }
                }
            }
        }

        if (internalSelectionTreeDropItem_ != nextTreeDropItem)
        {
            internalSelectionTreeDropItem_ = nextTreeDropItem;
            if (treePane_)
            {
                TreeView_SelectDropTarget(treePane_, internalSelectionTreeDropItem_);
            }
        }
        internalSelectionTreeDropPath_ = std::move(nextTreeDropPath);

        if ((previousQuickAccessRow != quickAccessHotRowIndex_
             || previousQuickAccessButton != quickAccessHotButtonIndex_)
            && !IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
        }

        SetCursor(LoadCursorW(nullptr,
                              (quickAccessHotRowIndex_ >= 0 || internalSelectionTreeDropItem_ != nullptr)
                                  ? IDC_HAND
                                  : IDC_NO));
    }

    void MainWindow::FinishInternalSelectionDrag(bool commitDrop)
    {
        if (dragMode_ != DragMode::QuickAccessInternal)
        {
            return;
        }

        const int quickAccessRow = (commitDrop && quickAccessHotRowIndex_ >= 0)
            ? quickAccessHotRowIndex_
            : -1;
        const std::wstring treeDropPath = (commitDrop && !internalSelectionTreeDropPath_.empty())
            ? internalSelectionTreeDropPath_
            : std::wstring{};

        dragMode_ = DragMode::None;
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }

        quickAccessHotRowIndex_ = -1;
        quickAccessHotButtonIndex_ = -1;
        internalSelectionTreeDropPath_.clear();
        internalSelectionTreeDropItem_ = nullptr;
        if (treePane_)
        {
            TreeView_SelectDropTarget(treePane_, nullptr);
        }
        if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
        }

        if (quickAccessRow >= 0 && quickAccessRow < static_cast<int>(quickAccessDestinationRows_.size()))
        {
            const services::FileOperationType type = ResolveQuickAccessDropOperationType(
                browserPaneController_->SelectedFilePathsSnapshot(),
                quickAccessDestinationRows_[static_cast<std::size_t>(quickAccessRow)].destinationPath);
            StartSelectionFileOperationToDestination(type,
                                                     quickAccessDestinationRows_[static_cast<std::size_t>(quickAccessRow)].destinationPath);
            return;
        }

        if (!treeDropPath.empty())
        {
            const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
            if (sourcePaths.empty())
            {
                return;
            }

            services::FileOperationType type = services::FileOperationType::Move;
            if (!AreAllSourcePathsOnSameDrive(sourcePaths, treeDropPath))
            {
                if (!PromptForCrossDriveDropOperation(hwnd_, treeDropPath, &type))
                {
                    return;
                }
            }

            StartSelectionFileOperationToDestination(type, treeDropPath);
        }
    }

    void MainWindow::StartExternalSelectionDrag()
    {
        if (dragMode_ != DragMode::QuickAccessInternal || !browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        FinishInternalSelectionDrag(false);
        if (sourcePaths.empty())
        {
            return;
        }

        Microsoft::WRL::ComPtr<IDataObject> dataObject;
        if (!CreateShellFileDataObject(sourcePaths, &dataObject))
        {
            util::LogError(L"Failed to create the shell data object for the selected file drag");
            return;
        }

        Microsoft::WRL::ComPtr<IDropSource> dropSource;
        dropSource = CreateShellFileDragSource();

        DWORD performedEffect = DROPEFFECT_NONE;
        const HRESULT dragResult = DoDragDrop(dataObject.Get(),
                                              dropSource.Get(),
                                              DROPEFFECT_COPY | DROPEFFECT_MOVE,
                                              &performedEffect);
        if (FAILED(dragResult))
        {
            util::LogError(std::wstring(L"External file drag failed with HRESULT value ")
                           + std::to_wstring(static_cast<unsigned long>(dragResult)));
        }
    }
}
