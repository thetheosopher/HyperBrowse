#include "browser/BrowserPaneInternal.h"

namespace
{

    constexpr int kWheelScrollAmount = 72;
    constexpr double kThumbnailWheelRowsPerNotch = 1.0;

    RECT NormalizeRect(POINT start, POINT end)
    {
        RECT rect{};
        rect.left = std::min(start.x, end.x);
        rect.top = std::min(start.y, end.y);
        rect.right = std::max(start.x, end.x);
        rect.bottom = std::max(start.y, end.y);
        return rect;
    }
}

namespace hyperbrowse::browser
{

    void BrowserPane::FocusSelectedViewIndex(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        anchorModelIndex_ = modelIndex;
        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::SelectSingleViewIndex(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        selectedModelIndices_.clear();
        selectedModelIndices_.insert(modelIndex);
        anchorModelIndex_ = modelIndex;
        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::ToggleViewIndexSelection(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        if (selectedModelIndices_.contains(modelIndex))
        {
            selectedModelIndices_.erase(modelIndex);
        }
        else
        {
            selectedModelIndices_.insert(modelIndex);
        }

        anchorModelIndex_ = modelIndex;
        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::ExtendSelectionToViewIndex(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        if (anchorModelIndex_ < 0)
        {
            SelectSingleViewIndex(viewIndex);
            return;
        }

        const int anchorViewIndex = ViewIndexFromModelIndex(anchorModelIndex_);
        if (anchorViewIndex < 0)
        {
            SelectSingleViewIndex(viewIndex);
            return;
        }

        selectedModelIndices_.clear();
        const int rangeStart = std::min(anchorViewIndex, viewIndex);
        const int rangeEnd = std::max(anchorViewIndex, viewIndex);
        for (int index = rangeStart; index <= rangeEnd; ++index)
        {
            const int rangeModelIndex = ModelIndexFromViewIndex(index);
            if (rangeModelIndex >= 0)
            {
                selectedModelIndices_.insert(rangeModelIndex);
            }
        }

        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::ArmQuickSendDrag(POINT point)
    {
        quickSendDragArmed_ = true;
        quickSendDragStartPoint_ = point;
        SetCapture(hwnd_);
        quickSendDragOwnsCapture_ = GetCapture() == hwnd_;
    }

    void BrowserPane::ClearQuickSendDragState()
    {
        const bool shouldReleaseCapture = quickSendDragOwnsCapture_ && !rubberBandActive_ && GetCapture() == hwnd_;
        quickSendDragArmed_ = false;
        quickSendDragOwnsCapture_ = false;
        quickSendDragStartPoint_ = {};
        if (shouldReleaseCapture)
        {
            ReleaseCapture();
        }
    }

    bool BrowserPane::ShouldBeginQuickSendDrag(POINT point) const
    {
        if (!quickSendDragArmed_ || selectedModelIndices_.empty())
        {
            return false;
        }

        const int dragThresholdX = std::max(1, GetSystemMetrics(SM_CXDRAG));
        const int dragThresholdY = std::max(1, GetSystemMetrics(SM_CYDRAG));
        return std::abs(point.x - quickSendDragStartPoint_.x) >= dragThresholdX
            || std::abs(point.y - quickSendDragStartPoint_.y) >= dragThresholdY;
    }

    bool BrowserPane::RequestQuickSendDrag() const
    {
        return parent_
            && SendMessageW(parent_, kQuickSendDragMessage, reinterpret_cast<WPARAM>(hwnd_), 0) != 0;
    }

    void BrowserPane::BeginRubberBandSelection(POINT point, bool additive)
    {
        ClearQuickSendDragState();
        rubberBandActive_ = true;
        rubberBandStart_ = point;
        rubberBandRect_ = {point.x, point.y, point.x, point.y};
        rubberBandSeedSelection_ = additive ? selectedModelIndices_ : std::unordered_set<int>{};
        SetCapture(hwnd_);
        SetFocus(hwnd_);
        UpdateRubberBandSelection(point);
    }

    void BrowserPane::UpdateRubberBandSelection(POINT point)
    {
        if (!rubberBandActive_)
        {
            return;
        }

        rubberBandRect_ = NormalizeRect(rubberBandStart_, point);
        std::unordered_set<int> newSelection = rubberBandSeedSelection_;
        for (int viewIndex = 0; viewIndex < static_cast<int>(orderedModelIndices_.size()); ++viewIndex)
        {
            RECT cellRect = GetThumbnailCellRect(viewIndex);
            RECT intersection{};
            if (IntersectRect(&intersection, &cellRect, &rubberBandRect_))
            {
                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex >= 0)
                {
                    newSelection.insert(modelIndex);
                }
            }
        }

        selectedModelIndices_ = std::move(newSelection);
        UpdateSelectionBytes();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::EndRubberBandSelection()
    {
        if (!rubberBandActive_)
        {
            return;
        }

        rubberBandActive_ = false;
        rubberBandSeedSelection_.clear();
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::RequestOpenPrimarySelection() const
    {
        const int primaryModelIndex = PrimarySelectedModelIndex();
        if (primaryModelIndex < 0)
        {
            return;
        }

        const int viewIndex = ViewIndexFromModelIndex(primaryModelIndex);
        if (viewIndex >= 0)
        {
            RequestOpenItemForViewIndex(viewIndex);
        }
    }

    void BrowserPane::RequestOpenItemForViewIndex(int viewIndex) const
    {
        if (!parent_)
        {
            return;
        }

        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        PostMessageW(parent_, kOpenItemMessage, reinterpret_cast<WPARAM>(hwnd_), static_cast<LPARAM>(modelIndex));
    }

    POINT BrowserPane::ContextMenuAnchorScreenPoint() const
    {
        POINT screenPoint{};
        const int primaryModelIndex = PrimarySelectedModelIndex();
        const int viewIndex = ViewIndexFromModelIndex(primaryModelIndex);

        if (viewMode_ == BrowserViewMode::Details && detailsList_ && IsWindowVisible(detailsList_))
        {
            RECT itemRect{};
            if (viewIndex >= 0 && ListView_GetItemRect(detailsList_, viewIndex, &itemRect, LVIR_BOUNDS) != FALSE)
            {
                screenPoint.x = itemRect.left + ((itemRect.right - itemRect.left) / 2);
                screenPoint.y = itemRect.top + ((itemRect.bottom - itemRect.top) / 2);
                ClientToScreen(detailsList_, &screenPoint);
                return screenPoint;
            }
        }

        RECT anchorRect{};
        if (viewMode_ == BrowserViewMode::Thumbnails && viewIndex >= 0)
        {
            anchorRect = GetThumbnailCellRect(viewIndex);
        }
        else if (hwnd_)
        {
            GetClientRect(hwnd_, &anchorRect);
        }

        screenPoint.x = anchorRect.left + ((anchorRect.right - anchorRect.left) / 2);
        screenPoint.y = anchorRect.top + ((anchorRect.bottom - anchorRect.top) / 2);
        if (hwnd_)
        {
            ClientToScreen(hwnd_, &screenPoint);
        }
        return screenPoint;
    }

    void BrowserPane::ShowContextMenu(POINT screenPoint) const
    {
        if (!parent_)
        {
            return;
        }

        SendMessageW(parent_, kContextMenuMessage, reinterpret_cast<WPARAM>(hwnd_), MAKELPARAM(screenPoint.x, screenPoint.y));
    }

    LRESULT BrowserPane::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == services::DisplayColorService::kReadyMessage)
        {
            displayColors_->AcknowledgeNotification();
            d2dBitmapCache_.clear();
            InvalidateRect(hwnd_, nullptr, FALSE);
            if (parent_) InvalidateRect(parent_, nullptr, FALSE);
            return 0;
        }
        if (message == WM_TIMER && wParam == services::DisplayColorService::kProfilePollTimer)
        {
            if (!IsIconic(GetAncestor(hwnd_, GA_ROOT))) RefreshColorProfile(true);
            return 0;
        }
        switch (message)
        {
        case WM_CREATE:
            if (!CreateDetailsListView())
            {
                return -1;
            }
            CreateThumbnailTooltip();
            return 0;
        case WM_SIZE:
            LayoutChildren();
            return 0;
        case WM_WINDOWPOSCHANGED:
            RefreshColorProfile();
            break;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            RefreshColorProfile(true);
            break;
        case WM_NCDESTROY:
            if (displayColors_) displayColors_->BindTargetWindow(nullptr);
            KillTimer(hwnd_, services::DisplayColorService::kProfilePollTimer);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETFOCUS:
            if (viewMode_ == BrowserViewMode::Details && detailsList_ && IsWindowVisible(detailsList_))
            {
                SetFocus(detailsList_);
            }
            return 0;
        case WM_KEYDOWN:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                const bool controlPressed = GetKeyState(VK_CONTROL) < 0;
                const bool shiftPressed = GetKeyState(VK_SHIFT) < 0;
                if (wParam == VK_RETURN)
                {
                    RequestOpenPrimarySelection();
                    return 0;
                }
                if ((wParam == 'A' || wParam == 'a') && controlPressed)
                {
                    SelectAll();
                    return 0;
                }
                if (wParam == VK_ESCAPE && !selectedModelIndices_.empty())
                {
                    ClearSelection();
                    return 0;
                }
                if (wParam == VK_APPS || (wParam == VK_F10 && GetKeyState(VK_SHIFT) < 0))
                {
                    ShowContextMenu(ContextMenuAnchorScreenPoint());
                    return 0;
                }
                if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN
                    || wParam == VK_PRIOR || wParam == VK_NEXT
                    || wParam == VK_HOME || wParam == VK_END)
                {
                    if (HandleThumbnailNavigationKey(wParam, shiftPressed))
                    {
                        return 0;
                    }
                }
            }
            break;
        case WM_VSCROLL:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                HideThumbnailTooltip();
                CancelSmoothScrollAnimation();
                SCROLLINFO scrollInfo{};
                scrollInfo.cbSize = sizeof(scrollInfo);
                scrollInfo.fMask = SIF_ALL;
                const BOOL hasScrollInfo = GetScrollInfo(hwnd_, SB_VERT, &scrollInfo);
                const int thumbPosition = static_cast<int>(HIWORD(wParam));

                int nextOffset = scrollOffsetY_;
                bool scheduleVisibleWork = true;
                bool forceVisibleWork = false;
                switch (LOWORD(wParam))
                {
                case SB_LINEUP:
                    nextOffset -= kWheelScrollAmount;
                    break;
                case SB_LINEDOWN:
                    nextOffset += kWheelScrollAmount;
                    break;
                case SB_PAGEUP:
                    nextOffset -= static_cast<int>(scrollInfo.nPage);
                    break;
                case SB_PAGEDOWN:
                    nextOffset += static_cast<int>(scrollInfo.nPage);
                    break;
                case SB_THUMBTRACK:
                    thumbnailScrollbarThumbTracking_ = true;
                    nextOffset = hasScrollInfo ? scrollInfo.nTrackPos : thumbPosition;
                    break;
                case SB_THUMBPOSITION:
                    thumbnailScrollbarThumbTracking_ = false;
                    nextOffset = hasScrollInfo ? scrollInfo.nTrackPos : thumbPosition;
                    forceVisibleWork = LOWORD(wParam) == SB_THUMBPOSITION;
                    break;
                case SB_ENDSCROLL:
                    thumbnailScrollbarThumbTracking_ = false;
                    forceVisibleWork = true;
                    break;
                case SB_TOP:
                    thumbnailScrollbarThumbTracking_ = false;
                    nextOffset = 0;
                    break;
                case SB_BOTTOM:
                    thumbnailScrollbarThumbTracking_ = false;
                    nextOffset = scrollInfo.nMax;
                    break;
                default:
                    break;
                }

                SetScrollOffset(nextOffset, scheduleVisibleWork, forceVisibleWork);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                HideThumbnailTooltip();
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
                const int wheelScrollDistance = layout.itemHeight + layout.cellPadding;
                const double wheelNotches = static_cast<double>(delta) / static_cast<double>(WHEEL_DELTA);
                const double scrollAmount = wheelNotches
                    * wheelScrollDistance
                    * kThumbnailWheelRowsPerNotch;

                if (!smoothScrollTimerId_)
                {
                    smoothScrollCurrent_ = static_cast<double>(scrollOffsetY_);
                }

                smoothScrollTarget_ = smoothScrollCurrent_ - scrollAmount;

                if (!smoothScrollTimerId_)
                {
                    smoothScrollTimerId_ = SetTimer(hwnd_, kSmoothScrollTimerId, kSmoothScrollIntervalMs, nullptr);
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kSmoothScrollTimerId && smoothScrollTimerId_)
            {
                const double diff = smoothScrollTarget_ - smoothScrollCurrent_;
                if (std::abs(diff) < kSmoothScrollSnapThreshold)
                {
                    smoothScrollCurrent_ = smoothScrollTarget_;
                    KillTimer(hwnd_, kSmoothScrollTimerId);
                    smoothScrollTimerId_ = 0;
                }
                else
                {
                    smoothScrollCurrent_ += diff * 0.18;
                }

                const int targetOffset = static_cast<int>(std::lround(smoothScrollCurrent_));
                SetScrollOffset(targetOffset);

                // If the scroll position was clamped at a boundary, stop animation
                if (scrollOffsetY_ != targetOffset)
                {
                    smoothScrollCurrent_ = static_cast<double>(scrollOffsetY_);
                    smoothScrollTarget_ = smoothScrollCurrent_;
                    KillTimer(hwnd_, kSmoothScrollTimerId);
                    smoothScrollTimerId_ = 0;
                }
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                HideThumbnailTooltip();
                SetFocus(hwnd_);
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int viewIndex = HitTestThumbnailItem(point);
                const bool shiftPressed = (wParam & MK_SHIFT) != 0;
                const bool controlPressed = (wParam & MK_CONTROL) != 0;

                if (viewIndex >= 0)
                {
                    const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                    const bool alreadySelected = modelIndex >= 0 && selectedModelIndices_.contains(modelIndex);
                    if (shiftPressed)
                    {
                        ClearQuickSendDragState();
                        ExtendSelectionToViewIndex(viewIndex);
                    }
                    else if (controlPressed)
                    {
                        ClearQuickSendDragState();
                        ToggleViewIndexSelection(viewIndex);
                    }
                    else if (alreadySelected && selectedModelIndices_.size() > 1)
                    {
                        FocusSelectedViewIndex(viewIndex);
                        ArmQuickSendDrag(point);
                    }
                    else
                    {
                        SelectSingleViewIndex(viewIndex);
                        ArmQuickSendDrag(point);
                    }
                }
                else
                {
                    ClearQuickSendDragState();
                    BeginRubberBandSelection(point, controlPressed);
                }
                return 0;
            }
            break;
        case WM_LBUTTONDBLCLK:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                ClearQuickSendDragState();
                const int viewIndex = HitTestThumbnailItem(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (viewIndex >= 0)
                {
                    SelectSingleViewIndex(viewIndex);
                    RequestOpenItemForViewIndex(viewIndex);
                }
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if ((wParam & MK_LBUTTON) == 0)
                {
                    ClearQuickSendDragState();
                }
                else if (!rubberBandActive_ && ShouldBeginQuickSendDrag(point))
                {
                    HideThumbnailTooltip();
                    const bool dragStarted = RequestQuickSendDrag();
                    ClearQuickSendDragState();
                    if (dragStarted)
                    {
                        return 0;
                    }
                }

                if (rubberBandActive_)
                {
                    HideThumbnailTooltip();
                    UpdateRubberBandSelection(point);
                }
                else
                {
                    UpdateThumbnailTooltip(point, wParam, lParam);
                }
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            HideThumbnailTooltip();
            return 0;
        case WM_LBUTTONUP:
            ClearQuickSendDragState();
            if (rubberBandActive_)
            {
                EndRubberBandSelection();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            HideThumbnailTooltip();
            ClearQuickSendDragState();
            EndRubberBandSelection();
            return 0;
        case WM_CONTEXTMENU:
            if (viewMode_ == BrowserViewMode::Thumbnails && reinterpret_cast<HWND>(wParam) == hwnd_)
            {
                HideThumbnailTooltip();
                POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (screenPoint.x == -1 && screenPoint.y == -1)
                {
                    ShowContextMenu(ContextMenuAnchorScreenPoint());
                    return 0;
                }

                POINT clientPoint = screenPoint;
                ScreenToClient(hwnd_, &clientPoint);
                const int viewIndex = HitTestThumbnailItem(clientPoint);
                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex >= 0 && !selectedModelIndices_.contains(modelIndex))
                {
                    SelectSingleViewIndex(viewIndex);
                }

                ShowContextMenu(screenPoint);
                return 0;
            }
            break;
        case services::ThumbnailScheduler::kMessageId:
        {
            std::unique_ptr<services::ThumbnailReadyUpdate> update(
                reinterpret_cast<services::ThumbnailReadyUpdate*>(lParam));
            if (!update || update->sessionId != thumbnailSessionId_)
            {
                return 0;
            }

            if (!update->success)
            {
                InvalidateThumbnailCellForModelIndex(update->modelIndex);
                return 0;
            }

            bool dimensionsChanged = false;
            if (model_)
            {
                dimensionsChanged = model_->UpdateDecodedDimensions(update->modelIndex, update->imageWidth, update->imageHeight);
            }

            if (dimensionsChanged && sortMode_ == BrowserSortMode::Dimensions)
            {
                RebuildOrder();
                UpdateDetailsListView();
                UpdateVerticalScrollBar();
                ScheduleVisibleThumbnailWork();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }

            if (dimensionsChanged && viewMode_ == BrowserViewMode::Details)
            {
                UpdateDetailsListView();
                InvalidateRect(detailsList_, nullptr, FALSE);
            }

            InvalidateThumbnailCellForModelIndex(update->modelIndex);
            return 0;
        }
        case services::ImageMetadataService::kMessageId:
        {
            std::unique_ptr<services::MetadataReadyUpdate> update(
                reinterpret_cast<services::MetadataReadyUpdate*>(lParam));
            if (!update || !update->success || update->sessionId != metadataSessionId_)
            {
                return 0;
            }

            if (model_ && metadataService_)
            {
                auto metadata = metadataService_->FindCachedMetadata(update->item);
                bool dimensionsChanged = false;
                if (metadata && metadata->imageWidth > 0 && metadata->imageHeight > 0)
                {
                    dimensionsChanged = model_->UpdateDecodedDimensions(update->modelIndex,
                                                                        metadata->imageWidth,
                                                                        metadata->imageHeight);
                }
                if (metadata && metadata->dateTakenTimestampUtc != 0)
                {
                    model_->UpdateDateTakenTimestamp(update->modelIndex, metadata->dateTakenTimestampUtc);
                }

                if (dimensionsChanged && sortMode_ == BrowserSortMode::Dimensions)
                {
                    RebuildOrder();
                    UpdateDetailsListView();
                    UpdateVerticalScrollBar();
                    ScheduleVisibleThumbnailWork();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return 0;
                }

                if (dimensionsChanged)
                {
                    InvalidateThumbnailCellForModelIndex(update->modelIndex);
                }
            }

            if (detailsList_ && viewMode_ == BrowserViewMode::Details)
            {
                InvalidateRect(detailsList_, nullptr, FALSE);
            }
            if (update->modelIndex == ModelIndexFromViewIndex(hoveredTooltipViewIndex_))
            {
                SendMessageW(thumbnailTooltip_, TTM_UPDATE, 0, 0);
            }
            if (update->modelIndex == focusedModelIndex_ || selectedModelIndices_.contains(update->modelIndex))
            {
                NotifyStateChanged();
            }
            return 0;
        }
        case WM_NOTIFY:
            if (thumbnailTooltip_ && reinterpret_cast<const NMHDR*>(lParam)->hwndFrom == thumbnailTooltip_)
            {
                const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
                if (header->code == TTN_GETDISPINFOW)
                {
                    auto* displayInfo = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                    tooltipTextBuffer_ = BuildThumbnailTooltipText(hoveredTooltipViewIndex_);
                    displayInfo->lpszText = tooltipTextBuffer_.empty()
                        ? const_cast<wchar_t*>(L"")
                        : tooltipTextBuffer_.data();
                    return 0;
                }
            }
            if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == detailsList_)
            {
                const NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
                switch (header->code)
                {
                case LVN_GETDISPINFOW:
                {
                    auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                    if ((info->item.mask & LVIF_IMAGE) != 0)
                    {
                        const BrowserItem* item = ItemFromViewIndex(info->item.iItem);
                        info->item.iImage = item && item->isDirectory ? folderIconIndex_ : -1;
                    }
                    if ((info->item.mask & LVIF_TEXT) != 0 && info->item.pszText && info->item.cchTextMax > 0)
                    {
                        listViewTextBuffer_ = BuildListText(info->item.iItem, info->item.iSubItem);
                        wcsncpy_s(info->item.pszText, info->item.cchTextMax, listViewTextBuffer_.c_str(), _TRUNCATE);
                    }
                    return 0;
                }
                case LVN_ITEMCHANGED:
                    RebuildSelectionFromDetailsList();
                    return 0;
                case LVN_BEGINDRAG:
                {
                    const auto* dragInfo = reinterpret_cast<const NMLISTVIEW*>(lParam);
                    if (dragInfo->iItem >= 0)
                    {
                        const int modelIndex = ModelIndexFromViewIndex(dragInfo->iItem);
                        if (modelIndex >= 0)
                        {
                            if (selectedModelIndices_.contains(modelIndex))
                            {
                                FocusSelectedViewIndex(dragInfo->iItem);
                            }
                            else
                            {
                                SelectSingleViewIndex(dragInfo->iItem);
                            }
                        }
                    }

                    RequestQuickSendDrag();
                    return 0;
                }
                case NM_CUSTOMDRAW:
                    return HandleDetailsListCustomDraw(lParam);
                case LVN_KEYDOWN:
                {
                    const auto* keyDown = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
                    const bool controlPressed = GetKeyState(VK_CONTROL) < 0;
                    if (keyDown->wVKey == VK_RETURN)
                    {
                        const int focusedItem = ListView_GetNextItem(detailsList_, -1, LVNI_FOCUSED);
                        if (focusedItem >= 0)
                        {
                            RequestOpenItemForViewIndex(focusedItem);
                        }
                        return 0;
                    }
                    if ((keyDown->wVKey == 'A' || keyDown->wVKey == 'a') && controlPressed)
                    {
                        SelectAll();
                        return 0;
                    }
                    if (keyDown->wVKey == VK_ESCAPE && !selectedModelIndices_.empty())
                    {
                        ClearSelection();
                        return 0;
                    }
                    if (keyDown->wVKey == VK_APPS || (keyDown->wVKey == VK_F10 && GetKeyState(VK_SHIFT) < 0))
                    {
                        ShowContextMenu(ContextMenuAnchorScreenPoint());
                        return 0;
                    }
                    break;
                }
                case LVN_COLUMNCLICK:
                {
                    const auto* columnClick = reinterpret_cast<const NMLISTVIEW*>(lParam);
                    switch (columnClick->iSubItem)
                    {
                    case 0:
                        SetSortMode(BrowserSortMode::FileName);
                        break;
                    case 1:
                        SetSortMode(BrowserSortMode::FileType);
                        break;
                    case 2:
                        SetSortMode(BrowserSortMode::FileSize);
                        break;
                    case 3:
                        SetSortMode(BrowserSortMode::ModifiedDate);
                        break;
                    case 4:
                        SetSortMode(BrowserSortMode::Dimensions);
                        break;
                    default:
                        break;
                    }
                    return 0;
                }
                case NM_DBLCLK:
                {
                    const auto* activate = reinterpret_cast<const NMLISTVIEW*>(lParam);
                    if (activate->iItem >= 0)
                    {
                        RequestOpenItemForViewIndex(activate->iItem);
                    }
                    return 0;
                }
                case NM_RCLICK:
                {
                    POINT screenPoint{};
                    GetCursorPos(&screenPoint);

                    POINT clientPoint = screenPoint;
                    ScreenToClient(detailsList_, &clientPoint);

                    LVHITTESTINFO hitTestInfo{};
                    hitTestInfo.pt = clientPoint;
                    ListView_SubItemHitTest(detailsList_, &hitTestInfo);
                    const int modelIndex = ModelIndexFromViewIndex(hitTestInfo.iItem);
                    if (modelIndex >= 0 && !selectedModelIndices_.contains(modelIndex))
                    {
                        SelectSingleViewIndex(hitTestInfo.iItem);
                    }

                    ShowContextMenu(screenPoint);
                    return 0;
                }
                default:
                    break;
                }
            }
            break;
        case WM_PAINT:
        {
            EnsureD2DRenderTarget();

            if (d2dRenderTarget_ && viewMode_ != BrowserViewMode::Details)
            {
                PAINTSTRUCT paintStruct{};
                BeginPaint(hwnd_, &paintStruct);

                render::D2DRenderer::Instance().ResizeRenderTarget(d2dRenderTarget_.Get(), hwnd_);

                d2dRenderTarget_->BeginDraw();
                const D2D1_SIZE_F size = d2dRenderTarget_->GetSize();

                if (orderedModelIndices_.empty() || viewMode_ != BrowserViewMode::Thumbnails)
                {
                    d2dRenderTarget_->Clear(render::ToD2DColor(colors_.windowBackground));
                    D2DDrawPlaceholderState(d2dRenderTarget_.Get(), size);
                }
                else
                {
                    D2DDrawThumbnailCells(d2dRenderTarget_.Get(), size);
                }

                const HRESULT hr = d2dRenderTarget_->EndDraw();
                const bool recreateTarget = hr == D2DERR_RECREATE_TARGET;
                if (recreateTarget)
                {
                    ReleaseD2DResources();
                }

                if (util::IsStartupBenchmarkEnabled() && HasVisibleCachedThumbnail())
                {
                    util::MarkStartupFirstThumbnailPainted();
                }

                EndPaint(hwnd_, &paintStruct);
                if (recreateTarget)
                {
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return 0;
            }

            PAINTSTRUCT paintStruct{};
            HDC hdc = BeginPaint(hwnd_, &paintStruct);

            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const int width = clientRect.right - clientRect.left;
            const int height = clientRect.bottom - clientRect.top;

            HDC memoryDc = CreateCompatibleDC(hdc);
            HBITMAP bitmap = CreateCompatibleBitmap(hdc, std::max(1, width), std::max(1, height));
            HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

            if (viewMode_ == BrowserViewMode::Details && !orderedModelIndices_.empty())
            {
                FillRect(memoryDc, &clientRect, surfaceBrush_);
            }
            else
            {
                DrawThumbnailCells(memoryDc, clientRect);
            }

            BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);

            SelectObject(memoryDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memoryDc);
            EndPaint(hwnd_, &paintStruct);
            if (util::IsStartupBenchmarkEnabled() && HasVisibleCachedThumbnail())
            {
                util::MarkStartupFirstThumbnailPainted();
            }
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK BrowserPane::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        BrowserPane* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<BrowserPane*>(createStruct->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<BrowserPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
