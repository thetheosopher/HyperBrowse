#include "ui/MainWindowInternal.h"

namespace
{
    constexpr int kDetailsPanelPreferredWidth = 540;

    std::vector<std::wstring> CollectShellDropPaths(HDROP dropHandle)
    {
        std::vector<std::wstring> paths;
        if (!dropHandle)
        {
            return paths;
        }

        const UINT pathCount = DragQueryFileW(dropHandle, 0xFFFFFFFFu, nullptr, 0);
        paths.reserve(pathCount);
        for (UINT index = 0; index < pathCount; ++index)
        {
            const UINT pathLength = DragQueryFileW(dropHandle, index, nullptr, 0);
            if (pathLength == 0)
            {
                continue;
            }

            std::wstring path(pathLength + 1, L'\0');
            const UINT copiedLength = DragQueryFileW(dropHandle, index, path.data(), pathLength + 1);
            path.resize(copiedLength);
            if (!path.empty())
            {
                paths.push_back(std::move(path));
            }
        }

        return paths;
    }

    // Reads a CF_HDROP out of an OLE IDataObject (as opposed to the raw HDROP the
    // legacy WM_DROPFILES path receives).
    std::vector<std::wstring> CollectShellDropPathsFromDataObject(IDataObject* dataObject)
    {
        std::vector<std::wstring> paths;
        if (!dataObject)
        {
            return paths;
        }

        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM storage{};
        if (FAILED(dataObject->GetData(&format, &storage)) || !storage.hGlobal)
        {
            return paths;
        }

        paths = CollectShellDropPaths(static_cast<HDROP>(storage.hGlobal));
        ReleaseStgMedium(&storage);
        return paths;
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::OnSize()
    {
        LayoutChildren();
    }

    void MainWindow::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const
    {
        minMaxInfo->ptMinTrackSize.x = kMinWindowWidth;
        minMaxInfo->ptMinTrackSize.y = kMinWindowHeight;
    }

    bool MainWindow::IsOverSplitter(int x, int y) const
    {
        if (y < actionStripHeight_)
        {
            return false;
        }

        if (x >= leftPaneWidth_ && x < leftPaneWidth_ + kSplitterWidth)
        {
            return true;
        }

        return IsOverDetailsPanelSplitter(x, y);
    }

    bool MainWindow::IsOverDetailsPanelSplitter(int x, int y) const
    {
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelRect_))
        {
            return false;
        }

