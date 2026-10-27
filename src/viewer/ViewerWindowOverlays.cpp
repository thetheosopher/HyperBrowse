#include "viewer/ViewerWindowInternal.h"

#include "ui/DetailsPanelHistogramPainter.h"

namespace hyperbrowse::viewer
{
    ViewerOverlaySettings ViewerWindow::DefaultOverlaySettings()
    {
        return ViewerOverlayPersistence::LoadRegistry();
    }

    bool ViewerWindow::SetDefaultOverlaySettings(const ViewerOverlaySettings& settings)
    {
        if (ViewerOverlayPersistence::SaveRegistry(settings)) return true;
        util::LogError(L"Unable to save viewer overlay settings.");
        return false;
    }

    const ViewerOverlaySettings& ViewerWindow::OverlaySettings() const noexcept
    {
        return overlaySettings_;
    }

    void ViewerWindow::SetOverlaySettings(const ViewerOverlaySettings& settings)
    {
        overlaySettings_ = NormalizeOverlaySettings(settings);
        infoOverlaysVisible_ = overlaySettings_.masterVisible;
        const bool textChanged = infoOverlayTextSize_ != overlaySettings_.textSize;
        infoOverlayTextSize_ = overlaySettings_.textSize;
        const auto& metadata = overlaySettings_.panes[static_cast<std::size_t>(OverlayPane::FullMetadata)];
        fullMetadataVisible_ = fullScreen_ ? metadata.fullScreenEnabled : metadata.windowedEnabled;
        if (textChanged && d2dRenderTarget_) RebuildD2DTextFormats();
        histogramGeneration_->fetch_add(1, std::memory_order_acq_rel);
        histogramSource_.reset();
        RefreshOverlayContent();
        if (hwnd_) RequestRepaint();
    }

    int ViewerWindow::OverlayImageIndex() const noexcept
    {
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
            return compareTiles_[focusedCompareTile_].itemIndex;
        return DisplayedImageIndex();
    }

    std::shared_ptr<const cache::CachedThumbnail> ViewerWindow::OverlayImage() const
    {
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
            return compareTiles_[focusedCompareTile_].image;
        return currentImage_;
    }

    void ViewerWindow::RefreshOverlayContent()
    {
        const int index = OverlayImageIndex();
        if (fullMetadataVisible_ && infoOverlaysVisible_ && OverlayImage())
        {
            if (currentMetadataIndex_ != index) LoadMetadataAsyncForIndex(index);
        }
        else ClearCurrentMetadata();
        RefreshHistogram();
    }

