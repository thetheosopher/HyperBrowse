#include "viewer/ViewerWindowInternal.h"
#include "ui/DetailsPanelHistogramPainter.h"

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

        if (d2dNameFormat_)
        {
            d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dNameFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        if (d2dInfoFormat_)
        {
            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (d2dBottomInfoFormat_)
        {
            d2dBottomInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dBottomInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dBottomInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (d2dMetadataFormat_)
        {
            d2dMetadataFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dMetadataFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dMetadataFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }

        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        for (auto* format : {d2dNameFormat_.Get(), d2dInfoFormat_.Get(),
                             d2dBottomInfoFormat_.Get(), d2dMetadataFormat_.Get()})
        {
            if (!format) continue;
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
            HRESULT result = renderer.DWriteFactory()->CreateEllipsisTrimmingSign(format, ellipsis.GetAddressOf());
            if (SUCCEEDED(result)) result = format->SetTrimming(&trimming, ellipsis.Get());
            if (FAILED(result))
                util::LogError(L"Unable to initialize viewer overlay text trimming: " + std::to_wstring(result));
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
        const auto layout = CurrentOverlayLayout();
        const auto& placement = layout[static_cast<std::size_t>(OverlayPane::FileInformation)];
        return placement.visibility == OverlayVisibility::Shown ? placement.rect : RECT{};
    }

    void ViewerWindow::DrawInfoOverlays(ID2D1RenderTarget* renderTarget,
                                        float,
                                        float,
                                        int rotatedWidth,
                                        int rotatedHeight,
                                        int zoomPercent,
                                        const cache::CachedThumbnail*,
                                        CompareDirection,
                                        bool topOnly) const
    {
        if (!renderTarget || !infoOverlaysVisible_)
        {
            return;
        }

        const auto layout = CurrentOverlayLayout();
        const auto metrics = ViewerOverlayMetricsForWindow(hwnd_, infoOverlayTextSize_);
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            const auto pane = static_cast<OverlayPane>(i);
            if (layout[i].visibility != OverlayVisibility::Shown
                || (topOnly && pane != OverlayPane::FileInformation)) continue;
            const auto panel = render::ToD2DRect(layout[i].rect);
            const auto rounded = D2D1::RoundedRect(panel, 8, 8);
            if (d2dPanelFillBrush_) renderTarget->FillRoundedRectangle(rounded, d2dPanelFillBrush_.Get());
            if (d2dPanelBorderBrush_) renderTarget->DrawRoundedRectangle(rounded, d2dPanelBorderBrush_.Get());
            const auto textRect = ViewerOverlayTextRect(layout[i].rect, metrics, pane);
            auto text = OverlayPaneText(pane, rotatedWidth, rotatedHeight, zoomPercent);
            const auto drawText = [&](std::wstring_view value, IDWriteTextFormat* format, const D2D1_RECT_F& bounds)
            {
                if (!format || !d2dTextBrush_) return;
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                renderTarget->DrawText(value.data(), static_cast<UINT32>(value.size()), format, bounds,
                    d2dTextBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            };
            if (pane == OverlayPane::FileInformation || pane == OverlayPane::FullMetadata)
            {
                const auto split = text.find(L'\n');
                auto titleRect = textRect;
                titleRect.bottom = (std::min)(titleRect.bottom, titleRect.top + metrics.topNameHeight);
                drawText(std::wstring_view(text).substr(0, split), d2dNameFormat_.Get(), titleRect);
                if (split != std::wstring::npos)
                {
                    auto bodyRect = textRect;
                    bodyRect.top = titleRect.bottom;
                    drawText(std::wstring_view(text).substr(split + 1),
                        pane == OverlayPane::FullMetadata ? d2dMetadataFormat_.Get() : d2dInfoFormat_.Get(), bodyRect);
                }
            }
            else drawText(text, pane == OverlayPane::Histogram ? d2dInfoFormat_.Get() : d2dBottomInfoFormat_.Get(), textRect);
            if (pane == OverlayPane::Histogram && histogram_.visible)
            {
                ui::DetailsPanelHistogramPainter::State state;
                state.rect = {static_cast<LONG>(textRect.left), static_cast<LONG>(textRect.top),
                    static_cast<LONG>(textRect.right), static_cast<LONG>(textRect.bottom)};
                state.red = histogram_.red; state.green = histogram_.green; state.blue = histogram_.blue;
                state.peak = histogram_.peak; state.visible = true;
                ui::DetailsPanelHistogramPainter::PaintD2D(renderTarget, state,
                    {PanelFillColor(darkTheme_), PanelBorderColor(darkTheme_), MutedTextColor(darkTheme_)},
                    d2dPanelBorderBrush_.Get(), d2dMutedTextBrush_.Get(), d2dInfoFormat_.Get());
            }
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
