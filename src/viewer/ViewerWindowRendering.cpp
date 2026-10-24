#include "viewer/ViewerWindowInternal.h"

namespace
{

    float MetadataPanelBorderAlpha(bool darkTheme)
    {
        return darkTheme ? 0.88f : 0.82f;
    }
}

namespace hyperbrowse::viewer
{

    void ViewerWindow::RecoverDisplaySurface()
    {
        RefreshColorProfile(true);
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        ReleaseD2DResources();
        EnsureD2DRenderTarget();
        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    }

    void ViewerWindow::EnsureD2DRenderTarget()
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        if (d2dRenderTarget_)
        {
            return;
        }

        auto& renderer = render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return;
        }

        d2dRenderTarget_ = renderer.CreateHwndRenderTarget(hwnd_);
        if (d2dRenderTarget_)
        {
            repaintMode_ = RepaintMode::Full;
            util::LogInfo(L"ViewerWindow created D2D render target for HWND " + FormatWindowHandle(hwnd_));
            RebuildD2DBrushes();
            RebuildD2DTextFormats();

            if (statusArt_)
            {
                d2dStatusArtBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *statusArt_);
            }
        }
    }

    void ViewerWindow::RebuildD2DTextFormats()
    {
        d2dNameFormat_.Reset();
        d2dInfoFormat_.Reset();
        d2dBottomInfoFormat_.Reset();
        d2dMetadataFormat_.Reset();

        if (!d2dRenderTarget_)
        {
            return;
        }

        auto& renderer = render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return;
        }

        const ViewerOverlayMetrics overlayMetrics = ViewerOverlayMetricsForWindow(hwnd_, infoOverlayTextSize_);
        d2dNameFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.nameFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        d2dInfoFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.infoFontSize, DWRITE_FONT_WEIGHT_NORMAL);
    d2dBottomInfoFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.bottomInfoFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    d2dMetadataFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.metadataFontSize, DWRITE_FONT_WEIGHT_NORMAL);

        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        if (d2dNameFormat_)
        {
            d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dNameFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dNameFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dInfoFormat_)
        {
            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dInfoFormat_->SetTrimming(&trimming, nullptr);
            d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (d2dBottomInfoFormat_)
        {
            d2dBottomInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dBottomInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dBottomInfoFormat_->SetTrimming(&trimming, nullptr);
            d2dBottomInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (d2dMetadataFormat_)
        {
            d2dMetadataFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dMetadataFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dMetadataFormat_->SetTrimming(&trimming, nullptr);
            d2dMetadataFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
    }

    void ViewerWindow::ReleaseD2DResources()
    {
        repaintMode_ = RepaintMode::Full;
        if (d2dRenderTarget_)
        {
            util::LogInfo(L"ViewerWindow releasing D2D resources for HWND " + FormatWindowHandle(hwnd_));
        }

        d2dCurrentImageBitmap_.Reset();
        d2dCompareImageBitmap_.Reset();
        d2dCurrentDisplayImage_.reset();
        d2dCompareDisplayImage_.reset();
        for (CompareTileState& tile : compareTiles_)
        {
            tile.bitmap.Reset();
            tile.displayPixels.reset();
        }
        transitionFromBitmap_.Reset();
        transitionFromDisplayImage_.reset();
        pendingTransitionFromBitmap_.Reset();
        pendingTransitionFromDisplayImage_.reset();
        d2dStatusArtBitmap_.Reset();
        d2dNameFormat_.Reset();
        d2dInfoFormat_.Reset();
        d2dBottomInfoFormat_.Reset();
        d2dMetadataFormat_.Reset();
        d2dBackgroundBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dPanelFillBrush_.Reset();
        d2dPanelBorderBrush_.Reset();
        d2dMetadataPanelFillBrush_.Reset();
        d2dMetadataPanelBorderBrush_.Reset();
        d2dRenderTarget_.Reset();
        d2dCurrentImageIndex_ = -1;
        d2dCompareImageIndex_ = -1;
    }

    void ViewerWindow::RebuildD2DBrushes()
    {
        if (!d2dRenderTarget_)
        {
            return;
        }

        d2dBackgroundBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dPanelFillBrush_.Reset();
        d2dPanelBorderBrush_.Reset();
        d2dMetadataPanelFillBrush_.Reset();
        d2dMetadataPanelBorderBrush_.Reset();

        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(BackgroundColor(darkTheme_)), d2dBackgroundBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(TextColor(darkTheme_)), d2dTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(MutedTextColor(darkTheme_)), d2dMutedTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelFillColor(darkTheme_)), d2dPanelFillBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelBorderColor(darkTheme_)), d2dPanelBorderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelFillColor(darkTheme_), MetadataPanelFillAlpha(darkTheme_)),
                                                d2dMetadataPanelFillBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelBorderColor(darkTheme_), MetadataPanelBorderAlpha(darkTheme_)),
                                                d2dMetadataPanelBorderBrush_.GetAddressOf());
    }

    RECT ViewerWindow::InfoOverlayTopPanelRect() const noexcept
    {
        RECT clientRect{};
        if (!hwnd_ || GetClientRect(hwnd_, &clientRect) == FALSE)
        {
            return {};
        }

        const float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
        const ViewerOverlayMetrics overlayMetrics = ViewerOverlayMetricsForWindow(hwnd_, infoOverlayTextSize_);
        const bool fullMetadataShown = fullMetadataVisible_ && infoOverlaysVisible_;
        const bool compareLayout = compareMode_ && ActiveCompareIndex() >= 0;
        const float availablePanelWidth = fullMetadataShown
            ? std::max(120.0f, (clientWidth * (2.0f / 3.0f)) - 32.0f)
            : std::max(120.0f, clientWidth - 32.0f);
        const float topPanelWidth = std::min((compareLayout ? 760.0f : 560.0f) * overlayMetrics.overlayWidthScale,
                                             availablePanelWidth);
        const float topPanelHeight = (overlayMetrics.topPanelPaddingY * 2.0f)
            + overlayMetrics.topNameHeight
            + overlayMetrics.topInfoHeight;

        return RECT{
            16,
            16,
            16 + static_cast<LONG>(std::lround(topPanelWidth)),
            16 + static_cast<LONG>(std::lround(topPanelHeight)),
        };
    }

    void ViewerWindow::DrawInfoOverlays(ID2D1RenderTarget* renderTarget,
                                        float clientWidth,
                                        float clientHeight,
                                        int rotatedWidth,
                                        int rotatedHeight,
                                        int zoomPercent,
                                        const cache::CachedThumbnail* compareImage,
                                        CompareDirection activeCompareDirection,
                                        bool topOnly) const
    {
        if (!renderTarget || !infoOverlaysVisible_)
        {
            return;
        }

        const ViewerOverlayMetrics overlayMetrics = ViewerOverlayMetricsForWindow(hwnd_, infoOverlayTextSize_);
        const int compareIndex = compareMode_ ? CompareIndexForDirection(activeCompareDirection) : -1;
        const browser::BrowserItem* currentItem =
            (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(items_.size()))
            ? &items_[static_cast<std::size_t>(currentIndex_)]
            : nullptr;
        const browser::BrowserItem* compareItem =
            (compareIndex >= 0 && compareIndex < static_cast<int>(items_.size()))
            ? &items_[static_cast<std::size_t>(compareIndex)]
            : nullptr;
        const bool compareLayout = compareMode_ && compareItem != nullptr;

        std::wstring fileName = currentItem ? currentItem->fileName : std::wstring(L"Image");
        if (compareLayout)
        {
            fileName.append(L"  <->  ");
            fileName.append(compareItem->fileName);
        }

        std::wstring topLine = std::to_wstring(currentIndex_ + 1) + L" / "
            + std::to_wstring(static_cast<int>(items_.size()));
        if (currentItem)
        {
            topLine.append(L"  |  ");
            topLine.append(currentItem->fileType);
            topLine.append(L"  |  ");
            topLine.append(browser::FormatByteSize(currentItem->fileSizeBytes));
        }
        if (compareLayout)
        {
            topLine.append(L"  |  Compare ");
            topLine.append(activeCompareDirection == CompareDirection::Next ? L"next" : L"previous");
        }

        const RECT topPanelRect = InfoOverlayTopPanelRect();
        const bool fullMetadataShown = fullMetadataVisible_ && infoOverlaysVisible_;
        const float availablePanelWidth = fullMetadataShown
            ? std::max(120.0f, (clientWidth * (2.0f / 3.0f)) - 32.0f)
            : std::max(120.0f, clientWidth - 32.0f);
        const float bottomPanelWidth = std::min((compareLayout ? 640.0f : 380.0f) * overlayMetrics.overlayWidthScale,
                                                availablePanelWidth);
        const float bottomPanelHeight = (overlayMetrics.bottomPanelPaddingY * 2.0f)
            + overlayMetrics.bottomInfoHeight;
        const float bottomPanelLeft = clientWidth - 16.0f - bottomPanelWidth;
        const float bottomPanelTop = clientHeight - 16.0f - bottomPanelHeight;
        const D2D1_RECT_F topPanel = D2D1::RectF(
            static_cast<float>(topPanelRect.left),
            static_cast<float>(topPanelRect.top),
            static_cast<float>(topPanelRect.right),
            static_cast<float>(topPanelRect.bottom));
        const D2D1_RECT_F bottomPanel = D2D1::RectF(bottomPanelLeft,
                                                   bottomPanelTop,
                                                   bottomPanelLeft + bottomPanelWidth,
                                                   bottomPanelTop + bottomPanelHeight);

        const D2D1_ROUNDED_RECT roundedTop = D2D1::RoundedRect(topPanel, 8.0f, 8.0f);
        if (d2dPanelFillBrush_) renderTarget->FillRoundedRectangle(roundedTop, d2dPanelFillBrush_.Get());
        if (d2dPanelBorderBrush_) renderTarget->DrawRoundedRectangle(roundedTop, d2dPanelBorderBrush_.Get(), 1.0f);

        const D2D1_RECT_F nameRect = D2D1::RectF(topPanel.left + overlayMetrics.topPanelPaddingX,
                                                topPanel.top + overlayMetrics.topPanelPaddingY,
                                                topPanel.right - overlayMetrics.topPanelPaddingX,
                                                topPanel.top + overlayMetrics.topPanelPaddingY + overlayMetrics.topNameHeight);
        const D2D1_RECT_F topInfoRect = D2D1::RectF(topPanel.left + overlayMetrics.topPanelPaddingX,
                                                   nameRect.bottom,
                                                   topPanel.right - overlayMetrics.topPanelPaddingX,
                                                   nameRect.bottom + overlayMetrics.topInfoHeight);

        if (d2dNameFormat_ && d2dTextBrush_)
        {
            d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            renderTarget->DrawText(fileName.c_str(), static_cast<UINT32>(fileName.size()),
                                   d2dNameFormat_.Get(), nameRect, d2dTextBrush_.Get());
        }
        if (d2dInfoFormat_ && d2dMutedTextBrush_)
        {
            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            renderTarget->DrawText(topLine.c_str(), static_cast<UINT32>(topLine.size()),
                                   d2dInfoFormat_.Get(), topInfoRect, d2dMutedTextBrush_.Get());
        }

        if (topOnly)
        {
            return;
        }

        const D2D1_ROUNDED_RECT roundedBottom = D2D1::RoundedRect(bottomPanel, 8.0f, 8.0f);
        if (d2dPanelFillBrush_) renderTarget->FillRoundedRectangle(roundedBottom, d2dPanelFillBrush_.Get());
        if (d2dPanelBorderBrush_) renderTarget->DrawRoundedRectangle(roundedBottom, d2dPanelBorderBrush_.Get(), 1.0f);

        std::wstring bottomLine = std::to_wstring(rotatedWidth) + L" x " + std::to_wstring(rotatedHeight);
        bottomLine.append(L"  |  ");
        bottomLine.append(std::to_wstring(zoomPercent));
        bottomLine.append(L"%");
        bottomLine.append(L"  |  ");
        bottomLine.append(zoomMode_ == ZoomMode::Fit
            ? L"Fit"
            : (zoomMode_ == ZoomMode::FitHeight
                ? L"Fit Height"
                : (zoomMode_ == ZoomMode::FitWidth ? L"Fit Width" : L"Custom")));
        if (compareLayout)
        {
            bottomLine.append(compareImage
                ? L"  |  Shift+Left/Right change pair  |  C toggle  |  X swap"
                : L"  |  Loading compare image...");
        }

        const D2D1_RECT_F bottomInfoRect = D2D1::RectF(bottomPanel.left + overlayMetrics.bottomPanelPaddingX,
                                                       bottomPanel.top + overlayMetrics.bottomPanelPaddingY,
                                                       bottomPanel.right - overlayMetrics.bottomPanelPaddingX,
                                                       bottomPanel.top + overlayMetrics.bottomPanelPaddingY + overlayMetrics.bottomInfoHeight);
        if (d2dBottomInfoFormat_ && d2dTextBrush_)
        {
            d2dBottomInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            renderTarget->DrawText(bottomLine.c_str(), static_cast<UINT32>(bottomLine.size()),
                                   d2dBottomInfoFormat_.Get(), bottomInfoRect, d2dTextBrush_.Get());
        }
    }

    void ViewerWindow::QueueTransitionFromCurrent(bool forward, bool slideshowNavigation)
    {
        StopTransition(false);
        pendingTransitionIsSlideshowNavigation_ = slideshowNavigation;
        activeTransitionStyle_ = ui::ShouldUseViewerTransition(slideshowNavigation, manualTransitionEnabled_)
            ? ResolveActiveTransitionStyle()
            : TransitionStyle::Cut;

        pendingTransitionFromImage_.reset();
        pendingTransitionFromDisplayImage_.reset();
        pendingTransitionFromBitmap_.Reset();
        pendingTransitionFromIndex_ = -1;
        pendingTransitionForward_ = forward;

        if (activeTransitionStyle_ == TransitionStyle::Cut || transitionDurationMs_ == 0 || !currentImage_)
        {
            return;
        }

        pendingTransitionFromImage_ = currentImage_;
        pendingTransitionFromIndex_ = DisplayedImageIndex();
        if (d2dCurrentImageIndex_ == pendingTransitionFromIndex_ && d2dCurrentImageBitmap_)
        {
            pendingTransitionFromBitmap_ = d2dCurrentImageBitmap_;
            pendingTransitionFromDisplayImage_ = d2dCurrentDisplayImage_;
        }
    }

    void ViewerWindow::BeginTransitionFromPending()
    {
        if (!hwnd_
            || activeTransitionStyle_ == TransitionStyle::Cut
            || transitionDurationMs_ == 0
            || !pendingTransitionFromImage_
            || !currentImage_
            || !ui::ShouldUseViewerTransition(pendingTransitionIsSlideshowNavigation_, manualTransitionEnabled_)
            || pendingTransitionFromIndex_ == currentIndex_)
        {
            StopTransition();
            return;
        }

        StopTransition(false);

        transitionFromImage_ = pendingTransitionFromImage_;
        transitionFromBitmap_ = pendingTransitionFromBitmap_;
        transitionFromDisplayImage_ = pendingTransitionFromDisplayImage_;
        transitionFromIndex_ = pendingTransitionFromIndex_;
        transitionForward_ = pendingTransitionForward_;

        pendingTransitionFromImage_.reset();
        pendingTransitionFromBitmap_.Reset();
        pendingTransitionFromDisplayImage_.reset();
        pendingTransitionFromIndex_ = -1;

        if (d2dRenderTarget_)
        {
            auto& renderer = render::D2DRenderer::Instance();
            const auto transitionDisplay = ImageForDisplay(transitionFromIndex_, transitionFromImage_);
            if (transitionDisplay && (!transitionFromBitmap_ || transitionFromDisplayImage_.lock() != transitionDisplay))
            {
                transitionFromBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *transitionDisplay);
                transitionFromDisplayImage_ = transitionDisplay;
            }

            const auto currentDisplay = ImageForDisplay(DisplayedImageIndex(), currentImage_);
            if (currentDisplay && (!d2dCurrentImageBitmap_ || d2dCurrentImageIndex_ != currentIndex_
                                  || d2dCurrentDisplayImage_.lock() != currentDisplay))
            {
                const auto uploadStartedAt = std::chrono::steady_clock::now();
                d2dCurrentImageBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *currentDisplay);
                d2dCurrentDisplayImage_ = currentDisplay;
                const double uploadMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - uploadStartedAt).count();
                util::RecordTiming(L"viewer.upload.d2d", uploadMs);
                util::LogInfo(L"ViewerWindow D2D upload ms="
                    + std::to_wstring(uploadMs)
                    + L", size=" + std::to_wstring(currentImage_->SourceWidth())
                    + L"x" + std::to_wstring(currentImage_->SourceHeight())
                    + L", bytes=" + std::to_wstring(currentImage_->ByteCount()));
                d2dCurrentImageIndex_ = d2dCurrentImageBitmap_ ? DisplayedImageIndex() : -1;
            }
        }

        transitionStartedAt_ = std::chrono::steady_clock::now();
        transitionActive_ = true;
        transitionTimerId_ = SetTimer(hwnd_, kTransitionTimerId, kTransitionIntervalMs, nullptr);
        if (!transitionTimerId_)
        {
            StopTransition();
        }
    }

    void ViewerWindow::StopTransition(bool clearPending)
    {
        if (hwnd_ && transitionTimerId_ != 0)
        {
            KillTimer(hwnd_, kTransitionTimerId);
        }

        transitionTimerId_ = 0;
        transitionActive_ = false;
        transitionFromImage_.reset();
        transitionFromDisplayImage_.reset();
        transitionFromBitmap_.Reset();
        transitionFromIndex_ = -1;
        transitionStartedAt_ = std::chrono::steady_clock::time_point{};

        if (clearPending)
        {
            pendingTransitionFromImage_.reset();
            pendingTransitionFromDisplayImage_.reset();
            pendingTransitionFromBitmap_.Reset();
            pendingTransitionFromIndex_ = -1;
            pendingTransitionIsSlideshowNavigation_ = false;
            activeTransitionStyle_ = TransitionStyle::Cut;
        }
    }

    double ViewerWindow::FitScaleForImage(const cache::CachedThumbnail& image, const RECT& clientRect) const
    {
        const int clientWidth = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
        const int clientHeight = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const double imageWidth = static_cast<double>(swapDimensions ? image.SourceHeight() : image.SourceWidth());
        const double imageHeight = static_cast<double>(swapDimensions ? image.SourceWidth() : image.SourceHeight());
        const double widthRatio = static_cast<double>(clientWidth) / std::max(1.0, imageWidth);
        const double heightRatio = static_cast<double>(clientHeight) / std::max(1.0, imageHeight);
        return std::min(widthRatio, heightRatio);
    }

    double ViewerWindow::FitHeightScaleForImage(const cache::CachedThumbnail& image, const RECT& clientRect) const
    {
        const int clientHeight = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const double imageHeight = static_cast<double>(swapDimensions ? image.SourceWidth() : image.SourceHeight());
        return static_cast<double>(clientHeight) / std::max(1.0, imageHeight);
    }

    double ViewerWindow::FitWidthScaleForImage(const cache::CachedThumbnail& image, const RECT& clientRect) const
    {
        const int clientWidth = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const double imageWidth = static_cast<double>(swapDimensions ? image.SourceHeight() : image.SourceWidth());
        return static_cast<double>(clientWidth) / std::max(1.0, imageWidth);
    }

    double ViewerWindow::FitScaleForClient(const RECT& clientRect) const
    {
        if (!currentImage_)
        {
            return 1.0;
        }

        return FitScaleForImage(*currentImage_, clientRect);
    }

    double ViewerWindow::FitHeightScaleForClient(const RECT& clientRect) const
    {
        if (!currentImage_)
        {
            return 1.0;
        }

        return FitHeightScaleForImage(*currentImage_, clientRect);
    }

    double ViewerWindow::FitWidthScaleForClient(const RECT& clientRect) const
    {
        if (!currentImage_)
        {
            return 1.0;
        }

        return FitWidthScaleForImage(*currentImage_, clientRect);
    }

    double ViewerWindow::EffectiveScaleForClient(const RECT& clientRect) const
    {
        if (zoomMode_ == ZoomMode::Fit)
        {
            return FitScaleForClient(clientRect);
        }
        if (zoomMode_ == ZoomMode::FitHeight)
        {
            return FitHeightScaleForClient(clientRect);
        }
        if (zoomMode_ == ZoomMode::FitWidth)
        {
            return FitWidthScaleForClient(clientRect);
        }
        return customZoomScale_;
    }

    void ViewerWindow::DrawImageBitmap(ID2D1RenderTarget* renderTarget,
                                       ID2D1Bitmap* bitmap,
                                       const cache::CachedThumbnail& image,
                                       const RECT& clientRect,
                                       float opacity,
                                       float scaleMultiplier,
                                       float offsetX,
                                       float offsetY) const
    {
        if (!renderTarget || !bitmap || opacity <= 0.0f)
        {
            return;
        }

        const float clientLeft = static_cast<float>(clientRect.left);
        const float clientTop = static_cast<float>(clientRect.top);
        const float clientWidth = static_cast<float>(std::max(1L, clientRect.right - clientRect.left));
        const float clientHeight = static_cast<float>(std::max(1L, clientRect.bottom - clientRect.top));
        const double baseScale = FitScaleForImage(image, clientRect);
        const double scale = std::max(0.01, baseScale * static_cast<double>(scaleMultiplier));
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const int sourceWidth = image.SourceWidth();
        const int sourceHeight = image.SourceHeight();
        const int rotatedWidth = swapDimensions ? sourceHeight : sourceWidth;
        const int rotatedHeight = swapDimensions ? sourceWidth : sourceHeight;
        const float destW = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedWidth) * scale))));
        const float destH = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedHeight) * scale))));
        const float cx = clientLeft + ((clientWidth - destW) / 2.0f) + offsetX;
        const float cy = clientTop + ((clientHeight - destH) / 2.0f) + offsetY;

        if (rotationQuarterTurns_ == 0)
        {
            hyperbrowse::render::DrawBitmapHighQuality(renderTarget,
                bitmap,
                D2D1::RectF(cx, cy, cx + destW, cy + destH),
                opacity);
            return;
        }

        const float centerX = cx + (destW / 2.0f);
        const float centerY = cy + (destH / 2.0f);
        const float unrotW = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceWidth) * scale))));
        const float unrotH = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceHeight) * scale))));
        const float drawX = centerX - (unrotW / 2.0f);
        const float drawY = centerY - (unrotH / 2.0f);

        D2D1_MATRIX_3X2_F previousTransform{};
        renderTarget->GetTransform(&previousTransform);
        renderTarget->SetTransform(previousTransform * D2D1::Matrix3x2F::Rotation(
            static_cast<float>(rotationQuarterTurns_) * 90.0f,
            D2D1::Point2F(centerX, centerY)));
        hyperbrowse::render::DrawBitmapHighQuality(renderTarget,
            bitmap,
            D2D1::RectF(drawX, drawY, drawX + unrotW, drawY + unrotH),
            opacity);
        renderTarget->SetTransform(previousTransform);
    }

    bool ViewerWindow::DrawImageEffect(ID2D1RenderTarget* renderTarget,
                                       ID2D1Image* imageSource,
                                       const cache::CachedThumbnail& image,
                                       const RECT& clientRect,
                                       float opacity,
                                       float scaleMultiplier,
                                       float offsetX,
                                       float offsetY) const
    {
        if (!renderTarget || !imageSource || opacity <= 0.0f)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<ID2D1DeviceContext> deviceContext;
        if (FAILED(renderTarget->QueryInterface(__uuidof(ID2D1DeviceContext),
                                                reinterpret_cast<void**>(deviceContext.GetAddressOf()))))
        {
            return false;
        }

        const float clientLeft = static_cast<float>(clientRect.left);
        const float clientTop = static_cast<float>(clientRect.top);
        const float clientWidth = static_cast<float>(std::max(1L, clientRect.right - clientRect.left));
        const float clientHeight = static_cast<float>(std::max(1L, clientRect.bottom - clientRect.top));
        const double baseScale = FitScaleForImage(image, clientRect);
        const double scale = std::max(0.01, baseScale * static_cast<double>(scaleMultiplier));
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const int sourceWidth = image.SourceWidth();
        const int sourceHeight = image.SourceHeight();
        const int rotatedWidth = swapDimensions ? sourceHeight : sourceWidth;
        const int rotatedHeight = swapDimensions ? sourceWidth : sourceHeight;
        const float destW = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedWidth) * scale))));
        const float destH = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedHeight) * scale))));
        const float cx = clientLeft + ((clientWidth - destW) / 2.0f) + offsetX;
        const float cy = clientTop + ((clientHeight - destH) / 2.0f) + offsetY;

        D2D1_MATRIX_3X2_F previousTransform{};
        deviceContext->GetTransform(&previousTransform);

        Microsoft::WRL::ComPtr<ID2D1Layer> opacityLayer;
        const bool useOpacityLayer = opacity < 0.999f;
        if (useOpacityLayer)
        {
            if (FAILED(deviceContext->CreateLayer(nullptr, opacityLayer.GetAddressOf())) || !opacityLayer)
            {
                return false;
            }

            deviceContext->PushLayer(
                D2D1::LayerParameters1(
                    D2D1::InfiniteRect(),
                    nullptr,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(),
                    opacity,
                    nullptr,
                    D2D1_LAYER_OPTIONS1_NONE),
                opacityLayer.Get());
        }

        if (rotationQuarterTurns_ == 0)
        {
            const float scaleX = destW / static_cast<float>(std::max(1, sourceWidth));
            const float scaleY = destH / static_cast<float>(std::max(1, sourceHeight));
            const D2D1_POINT_2F drawOrigin = D2D1::Point2F(0.0f, 0.0f);
            deviceContext->SetTransform(previousTransform
                * D2D1::Matrix3x2F::Scale(scaleX, scaleY, D2D1::Point2F(0.0f, 0.0f))
                * D2D1::Matrix3x2F::Translation(cx, cy));
            deviceContext->DrawImage(imageSource,
                                     &drawOrigin,
                                     nullptr,
                                     D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                     D2D1_COMPOSITE_MODE_SOURCE_OVER);
            if (useOpacityLayer)
            {
                deviceContext->PopLayer();
            }
            deviceContext->SetTransform(previousTransform);
            return true;
        }

        const float centerX = cx + (destW / 2.0f);
        const float centerY = cy + (destH / 2.0f);
        const float unrotW = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceWidth) * scale))));
        const float unrotH = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceHeight) * scale))));
        const float drawX = centerX - (unrotW / 2.0f);
        const float drawY = centerY - (unrotH / 2.0f);
        const D2D1_POINT_2F drawOrigin = D2D1::Point2F(0.0f, 0.0f);

        deviceContext->SetTransform(previousTransform
            * D2D1::Matrix3x2F::Scale(static_cast<float>(scale), static_cast<float>(scale), D2D1::Point2F(0.0f, 0.0f))
            * D2D1::Matrix3x2F::Translation(drawX, drawY)
            * D2D1::Matrix3x2F::Rotation(static_cast<float>(rotationQuarterTurns_) * 90.0f,
                                         D2D1::Point2F(centerX, centerY)));
        deviceContext->DrawImage(imageSource,
                                 &drawOrigin,
                                 nullptr,
                                 D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                 D2D1_COMPOSITE_MODE_SOURCE_OVER);
        if (useOpacityLayer)
        {
            deviceContext->PopLayer();
        }
        deviceContext->SetTransform(previousTransform);
        return true;
    }

    void ViewerWindow::RequestRepaint() const
    {
        repaintMode_ = RepaintMode::Full;
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        }
    }

    void ViewerWindow::RequestInfoOverlayRepaint()
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE || !currentImage_ || !infoOverlaysVisible_)
        {
            RequestRepaint();
            return;
        }

        RECT invalidRect = InfoOverlayTopPanelRect();
        if (invalidRect.right <= invalidRect.left || invalidRect.bottom <= invalidRect.top)
        {
            RequestRepaint();
            return;
        }

        InflateRect(&invalidRect, 2, 2);
        repaintMode_ = RepaintMode::InfoOverlayOnly;
        RedrawWindow(hwnd_, &invalidRect, nullptr,
                     RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }

    void ViewerWindow::NotifyZoomChanged(int zoomPercent)
    {
        currentZoomPercent_ = zoomPercent;
        if (owner_)
        {
            PostMessageW(owner_, kZoomChangedMessage, reinterpret_cast<WPARAM>(hwnd_), static_cast<LPARAM>(zoomPercent));
        }
    }

    void ViewerWindow::NotifyActivityChanged(bool isActive) const
    {
        if (owner_)
        {
            PostMessageW(owner_, kActivityChangedMessage, reinterpret_cast<WPARAM>(hwnd_), static_cast<LPARAM>(isActive ? 1 : 0));
        }
    }

    void ViewerWindow::NotifyCurrentItemChanged() const
    {
        if (owner_)
        {
            PostMessageW(owner_, kCurrentItemChangedMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
        }
    }
}
