#include "viewer/ViewerWindowInternal.h"

namespace
{
    constexpr CLSID kGaussianBlurEffectClsid =
        {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
    constexpr CLSID kDirectionalBlurEffectClsid =
        {0x174319a6, 0x58e9, 0x49b2, {0xbb, 0x63, 0xca, 0xf2, 0xc8, 0x11, 0xa3, 0xdb}};
    constexpr CLSID kColorMatrixEffectClsid =
        {0x921f03d6, 0x641c, 0x47df, {0x85, 0x2d, 0xb4, 0xbb, 0x61, 0x53, 0xae, 0x11}};
    constexpr double kKeyboardPanStep = 64.0;

    float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - (2.0f * value));
    }

    D2D1_MATRIX_5X4_F LerpColorMatrix(const D2D1_MATRIX_5X4_F& start,
                                      const D2D1_MATRIX_5X4_F& end,
                                      float amount)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        const auto lerp = [amount](float a, float b)
        {
            return a + ((b - a) * amount);
        };

        return D2D1::Matrix5x4F(
            lerp(start._11, end._11), lerp(start._12, end._12), lerp(start._13, end._13), lerp(start._14, end._14),
            lerp(start._21, end._21), lerp(start._22, end._22), lerp(start._23, end._23), lerp(start._24, end._24),
            lerp(start._31, end._31), lerp(start._32, end._32), lerp(start._33, end._33), lerp(start._34, end._34),
            lerp(start._41, end._41), lerp(start._42, end._42), lerp(start._43, end._43), lerp(start._44, end._44),
            lerp(start._51, end._51), lerp(start._52, end._52), lerp(start._53, end._53), lerp(start._54, end._54));
    }

    bool ExceedsPanDragThreshold(POINT origin, POINT current)
    {
        const int dragThresholdX = std::max(4, GetSystemMetrics(SM_CXDRAG));
        const int dragThresholdY = std::max(4, GetSystemMetrics(SM_CYDRAG));
        return std::abs(current.x - origin.x) >= dragThresholdX
            || std::abs(current.y - origin.y) >= dragThresholdY;
    }
}

namespace hyperbrowse::viewer
{