        return y >= detailsPanelRect_.top
            && y < detailsPanelRect_.bottom
            && x >= detailsPanelRect_.left - kSplitterWidth
            && x < detailsPanelRect_.left;
    }

    void MainWindow::OnLButtonDown(int x, int y)
    {
        auto invalidateDetailsPanelTabs = [&]()
        {
            if (!IsRectEmpty(&detailsPanelTabStripRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
            }
        };

        const int commandBarMenuHit = y < actionStripHeight_ ? CommandBarMenuHitTest(x, y) : -1;
        if (commandBarKeyboardActive_ && commandBarMenuHit < 0)
        {
            DeactivateCommandBarKeyboardMode(false);
        }

        // Toolbar hit test
        if (y < actionStripHeight_)
        {
            const int menuHit = commandBarMenuHit;
            if (menuHit >= 0)
            {
                commandBarPressedIndex_ = menuHit;
                InvalidateToolbarStrip();
                SetCapture(hwnd_);
                return;
            }

            const int hit = ToolbarHitTest(x, y);
            if (hit >= 0)
            {
                FocusKeyboardTarget({KeyboardFocusTargetKind::ToolbarItem, hit});
                toolbarPressedIndex_ = hit;
                InvalidateToolbarStrip();
                SetCapture(hwnd_);
                return;
            }
        }

        const int detailsPanelTab = HitTestDetailsPanelTab(x, y);
        if (detailsPanelTab >= 0)
        {
            FocusKeyboardTarget({KeyboardFocusTargetKind::DetailsTab, detailsPanelTab});
            detailsPanelPressedTabIndex_ = detailsPanelTab;
            SetCapture(hwnd_);
            invalidateDetailsPanelTabs();
            return;
        }

        if (HitTestDetailsPanelCloseButton(x, y) >= 0)
        {
            FocusKeyboardTarget({KeyboardFocusTargetKind::DetailsCloseButton});
            detailsPanelCloseButtonPressed_ = true;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }
            return;
        }

        if (HitTestQuickAccessSortButton(x, y) >= 0)
        {
            FocusKeyboardTarget({KeyboardFocusTargetKind::QuickAccessSortButton});
            quickAccessSortButtonPressed_ = true;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
            return;
        }

        const int quickAccessButton = HitTestQuickAccessDestinationButton(x, y);
        if (quickAccessButton >= 0)
        {
            FocusKeyboardTarget({KeyboardFocusTargetKind::QuickAccessButton, quickAccessButton});
            quickAccessPressedButtonIndex_ = quickAccessButton;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
            return;
        }

        const int quickAccessRow = HitTestQuickAccessDestinationRow(x, y);
        if (quickAccessRow >= 0)
        {
            FocusKeyboardTarget({KeyboardFocusTargetKind::QuickAccessRow, quickAccessRow});
            quickAccessPressedRowIndex_ = quickAccessRow;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
            return;
        }

        if (IsOverDetailsPanelSplitter(x, y))
        {
            dragMode_ = DragMode::DetailsSplitter;
            SetCapture(hwnd_);
            return;
        }

        if (IsOverSplitter(x, y))
        {
            dragMode_ = DragMode::LeftSplitter;
            SetCapture(hwnd_);
        }
    }

    void MainWindow::OnLButtonDoubleClick(int x, int y)
    {
        if (IsOverDetailsPanelSplitter(x, y))
        {
            detailsPanelWidth_ = kDetailsPanelPreferredWidth;
            util::LogInfo(L"Reset details and Quick Actions panel to default width");
            LayoutChildren();
            return;
        }

        if (IsOverSplitter(x, y))
        {
            leftPaneWidth_ = kDefaultLeftPaneWidth;
            util::LogInfo(L"Reset splitter to default width");
            LayoutChildren();
        }
    }

    void MainWindow::OnLButtonUp()
    {
        if (folderTreeDragController_.IsActive())
        {
            FinishFolderTreeDrag(true);
            return;
        }

        if (commandBarPressedIndex_ >= 0)
        {
            const int pressedIdx = commandBarPressedIndex_;
            commandBarPressedIndex_ = -1;
            ReleaseCapture();

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const int hit = CommandBarMenuHitTest(point.x, point.y);
            commandBarHotIndex_ = commandBarKeyboardActive_
                ? (hit >= 0 ? hit : pressedIdx)
                : hit;
            InvalidateToolbarStrip();
            if (hit == pressedIdx)
            {
                OpenCommandBarMenu(pressedIdx);
            }
            return;
        }

        auto invalidateDetailsPanelTabs = [&]()
        {
            if (!IsRectEmpty(&detailsPanelTabStripRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
            }
        };

        if (toolbarPressedIndex_ >= 0)
        {
            const int pressedIdx = toolbarPressedIndex_;
            toolbarPressedIndex_ = -1;
            ReleaseCapture();
            InvalidateToolbarStrip();

            // Confirm click: mouse must still be over the same item
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            if (ToolbarHitTest(pt.x, pt.y) == pressedIdx)
            {
                ToolbarHandleClick(pressedIdx);
            }
            return;
        }

        if (detailsPanelPressedTabIndex_ >= 0)
        {
            const int pressedTab = detailsPanelPressedTabIndex_;
            detailsPanelPressedTabIndex_ = -1;
            ReleaseCapture();

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const int hitTab = HitTestDetailsPanelTab(point.x, point.y);
            detailsPanelHotTabIndex_ = hitTab;
            invalidateDetailsPanelTabs();

            if (hitTab == pressedTab)
            {
                SelectRightPaneTab(static_cast<RightPaneTab>(hitTab));
            }
            return;
        }

        if (detailsPanelCloseButtonPressed_)
        {
            detailsPanelCloseButtonPressed_ = false;
            ReleaseCapture();

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            detailsPanelCloseButtonHot_ = HitTestDetailsPanelCloseButton(point.x, point.y) >= 0;
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }

            if (detailsPanelCloseButtonHot_)
            {
                ToggleDetailsPanelVisibility();
            }
            return;
        }

        if (quickAccessSortButtonPressed_)
        {
            quickAccessSortButtonPressed_ = false;
            ReleaseCapture();
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (HitTestQuickAccessSortButton(point.x, point.y) >= 0)
            {
                SortFavoriteDestinationsByShortcut();
                if (hwnd_ && detailsStripVisible_)
                {
                    LayoutChildren();
                }
                UpdateMenuState();
            }
            return;
        }

        if (quickAccessPressedButtonIndex_ >= 0)
        {
            const int pressedButton = quickAccessPressedButtonIndex_;
            quickAccessPressedButtonIndex_ = -1;
            ReleaseCapture();
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            services::FileOperationType type = services::FileOperationType::Copy;
            const int hitButton = HitTestQuickAccessDestinationButton(point.x, point.y, &type);
            if (hitButton == pressedButton)
            {
                const std::size_t rowIndex = static_cast<std::size_t>(pressedButton / 3);
                if (rowIndex < quickAccessDestinationRows_.size())
                {
                    const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
                    const int actionIndex = pressedButton % 3;
                    if (actionIndex == 2)
                    {
                        if (row.favorite)
                        {
                            RemoveFavoriteDestination(row.destinationPath);
                        }
                        else
                        {
                            RemoveRecentDestination(row.destinationPath);
                        }
                    }
                    else
                    {
                        StartSelectionFileOperationToDestination(type, row.destinationPath);
                    }
                }
            }
            return;
        }

        if (quickAccessPressedRowIndex_ >= 0)
        {
            const int pressedRow = quickAccessPressedRowIndex_;
            quickAccessPressedRowIndex_ = -1;
            ReleaseCapture();
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const int hitRow = HitTestQuickAccessDestinationRow(point.x, point.y);
            if (hitRow == pressedRow)
            {
                const std::size_t rowIndex = static_cast<std::size_t>(pressedRow);
                if (rowIndex < quickAccessDestinationRows_.size())
                {
                    LoadFolderAsync(quickAccessDestinationRows_[rowIndex].destinationPath);
                }
            }
            return;
        }

        if (dragMode_ == DragMode::QuickAccessInternal)
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            UpdateInternalSelectionDrag(point);
            FinishInternalSelectionDrag(true);
            return;
        }

        if (dragMode_ != DragMode::None)
        {
            const bool splitterResizeCompleted = dragMode_ == DragMode::LeftSplitter;
            dragMode_ = DragMode::None;
            ReleaseCapture();
            if (splitterResizeCompleted && IsRectEmpty(&breadcrumbBarRect_) == FALSE)
            {
                RECT client{};
                GetClientRect(hwnd_, &client);
                const RECT breadcrumbRow{0, breadcrumbBarRect_.top, client.right, breadcrumbBarRect_.bottom};
                RedrawWindow(hwnd_,
                             &breadcrumbRow,
                             nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
    }

    void MainWindow::OnMouseMove(int x, int y)
    {
        if (folderTreeDragController_.IsActive())
        {
            UpdateFolderTreeDrag(POINT{x, y});
            return;
        }

        if (dragMode_ == DragMode::QuickAccessInternal)
        {
            POINT screenPoint{x, y};
            ClientToScreen(hwnd_, &screenPoint);
            RECT windowRect{};
            GetWindowRect(hwnd_, &windowRect);
            if (PtInRect(&windowRect, screenPoint) == FALSE)
            {
                StartExternalSelectionDrag();
                return;
            }

            UpdateInternalSelectionDrag(POINT{x, y});
            return;
        }

        auto invalidateDetailsPanelTabs = [&]()
        {
            if (!IsRectEmpty(&detailsPanelTabStripRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
            }
        };

        auto invalidateQuickAccessPanel = [&]()
        {
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
        };

        // Track mouse leave for toolbar hover reset
        if (!toolbarMouseTracking_)
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
            toolbarMouseTracking_ = true;
        }

        // Toolbar hover
        if (y < actionStripHeight_ && dragMode_ == DragMode::None)
        {
            if (detailsPanelHotTabIndex_ >= 0)
            {
                detailsPanelHotTabIndex_ = -1;
                invalidateDetailsPanelTabs();
            }
            if (detailsPanelCloseButtonHot_)
            {
                detailsPanelCloseButtonHot_ = false;
                if (!IsRectEmpty(&detailsPanelRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
                }
            }

            const int menuHit = CommandBarMenuHitTest(x, y);
            if (!commandBarKeyboardActive_ && menuHit != commandBarHotIndex_)
            {
                commandBarHotIndex_ = menuHit;
                InvalidateToolbarStrip();
            }

            bool quickAccessChanged = false;
            if (quickAccessHotRowIndex_ >= 0)
            {
                quickAccessHotRowIndex_ = -1;
                quickAccessChanged = true;
            }
            if (quickAccessHotButtonIndex_ >= 0)
            {
                quickAccessHotButtonIndex_ = -1;
                quickAccessChanged = true;
            }
            if (quickAccessChanged)
            {
                invalidateQuickAccessPanel();
            }

            const int hit = ToolbarHitTest(x, y);
            if (hit != toolbarHotIndex_)
            {
                toolbarHotIndex_ = hit;
                InvalidateToolbarStrip();
            }

            // Relay to tooltip
            if (tooltipControl_)
            {
                MSG msg{};
                msg.hwnd = hwnd_;
                msg.message = WM_MOUSEMOVE;
                msg.lParam = MAKELPARAM(x, y);
                SendMessageW(tooltipControl_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
            }
            return;
        }

        // Clear toolbar hover when mouse leaves strip
        if (toolbarHotIndex_ >= 0)
        {
            toolbarHotIndex_ = -1;
            InvalidateToolbarStrip();
        }
        if (!commandBarKeyboardActive_ && commandBarHotIndex_ >= 0)
        {
            commandBarHotIndex_ = -1;
            InvalidateToolbarStrip();
        }

        const int detailsPanelTabHit = HitTestDetailsPanelTab(x, y);
        if (detailsPanelTabHit != detailsPanelHotTabIndex_)
        {
            detailsPanelHotTabIndex_ = detailsPanelTabHit;
            invalidateDetailsPanelTabs();
        }

        const int detailsPanelCloseButtonHit = HitTestDetailsPanelCloseButton(x, y);
        if ((detailsPanelCloseButtonHit >= 0) != detailsPanelCloseButtonHot_)
        {
            detailsPanelCloseButtonHot_ = detailsPanelCloseButtonHit >= 0;
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }
        }

        const int quickAccessRowHit = HitTestQuickAccessDestinationRow(x, y);
        const int quickAccessHit = HitTestQuickAccessDestinationButton(x, y);
        const bool quickAccessSortHit = HitTestQuickAccessSortButton(x, y) >= 0;
        if (quickAccessRowHit != quickAccessHotRowIndex_
            || quickAccessHit != quickAccessHotButtonIndex_
            || quickAccessSortHit != quickAccessSortButtonHot_)
        {
            quickAccessHotRowIndex_ = quickAccessRowHit;
            quickAccessHotButtonIndex_ = quickAccessHit;
            quickAccessSortButtonHot_ = quickAccessSortHit;
            invalidateQuickAccessPanel();
        }

        if (dragMode_ == DragMode::LeftSplitter)
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int detailsSplitterWidth = detailsStripVisible_ ? kSplitterWidth : 0;
            const int visibleDetailsPanelWidth = detailsStripVisible_ ? detailsPanelWidth_ : 0;
            const int maxLeft = std::max(kMinLeftPaneWidth,
                                         static_cast<int>(client.right) - visibleDetailsPanelWidth - kMinRightPaneWidth - kSplitterWidth - detailsSplitterWidth);
            leftPaneWidth_ = std::clamp(x, kMinLeftPaneWidth, maxLeft);
            LayoutChildren();
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        else if (dragMode_ == DragMode::DetailsSplitter)
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int maxDetailsPanelWidth = std::max(0,
                                                      static_cast<int>(client.right) - leftPaneWidth_ - kMinRightPaneWidth - (kSplitterWidth * 2));
            if (maxDetailsPanelWidth >= kDetailsPanelMinWidth)
            {
                detailsPanelWidth_ = std::clamp(static_cast<int>(client.right) - x - kSplitterWidth,
                                                kDetailsPanelMinWidth,
                                                maxDetailsPanelWidth);
            }
            LayoutChildren();
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        else
        {
            SetCursor(LoadCursorW(nullptr,
                                  (IsOverSplitter(x, y)
                                      ? IDC_SIZEWE
                                      : ((detailsPanelCloseButtonHit >= 0
                                          || quickAccessSortHit
                                          || quickAccessRowHit >= 0
                                          || quickAccessHit >= 0)
                                          ? IDC_HAND
                                          : IDC_ARROW))));
        }
    }

    bool MainWindow::OnQuickAccessMouseWheel(WPARAM wParam, LPARAM lParam)
    {
        if (quickAccessDestinationRows_.empty() || IsRectEmpty(&quickAccessDestinationViewportRect_))
        {
            return false;
        }

        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        if (PtInRect(&quickAccessDestinationPanelRect_, point) == FALSE)
        {
            return false;
        }

        const int wheelSteps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (wheelSteps == 0)
        {
            return true;
        }

            const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(
                detailsPanelSummaryFont_,
                detailsPanelBodyFont_,
                MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u));
        const int totalRowsHeight = static_cast<int>(quickAccessDestinationRows_.size()) * metrics.rowHeight
            + static_cast<int>((quickAccessDestinationRows_.size() - 1) * metrics.rowGap);
        const int maximumScrollOffset = std::max(
            0,
            totalRowsHeight - static_cast<int>(quickAccessDestinationViewportRect_.bottom - quickAccessDestinationViewportRect_.top));
        const int scrollStep = metrics.rowHeight + metrics.rowGap;
        quickAccessScrollOffset_ = std::clamp(quickAccessScrollOffset_ - (wheelSteps * scrollStep * 2),
                                              0,
                                              maximumScrollOffset);
        LayoutChildren();
        return true;
    }

    void MainWindow::OnQuickAccessScroll(WPARAM wParam)
    {
        if (!quickAccessScrollBar_)
        {
            return;
        }

        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_ALL;
        if (GetScrollInfo(quickAccessScrollBar_, SB_CTL, &scrollInfo) == FALSE)
        {
            return;
        }

        const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(
            detailsPanelSummaryFont_,
            detailsPanelBodyFont_,
            MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u));
        const int maximumScrollOffset = std::max(0,
                                                  scrollInfo.nMax
                                                      - std::max(0, static_cast<int>(scrollInfo.nPage) - 1));
        const int scrollStep = metrics.rowHeight + metrics.rowGap;
        int nextOffset = quickAccessScrollOffset_;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:
            nextOffset -= scrollStep;
            break;
        case SB_LINEDOWN:
            nextOffset += scrollStep;
            break;
        case SB_PAGEUP:
            nextOffset -= static_cast<int>(scrollInfo.nPage);
            break;
        case SB_PAGEDOWN:
            nextOffset += static_cast<int>(scrollInfo.nPage);
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            nextOffset = scrollInfo.nTrackPos;
            break;
        case SB_TOP:
            nextOffset = 0;
            break;
        case SB_BOTTOM:
            nextOffset = maximumScrollOffset;
            break;
        default:
            return;
        }

        nextOffset = std::clamp(nextOffset, 0, maximumScrollOffset);
        if (nextOffset != quickAccessScrollOffset_)
        {
            quickAccessScrollOffset_ = nextOffset;
            LayoutChildren();
        }
    }

    LRESULT MainWindow::OnDropFiles(HDROP dropHandle)
    {
        if (!dropHandle)
        {
            return 0;
        }

        POINT dropPoint{};
        DragQueryPoint(dropHandle, &dropPoint);
        const int rowIndex = HitTestQuickAccessDestinationRow(dropPoint.x, dropPoint.y);
        std::vector<std::wstring> sourcePaths = CollectShellDropPaths(dropHandle);
        DragFinish(dropHandle);

        if (rowIndex < 0
            || rowIndex >= static_cast<int>(quickAccessDestinationRows_.size())
            || sourcePaths.empty()
            || fileOperationActive_)
        {
            return 0;
        }

        std::wstring destinationFolder = NormalizeFolderPath(quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)].destinationPath);
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        L"Quick Actions",
                        MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        const services::FileOperationType type = ResolveQuickAccessDropOperationType(sourcePaths, destinationFolder);
        StartFileOperation(type,
                           std::move(sourcePaths),
                           std::move(destinationFolder),
                           services::FileConflictPolicy::PromptShell,
                           {});
        return 0;
    }

    std::vector<std::wstring> MainWindow::ShellPathsFromDataObject(IDataObject* dataObject) const
    {
        return CollectShellDropPathsFromDataObject(dataObject);
    }

    DWORD MainWindow::DropEffectForKeyState(DWORD keyState,
                                            const std::wstring& destinationFolder,
                                            const std::vector<std::wstring>& sourcePaths) const
    {
        if (destinationFolder.empty() || sourcePaths.empty())
        {
            return DROPEFFECT_NONE;
        }

        if ((keyState & MK_CONTROL) != 0)
        {
            return DROPEFFECT_COPY;
        }
        if ((keyState & MK_SHIFT) != 0)
        {
            return DROPEFFECT_MOVE;
        }
        return AreAllSourcePathsOnSameDrive(sourcePaths, destinationFolder) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    }

    std::wstring MainWindow::ResolveExternalDropTarget(POINT clientPoint, HTREEITEM* treeItemOut) const
    {
        if (treeItemOut)
        {
            *treeItemOut = nullptr;
        }

        // Quick-access destination row first (screen-space panel on the right).
        const int rowIndex = HitTestQuickAccessDestinationRow(clientPoint.x, clientPoint.y);
        if (rowIndex >= 0 && rowIndex < static_cast<int>(quickAccessDestinationRows_.size()))
        {
            return NormalizeFolderPath(quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)].destinationPath);
        }

        // Then the folder tree.
        if (treePane_)
        {
            RECT treeRect{};
            GetWindowRect(treePane_, &treeRect);
            POINT screenPoint = clientPoint;
            ClientToScreen(hwnd_, &screenPoint);
            if (PtInRect(&treeRect, screenPoint) != FALSE)
            {
                POINT treePoint = screenPoint;
                ScreenToClient(treePane_, &treePoint);
                TVHITTESTINFO hitTest{};
                hitTest.pt = treePoint;
                if (HTREEITEM item = TreeView_HitTest(treePane_, &hitTest))
                {
                    if ((hitTest.flags & TVHT_ONITEM) != 0)
                    {
                        if (const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item))
                        {
                            if (!nodeData->path.empty())
                            {
                                if (treeItemOut)
                                {
                                    *treeItemOut = item;
                                }
                                return NormalizeFolderPath(nodeData->path);
                            }
                        }
                    }
                }
            }
            // Dropping anywhere else in the tree pane targets the current folder.
            if (browserModel_ && !browserModel_->FolderPath().empty())
            {
                return browserModel_->FolderPath();
            }
            return {};
        }

        // Browser/details area: drop into the currently open folder.
        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            return browserModel_->FolderPath();
        }
        return {};
    }

    void MainWindow::ClearExternalDropVisuals()
    {
        if (externalDropTreeHoverItem_ && treePane_)
        {
            TreeView_SelectDropTarget(treePane_, nullptr);
        }
        externalDropTreeHoverItem_ = nullptr;
        if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
        }
    }

    DWORD MainWindow::UpdateExternalDropFeedback(IDataObject* dataObject, DWORD keyState, POINT clientPoint)
    {
        HTREEITEM treeItem = nullptr;
        const std::vector<std::wstring> sourcePaths = ShellPathsFromDataObject(dataObject);
        const std::wstring destination = ResolveExternalDropTarget(clientPoint, &treeItem);
        const DWORD allowed = DropEffectForKeyState(keyState, destination, sourcePaths);

        if (externalDropTreeHoverItem_ != treeItem && treePane_)
        {
            TreeView_SelectDropTarget(treePane_, treeItem);
            externalDropTreeHoverItem_ = treeItem;
        }
        return allowed;
    }

    DWORD MainWindow::HandleExternalDrop(IDataObject* dataObject, DWORD keyState, POINT clientPoint)
    {
        std::vector<std::wstring> sourcePaths = ShellPathsFromDataObject(dataObject);
        HTREEITEM treeItem = nullptr;
        const std::wstring destinationFolder = ResolveExternalDropTarget(clientPoint, &treeItem);
        ClearExternalDropVisuals();

        if (sourcePaths.empty() || destinationFolder.empty() || fileOperationActive_)
        {
            return DROPEFFECT_NONE;
        }

        const DWORD effect = DropEffectForKeyState(keyState, destinationFolder, sourcePaths);
        if (effect == DROPEFFECT_NONE)
        {
            return DROPEFFECT_NONE;
        }

        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The drop destination folder is no longer available.",
                        L"Drop Files",
                        MB_OK | MB_ICONINFORMATION);
            return DROPEFFECT_NONE;
        }

        const services::FileOperationType type = (effect & DROPEFFECT_MOVE) != 0
            ? services::FileOperationType::Move
            : services::FileOperationType::Copy;
        StartFileOperation(type,
                           std::move(sourcePaths),
                           std::move(destinationFolder),
                           services::FileConflictPolicy::PromptShell,
                           {});
                return effect;
    }

    void MainWindow::ProcessGamepadInput()
    {
        XINPUT_STATE xinputState{};
        const DWORD result = XInputGetState(0, &xinputState);
        GamepadState state;
        if (result == ERROR_SUCCESS)
        {
            lastXInputError_ = ERROR_SUCCESS;
            const XINPUT_GAMEPAD& gamepad = xinputState.Gamepad;
            state.buttons = gamepad.wButtons;
            state.leftStickX = std::clamp(static_cast<float>(gamepad.sThumbLX) / 32767.0F, -1.0F, 1.0F);
            state.leftStickY = std::clamp(static_cast<float>(gamepad.sThumbLY) / 32767.0F, -1.0F, 1.0F);
            state.rightStickX = std::clamp(static_cast<float>(gamepad.sThumbRX) / 32767.0F, -1.0F, 1.0F);
            state.rightStickY = std::clamp(static_cast<float>(gamepad.sThumbRY) / 32767.0F, -1.0F, 1.0F);
            state.leftTrigger = static_cast<float>(gamepad.bLeftTrigger) / 255.0F;
            state.rightTrigger = static_cast<float>(gamepad.bRightTrigger) / 255.0F;
        }
        else if (result == ERROR_DEVICE_NOT_CONNECTED)
        {
            lastXInputError_ = ERROR_SUCCESS;
        }
        else if (lastXInputError_ != result)
        {
            lastXInputError_ = result;
            util::LogError(L"XInputGetState failed with error " + std::to_wstring(result));
        }

        const HWND foregroundWindow = GetForegroundWindow();
        viewer::ViewerWindow* activeViewer = FindViewerByHwnd(foregroundWindow);
        const bool browserActive = foregroundWindow == hwnd_;
        const GamepadContext context = activeViewer ? GamepadContext::Viewer : GamepadContext::Browser;
        const HWND focus = GetFocus();
        const bool inputAllowed = (browserActive || activeViewer != nullptr)
            && !menuLoopActive_
            && !folderTreeDragController_.IsActive()
            && GetCapture() == nullptr
            && !IsTextInputControlWindow(focus)
            && !fileOperationActive_
            && !batchConvertActive_
            && !imageCommandActive_;
        const ULONGLONG now = GetTickCount64();
        const float elapsedSeconds = lastGamepadPollTick_ == 0
            ? static_cast<float>(kGamepadPollIntervalMs) / 1000.0F
            : std::clamp(static_cast<float>(now - lastGamepadPollTick_) / 1000.0F, 0.0F, 0.1F);
        lastGamepadPollTick_ = now;
        const GamepadActions actions = gamepadInputPolicy_.Update(
            state, context, inputAllowed, elapsedSeconds);

        if (actions.navigationKey != 0)
        {
            if (activeViewer)
            {
                SendMessageW(activeViewer->Hwnd(), WM_KEYDOWN,
                             static_cast<WPARAM>(actions.navigationKey), 0);
            }
            else if (browserPaneController_)
            {
                browserPaneController_->HandleNavigationKey(
                    WM_KEYDOWN, static_cast<WPARAM>(actions.navigationKey), 0);
            }
        }

        if (activeViewer)
        {
            if (actions.toggleViewerZoom)
            {
                activeViewer->ToggleFitActualSize();
            }
            if (actions.toggleViewerOverlays)
            {
                SendMessageW(activeViewer->Hwnd(), WM_KEYDOWN, VK_TAB, 0);
            }
            if (actions.escape)
            {
                SendMessageW(activeViewer->Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
            }
            if (actions.recycle)
            {
                OnViewerDeleteRequested(0, reinterpret_cast<LPARAM>(activeViewer->Hwnd()));
            }

            const double zoomFactor = std::pow(
                1.8,
                static_cast<double>(actions.zoomRate) * elapsedSeconds);
            constexpr double kGamepadPanSpeed = 900.0;
            activeViewer->ApplyGamepadMotion(
                zoomFactor,
                static_cast<double>(actions.panX) * kGamepadPanSpeed
                    * static_cast<double>(actions.panSpeedMultiplier) * elapsedSeconds,
                static_cast<double>(actions.panY) * kGamepadPanSpeed
                    * static_cast<double>(actions.panSpeedMultiplier) * elapsedSeconds);
            return;
        }

        if (actions.escape)
        {
            const HWND browserPaneHwnd = browserPaneController_ ? browserPaneController_->Hwnd() : nullptr;
            if (browserPaneController_
                && browserPaneHwnd
                && GetFocus() == browserPaneHwnd
                && browserPaneController_->GetViewMode() == browser::BrowserViewMode::Thumbnails
                && browserPaneController_->SelectedCount() > 0)
            {
                browserPaneController_->ClearSelection();
            }
            else
            {
                HandleCommand(command_ids::ID_FILE_ESCAPE);
            }
        }
        if (actions.open)
        {
            HandleCommand(command_ids::ID_FILE_OPEN_SELECTED);
        }
        if (actions.recycle
            && browserPaneController_
            && !browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            HandleCommand(command_ids::ID_FILE_DELETE_SELECTION);
        }
    }
}
