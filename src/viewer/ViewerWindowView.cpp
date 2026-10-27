#include "viewer/ViewerWindowInternal.h"

namespace hyperbrowse::viewer
{

    void ViewerWindow::CompleteSmoothZoom()
    {
        if (!smoothZoomTimerId_)
        {
            return;
        }

        smoothZoomCurrent_ = smoothZoomTarget_;
        customZoomScale_ = smoothZoomTarget_;
        panOffsetX_ = smoothZoomTargetPanX_;
        panOffsetY_ = smoothZoomTargetPanY_;
        KillTimer(hwnd_, kSmoothZoomTimerId);
        smoothZoomTimerId_ = 0;
        ClampPanOffsets();
    }

    void ViewerWindow::ZoomBy(double factor, const POINT* anchorPoint)
    {
        if (compareSessionActive_)
        {
            if (focusedCompareTile_ >= compareTiles_.size() || !compareTiles_[focusedCompareTile_].image
                || factor <= 0.0 || !hwnd_)
            {
                return;
            }
            const CompareTileState& tile = compareTiles_[focusedCompareTile_];
            const RECT tileRect = CompareTileClientRect(focusedCompareTile_);
            const int previousRotation = rotationQuarterTurns_;
            rotationQuarterTurns_ = tile.rotationQuarterTurns;
            const double fitScale = FitScaleForImage(*tile.image, tileRect);
            const double minimumScale = zoomMode_ == ZoomMode::FitHeight
                ? FitHeightScaleForImage(*tile.image, tileRect)
                : (zoomMode_ == ZoomMode::FitWidth ? FitWidthScaleForImage(*tile.image, tileRect) : fitScale);
            const double currentScale = zoomMode_ == ZoomMode::Custom
                ? customZoomScale_
                : minimumScale;
            const double targetScale = std::clamp(currentScale * factor,
                                                  std::min(currentScale, minimumScale),
                                                  kMaximumZoomScale);
            double targetPanX = panOffsetX_;
            double targetPanY = panOffsetY_;
            if (anchorPoint)
            {
                const double centerX = static_cast<double>(tileRect.left + tileRect.right) / 2.0;
                const double centerY = static_cast<double>(tileRect.top + tileRect.bottom) / 2.0;
                const double imagePointX = (static_cast<double>(anchorPoint->x) - centerX - panOffsetX_) / std::max(0.01, currentScale);
                const double imagePointY = (static_cast<double>(anchorPoint->y) - centerY - panOffsetY_) / std::max(0.01, currentScale);
                targetPanX = static_cast<double>(anchorPoint->x) - centerX - (imagePointX * targetScale);
                targetPanY = static_cast<double>(anchorPoint->y) - centerY - (imagePointY * targetScale);
            }
            rotationQuarterTurns_ = previousRotation;
            zoomMode_ = ZoomMode::Custom;
            customZoomScale_ = targetScale;
            panOffsetX_ = targetPanX;
            panOffsetY_ = targetPanY;
            smoothZoomTarget_ = targetScale;
            smoothZoomCurrent_ = targetScale;
            if (smoothZoomTimerId_)
            {
                KillTimer(hwnd_, kSmoothZoomTimerId);
                smoothZoomTimerId_ = 0;
            }
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
            ClampPanOffsets();
            RequestRepaint();
            return;
        }
        if (!currentImage_ || factor <= 0.0 || !hwnd_)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const double fitScale = FitScaleForClient(clientRect);
        const bool fitHeight = zoomMode_ == ZoomMode::FitHeight;
        const bool fitWidth = zoomMode_ == ZoomMode::FitWidth;
        const double minimumScale = fitHeight
            ? FitHeightScaleForClient(clientRect)
            : (fitWidth ? FitWidthScaleForClient(clientRect) : fitScale);
        const bool smoothZoomActive = smoothZoomTimerId_ != 0;
        const double baseScale = smoothZoomActive
            ? smoothZoomTarget_
            : (zoomMode_ == ZoomMode::Fit || fitHeight || fitWidth ? minimumScale : customZoomScale_);
        double targetPanX = panOffsetX_;
        double targetPanY = panOffsetY_;

        double targetScale = std::clamp(baseScale * factor, std::min(baseScale, minimumScale), kMaximumZoomScale);

        if (factor < 1.0 && std::abs(targetScale - minimumScale) < 0.0001)
        {
            if (smoothZoomTimerId_)
            {
                KillTimer(hwnd_, kSmoothZoomTimerId);
                smoothZoomTimerId_ = 0;
            }
            if (fitHeight)
            {
                FitToHeight();
            }
            else if (fitWidth)
            {
                FitToWidth();
            }
            else
            {
                FitToWindow();
            }
            return;
        }

        const double currentScale = smoothZoomActive
            ? smoothZoomCurrent_
            : (zoomMode_ == ZoomMode::Fit || fitHeight || fitWidth ? minimumScale : customZoomScale_);

        if (anchorPoint)
        {
            const double centerX = static_cast<double>(clientRect.right - clientRect.left) / 2.0;
            const double centerY = static_cast<double>(clientRect.bottom - clientRect.top) / 2.0;
            const double imagePointX = (static_cast<double>(anchorPoint->x) - centerX - panOffsetX_) / std::max(0.01, currentScale);
            const double imagePointY = (static_cast<double>(anchorPoint->y) - centerY - panOffsetY_) / std::max(0.01, currentScale);
            targetPanX = static_cast<double>(anchorPoint->x) - centerX - (imagePointX * targetScale);
            targetPanY = static_cast<double>(anchorPoint->y) - centerY - (imagePointY * targetScale);
        }

        zoomMode_ = ZoomMode::Custom;
        smoothZoomTarget_ = targetScale;
        smoothZoomCurrent_ = currentScale;
        smoothZoomTargetPanX_ = targetPanX;
        smoothZoomTargetPanY_ = targetPanY;
        customZoomScale_ = smoothZoomCurrent_;

        if (!smoothZoomTimerId_)
        {
            smoothZoomTimerId_ = SetTimer(hwnd_, kSmoothZoomTimerId, kSmoothZoomIntervalMs, nullptr);
        }
        RequestRepaint();
    }