    LRESULT ViewerWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == services::DisplayColorService::kReadyMessage)
        {
            displayColors_->AcknowledgeNotification();
            RequestRepaint();
            return 0;
        }
        if (message == WM_TIMER && wParam == services::DisplayColorService::kProfilePollTimer)
        {
            if (!IsIconic(hwnd_)) RefreshColorProfile(true);
            return 0;
        }
        switch (message)
        {
        case WM_GETMINMAXINFO:
        {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            if (minMaxInfo)
            {
                minMaxInfo->ptMaxTrackSize.x = std::numeric_limits<LONG>::max();
                minMaxInfo->ptMaxTrackSize.y = std::numeric_limits<LONG>::max();
            }
            return 0;
        }
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED)
            {
                ReleaseD2DResources();
                return 0;
            }
            ClampPanOffsets();
            RequestRepaint();
            return 0;
        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RebuildD2DTextFormats();
            RefreshColorProfile(true);
            RequestRepaint();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            RecoverDisplaySurface();
            return 0;
        case WM_WINDOWPOSCHANGED:
            RefreshColorProfile();
            break;
        case WM_SETTINGCHANGE:
            RefreshColorProfile(true);
            SetDarkTheme(darkTheme_);
            return 0;
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            SetDarkTheme(darkTheme_);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            NotifyActivityChanged(LOWORD(wParam) != WA_INACTIVE);
            RequestRepaint();
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            RequestRepaint();
            return 0;
        case WM_MEASUREITEM:
        {
            auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measureItem && measureItem->CtlType == ODT_MENU)
            {
                MeasureContextMenuItem(measureItem);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (drawItem && drawItem->CtlType == ODT_MENU)
            {
                DrawContextMenuItem(*drawItem);
                return TRUE;
            }
            break;
        }
        case WM_KEYDOWN:
            if (compareSessionActive_)
            {
                const auto rating = browser::ThumbnailRatingKeyPolicy::RatingFromVirtualKey(
                    static_cast<UINT>(wParam),
                    GetKeyState(VK_CONTROL) < 0,
                    GetKeyState(VK_SHIFT) < 0,
                    GetKeyState(VK_MENU) < 0);
                if (rating)
                {
                    const bool keyRepeat = (lParam & (1LL << 30)) != 0;
                    if (!keyRepeat && owner_ && IsWindow(owner_) != FALSE)
                    {
                        PostMessageW(owner_,
                                     kContextMenuCommandMessage,
                                     kContextMenuSetCompareRatingBase + static_cast<UINT>(*rating),
                                     reinterpret_cast<LPARAM>(hwnd_));
                    }
                    return 0;
                }
            }

            if ((wParam == VK_F7 || wParam == VK_F8)
                && (GetKeyState(VK_CONTROL) & 0x8000) == 0
                && (GetKeyState(VK_SHIFT) & 0x8000) == 0
                && (GetKeyState(VK_MENU) & 0x8000) == 0)
            {
                if ((lParam & (1LL << 30)) != 0)
                {
                    return 0;
                }

                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    const QuickSendOperation operation = wParam == VK_F7
                        ? QuickSendOperation::Move
                        : QuickSendOperation::Copy;
                    PostMessageW(owner_,
                                 kQuickSendRequestedMessage,
                                 static_cast<WPARAM>(operation),
                                 reinterpret_cast<LPARAM>(hwnd_));
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('F')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    PostMessageW(owner_, kStartFolderSlideshowMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('I')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_SHIFT) & 0x8000) == 0
                && (GetKeyState(VK_MENU) & 0x8000) == 0)
            {
                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    PostMessageW(owner_, kContextMenuCommandMessage,
                                 kContextMenuImageInformation,
                                 reinterpret_cast<LPARAM>(hwnd_));
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('I')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    PostMessageW(owner_, kContextMenuCommandMessage,
                                 kContextMenuCopyImage,
                                 reinterpret_cast<LPARAM>(hwnd_));
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('G')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_SHIFT) & 0x8000) == 0
                && (GetKeyState(VK_MENU) & 0x8000) == 0
                && (lParam & (1LL << 30)) == 0)
            {
                PromptForItemNumber();
                return 0;
            }

            {
                const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool menuPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                if ((wParam == static_cast<WPARAM>('H') || wParam == static_cast<WPARAM>('W'))
                    && controlPressed
                    && shiftPressed
                    && !menuPressed)
                {
                    if (!fullScreen_)
                    {
                        SetWindowFitMode(wParam == static_cast<WPARAM>('H')
                            ? WindowFitMode::Height
                            : WindowFitMode::Width);
                    }
                    return 0;
                }

                if (wParam == static_cast<WPARAM>('W')
                    && controlPressed
                    && !shiftPressed
                    && !menuPressed)
                {
                    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                    return 0;
                }
            }

            if (compareSessionActive_
                && wParam == VK_TAB
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_MENU) & 0x8000) == 0)
            {
                const int direction = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1;
                const std::size_t tileCount = compareTiles_.size();
                const std::size_t nextTile = direction > 0
                    ? (focusedCompareTile_ + 1) % tileCount
                    : (focusedCompareTile_ + tileCount - 1) % tileCount;
                FocusCompareTileInternal(nextTile);
                RequestRepaint();
                return 0;
            }

            if (compareSessionActive_ && (wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD))
            {
                CycleCompareCandidate(wParam == VK_OEM_PERIOD ? 1 : -1);
                return 0;
            }

            switch (wParam)
            {
            case VK_DELETE:
                if (owner_ && IsWindow(owner_))
                {
                    const WPARAM deleteRequestFlags = (GetKeyState(VK_SHIFT) & 0x8000) != 0
                        ? kDeleteRequestPermanent
                        : 0;
                    util::LogInfo(L"ViewerWindow VK_DELETE dispatch starting, currentIndex=" + std::to_wstring(currentIndex_));
                    {
                        util::ScopedTimer timer(L"ViewerWindow VK_DELETE SendMessageW round-trip");
                        SendMessageW(owner_, kDeleteRequestedMessage,
                                     deleteRequestFlags,
                                     reinterpret_cast<LPARAM>(hwnd_));
                    }
                    util::LogInfo(L"ViewerWindow VK_DELETE dispatch returned, currentIndex=" + std::to_wstring(currentIndex_));
                }
                return 0;
            case VK_RIGHT:
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                {
                    if (compareSessionActive_ && compareTiles_.size() >= 3)
                    {
                        CycleCompareCandidate(1);
                    }
                    else if (compareSessionActive_ && compareTiles_.size() == 2)
                    {
                        CycleAdjacentCompareCandidate(1);
                    }
                    else
                    {
                        SetCompareMode(true, CompareDirection::Next);
                    }
                }
                else
                {
                    CompleteSmoothZoom();
                    if (CanPanHorizontally())
                    {
                        double maxPanX = 0.0;
                        double maxPanY = 0.0;
                        CalculatePanLimits(maxPanX, maxPanY);
                        const double panStep = std::min(kKeyboardPanStep, maxPanX);
                        panOffsetX_ += keyboardPanningInverted_ ? panStep : -panStep;
                        ClampPanOffsets();
                        RequestRepaint();
                    }
                    else
                    {
                        if (compareSessionActive_ && compareTiles_.size() >= 3)
                        {
                            EndCompareSessionPromoteFocused();
                        }
                        Navigate(+1);
                    }
                }
                return 0;
            case VK_LEFT:
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                {
                    if (compareSessionActive_ && compareTiles_.size() >= 3)
                    {
                        CycleCompareCandidate(-1);
                    }
                    else if (compareSessionActive_ && compareTiles_.size() == 2)
                    {
                        CycleAdjacentCompareCandidate(-1);
                    }
                    else
                    {
                        SetCompareMode(true, CompareDirection::Previous);
                    }
                }
                else
                {
                    CompleteSmoothZoom();
                    if (CanPanHorizontally())
                    {
                        double maxPanX = 0.0;
                        double maxPanY = 0.0;
                        CalculatePanLimits(maxPanX, maxPanY);
                        const double panStep = std::min(kKeyboardPanStep, maxPanX);
                        panOffsetX_ -= keyboardPanningInverted_ ? panStep : -panStep;
                        ClampPanOffsets();
                        RequestRepaint();
                    }
                    else
                    {
                        if (compareSessionActive_ && compareTiles_.size() >= 3)
                        {
                            EndCompareSessionPromoteFocused();
                        }
                        Navigate(-1);
                    }
                }
                return 0;
            case VK_UP:
            case VK_DOWN:
                CompleteSmoothZoom();
                if (CanPanVertically())
                {
                    double maxPanX = 0.0;
                    double maxPanY = 0.0;
                    CalculatePanLimits(maxPanX, maxPanY);
                    const double panStep = std::min(kKeyboardPanStep, maxPanY);
                    const double direction = wParam == VK_UP ? 1.0 : -1.0;
                    panOffsetY_ += (keyboardPanningInverted_ ? -direction : direction) * panStep;
                    ClampPanOffsets();
                    RequestRepaint();
                }
                else
                {
                    Navigate(wParam == VK_UP ? -1 : +1);
                }
                return 0;
            case VK_PRIOR:
                if (compareSessionActive_ && compareTiles_.size() >= 3)
                {
                    EndCompareSessionPromoteFocused();
                }
                Navigate(-1);
                return 0;
            case VK_NEXT:
                if (compareSessionActive_ && compareTiles_.size() >= 3)
                {
                    EndCompareSessionPromoteFocused();
                }
                Navigate(+1);
                return 0;
            case VK_HOME:
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    NavigateToBoundary(true);
                }
                return 0;
            case VK_END:
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    NavigateToBoundary(false);
                }
                return 0;
            case VK_TAB:
                ToggleInfoOverlays();
                return 0;
            case VK_ADD:
            case VK_OEM_PLUS:
                ZoomBy(1.25);
                return 0;
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                ZoomBy(0.8);
                return 0;
            case VK_RETURN:
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    ToggleFullScreen();
                    return 0;
                }
                ToggleFitActualSize();
                return 0;
            case '0':
                FitToWindow();
                return 0;
            case '1':
                SetActualSize();
                return 0;
            case 'H':
                FitToHeight();
                return 0;
            case 'W':
                FitToWidth();
                return 0;
            case 'L':
                RotateLeft();
                return 0;
            case 'R':
                RotateRight();
                return 0;
            case 'C':
                ToggleCompareMode();
                return 0;
            case 'X':
                ActivateComparedImage();
                return 0;
            case VK_SPACE:
                if (IsSlideshowActive())
                {
                    StopSlideshow();
                }
                else
                {
                    StartSlideshow(slideshowIntervalMs_);
                }
                return 0;
            case VK_F11:
                ToggleFullScreen();
                return 0;
            case VK_ESCAPE:
                if (fullScreen_)
                {
                    switch (escapeKeyBehavior_)
                    {
                    case EscapeKeyBehavior::FitWidth:
                        SetWindowFitMode(WindowFitMode::Width);
                        break;
                    case EscapeKeyBehavior::FitHeight:
                        SetWindowFitMode(WindowFitMode::Height);
                        break;
                    case EscapeKeyBehavior::ActualSize:
                        SetFullScreen(false);
                        if (CurrentImageFitsWorkArea())
                        {
                            SetActualSize();
                        }
                        else
                        {
                            FitToWindow();
                        }
                        break;
                    case EscapeKeyBehavior::Close:
                    default:
                        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                        break;
                    }
                }
                else
                {
                    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                }
                return 0;
            default:
                break;
            }
            break;
        case WM_CONTEXTMENU:
        {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (screenPoint.x == -1 && screenPoint.y == -1)
            {
                RECT clientRect{};
                GetClientRect(hwnd_, &clientRect);
                screenPoint.x = (clientRect.left + clientRect.right) / 2;
                screenPoint.y = (clientRect.top + clientRect.bottom) / 2;
                ClientToScreen(hwnd_, &screenPoint);
            }
            ShowContextMenu(screenPoint);
            return 0;
        }
        case WM_DROPFILES:
        {
            HDROP dropHandle = reinterpret_cast<HDROP>(wParam);
            if (dropHandle && owner_ && IsWindow(owner_))
            {
                const UINT pathCount = DragQueryFileW(dropHandle, 0xFFFFFFFFu, nullptr, 0);
                for (UINT index = 0; index < pathCount; ++index)
                {
                    const UINT length = DragQueryFileW(dropHandle, index, nullptr, 0);
                    if (length == 0)
                    {
                        continue;
                    }
                    std::wstring path(length + 1, L'\0');
                    DragQueryFileW(dropHandle, index, path.data(), length + 1);
                    path.resize(length);
                    if (!path.empty())
                    {
                        // Hand off to the main window, which owns folder navigation
                        // and the viewer item list.
                        auto* payload = new std::wstring(std::move(path));
                        if (!PostMessageW(owner_, kDroppedFileMessage, 0, reinterpret_cast<LPARAM>(payload)))
                        {
                            delete payload;
                        }
                        break; // only open the first dropped image
                    }
                }
            }
            if (dropHandle)
            {
                DragFinish(dropHandle);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            const short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int wheelSteps = static_cast<int>(wheelDelta / WHEEL_DELTA);
            int stepCount = wheelSteps < 0 ? -wheelSteps : wheelSteps;
            if (stepCount == 0)
            {
                stepCount = 1;
            }

            if (mouseWheelBehavior_ == MouseWheelBehavior::Navigate)
            {
                const int navigateDelta = wheelDelta > 0 ? -1 : +1;
                for (int step = 0; step < stepCount; ++step)
                {
                    Navigate(navigateDelta);
                }
            }
            else
            {
                POINT zoomPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const bool hasZoomPoint = ScreenToClient(hwnd_, &zoomPoint) != FALSE;
                const double zoomFactor = wheelDelta > 0 ? 1.1 : 0.9;
                for (int step = 0; step < stepCount; ++step)
                {
                    ZoomBy(zoomFactor, hasZoomPoint ? &zoomPoint : nullptr);
                }
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && compareSessionActive_)
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            if (LOWORD(lParam) == HTCLIENT && !panning_)
            {
                POINT point{};
                if (GetCursorPos(&point) != FALSE
                    && ScreenToClient(hwnd_, &point) != FALSE
                    && SetNavigationCursorForPoint(point))
                {
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN:
        {
            const POINT clickPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (compareSessionActive_)
            {
                RECT clientRect{};
                GetClientRect(hwnd_, &clientRect);
                const UINT dpi = std::max<UINT>(96, GetDpiForWindow(hwnd_));
                const LONG gap = std::clamp<LONG>(static_cast<LONG>((static_cast<double>(dpi) / 96.0) * 16.0), 12, 32);
                const std::vector<RECT> tileBounds = CompareTileBounds(clientRect, compareTiles_.size(), gap);
                const int hitTile = HitTestCompareTile(tileBounds, clickPoint);
                if (hitTile >= 0)
                {
                    FocusCompareTileInternal(static_cast<std::size_t>(hitTile));
                    CompleteSmoothZoom();
                    panning_ = true;
                    lastPanPoint_ = clickPoint;
                    SetCapture(hwnd_);
                    return 0;
                }
            }
            const int navigationDelta = NavigationDeltaForPoint(clickPoint);
            if (navigationDelta != 0)
            {
                pendingNavigationDelta_ = navigationDelta;
                pendingNavigationPoint_ = clickPoint;
                SetCapture(hwnd_);
                return 0;
            }

            if (currentImage_)
            {
                CompleteSmoothZoom();

                panning_ = true;
                lastPanPoint_ = clickPoint;
                SetCapture(hwnd_);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            const POINT currentPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pendingNavigationDelta_ != 0)
            {
                if (currentImage_ && ExceedsPanDragThreshold(pendingNavigationPoint_, currentPoint))
                {
                    pendingNavigationDelta_ = 0;
                    panning_ = true;
                    lastPanPoint_ = pendingNavigationPoint_;
                    panOffsetX_ += static_cast<double>(currentPoint.x - lastPanPoint_.x);
                    panOffsetY_ += static_cast<double>(currentPoint.y - lastPanPoint_.y);
                    ClampPanOffsets();
                    if (compareSessionActive_)
                    {
                        CaptureFocusedCompareView();
                        if (compareViewsSynchronized_)
                        {
                            ApplyCompareViewToTiles();
                        }
                    }
                    lastPanPoint_ = currentPoint;
                    RequestRepaint();
                    return 0;
                }

                if (!SetNavigationCursorForPoint(currentPoint))
                {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                }
                return 0;
            }

            if (panning_)
            {
                panOffsetX_ += static_cast<double>(currentPoint.x - lastPanPoint_.x);
                panOffsetY_ += static_cast<double>(currentPoint.y - lastPanPoint_.y);
                ClampPanOffsets();
                if (compareSessionActive_)
                {
                    CaptureFocusedCompareView();
                    if (compareViewsSynchronized_)
                    {
                        ApplyCompareViewToTiles();
                    }
                }
                lastPanPoint_ = currentPoint;
                RequestRepaint();
                return 0;
            }

            if (!SetNavigationCursorForPoint(currentPoint))
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            const POINT releasePoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int navigationDelta = pendingNavigationDelta_;
            pendingNavigationDelta_ = 0;

            if (panning_)
            {
                panning_ = false;
            }

            if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }

            if (navigationDelta != 0)
            {
                Navigate(navigationDelta);
            }

            if (!SetNavigationCursorForPoint(releasePoint))
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            panning_ = false;
            pendingNavigationDelta_ = 0;
            return 0;
        case WM_LBUTTONDBLCLK:
            ToggleFullScreen();
            return 0;
        case WM_TIMER:
            if (wParam == kWraparoundTimerId && wraparoundTimerId_)
            {
                KillTimer(hwnd_, wraparoundTimerId_);
                wraparoundTimerId_ = 0;
                wraparoundMessage_.clear();
                RequestRepaint();
                return 0;
            }
            if (wParam == slideshowTimerId_)
            {
                AdvanceSlideshow();
                return 0;
            }
            if (wParam == kTransitionTimerId && transitionTimerId_)
            {
                if (!transitionActive_ || !transitionFromImage_ || !currentImage_)
                {
                    StopTransition();
                    RequestRepaint();
                    return 0;
                }

                const double elapsedMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - transitionStartedAt_).count();
                if (elapsedMs >= static_cast<double>(transitionDurationMs_))
                {
                    StopTransition(false);
                }
                RequestRepaint();
                return 0;
            }
            if (wParam == kSmoothZoomTimerId && smoothZoomTimerId_)
            {
                const double diff = smoothZoomTarget_ - smoothZoomCurrent_;
                if (std::abs(diff) < 0.0005)
                {
                    customZoomScale_ = smoothZoomTarget_;
                    smoothZoomCurrent_ = smoothZoomTarget_;
                    panOffsetX_ = smoothZoomTargetPanX_;
                    panOffsetY_ = smoothZoomTargetPanY_;
                    KillTimer(hwnd_, kSmoothZoomTimerId);
                    smoothZoomTimerId_ = 0;
                }
                else
                {
                    smoothZoomCurrent_ += diff * 0.22;
                    customZoomScale_ = smoothZoomCurrent_;
                    panOffsetX_ += (smoothZoomTargetPanX_ - panOffsetX_) * 0.22;
                    panOffsetY_ += (smoothZoomTargetPanY_ - panOffsetY_) * 0.22;
                }
                ClampPanOffsets();
                RequestRepaint();
                return 0;
            }
            break;
        case kDecodedImageMessage:
            return HandleDecodedImageMessage(lParam);
        case kCompareTileReadyMessage:
            return HandleCompareTileReadyMessage(lParam);
        case kPrefetchImageMessage:
            return HandlePrefetchImageMessage(lParam);
        case kMetadataReadyMessage:
            return HandleMetadataReadyMessage(lParam);
        case kHistogramReadyMessage:
            return HandleHistogramReadyMessage(lParam);
        case WM_PAINT:
        {
            EnsureD2DRenderTarget();

            if (d2dRenderTarget_)
            {
                PAINTSTRUCT paintStruct{};
                BeginPaint(hwnd_, &paintStruct);

                render::D2DRenderer::Instance().ResizeRenderTarget(d2dRenderTarget_.Get(), hwnd_);

                d2dRenderTarget_->BeginDraw();
                const D2D1_SIZE_F size = d2dRenderTarget_->GetSize();
                const float clientWidth = size.width;
                const float clientHeight = size.height;

                bool overlayOnly = repaintMode_ == RepaintMode::InfoOverlayOnly
                    && currentImage_
                    && infoOverlaysVisible_;
                repaintMode_ = RepaintMode::Full;
                if (overlayOnly)
                {
                    RECT invalidRect = paintStruct.rcPaint;
                    RECT overlayRect = InfoOverlayTopPanelRect();
                    InflateRect(&overlayRect, 2, 2);
                    RECT intersection{};
                    if (!IntersectRect(&intersection, &invalidRect, &overlayRect)
                        || !EqualRect(&intersection, &invalidRect))
                    {
                        overlayOnly = false;
                    }
                }

                if (overlayOnly)
                {
                    const RECT clientRect{
                        0,
                        0,
                        static_cast<LONG>(std::lround(clientWidth)),
                        static_cast<LONG>(std::lround(clientHeight)),
                    };
                    const double scale = EffectiveScaleForClient(clientRect);
                    const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                    const int rotatedWidth = swapDimensions ? currentImage_->SourceHeight() : currentImage_->SourceWidth();
                    const int rotatedHeight = swapDimensions ? currentImage_->SourceWidth() : currentImage_->SourceHeight();
                    const int zoomPercent = std::max(1, static_cast<int>(std::lround(scale * 100.0)));
                    if (zoomPercent != currentZoomPercent_)
                    {
                        NotifyZoomChanged(zoomPercent);
                    }

                    DrawInfoOverlays(d2dRenderTarget_.Get(),
                                     clientWidth,
                                     clientHeight,
                                     rotatedWidth,
                                     rotatedHeight,
                                     zoomPercent,
                                     nullptr,
                                     ResolveCompareDirection(compareDirection_),
                                     true);

                    const HRESULT hr = d2dRenderTarget_->EndDraw();
                    const bool recreateTarget = hr == D2DERR_RECREATE_TARGET;
                    if (recreateTarget)
                    {
                        ReleaseD2DResources();
                    }

                    EndPaint(hwnd_, &paintStruct);
                    if (recreateTarget)
                    {
                        RequestRepaint();
                    }
                    return 0;
                }

                d2dRenderTarget_->Clear(render::ToD2DColor(BackgroundColor(darkTheme_)));

                if (compareSessionActive_)
                {
                    DrawCompareSession(d2dRenderTarget_.Get(), clientWidth, clientHeight);
                    if (const auto image = OverlayImage())
                    {
                        const bool rotated = rotationQuarterTurns_ % 2 != 0;
                        DrawInfoOverlays(d2dRenderTarget_.Get(), clientWidth, clientHeight,
                            rotated ? image->SourceHeight() : image->SourceWidth(),
                            rotated ? image->SourceWidth() : image->SourceHeight(), currentZoomPercent_,
                            nullptr, compareDirection_, false);
                    }
                }
                else if (!currentImage_)
                {
                    const bool showIcon = loading_ || errorMessage_.empty();
                    const bool stackedEmptyState = !loading_ && errorMessage_.empty() && d2dStatusArtBitmap_;
                    constexpr float kPanelPaddingLeft = 28.0f;
                    constexpr float kPanelPaddingRight = 32.0f;
                    constexpr float kPanelPaddingVertical = 16.0f;
                    constexpr float kIconTextGap = 30.0f;
                    constexpr float kDesiredTextBlockWidth = 320.0f;
                    constexpr float kMinimumTextBlockWidth = 240.0f;

                    const float maxPanelWidth = std::max(320.0f, clientWidth - 48.0f);
                    const float maxPanelHeight = std::max(140.0f, clientHeight - 36.0f);
                    const ViewerOverlayMetrics overlayMetrics = ViewerOverlayMetricsForWindow(hwnd_, infoOverlayTextSize_);
                    const float loadingTextBlockHeight = overlayMetrics.loadingTitleHeight
                        + overlayMetrics.loadingGap
                        + overlayMetrics.loadingBodyHeight;
                    float renderedIconSize = 0.0f;
                    float panelWidth = std::max(320.0f, std::min(560.0f, clientWidth - 64.0f));
                    float panelHeight = showIcon
                        ? std::min(198.0f, std::max(152.0f, clientHeight - 64.0f))
                        : std::min(maxPanelHeight,
                            std::max(126.0f, (overlayMetrics.loadingTextInset * 2.0f) + loadingTextBlockHeight));

                    if (showIcon && d2dStatusArtBitmap_)
                    {
                        const float artW = d2dStatusArtBitmap_->GetSize().width;
                        if (stackedEmptyState)
                        {
                            const float stackedMaxWidth = std::max(180.0f, clientWidth - 32.0f);
                            const float textBlockWidth = std::min(kDesiredTextBlockWidth,
                                stackedMaxWidth - kPanelPaddingLeft - kPanelPaddingRight);
                            const float maxIconHeight = std::max(24.0f,
                                maxPanelHeight - (kPanelPaddingVertical * 2.0f) - kIconTextGap - loadingTextBlockHeight);
                            renderedIconSize = std::min({128.0f,
                                                         artW,
                                                         std::max(24.0f, stackedMaxWidth - kPanelPaddingLeft - kPanelPaddingRight),
                                                         maxIconHeight});
                            panelWidth = std::min(stackedMaxWidth,
                                std::max(240.0f, textBlockWidth + kPanelPaddingLeft + kPanelPaddingRight));
                            panelHeight = std::min(maxPanelHeight,
                                (kPanelPaddingVertical * 2.0f) + renderedIconSize + kIconTextGap + loadingTextBlockHeight);
                        }
                        else
                        {
                            const float maxIconW = std::max(96.0f, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - kMinimumTextBlockWidth);
                            const float maxIconH = std::max(96.0f, maxPanelHeight - (kPanelPaddingVertical * 2.0f));
                            renderedIconSize = std::min({artW, maxIconW, maxIconH});

                            const float textBlockWidth = std::max(kMinimumTextBlockWidth,
                                std::min(kDesiredTextBlockWidth,
                                         maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - renderedIconSize));
                            panelWidth = std::min(maxPanelWidth,
                                kPanelPaddingLeft + renderedIconSize + kIconTextGap + textBlockWidth + kPanelPaddingRight);
                            panelHeight = std::min(maxPanelHeight,
                                (kPanelPaddingVertical * 2.0f) + std::max(renderedIconSize, loadingTextBlockHeight));
                        }
                    }
                    else
                    {
                        panelHeight = std::min(maxPanelHeight,
                            std::max(panelHeight, (overlayMetrics.loadingTextInset * 2.0f) + loadingTextBlockHeight));
                    }

                    const float panelLeft = (clientWidth - panelWidth) / 2.0f;
                    const float panelTop = (clientHeight - panelHeight) / 2.0f;
                    const D2D1_RECT_F panelRect = D2D1::RectF(panelLeft, panelTop, panelLeft + panelWidth, panelTop + panelHeight);
                    const D2D1_ROUNDED_RECT roundedPanel = D2D1::RoundedRect(panelRect, 11.0f, 11.0f);

                    if (d2dPanelFillBrush_) d2dRenderTarget_->FillRoundedRectangle(roundedPanel, d2dPanelFillBrush_.Get());
                    if (d2dPanelBorderBrush_) d2dRenderTarget_->DrawRoundedRectangle(roundedPanel, d2dPanelBorderBrush_.Get(), 1.0f);

                    const std::wstring title = loading_
                        ? L"Loading Image"
                        : (errorMessage_.empty() ? L"No Image Loaded" : L"Unable to Open Image");
                    const std::wstring messageText = loading_
                        ? L"Opening image..."
                        : (errorMessage_.empty() ? L"Open an image to begin viewing." : errorMessage_);

                    D2D1_RECT_F titleRect{};
                    D2D1_RECT_F bodyRect{};
                    if (showIcon && d2dStatusArtBitmap_)
                    {
                        const float iconX = stackedEmptyState
                            ? panelLeft + (panelWidth - renderedIconSize) / 2.0f
                            : panelLeft + kPanelPaddingLeft;
                        const float iconY = stackedEmptyState
                            ? panelTop + kPanelPaddingVertical
                            : panelTop + (panelHeight - renderedIconSize) / 2.0f;
                        d2dRenderTarget_->DrawBitmap(d2dStatusArtBitmap_.Get(),
                            D2D1::RectF(iconX, iconY, iconX + renderedIconSize, iconY + renderedIconSize),
                            stackedEmptyState ? 0.62f : 1.0f,
                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

                        if (stackedEmptyState)
                        {
                            const float contentLeft = panelLeft + kPanelPaddingLeft;
                            const float contentRight = panelLeft + panelWidth - kPanelPaddingRight;
                            const float contentTop = iconY + renderedIconSize + kIconTextGap;
                            titleRect = D2D1::RectF(contentLeft,
                                                    contentTop,
                                                    contentRight,
                                                    contentTop + overlayMetrics.loadingTitleHeight);
                            bodyRect = D2D1::RectF(contentLeft,
                                                   titleRect.bottom + overlayMetrics.loadingGap,
                                                   contentRight,
                                                   titleRect.bottom + overlayMetrics.loadingGap + overlayMetrics.loadingBodyHeight);
                        }
                        else
                        {
                            const float contentLeft = iconX + renderedIconSize + kIconTextGap;
                            const float contentRight = panelLeft + panelWidth - kPanelPaddingRight;
                            const float textBlockHeight = overlayMetrics.loadingTitleHeight
                                + overlayMetrics.loadingGap
                                + overlayMetrics.loadingBodyHeight;
                            const float contentTop = panelTop + std::max(kPanelPaddingVertical, (panelHeight - textBlockHeight) / 2.0f);
                            titleRect = D2D1::RectF(contentLeft,
                                                    contentTop,
                                                    contentRight,
                                                    contentTop + overlayMetrics.loadingTitleHeight);
                            bodyRect = D2D1::RectF(contentLeft,
                                                   titleRect.bottom + overlayMetrics.loadingGap,
                                                   contentRight,
                                                   titleRect.bottom + overlayMetrics.loadingGap + overlayMetrics.loadingBodyHeight);
                        }
                    }
                    else
                    {
                        titleRect = D2D1::RectF(panelLeft + overlayMetrics.loadingTextInset,
                                                panelTop + overlayMetrics.loadingTextInset,
                                                panelLeft + panelWidth - overlayMetrics.loadingTextInset,
                                                panelTop + overlayMetrics.loadingTextInset + overlayMetrics.loadingTitleHeight);
                        bodyRect = D2D1::RectF(panelLeft + overlayMetrics.loadingTextInset,
                                               titleRect.bottom + overlayMetrics.loadingGap,
                                               panelLeft + panelWidth - overlayMetrics.loadingTextInset,
                                               panelTop + panelHeight - overlayMetrics.loadingTextInset);
                    }

                    if (d2dNameFormat_ && d2dTextBrush_)
                    {
                        d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        d2dRenderTarget_->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                                                   d2dNameFormat_.Get(), titleRect, d2dTextBrush_.Get());
                    }
                    if (d2dInfoFormat_ && d2dMutedTextBrush_)
                    {
                        d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                        d2dRenderTarget_->DrawText(messageText.c_str(), static_cast<UINT32>(messageText.size()),
                                                   d2dInfoFormat_.Get(), bodyRect, d2dMutedTextBrush_.Get());
                        d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    }
                }
                else
                {
                    RECT gdiClientRect{0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight)};
                    const CompareDirection activeCompareDirection = ResolveCompareDirection(compareDirection_);
                    const int compareIndex = compareMode_ ? CompareIndexForDirection(activeCompareDirection) : -1;
                    const browser::BrowserItem* compareItem =
                        (compareIndex >= 0 && compareIndex < static_cast<int>(items_.size()))
                        ? &items_[static_cast<std::size_t>(compareIndex)]
                        : nullptr;
                    const bool compareLayout = compareMode_ && compareItem != nullptr;
                    RECT primaryRect = gdiClientRect;
                    RECT compareRect{};
                    if (compareLayout)
                    {
                        const LONG totalWidth = gdiClientRect.right - gdiClientRect.left;
                        const LONG gapWidth = std::clamp<LONG>(totalWidth / 40, 12, 24);
                        const LONG paneWidth = std::max<LONG>(1, (totalWidth - gapWidth) / 2);
                        primaryRect.right = primaryRect.left + paneWidth;
                        compareRect.left = primaryRect.right + gapWidth;
                        compareRect.top = gdiClientRect.top;
                        compareRect.right = gdiClientRect.right;
                        compareRect.bottom = gdiClientRect.bottom;
                    }

                    const double scale = EffectiveScaleForClient(primaryRect);
                    const int zoomPercent = std::max(1, static_cast<int>(std::lround(scale * 100.0)));
                    if (zoomPercent != currentZoomPercent_)
                    {
                        NotifyZoomChanged(zoomPercent);
                    }

                    auto ensureCurrentBitmap = [&]()
                    {
                        if (!currentImage_)
                        {
                            return;
                        }

                        const int displayedImageIndex = DisplayedImageIndex();
                        const auto displayImage = ImageForDisplay(displayedImageIndex, currentImage_);
                        if (d2dCurrentImageIndex_ == displayedImageIndex && d2dCurrentImageBitmap_
                            && d2dCurrentDisplayImage_.lock() == displayImage)
                        {
                            return;
                        }

                        const auto uploadStartedAt = std::chrono::steady_clock::now();
                        d2dCurrentImageBitmap_ = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                            d2dRenderTarget_.Get(), *displayImage);
                        d2dCurrentDisplayImage_ = displayImage;
                        const double uploadMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - uploadStartedAt).count();
                        util::RecordTiming(L"viewer.upload.d2d", uploadMs);
                        util::LogInfo(L"ViewerWindow D2D upload ms="
                            + std::to_wstring(uploadMs)
                            + L", size=" + std::to_wstring(currentImage_->SourceWidth())
                            + L"x" + std::to_wstring(currentImage_->SourceHeight())
                            + L", bytes=" + std::to_wstring(currentImage_->ByteCount()));
                        d2dCurrentImageIndex_ = d2dCurrentImageBitmap_ ? displayedImageIndex : -1;
                    };

                    const cache::CachedThumbnail* compareImage = nullptr;
                    auto ensureCompareBitmap = [&]()
                    {
                        if (!compareLayout)
                        {
                            d2dCompareImageBitmap_.Reset();
                            d2dCompareImageIndex_ = -1;
                            return;
                        }

                        const CachedImageSlot& compareSlot = activeCompareDirection == CompareDirection::Previous
                            ? previousSlot_
                            : nextSlot_;
                        if (compareSlot.index != compareIndex || !compareSlot.image)
                        {
                            d2dCompareImageBitmap_.Reset();
                            d2dCompareImageIndex_ = -1;
                            return;
                        }

                        compareImage = compareSlot.image.get();
                        const auto displayImage = ImageForDisplay(compareIndex, compareSlot.image);
                        if (d2dCompareImageIndex_ == compareIndex && d2dCompareImageBitmap_
                            && d2dCompareDisplayImage_.lock() == displayImage)
                        {
                            return;
                        }

                        const auto uploadStartedAt = std::chrono::steady_clock::now();
                        d2dCompareImageBitmap_ = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                            d2dRenderTarget_.Get(), *displayImage);
                        d2dCompareDisplayImage_ = displayImage;
                        const double uploadMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - uploadStartedAt).count();
                        util::RecordTiming(L"viewer.upload.d2d.compare", uploadMs);
                        d2dCompareImageIndex_ = d2dCompareImageBitmap_ ? compareIndex : -1;
                    };

                    auto drawComparePlaceholder = [&](std::wstring_view text)
                    {
                        if (!compareLayout)
                        {
                            return;
                        }

                        const D2D1_RECT_F panelRect = D2D1::RectF(
                            static_cast<float>(compareRect.left + 24),
                            static_cast<float>(compareRect.top + 24),
                            static_cast<float>(compareRect.right - 24),
                            static_cast<float>(compareRect.bottom - 24));
                        const D2D1_ROUNDED_RECT roundedPanel = D2D1::RoundedRect(panelRect, 10.0f, 10.0f);
                        if (d2dPanelFillBrush_)
                        {
                            d2dRenderTarget_->FillRoundedRectangle(roundedPanel, d2dPanelFillBrush_.Get());
                        }
                        if (d2dPanelBorderBrush_)
                        {
                            d2dRenderTarget_->DrawRoundedRectangle(roundedPanel, d2dPanelBorderBrush_.Get(), 1.0f);
                        }
                        if (d2dInfoFormat_ && d2dMutedTextBrush_)
                        {
                            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            d2dRenderTarget_->DrawText(
                                text.data(),
                                static_cast<UINT32>(text.size()),
                                d2dInfoFormat_.Get(),
                                panelRect,
                                d2dMutedTextBrush_.Get());
                            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                        }
                    };

                    const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                    const int sourceWidth = currentImage_->SourceWidth();
                    const int sourceHeight = currentImage_->SourceHeight();
                    const int rotatedWidth = swapDimensions ? sourceHeight : sourceWidth;
                    const int rotatedHeight = swapDimensions ? sourceWidth : sourceHeight;
                    ensureCurrentBitmap();
                    ensureCompareBitmap();

                    const double fitScale = FitScaleForImage(*currentImage_, primaryRect);
                    const float currentScaleMultiplier = static_cast<float>(scale / std::max(0.01, fitScale));
                    bool drewTransition = false;

                    if (compareLayout && transitionActive_)
                    {
                        StopTransition();
                    }

                    if (!compareLayout && transitionActive_ && transitionFromImage_)
                    {
                        const double elapsedMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - transitionStartedAt_).count();
                        const float progress = transitionDurationMs_ == 0
                            ? 1.0f
                            : std::clamp(static_cast<float>(elapsedMs / static_cast<double>(transitionDurationMs_)), 0.0f, 1.0f);

                        if (progress >= 1.0f)
                        {
                            StopTransition(false);
                        }
                        else
                        {
                            const auto transitionDisplay = ImageForDisplay(transitionFromIndex_, transitionFromImage_);
                            if (transitionDisplay && d2dRenderTarget_
                                && (!transitionFromBitmap_ || transitionFromDisplayImage_.lock() != transitionDisplay))
                            {
                                transitionFromBitmap_ = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                                    d2dRenderTarget_.Get(), *transitionDisplay);
                                transitionFromDisplayImage_ = transitionDisplay;
                            }

                            if (transitionFromBitmap_ && d2dCurrentImageBitmap_)
                            {
                                const float eased = SmoothStep(progress);
                                const float direction = transitionForward_ ? 1.0f : -1.0f;
                                const float parityDirection = (transitionFromIndex_ % 2 == 0) ? 1.0f : -1.0f;
                                Microsoft::WRL::ComPtr<ID2D1DeviceContext> transitionDeviceContext;
                                if (d2dRenderTarget_)
                                {
                                    d2dRenderTarget_->QueryInterface(__uuidof(ID2D1DeviceContext),
                                                                     reinterpret_cast<void**>(transitionDeviceContext.GetAddressOf()));
                                }
                                const auto drawClippedImage = [&](ID2D1Bitmap* bitmap,
                                                                  const cache::CachedThumbnail& image,
                                                                  const D2D1_RECT_F& clipRect,
                                                                  float opacity,
                                                                  float scaleMultiplier,
                                                                  float offsetX,
                                                                  float offsetY)
                                {
                                    if (!d2dRenderTarget_ || clipRect.right <= clipRect.left || clipRect.bottom <= clipRect.top)
                                    {
                                        return;
                                    }

                                    d2dRenderTarget_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_ALIASED);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), bitmap, image,
                                                    gdiClientRect, opacity, scaleMultiplier, offsetX, offsetY);
                                    d2dRenderTarget_->PopAxisAlignedClip();
                                };

                                switch (activeTransitionStyle_)
                                {
                                case TransitionStyle::Crossfade:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, eased, 1.0f, 0.0f, 0.0f);
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::Slide:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    -direction * clientWidth * eased, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    direction * clientWidth * (1.0f - eased), 0.0f);
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::KenBurns:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - eased, 1.0f + (0.08f * eased),
                                                    -direction * clientWidth * 0.06f * eased,
                                                    parityDirection * clientHeight * 0.04f * eased);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, eased, 1.08f - (0.08f * eased),
                                                    direction * clientWidth * 0.06f * (1.0f - eased),
                                                    -parityDirection * clientHeight * 0.04f * (1.0f - eased));
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::FadeToBlack:
                                    if (eased < 0.5f)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - (eased * 2.0f), 1.0f, 0.0f, 0.0f);
                                    }
                                    else
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, (eased - 0.5f) * 2.0f, 1.0f, 0.0f, 0.0f);
                                    }
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::DiagonalSlide:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    -direction * clientWidth * eased,
                                                    parityDirection * clientHeight * 0.18f * eased);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    direction * clientWidth * (1.0f - eased),
                                                    -parityDirection * clientHeight * 0.18f * (1.0f - eased));
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::Push:
                                {
                                    const float pushEased = std::min(eased * 1.15f, 1.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f, 1.0f - (0.03f * eased),
                                                    -direction * clientWidth * pushEased, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, 1.0f, 0.97f + (0.03f * eased),
                                                    direction * clientWidth * (1.0f - pushEased), 0.0f);
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::CenterWipe:
                                {
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.35f * eased), 1.0f, 0.0f, 0.0f);
                                    const float clipHalfWidth = (clientWidth * eased) * 0.5f;
                                    const float clipHalfHeight = (clientHeight * eased) * 0.5f;
                                    const float clipCenterX = static_cast<float>(gdiClientRect.left) + (clientWidth * 0.5f);
                                    const float clipCenterY = static_cast<float>(gdiClientRect.top) + (clientHeight * 0.5f);
                                    drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                     D2D1::RectF(clipCenterX - clipHalfWidth,
                                                                 clipCenterY - clipHalfHeight,
                                                                 clipCenterX + clipHalfWidth,
                                                                 clipCenterY + clipHalfHeight),
                                                     1.0f,
                                                     1.03f - (0.03f * eased),
                                                     0.0f,
                                                     0.0f);
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::VenetianBlinds:
                                {
                                    constexpr int kBlindCount = 10;
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.25f * eased), 1.0f, 0.0f, 0.0f);
                                    const float blindWidth = clientWidth / static_cast<float>(kBlindCount);
                                    for (int blindIndex = 0; blindIndex < kBlindCount; ++blindIndex)
                                    {
                                        const float blindLeft = static_cast<float>(gdiClientRect.left)
                                            + (blindWidth * static_cast<float>(blindIndex));
                                        const float blindRight = blindIndex == (kBlindCount - 1)
                                            ? static_cast<float>(gdiClientRect.right)
                                            : blindLeft + blindWidth;
                                        const float visibleWidth = (blindRight - blindLeft) * eased;
                                        const bool revealFromLeft = ((blindIndex % 2) == 0) == (direction > 0.0f);
                                        const float clipLeft = revealFromLeft ? blindLeft : (blindRight - visibleWidth);
                                        const float clipRight = revealFromLeft ? (blindLeft + visibleWidth) : blindRight;
                                        drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                         D2D1::RectF(clipLeft,
                                                                     static_cast<float>(gdiClientRect.top),
                                                                     clipRight,
                                                                     static_cast<float>(gdiClientRect.bottom)),
                                                         1.0f,
                                                         1.0f,
                                                         0.0f,
                                                         0.0f);
                                    }
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::SplitWipe:
                                {
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.20f * eased), 1.0f, 0.0f, 0.0f);
                                    const float centerX = static_cast<float>(gdiClientRect.left) + (clientWidth * 0.5f);
                                    const float halfRevealWidth = (clientWidth * 0.5f) * eased;
                                    drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                     D2D1::RectF(centerX - halfRevealWidth,
                                                                 static_cast<float>(gdiClientRect.top),
                                                                 centerX,
                                                                 static_cast<float>(gdiClientRect.bottom)),
                                                     1.0f,
                                                     1.0f,
                                                     -clientWidth * 0.05f * (1.0f - eased),
                                                     0.0f);
                                    drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                     D2D1::RectF(centerX,
                                                                 static_cast<float>(gdiClientRect.top),
                                                                 centerX + halfRevealWidth,
                                                                 static_cast<float>(gdiClientRect.bottom)),
                                                     1.0f,
                                                     1.0f,
                                                     clientWidth * 0.05f * (1.0f - eased),
                                                     0.0f);
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::HorizontalBlinds:
                                {
                                    constexpr int kBlindCount = 8;
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.25f * eased), 1.0f, 0.0f, 0.0f);
                                    const float blindHeight = clientHeight / static_cast<float>(kBlindCount);
                                    for (int blindIndex = 0; blindIndex < kBlindCount; ++blindIndex)
                                    {
                                        const float blindTop = static_cast<float>(gdiClientRect.top)
                                            + (blindHeight * static_cast<float>(blindIndex));
                                        const float blindBottom = blindIndex == (kBlindCount - 1)
                                            ? static_cast<float>(gdiClientRect.bottom)
                                            : blindTop + blindHeight;
                                        const float visibleHeight = (blindBottom - blindTop) * eased;
                                        const bool revealFromTop = ((blindIndex % 2) == 0) == (direction > 0.0f);
                                        const float clipTop = revealFromTop ? blindTop : (blindBottom - visibleHeight);
                                        const float clipBottom = revealFromTop ? (blindTop + visibleHeight) : blindBottom;
                                        drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                         D2D1::RectF(static_cast<float>(gdiClientRect.left),
                                                                     clipTop,
                                                                     static_cast<float>(gdiClientRect.right),
                                                                     clipBottom),
                                                         1.0f,
                                                         1.0f,
                                                         0.0f,
                                                         0.0f);
                                    }
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::CheckerboardWipe:
                                {
                                    constexpr int kGridColumns = 6;
                                    constexpr int kGridRows = 4;
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.15f * eased), 1.0f, 0.0f, 0.0f);
                                    const float cellWidth = clientWidth / static_cast<float>(kGridColumns);
                                    const float cellHeight = clientHeight / static_cast<float>(kGridRows);
                                    for (int row = 0; row < kGridRows; ++row)
                                    {
                                        for (int column = 0; column < kGridColumns; ++column)
                                        {
                                            const float cellLeft = static_cast<float>(gdiClientRect.left)
                                                + (cellWidth * static_cast<float>(column));
                                            const float cellRight = column == (kGridColumns - 1)
                                                ? static_cast<float>(gdiClientRect.right)
                                                : cellLeft + cellWidth;
                                            const float cellTop = static_cast<float>(gdiClientRect.top)
                                                + (cellHeight * static_cast<float>(row));
                                            const float cellBottom = row == (kGridRows - 1)
                                                ? static_cast<float>(gdiClientRect.bottom)
                                                : cellTop + cellHeight;
                                            const float phaseOffset = ((row + column) % 2 == 0) ? 0.0f : 0.18f;
                                            const float phased = std::clamp((eased - phaseOffset) / (1.0f - phaseOffset), 0.0f, 1.0f);
                                            if (phased <= 0.0f)
                                            {
                                                continue;
                                            }

                                            const float insetX = ((cellRight - cellLeft) * (1.0f - phased)) * 0.5f;
                                            const float insetY = ((cellBottom - cellTop) * (1.0f - phased)) * 0.5f;
                                            drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                             D2D1::RectF(cellLeft + insetX,
                                                                         cellTop + insetY,
                                                                         cellRight - insetX,
                                                                         cellBottom - insetY),
                                                             1.0f,
                                                             1.0f,
                                                             0.0f,
                                                             0.0f);
                                        }
                                    }
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::ZoomFade:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - eased, 1.0f + (0.10f * eased),
                                                    0.0f, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, eased, 1.08f - (0.08f * eased),
                                                    0.0f, 0.0f);
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::BlurCrossfade:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildBlurImage = [&](ID2D1Bitmap* bitmap,
                                                                        float blurDeviation,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> blurEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kGaussianBlurEffectClsid,
                                                                                            blurEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            blurEffect->SetInput(0, bitmap);
                                            blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurDeviation);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            blurEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float outgoingBlur = 12.0f * eased;
                                        const float incomingBlur = 12.0f * (1.0f - eased);
                                        if (buildBlurImage(transitionFromBitmap_.Get(), outgoingBlur, &fromImageEffect)
                                            && buildBlurImage(d2dCurrentImageBitmap_.Get(), incomingBlur, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f + (0.04f * eased), 0.0f, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.02f - (0.02f * eased), 0.0f, 0.0f))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.0f, 0.0f, 0.0f);
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::MotionBlur:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildDirectionalBlurImage = [&](ID2D1Bitmap* bitmap,
                                                                                   float blurDeviation,
                                                                                   float blurAngle,
                                                                                   Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> blurEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kDirectionalBlurEffectClsid,
                                                                                            blurEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            blurEffect->SetInput(0, bitmap);
                                            blurEffect->SetValue(D2D1_DIRECTIONALBLUR_PROP_STANDARD_DEVIATION, blurDeviation);
                                            blurEffect->SetValue(D2D1_DIRECTIONALBLUR_PROP_ANGLE, blurAngle);
                                            blurEffect->SetValue(D2D1_DIRECTIONALBLUR_PROP_OPTIMIZATION,
                                                                 D2D1_DIRECTIONALBLUR_OPTIMIZATION_BALANCED);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            blurEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float outgoingBlur = 18.0f * eased;
                                        const float incomingBlur = 18.0f * (1.0f - eased);
                                        const float motionAngle = direction > 0.0f ? 0.0f : 180.0f;
                                        if (buildDirectionalBlurImage(transitionFromBitmap_.Get(), outgoingBlur, motionAngle, &fromImageEffect)
                                            && buildDirectionalBlurImage(d2dCurrentImageBitmap_.Get(), incomingBlur, motionAngle, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f,
                                                               -direction * clientWidth * 0.10f * eased, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.0f,
                                                               direction * clientWidth * 0.10f * (1.0f - eased), 0.0f))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f,
                                                        -direction * clientWidth * 0.10f * eased, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.0f,
                                                        direction * clientWidth * 0.10f * (1.0f - eased), 0.0f);
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::ColorWash:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F warmMatrix = D2D1::Matrix5x4F(
                                            1.08f, 0.04f, 0.01f, 0.0f,
                                            0.14f, 1.00f, 0.06f, 0.0f,
                                            0.04f, 0.14f, 0.72f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.03f, 0.00f, -0.02f, 0.0f);
                                        const D2D1_MATRIX_5X4_F coolMatrix = D2D1::Matrix5x4F(
                                            0.82f, 0.06f, 0.02f, 0.0f,
                                            0.06f, 0.94f, 0.12f, 0.0f,
                                            0.12f, 0.18f, 1.10f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            -0.02f, 0.01f, 0.03f, 0.0f);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, warmMatrix, eased);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(coolMatrix, identityMatrix, eased);
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f + (0.03f * eased),
                                                               -direction * clientWidth * 0.02f * eased,
                                                               parityDirection * clientHeight * 0.02f * eased)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.03f - (0.03f * eased),
                                                               direction * clientWidth * 0.02f * (1.0f - eased),
                                                               -parityDirection * clientHeight * 0.02f * (1.0f - eased)))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f + (0.03f * eased),
                                                        -direction * clientWidth * 0.02f * eased,
                                                        parityDirection * clientHeight * 0.02f * eased);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.03f - (0.03f * eased),
                                                        direction * clientWidth * 0.02f * (1.0f - eased),
                                                        -parityDirection * clientHeight * 0.02f * (1.0f - eased));
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::SepiaDrift:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F sepiaMatrix = D2D1::Matrix5x4F(
                                            0.420f, 0.390f, 0.300f, 0.0f,
                                            0.770f, 0.720f, 0.560f, 0.0f,
                                            0.200f, 0.190f, 0.150f, 0.0f,
                                            0.000f, 0.000f, 0.000f, 1.0f,
                                            0.040f, 0.010f, -0.030f, 0.0f);
                                        const D2D1_MATRIX_5X4_F dustMatrix = D2D1::Matrix5x4F(
                                            0.82f, 0.05f, 0.01f, 0.0f,
                                            0.09f, 0.74f, 0.03f, 0.0f,
                                            0.03f, 0.10f, 0.63f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.030f, 0.020f, -0.020f, 0.0f);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float outgoingAmount = std::min(1.0f, eased * 1.15f);
                                        const float incomingAmount = 1.0f - eased;
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, sepiaMatrix, outgoingAmount);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(dustMatrix, identityMatrix, eased);
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.02f + (0.02f * eased),
                                                               -direction * clientWidth * 0.03f * eased,
                                                               -clientHeight * 0.01f * eased)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.05f - (0.05f * eased),
                                                               direction * clientWidth * 0.03f * incomingAmount,
                                                               clientHeight * 0.01f * incomingAmount))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.02f + (0.02f * eased),
                                                        -direction * clientWidth * 0.03f * eased,
                                                        -clientHeight * 0.01f * eased);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.05f - (0.05f * eased),
                                                        direction * clientWidth * 0.03f * (1.0f - eased),
                                                        clientHeight * 0.01f * (1.0f - eased));
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::Flashbulb:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F flashMatrix = D2D1::Matrix5x4F(
                                            1.35f, 0.18f, 0.18f, 0.0f,
                                            0.18f, 1.35f, 0.18f, 0.0f,
                                            0.18f, 0.18f, 1.35f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.22f, 0.22f, 0.22f, 0.0f);
                                        const D2D1_MATRIX_5X4_F bleachedMatrix = D2D1::Matrix5x4F(
                                            1.12f, 0.08f, 0.08f, 0.0f,
                                            0.08f, 1.12f, 0.08f, 0.0f,
                                            0.08f, 0.08f, 1.12f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.08f, 0.08f, 0.08f, 0.0f);

                                        const float outgoingFlash = std::clamp(eased / 0.45f, 0.0f, 1.0f);
                                        const float incomingRecover = std::clamp((eased - 0.22f) / 0.78f, 0.0f, 1.0f);
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, flashMatrix, outgoingFlash);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(bleachedMatrix, identityMatrix, incomingRecover);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f + (0.04f * outgoingFlash), 0.0f, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.02f - (0.02f * incomingRecover), 0.0f, 0.0f))
                                        {
                                            renderedWithEffects = true;

                                            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> flashBrush;
                                            if (SUCCEEDED(d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                                                                                  flashBrush.GetAddressOf()))
                                                && flashBrush)
                                            {
                                                const float flashPeak = 1.0f - std::min(1.0f, std::abs((eased - 0.35f) / 0.35f));
                                                const float overlayOpacity = 0.50f * flashPeak * flashPeak;
                                                if (overlayOpacity > 0.0f)
                                                {
                                                    flashBrush->SetOpacity(overlayOpacity);
                                                    d2dRenderTarget_->FillRectangle(D2D1::RectF(static_cast<float>(gdiClientRect.left),
                                                                                                static_cast<float>(gdiClientRect.top),
                                                                                                static_cast<float>(gdiClientRect.right),
                                                                                                static_cast<float>(gdiClientRect.bottom)),
                                                                                    flashBrush.Get());
                                                }
                                            }
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        const float flashPeak = 1.0f - std::min(1.0f, std::abs((eased - 0.35f) / 0.35f));
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f + (0.04f * flashPeak), 0.0f, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.02f - (0.02f * eased), 0.0f, 0.0f);

                                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> flashBrush;
                                        if (d2dRenderTarget_
                                            && SUCCEEDED(d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                                                                                  flashBrush.GetAddressOf()))
                                            && flashBrush)
                                        {
                                            const float overlayOpacity = 0.42f * flashPeak * flashPeak;
                                            if (overlayOpacity > 0.0f)
                                            {
                                                flashBrush->SetOpacity(overlayOpacity);
                                                d2dRenderTarget_->FillRectangle(D2D1::RectF(static_cast<float>(gdiClientRect.left),
                                                                                            static_cast<float>(gdiClientRect.top),
                                                                                            static_cast<float>(gdiClientRect.right),
                                                                                            static_cast<float>(gdiClientRect.bottom)),
                                                                                flashBrush.Get());
                                            }
                                        }
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::Prism:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F prismOutMatrix = D2D1::Matrix5x4F(
                                            1.10f, 0.08f, 0.05f, 0.0f,
                                            0.05f, 1.10f, 0.08f, 0.0f,
                                            0.08f, 0.05f, 1.10f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.02f, 0.02f, 0.02f, 0.0f);
                                        const D2D1_MATRIX_5X4_F prismInMatrix = D2D1::Matrix5x4F(
                                            1.00f, 0.06f, 0.14f, 0.0f,
                                            0.14f, 1.00f, 0.06f, 0.0f,
                                            0.06f, 0.14f, 1.00f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.00f, 0.01f, 0.02f, 0.0f);

                                        const float outgoingAmount = eased;
                                        const float incomingAmount = 1.0f - eased;
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, prismOutMatrix, outgoingAmount);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(prismInMatrix, identityMatrix, eased);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float prismOffset = clientWidth * 0.015f;
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.01f,
                                                               -direction * prismOffset * outgoingAmount,
                                                               -parityDirection * clientHeight * 0.01f * outgoingAmount)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.01f,
                                                               direction * prismOffset * incomingAmount,
                                                               parityDirection * clientHeight * 0.01f * incomingAmount))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.01f,
                                                        -direction * clientWidth * 0.015f * eased,
                                                        -parityDirection * clientHeight * 0.01f * eased);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.01f,
                                                        direction * clientWidth * 0.015f * (1.0f - eased),
                                                        parityDirection * clientHeight * 0.01f * (1.0f - eased));
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::MonochromeReveal:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F grayscaleMatrix = D2D1::Matrix5x4F(
                                            0.299f, 0.299f, 0.299f, 0.0f,
                                            0.587f, 0.587f, 0.587f, 0.0f,
                                            0.114f, 0.114f, 0.114f, 0.0f,
                                            0.000f, 0.000f, 0.000f, 1.0f,
                                            0.000f, 0.000f, 0.000f, 0.0f);
                                        const D2D1_MATRIX_5X4_F contrastGrayMatrix = D2D1::Matrix5x4F(
                                            0.380f, 0.380f, 0.380f, 0.0f,
                                            0.560f, 0.560f, 0.560f, 0.0f,
                                            0.120f, 0.120f, 0.120f, 0.0f,
                                            0.000f, 0.000f, 0.000f, 1.0f,
                                            0.020f, 0.020f, 0.020f, 0.0f);
                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();

                                        const float outgoingAmount = std::clamp(eased * 1.1f, 0.0f, 1.0f);
                                        const float incomingAmount = std::clamp((1.0f - eased) * 1.05f, 0.0f, 1.0f);
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, contrastGrayMatrix, outgoingAmount);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(grayscaleMatrix, identityMatrix, eased);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.01f + (0.01f * incomingAmount),
                                                               0.0f, 0.0f))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.01f + (0.01f * (1.0f - eased)), 0.0f, 0.0f);
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::Cut:
                                default:
                                    break;
                                }
                            }
                        }
                    }

                    if (!drewTransition && d2dCurrentImageBitmap_)
                    {
                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                        primaryRect, 1.0f, currentScaleMultiplier,
                                        static_cast<float>(panOffsetX_), static_cast<float>(panOffsetY_));
                        if (compareLayout)
                        {
                            if (compareImage && d2dCompareImageBitmap_)
                            {
                                DrawImageBitmap(d2dRenderTarget_.Get(), d2dCompareImageBitmap_.Get(), *compareImage,
                                                compareRect, 1.0f, currentScaleMultiplier,
                                                static_cast<float>(panOffsetX_), static_cast<float>(panOffsetY_));
                            }
                            else
                            {
                                drawComparePlaceholder(loading_ ? L"Loading compare image..." : L"Preparing adjacent image...");
                            }

                            if (d2dPanelBorderBrush_)
                            {
                                const float dividerX = static_cast<float>(primaryRect.right + ((compareRect.left - primaryRect.right) / 2));
                                d2dRenderTarget_->DrawLine(D2D1::Point2F(dividerX, 20.0f),
                                                           D2D1::Point2F(dividerX, clientHeight - 20.0f),
                                                           d2dPanelBorderBrush_.Get(),
                                                           1.0f);
                            }
                        }
                    }

                    if (infoOverlaysVisible_)
                    {
                        DrawInfoOverlays(d2dRenderTarget_.Get(),
                                         clientWidth,
                                         clientHeight,
                                         rotatedWidth,
                                         rotatedHeight,
                                         zoomPercent,
                                         compareImage,
                                         activeCompareDirection,
                                         false);
                    }

                    if (!wraparoundMessage_.empty() && d2dTextBrush_)
                    {
                        const UINT dpi = std::max<UINT>(96, GetDpiForWindow(hwnd_));
                        const float dpiScale = static_cast<float>(dpi) / 96.0f;
                        const float toastWidth = std::min(720.0f * dpiScale,
                                                          std::max(120.0f * dpiScale,
                                                                   clientWidth - (32.0f * dpiScale)));
                        const float toastHeight = static_cast<float>(
                            hyperbrowse::util::ScaleAppTextDimension(64, appTextSize_)) * dpiScale;
                        const float toastLeft = std::max(8.0f * dpiScale, (clientWidth - toastWidth) / 2.0f);
                        const D2D1_RECT_F toastRect = D2D1::RectF(toastLeft,
                                                                  16.0f * dpiScale,
                                                                  toastLeft + toastWidth,
                                                                  (16.0f * dpiScale) + toastHeight);
                        const D2D1_ROUNDED_RECT roundedToast = D2D1::RoundedRect(toastRect, 8.0f, 8.0f);
                        if (d2dPanelFillBrush_)
                        {
                            d2dRenderTarget_->FillRoundedRectangle(roundedToast, d2dPanelFillBrush_.Get());
                        }
                        if (d2dPanelBorderBrush_)
                        {
                            d2dRenderTarget_->DrawRoundedRectangle(roundedToast, d2dPanelBorderBrush_.Get(), 1.0f);
                        }

                        auto toastFormat = render::D2DRenderer::Instance().CreateTextFormatFromFont(
                            MenuFontOrDefault(menuFont_));
                        if (toastFormat)
                        {
                            toastFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            toastFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                            toastFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                            const float padding = 12.0f * dpiScale;
                            const auto textLayout = render::D2DRenderer::Instance().CreateTextLayout(
                                wraparoundMessage_,
                                toastFormat.Get(),
                                std::max(1.0f, toastWidth - (padding * 2.0f)),
                                toastHeight);
                            if (textLayout)
                            {
                                const DWRITE_TRIMMING trimming{
                                    DWRITE_TRIMMING_GRANULARITY_WORD,
                                    0,
                                    0};
                                Microsoft::WRL::ComPtr<IDWriteInlineObject> trimmingSign;
                                if (render::D2DRenderer::Instance().DWriteFactory())
                                {
                                    render::D2DRenderer::Instance().DWriteFactory()->CreateEllipsisTrimmingSign(
                                        toastFormat.Get(),
                                        trimmingSign.GetAddressOf());
                                }
                                textLayout->SetTrimming(&trimming, trimmingSign.Get());
                                d2dRenderTarget_->DrawTextLayout(
                                    D2D1::Point2F(toastRect.left + padding, toastRect.top),
                                    textLayout.Get(),
                                    d2dTextBrush_.Get(),
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
                            }
                        }
                    }
                }

                if (!currentImage_ && !compareSessionActive_)
                    DrawInfoOverlays(d2dRenderTarget_.Get(), clientWidth, clientHeight, 0, 0, 0, nullptr, compareDirection_, false);
                const HRESULT hr = d2dRenderTarget_->EndDraw();
                const bool recreateTarget = hr == D2DERR_RECREATE_TARGET;
                if (recreateTarget)
                {
                    ReleaseD2DResources();
                }

                EndPaint(hwnd_, &paintStruct);
                if (recreateTarget)
                {
                    RequestRepaint();
                }
                return 0;
            }

            // GDI fallback path
            if (transitionActive_)
            {
                StopTransition();
            }
            PAINTSTRUCT paintStruct{};
            HDC hdc = BeginPaint(hwnd_, &paintStruct);
            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const int clientWidth = clientRect.right - clientRect.left;
            const int clientHeight = clientRect.bottom - clientRect.top;
            if (clientWidth <= 0 || clientHeight <= 0)
            {
                EndPaint(hwnd_, &paintStruct);
                return 0;
            }

            HDC frameDc = hdc;
            HDC backBufferDc = CreateCompatibleDC(hdc);
            HBITMAP backBufferBitmap = nullptr;
            HGDIOBJ oldBackBufferBitmap = nullptr;
            if (backBufferDc)
            {
                backBufferBitmap = CreateCompatibleBitmap(hdc, std::max(1, clientWidth), std::max(1, clientHeight));
                if (backBufferBitmap)
                {
                    oldBackBufferBitmap = SelectObject(backBufferDc, backBufferBitmap);
                    frameDc = backBufferDc;
                }
                else
                {
                    DeleteDC(backBufferDc);
                    backBufferDc = nullptr;
                }
            }

            FillRect(frameDc, &clientRect, backgroundBrush_);
            SetBkMode(frameDc, TRANSPARENT);

            if (compareSessionActive_)
            {
                DrawCompareSessionGdi(frameDc, clientRect);
            }
            else if (currentImage_)
            {
                const double scale = EffectiveScaleForClient(clientRect);
                const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                const int sourceWidth = currentImage_->SourceWidth();
                const int sourceHeight = currentImage_->SourceHeight();
                const int rotatedWidth = swapDimensions ? sourceHeight : sourceWidth;
                const int rotatedHeight = swapDimensions ? sourceWidth : sourceHeight;
                const int destinationWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedWidth) * scale)));
                const int destinationHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedHeight) * scale)));
                const int x = static_cast<int>(std::lround(((clientWidth - destinationWidth) / 2.0) + panOffsetX_));
                const int y = static_cast<int>(std::lround(((clientHeight - destinationHeight) / 2.0) + panOffsetY_));

                HDC bitmapDc = CreateCompatibleDC(frameDc);
                if (bitmapDc)
                {
                    const auto displayImage = ImageForDisplay(DisplayedImageIndex(), currentImage_);
                    HGDIOBJ oldBitmap = SelectObject(bitmapDc, displayImage->Bitmap());
                    SetStretchBltMode(frameDc, HALFTONE);
                    SetBrushOrgEx(frameDc, 0, 0, nullptr);

                    if (rotationQuarterTurns_ == 0)
                    {
                        StretchBlt(frameDc, x, y, destinationWidth, destinationHeight, bitmapDc, 0, 0, sourceWidth, sourceHeight, SRCCOPY);
                    }
                    else
                    {
                        POINT destination[3]{};
                        switch (rotationQuarterTurns_)
                        {
                        case 1:
                            destination[0] = POINT{x + destinationWidth, y};
                            destination[1] = POINT{x + destinationWidth, y + destinationHeight};
                            destination[2] = POINT{x, y};
                            break;
                        case 2:
                            destination[0] = POINT{x + destinationWidth, y + destinationHeight};
                            destination[1] = POINT{x, y + destinationHeight};
                            destination[2] = POINT{x + destinationWidth, y};
                            break;
                        case 3:
                            destination[0] = POINT{x, y + destinationHeight};
                            destination[1] = POINT{x, y};
                            destination[2] = POINT{x + destinationWidth, y + destinationHeight};
                            break;
                        default:
                            break;
                        }
                        PlgBlt(frameDc, destination, bitmapDc, 0, 0, sourceWidth, sourceHeight, nullptr, 0, 0);
                    }
                    SelectObject(bitmapDc, oldBitmap);
                    DeleteDC(bitmapDc);
                }
            }

            DrawOverlaysGdi(frameDc);
            if (backBufferDc)
            {
                BitBlt(hdc, 0, 0, clientWidth, clientHeight, backBufferDc, 0, 0, SRCCOPY);
                SelectObject(backBufferDc, oldBackBufferBitmap);
                DeleteObject(backBufferBitmap);
                DeleteDC(backBufferDc);
            }

            if (!wraparoundMessage_.empty())
            {
                const int dpi = static_cast<int>(std::max<UINT>(96, GetDpiForWindow(hwnd_)));
                const auto scaleForDpi = [dpi](int value) { return MulDiv(value, dpi, 96); };
                const int toastWidth = std::min(scaleForDpi(720),
                                                std::max(scaleForDpi(120), clientWidth - scaleForDpi(32)));
                const int toastHeight = scaleForDpi(
                    hyperbrowse::util::ScaleAppTextDimension(64, appTextSize_));
                const int toastLeft = std::max(scaleForDpi(8), (clientWidth - toastWidth) / 2);
                const int toastTop = scaleForDpi(16);
                HBRUSH toastBrush = CreateSolidBrush(PanelFillColor(darkTheme_));
                HPEN toastPen = CreatePen(PS_SOLID, 1, PanelBorderColor(darkTheme_));
                HGDIOBJ oldBrush = SelectObject(hdc, toastBrush);
                HGDIOBJ oldPen = SelectObject(hdc, toastPen);
                RoundRect(hdc, toastLeft, toastTop, toastLeft + toastWidth, toastTop + toastHeight, 12, 12);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(toastPen);
                DeleteObject(toastBrush);

                const int textPadding = scaleForDpi(12);
                RECT toastRect{toastLeft + textPadding,
                               toastTop,
                               toastLeft + toastWidth - textPadding,
                               toastTop + toastHeight};
                render::DrawGdiText(hdc,
                                    MenuFontOrDefault(menuFont_),
                                    wraparoundMessage_.c_str(),
                                    static_cast<int>(wraparoundMessage_.size()),
                                    toastRect,
                                    DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS,
                                    TextColor(darkTheme_),
                                    PanelFillColor(darkTheme_));
            }

            EndPaint(hwnd_, &paintStruct);
            return 0;
        }
        case WM_CLOSE:
            util::LogInfo(L"ViewerWindow WM_CLOSE hwnd=" + FormatWindowHandle(hwnd));
            asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
            asyncState_->targetWindow.store(nullptr, std::memory_order_release);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            util::LogInfo(L"ViewerWindow WM_DESTROY hwnd=" + FormatWindowHandle(hwnd));
            DrainHistogramResults(hwnd);
            displayColors_->BindTargetWindow(nullptr);
            KillTimer(hwnd_, services::DisplayColorService::kProfilePollTimer);
            ClearWraparoundMessage();
            StopSlideshow();
            StopTransition();
            memoryPressureActive_ = false;
            if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }
            ReleaseD2DResources();
            asyncState_->targetWindow.store(nullptr, std::memory_order_release);
            fullScreen_ = false;
            hasWindowedStateToRestore_ = false;
            windowFitMode_ = WindowFitMode::Regular;
            hasRegularPlacementBeforeFit_ = false;
            regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
            windowedStyle_ = 0;
            windowedExStyle_ = 0;
            windowedPlacement_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
            LogPrefetchStats();
            NotifyActivityChanged(false);
            NotifyZoomChanged(0);
            if (owner_)
            {
                PostMessageW(owner_, kClosedMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
            }
            return 0;
        case WM_NCDESTROY:
        {
            util::LogInfo(L"ViewerWindow WM_NCDESTROY hwnd=" + FormatWindowHandle(hwnd));
            const HWND window = hwnd;
            if (hwnd_ == window)
            {
                hwnd_ = nullptr;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK ViewerWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ViewerWindow* self = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<ViewerWindow*>(createStruct->lpCreateParams);
            self->hwnd_ = hwnd;
            self->asyncState_->targetWindow.store(hwnd, std::memory_order_release);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