    void ViewerWindow::RefreshHistogram()
    {
        const auto& settings = overlaySettings_.panes[static_cast<std::size_t>(OverlayPane::Histogram)];
        const bool enabled = infoOverlaysVisible_ && (fullScreen_ ? settings.fullScreenEnabled : settings.windowedEnabled);
        const auto source = enabled ? OverlayImage() : nullptr;
        const int index = OverlayImageIndex();
        const auto identity = index >= 0 && index < static_cast<int>(items_.size())
            ? MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(index)]) : cache::ThumbnailCacheKey{};
        if (!source && !histogramSource_)
        {
            histogramLoading_ = enabled && (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size()
                ? compareTiles_[focusedCompareTile_].loading : loading_);
            histogramError_ = L"Decoded RGB unavailable";
            histogram_ = {};
            return;
        }
        if (source == histogramSource_ && (!source || identity == histogramIdentity_)) return;
        const auto generation = histogramGeneration_->fetch_add(1, std::memory_order_acq_rel) + 1;
        histogramMailbox_->store(nullptr, std::memory_order_release);
        histogramSource_ = source;
        histogramIdentity_ = identity;
        histogram_ = {};
        histogramLoading_ = false;
        histogramError_ = L"Decoded RGB unavailable";
        if (!source || !hwnd_ || index < 0 || index >= static_cast<int>(items_.size())) return;
        histogramLoading_ = true;
        const auto cancelled = [state = asyncState_, token = histogramGeneration_, generation]()
        {
            return state->shutdown.load(std::memory_order_acquire)
                || token->load(std::memory_order_acquire) != generation;
        };
        if (!backgroundExecutor_ || !backgroundExecutor_->Post(
            [source, index, identity, generation, cancelled, mailbox = histogramMailbox_, state = asyncState_]()
        {
            if (cancelled()) return;
            auto result = std::make_shared<HistogramReady>();
            result->generation = generation;
            result->source = source;
            result->index = index;
            result->identity = identity;
            try
            {
                if (!ui::DetailsPanelHistogram::Compute(source->Bitmap(), &result->bins, cancelled))
                    result->error = L"Decoded RGB unavailable";
            }
            catch (...)
            {
                result->error = L"Decoded RGB computation failed";
            }
            if (cancelled()) return;
            if (!result->error.empty()) util::LogError(result->error);
            auto previous = mailbox->load(std::memory_order_acquire);
            do
            {
                if (previous && previous->generation > generation) return;
            } while (!mailbox->compare_exchange_weak(previous, result, std::memory_order_acq_rel));
            const HWND window = state->targetWindow.load(std::memory_order_acquire);
            if (window && !PostMessageW(window, kHistogramReadyMessage, 0, 0))
                util::LogError(L"Unable to notify viewer of histogram completion.");
        }))
        {
            histogramLoading_ = false;
            histogramError_ = L"Decoded RGB unavailable (worker queue full)";
            util::LogError(histogramError_);
        }
    }

    LRESULT ViewerWindow::HandleHistogramReadyMessage(LPARAM)
    {
        const auto result = histogramMailbox_->exchange(nullptr, std::memory_order_acq_rel);
        const int index = OverlayImageIndex();
        if (!result || result->generation != histogramGeneration_->load(std::memory_order_acquire)
            || result->source != OverlayImage() || result->source != histogramSource_
            || index != result->index || index < 0 || index >= static_cast<int>(items_.size())
            || !(result->identity == MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(index)]))) return 0;
        histogram_ = result->bins;
        histogramError_ = result->error;
        histogramLoading_ = false;
        RequestRepaint();
        return 0;
    }

    void ViewerWindow::DrainHistogramResults(HWND)
    {
        histogramGeneration_->fetch_add(1, std::memory_order_acq_rel);
        histogramMailbox_->store(nullptr, std::memory_order_release);
        histogramSource_.reset();
    }

    OverlaySizes ViewerWindow::MeasureOverlaySizes(SIZE viewport, UINT dpi, InfoOverlayTextSize size)
    {
        const float scale = static_cast<float>((std::max)(96U, dpi)) / 96.0f;
        const auto metrics = ViewerOverlayMetricsForDpi(dpi, size);
        const LONG margin = static_cast<LONG>(std::lround(16 * scale));
        const LONG availableWidth = (std::max)(0L, viewport.cx - 2 * margin);
        const LONG availableHeight = (std::max)(0L, viewport.cy - 2 * margin);
        const auto width = [&](float value)
        {
            return (std::min)(availableWidth, static_cast<LONG>(std::lround(value * metrics.overlayWidthScale)));
        };
        const auto height = [](float value) { return static_cast<LONG>(std::ceil(value)); };
        return OverlaySizes{{
            {width(560), height(metrics.topPanelPaddingY * 2 + metrics.topNameHeight + metrics.topInfoHeight)},
            {width(380), height(metrics.bottomPanelPaddingY * 2 + metrics.bottomInfoHeight)},
            {width(420), (std::max)(height(metrics.topNameHeight + metrics.metadataFontSize * 2 + metrics.metadataPanelPadding * 2),
                (std::min)(availableHeight, height(340 * scale + metrics.metadataFontSize * 4)))},
            {width(320), height(170 * scale)},
        }};
    }

    OverlayLayout ViewerWindow::CurrentOverlayLayout() const
    {
        RECT client{};
        if (!hwnd_ || !GetClientRect(hwnd_, &client)) return {};
        const UINT dpi = (std::max)(96U, GetDpiForWindow(hwnd_));
        const SIZE viewport{client.right, client.bottom};
        return CalculateOverlayLayout(viewport, overlaySettings_, fullScreen_,
            MeasureOverlaySizes(viewport, dpi, infoOverlayTextSize_), MulDiv(16, dpi, 96), MulDiv(10, dpi, 96));
    }

    std::wstring ViewerWindow::OverlayPaneText(OverlayPane pane, int width, int height, int zoom) const
    {
        const int index = OverlayImageIndex();
        const auto* item = index >= 0 && index < static_cast<int>(items_.size())
            ? &items_[static_cast<std::size_t>(index)] : nullptr;
        switch (pane)
        {
        case OverlayPane::FileInformation:
        {
            std::wstring text = item ? item->fileName : L"Image";
            if (compareSessionActive_) text = L"Focused image: " + text;
            text += L"\n" + std::to_wstring(index + 1) + L" / " + std::to_wstring(items_.size());
            if (item) text += L"  |  " + item->fileType + L"  |  " + browser::FormatByteSize(item->fileSizeBytes);
            return text;
        }
        case OverlayPane::ImageStatus:
            return std::to_wstring(width) + L" x " + std::to_wstring(height) + L"  |  "
                + std::to_wstring(zoom) + L"%  |  "
                + (zoomMode_ == ZoomMode::Fit ? L"Fit" : zoomMode_ == ZoomMode::FitWidth ? L"Fit Width"
                    : zoomMode_ == ZoomMode::FitHeight ? L"Fit Height" : L"Custom");
        case OverlayPane::FullMetadata:
            return std::wstring(L"Image Metadata\nName: ") + (item ? item->fileName : L"Image") + L"\n"
                + (metadataLoading_ ? L"Loading metadata..."
                : currentMetadataText_.empty() ? L"No metadata is available." : currentMetadataText_.substr(0, 8192));
        case OverlayPane::Histogram:
            return histogramLoading_ ? L"Loading histogram..."
                : histogram_.visible ? std::wstring{} : histogramError_.empty() ? L"Decoded RGB unavailable" : histogramError_;
        default: return {};
        }
    }

    void ViewerWindow::DrawOverlaysGdi(HDC dc) const
    {
        const auto source = OverlayImage();
        const auto layout = CurrentOverlayLayout();
        const auto metrics = ViewerOverlayMetricsForWindow(hwnd_, infoOverlayTextSize_);
        LOGFONTW font{};
        font.lfHeight = -static_cast<LONG>(std::lround(metrics.infoFontSize));
        font.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(font.lfFaceName, L"Segoe UI");
        HFONT textFont = CreateFontIndirectW(&font);
        const auto oldFont = SelectObject(dc, textFont ? textFont : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, TextColor(darkTheme_));
        SetBkMode(dc, TRANSPARENT);
        const bool rotated = rotationQuarterTurns_ % 2 != 0;
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int zoom = !source ? 0 : compareSessionActive_ ? currentZoomPercent_
            : (std::max)(1, static_cast<int>(std::lround(EffectiveScaleForClient(client) * 100)));
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            if (layout[i].visibility != OverlayVisibility::Shown) continue;
            const auto pane = static_cast<OverlayPane>(i);
            RECT rect = layout[i].rect;
            HBRUSH fill = CreateSolidBrush(PanelFillColor(darkTheme_));
            FillRect(dc, &rect, fill);
            DeleteObject(fill);
            FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            const auto textRect = ViewerOverlayTextRect(rect, metrics, pane);
            rect = {static_cast<LONG>(std::ceil(textRect.left)), static_cast<LONG>(std::ceil(textRect.top)),
                static_cast<LONG>(std::floor(textRect.right)), static_cast<LONG>(std::floor(textRect.bottom))};
            const auto text = OverlayPaneText(pane, source ? (rotated ? source->SourceHeight() : source->SourceWidth()) : 0,
                source ? (rotated ? source->SourceWidth() : source->SourceHeight()) : 0, zoom);
            if (pane == OverlayPane::ImageStatus || pane == OverlayPane::Histogram)
            {
                DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            else
            {
                const auto split = text.find(L'\n');
                RECT titleRect = rect;
                titleRect.bottom = (std::min)(titleRect.bottom,
                    static_cast<LONG>(std::floor(textRect.top + metrics.topNameHeight)));
                const auto title = std::wstring_view(text).substr(0, split);
                DrawTextW(dc, title.data(), static_cast<int>(title.size()), &titleRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (split != std::wstring::npos)
                {
                    RECT bodyRect = rect;
                    bodyRect.top = titleRect.bottom;
                    const auto body = std::wstring_view(text).substr(split + 1);
                    DrawTextW(dc, body.data(), static_cast<int>(body.size()), &bodyRect,
                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS
                            | (pane == OverlayPane::FullMetadata ? DT_WORDBREAK : DT_SINGLELINE));
                }
            }
            if (pane == OverlayPane::Histogram && histogram_.visible)
            {
                ui::DetailsPanelHistogramPainter::State state;
                state.rect = rect;
                state.red = histogram_.red; state.green = histogram_.green; state.blue = histogram_.blue;
                state.peak = histogram_.peak; state.visible = true;
                ui::DetailsPanelHistogramPainter::PaintGdi(dc, state,
                    {PanelFillColor(darkTheme_), PanelBorderColor(darkTheme_), MutedTextColor(darkTheme_)}, textFont);
            }
        }
        SelectObject(dc, oldFont);
        if (textFont) DeleteObject(textFont);
    }
}