    void ViewerWindow::ApplyGamepadMotion(double zoomFactor, double panDeltaX, double panDeltaY)
    {
        if (!hwnd_)
        {
            return;
        }

        if (zoomFactor > 0.0 && std::abs(zoomFactor - 1.0) > 0.0001)
        {
            ZoomBy(zoomFactor);
        }

        if (std::abs(panDeltaX) < 0.01 && std::abs(panDeltaY) < 0.01)
        {
            return;
        }

        panOffsetX_ += panDeltaX;
        panOffsetY_ += panDeltaY;
        if (smoothZoomTimerId_)
        {
            smoothZoomTargetPanX_ += panDeltaX;
            smoothZoomTargetPanY_ += panDeltaY;
        }
        ClampPanOffsets();
        if (compareSessionActive_)
        {
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
        }
        RequestRepaint();
    }

    void ViewerWindow::ToggleFitActualSize()
    {
        if (zoomMode_ == ZoomMode::Fit
            || zoomMode_ == ZoomMode::FitHeight
            || zoomMode_ == ZoomMode::FitWidth)
        {
            SetActualSize();
        }
        else
        {
            FitToWindow();
        }
    }

    void ViewerWindow::ShowQuickSendConfirmation(std::wstring message)
    {
        ShowNavigationMessage(std::move(message));
    }

    void ViewerWindow::FitToWindow()
    {
        zoomMode_ = ZoomMode::Fit;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (smoothZoomTimerId_)
        {
            KillTimer(hwnd_, kSmoothZoomTimerId);
            smoothZoomTimerId_ = 0;
        }
        if (compareSessionActive_)
        {
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
        }
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::FitToHeight()
    {
        zoomMode_ = ZoomMode::FitHeight;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (smoothZoomTimerId_)
        {
            KillTimer(hwnd_, kSmoothZoomTimerId);
            smoothZoomTimerId_ = 0;
        }
        if (compareSessionActive_)
        {
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
        }
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::FitToWidth()
    {
        zoomMode_ = ZoomMode::FitWidth;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (smoothZoomTimerId_)
        {
            KillTimer(hwnd_, kSmoothZoomTimerId);
            smoothZoomTimerId_ = 0;
        }
        if (compareSessionActive_)
        {
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
        }
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::SetActualSize()
    {
        zoomMode_ = ZoomMode::Custom;
        customZoomScale_ = 1.0;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (smoothZoomTimerId_)
        {
            KillTimer(hwnd_, kSmoothZoomTimerId);
            smoothZoomTimerId_ = 0;
        }
        if (compareSessionActive_)
        {
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
        }
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::RotateLeft()
    {
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
        {
            CaptureFocusedCompareView();
            CompareTileState& tile = compareTiles_[focusedCompareTile_];
            tile.rotationQuarterTurns = (tile.rotationQuarterTurns + 3) % 4;
            rotationQuarterTurns_ = tile.rotationQuarterTurns;
            panOffsetX_ = 0.0;
            panOffsetY_ = 0.0;
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
            RequestRepaint();
            return;
        }
        rotationQuarterTurns_ = (rotationQuarterTurns_ + 3) % 4;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::RotateRight()
    {
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
        {
            CaptureFocusedCompareView();
            CompareTileState& tile = compareTiles_[focusedCompareTile_];
            tile.rotationQuarterTurns = (tile.rotationQuarterTurns + 1) % 4;
            rotationQuarterTurns_ = tile.rotationQuarterTurns;
            panOffsetX_ = 0.0;
            panOffsetY_ = 0.0;
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_)
            {
                ApplyCompareViewToTiles();
            }
            RequestRepaint();
            return;
        }
        rotationQuarterTurns_ = (rotationQuarterTurns_ + 1) % 4;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::ToggleInfoOverlays()
    {
        SetInfoOverlaysVisible(!infoOverlaysVisible_);
    }

    HMONITOR ViewerWindow::ResolveTargetMonitor(HMONITOR preferredMonitor) const noexcept
    {
        if (preferredMonitor)
        {
            return preferredMonitor;
        }

        if (owner_ && IsWindow(owner_))
        {
            return MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST);
        }

        if (hwnd_ && IsWindow(hwnd_))
        {
            return MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        }

        return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }

    void ViewerWindow::SetWindowFitMode(WindowFitMode mode)
    {
        if (!hwnd_)
        {
            return;
        }

        if (fullScreen_)
        {
            SetFullScreen(false);
        }

        if (mode == windowFitMode_)
        {
            if (mode != WindowFitMode::Regular)
            {
                RestoreRegularWindowPlacement();
            }
            return;
        }

        if (mode == WindowFitMode::Regular)
        {
            RestoreRegularWindowPlacement();
            return;
        }

        if (!currentImage_)
        {
            return;
        }

        if (IsIconic(hwnd_) != FALSE)
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }

        if (windowFitMode_ == WindowFitMode::Regular)
        {
            regularPlacementBeforeFit_.length = sizeof(WINDOWPLACEMENT);
            if (GetWindowPlacement(hwnd_, &regularPlacementBeforeFit_) == FALSE)
            {
                return;
            }
            hasRegularPlacementBeforeFit_ = true;
        }

        if (IsZoomed(hwnd_) != FALSE)
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }

        if (ResizeWindowForFitMode(mode, false))
        {
            windowFitMode_ = mode;
        }
    }

    bool ViewerWindow::CurrentImageFitsWorkArea() const noexcept
    {
        if (!hwnd_ || !currentImage_)
        {
            return false;
        }

        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (!monitor || GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
        {
            return false;
        }

        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const int imageWidth = swapDimensions ? currentImage_->SourceHeight() : currentImage_->SourceWidth();
        const int imageHeight = swapDimensions ? currentImage_->SourceWidth() : currentImage_->SourceHeight();
        const LONG workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const LONG workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        return imageWidth > 0 && imageHeight > 0
            && imageWidth <= workWidth
            && imageHeight <= workHeight;
    }

    bool ViewerWindow::ResizeWindowForFitMode(WindowFitMode mode, bool preservePosition)
    {
        if (!hwnd_ || fullScreen_ || mode == WindowFitMode::Regular || !currentImage_)
        {
            return false;
        }

        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (!monitor || GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
        {
            return false;
        }

        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE))
            & ~(WS_MAXIMIZE | WS_MINIMIZE);
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        RECT frameRect{0, 0, 0, 0};
        if (AdjustWindowRectEx(&frameRect, style, FALSE, exStyle) == FALSE)
        {
            return false;
        }

        const LONG frameWidth = frameRect.right - frameRect.left;
        const LONG frameHeight = frameRect.bottom - frameRect.top;
        const LONG workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const LONG workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const double imageWidth = static_cast<double>(swapDimensions
            ? currentImage_->SourceHeight()
            : currentImage_->SourceWidth());
        const double imageHeight = static_cast<double>(swapDimensions
            ? currentImage_->SourceWidth()
            : currentImage_->SourceHeight());
        const LONG availableClientWidth = std::max<LONG>(1, workWidth - frameWidth);
        const LONG availableClientHeight = std::max<LONG>(1, workHeight - frameHeight);
        LONG clientWidth = availableClientWidth;
        LONG clientHeight = availableClientHeight;
        if (mode == WindowFitMode::Height)
        {
            clientWidth = std::max<LONG>(1, static_cast<LONG>(std::lround(
                static_cast<double>(clientHeight) * imageWidth / std::max(1.0, imageHeight))));
        }
        else
        {
            const LONG aspectHeight = std::max<LONG>(1, static_cast<LONG>(std::lround(
                static_cast<double>(clientWidth) * imageHeight / std::max(1.0, imageWidth))));
            clientHeight = std::min(availableClientHeight, aspectHeight);
        }

        RECT desiredWindowRect{0, 0, clientWidth, clientHeight};
        if (AdjustWindowRectEx(&desiredWindowRect, style, FALSE, exStyle) == FALSE)
        {
            return false;
        }

        const LONG windowWidth = desiredWindowRect.right - desiredWindowRect.left;
        const LONG windowHeight = desiredWindowRect.bottom - desiredWindowRect.top;
        const LONG windowLeft = mode == WindowFitMode::Height
            ? monitorInfo.rcWork.left + (workWidth - windowWidth) / 2
            : monitorInfo.rcWork.left;
        const LONG windowTop = mode == WindowFitMode::Height
            ? monitorInfo.rcWork.top
            : monitorInfo.rcWork.top + std::max<LONG>(0, (workHeight - windowHeight) / 2);

        if (SetWindowPos(hwnd_, HWND_TOP,
                         windowLeft,
                         windowTop,
                         windowWidth,
                         windowHeight,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW
                             | (preservePosition ? SWP_NOMOVE : 0)) == FALSE)
        {
            return false;
        }
        FitToWindow();
        return true;
    }

    void ViewerWindow::RefreshWindowFitForCurrentImage()
    {
        if (windowFitMode_ != WindowFitMode::Regular)
        {
            ResizeWindowForFitMode(windowFitMode_, true);
        }
    }

    void ViewerWindow::RestoreRegularWindowPlacement()
    {
        if (!hwnd_ || !hasRegularPlacementBeforeFit_)
        {
            return;
        }

        SetWindowPlacement(hwnd_, &regularPlacementBeforeFit_);
        windowFitMode_ = WindowFitMode::Regular;
        hasRegularPlacementBeforeFit_ = false;
        regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
        RequestRepaint();
    }

    void ViewerWindow::SetFullScreen(bool enabled, HMONITOR targetMonitor)
    {
        if (!hwnd_)
        {
            return;
        }

        if (enabled)
        {
            if (!fullScreen_)
            {
                hasWindowedStateToRestore_ = true;
                if (windowFitMode_ != WindowFitMode::Regular)
                {
                    RestoreRegularWindowPlacement();
                }
                windowedStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
                windowedExStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
                windowedPlacement_.length = sizeof(WINDOWPLACEMENT);
                GetWindowPlacement(hwnd_, &windowedPlacement_);
                SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_ & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU));
                SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_ & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
            }

            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            const HMONITOR monitor = ResolveTargetMonitor(targetMonitor);
            if (GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
            {
                monitorInfo.rcMonitor.left = 0;
                monitorInfo.rcMonitor.top = 0;
                monitorInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
                monitorInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
            }

            SetWindowPos(hwnd_, HWND_TOP,
                         monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            fullScreen_ = true;
            auto settings = overlaySettings_;
            settings.panes[static_cast<std::size_t>(OverlayPane::FullMetadata)].fullScreenEnabled = DefaultFullScreenFullMetadataVisible();
            SetOverlaySettings(settings);
            FitToWindow();
            return;
        }

        if (!fullScreen_)
        {
            return;
        }

        SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_);
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fullScreen_ = false;
        auto settings = overlaySettings_;
        settings.panes[static_cast<std::size_t>(OverlayPane::FullMetadata)].windowedEnabled = DefaultWindowedFullMetadataVisible();
        SetOverlaySettings(settings);
        hasWindowedStateToRestore_ = true;
        windowFitMode_ = WindowFitMode::Regular;
        hasRegularPlacementBeforeFit_ = false;
        regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
        FitToWindow();
    }

    void ViewerWindow::ToggleFullScreen()
    {
        if (!hwnd_)
        {
            return;
        }

        if (!fullScreen_)
        {
            SetFullScreen(true);
            return;
        }

        SetFullScreen(false);
    }

    void ViewerWindow::ResetViewState(bool preserveFitMode)
    {
        const bool keepFitMode = preserveFitMode
            && (zoomMode_ == ZoomMode::FitHeight || zoomMode_ == ZoomMode::FitWidth);
        zoomMode_ = keepFitMode ? zoomMode_ : ZoomMode::Fit;
        customZoomScale_ = 1.0;
        currentZoomPercent_ = 0;
        rotationQuarterTurns_ = 0;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        panning_ = false;
        if (smoothZoomTimerId_)
        {
            KillTimer(hwnd_, kSmoothZoomTimerId);
            smoothZoomTimerId_ = 0;
        }
        smoothZoomTarget_ = 1.0;
        smoothZoomCurrent_ = 1.0;
        smoothZoomTargetPanX_ = 0.0;
        smoothZoomTargetPanY_ = 0.0;
    }

    void ViewerWindow::CalculatePanLimits(double& maxPanX, double& maxPanY) const
    {
        maxPanX = 0.0;
        maxPanY = 0.0;
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
        {
            const CompareTileState& tile = compareTiles_[focusedCompareTile_];
            if (!tile.image)
            {
                return;
            }
            const RECT tileRect = CompareTileClientRect(focusedCompareTile_);
            const bool swapDimensions = (tile.rotationQuarterTurns % 2) != 0;
            const double imageWidth = static_cast<double>(swapDimensions
                ? tile.image->SourceHeight()
                : tile.image->SourceWidth());
            const double imageHeight = static_cast<double>(swapDimensions
                ? tile.image->SourceWidth()
                : tile.image->SourceHeight());
            const double clientWidth = static_cast<double>(std::max<LONG>(1, tileRect.right - tileRect.left));
            const double clientHeight = static_cast<double>(std::max<LONG>(1, tileRect.bottom - tileRect.top));
            const double fitScale = std::min(clientWidth / std::max(1.0, imageWidth),
                                             clientHeight / std::max(1.0, imageHeight));
            const double scale = zoomMode_ == ZoomMode::FitHeight
                ? clientHeight / std::max(1.0, imageHeight)
                : (zoomMode_ == ZoomMode::FitWidth
                    ? clientWidth / std::max(1.0, imageWidth)
                    : (zoomMode_ == ZoomMode::Custom ? customZoomScale_ : fitScale));
            maxPanX = std::max(0.0, ((imageWidth * scale) - clientWidth) / 2.0);
            maxPanY = std::max(0.0, ((imageHeight * scale) - clientHeight) / 2.0);
            return;
        }
        if (!currentImage_ || !hwnd_ || zoomMode_ == ZoomMode::Fit)
        {
            return;
        }

        RECT clientRect{};
        if (GetClientRect(hwnd_, &clientRect) == FALSE)
        {
            return;
        }

        if (compareMode_ && ActiveCompareIndex() >= 0)
        {
            const LONG totalWidth = clientRect.right - clientRect.left;
            const LONG gapWidth = std::clamp<LONG>(totalWidth / 40, 12, 24);
            const LONG paneWidth = std::max<LONG>(1, (totalWidth - gapWidth) / 2);
            clientRect.right = clientRect.left + paneWidth;
        }

        const double scale = EffectiveScaleForClient(clientRect);
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const int rotatedWidth = swapDimensions ? currentImage_->SourceHeight() : currentImage_->SourceWidth();
        const int rotatedHeight = swapDimensions ? currentImage_->SourceWidth() : currentImage_->SourceHeight();
        const double destinationWidth = static_cast<double>(std::max(
            1, static_cast<int>(std::lround(static_cast<double>(rotatedWidth) * scale))));
        const double destinationHeight = static_cast<double>(std::max(
            1, static_cast<int>(std::lround(static_cast<double>(rotatedHeight) * scale))));
        const double clientWidth = static_cast<double>(std::max<LONG>(1, clientRect.right - clientRect.left));
        const double clientHeight = static_cast<double>(std::max<LONG>(1, clientRect.bottom - clientRect.top));
        maxPanX = std::max(0.0, (destinationWidth - clientWidth) / 2.0);
        maxPanY = std::max(0.0, (destinationHeight - clientHeight) / 2.0);
    }

    void ViewerWindow::ClampPanOffsets()
    {
        double maxPanX = 0.0;
        double maxPanY = 0.0;
        CalculatePanLimits(maxPanX, maxPanY);

        panOffsetX_ = std::clamp(panOffsetX_, -maxPanX, maxPanX);
        panOffsetY_ = std::clamp(panOffsetY_, -maxPanY, maxPanY);
    }

    bool ViewerWindow::CanPanHorizontally() const
    {
        double maxPanX = 0.0;
        double maxPanY = 0.0;
        CalculatePanLimits(maxPanX, maxPanY);
        return maxPanX > 0.0;
    }

    bool ViewerWindow::CanPanVertically() const
    {
        double maxPanX = 0.0;
        double maxPanY = 0.0;
        CalculatePanLimits(maxPanX, maxPanY);
        return maxPanY > 0.0;
    }

    int ViewerWindow::DisplayedImageIndex() const noexcept
    {
        if (currentImage_
            && currentSlot_.index >= 0
            && currentSlot_.index < static_cast<int>(items_.size()))
        {
            return currentSlot_.index;
        }

        return currentIndex_;
    }
}
