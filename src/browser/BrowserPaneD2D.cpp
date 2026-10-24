#include "browser/BrowserPaneInternal.h"

namespace
{

    D2D1_RECT_F InsetD2DRect(const D2D1_RECT_F& rect, float insetX, float insetY)
    {
        return D2D1::RectF(rect.left + insetX,
                           rect.top + insetY,
                           rect.right - insetX,
                           rect.bottom - insetY);
    }
}

namespace hyperbrowse::browser
{

    void BrowserPane::RecoverDisplaySurface()
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

    void BrowserPane::EnsureD2DRenderTarget()
    {
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
            RebuildD2DBrushes();
            RebuildD2DTextFormats();

            if (placeholderArt_)
            {
                d2dPlaceholderArtBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *placeholderArt_);
            }
        }
    }

    void BrowserPane::ReleaseD2DResources()
    {
        d2dBitmapCache_.clear();
        d2dPlaceholderArtBitmap_.Reset();
        d2dTitleFormat_.Reset();
        d2dFolderTitleFormat_.Reset();
        d2dMetaFormat_.Reset();
        d2dStatusFormat_.Reset();
        d2dPlaceholderTitleFormat_.Reset();
        d2dPlaceholderBodyFormat_.Reset();
        d2dPerformanceHudTextFormat_.Reset();
        d2dBackgroundBrush_.Reset();
        d2dSurfaceBrush_.Reset();
        d2dPreviewBrush_.Reset();
        d2dSelectedCellBrush_.Reset();
        d2dSelectedPreviewBrush_.Reset();
        d2dPlaceholderBrush_.Reset();
        d2dPerformanceHudBackgroundBrush_.Reset();
        d2dBorderBrush_.Reset();
        d2dSelectedBorderBrush_.Reset();
        d2dRubberBandBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dSelectionTextBrush_.Reset();
        d2dRenderTarget_.Reset();
    }

    void BrowserPane::RebuildD2DBrushes()
    {
        if (!d2dRenderTarget_)
        {
            return;
        }

        d2dBackgroundBrush_.Reset();
        d2dSurfaceBrush_.Reset();
        d2dPreviewBrush_.Reset();
        d2dSelectedCellBrush_.Reset();
        d2dSelectedPreviewBrush_.Reset();
        d2dPlaceholderBrush_.Reset();
        d2dPerformanceHudBackgroundBrush_.Reset();
        d2dBorderBrush_.Reset();
        d2dSelectedBorderBrush_.Reset();
        d2dRubberBandBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dSelectionTextBrush_.Reset();

        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.windowBackground), d2dBackgroundBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.surfaceBackground), d2dSurfaceBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.previewBackground), d2dPreviewBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.accentFill), d2dSelectedCellBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.selectedPreviewBackground), d2dSelectedPreviewBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.placeholderBackground), d2dPlaceholderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.windowBackground, 0.88f), d2dPerformanceHudBackgroundBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.border), d2dBorderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.accent), d2dSelectedBorderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.rubberBand), d2dRubberBandBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.text), d2dTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.mutedText), d2dMutedTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.selectionText), d2dSelectionTextBrush_.GetAddressOf());
    }

    void BrowserPane::RebuildD2DTextFormats()
    {
        auto& renderer = render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        d2dTitleFormat_ = renderer.CreateTextFormatFromFont(thumbnailTitleFont_);
        d2dFolderTitleFormat_ = renderer.CreateTextFormatFromFont(folderTitleFont_);
        d2dMetaFormat_ = renderer.CreateTextFormatFromFont(thumbnailMetaFont_);
        d2dStatusFormat_ = renderer.CreateTextFormatFromFont(thumbnailStatusFont_);
        d2dPlaceholderTitleFormat_ = renderer.CreateTextFormatFromFont(placeholderTitleFont_);
        d2dPlaceholderBodyFormat_ = renderer.CreateTextFormatFromFont(placeholderBodyFont_);
        d2dPerformanceHudTextFormat_ = renderer.CreateTextFormatFromFont(thumbnailMetaFont_);

        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};

        if (d2dTitleFormat_)
        {
            d2dTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dTitleFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dFolderTitleFormat_)
        {
            d2dFolderTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dFolderTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dFolderTitleFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dMetaFormat_)
        {
            d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dMetaFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dMetaFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dStatusFormat_)
        {
            d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dStatusFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dStatusFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dPlaceholderTitleFormat_)
        {
            d2dPlaceholderTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dPlaceholderTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dPlaceholderTitleFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dPlaceholderBodyFormat_)
        {
            d2dPlaceholderBodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dPlaceholderBodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dPlaceholderBodyFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dPerformanceHudTextFormat_)
        {
            d2dPerformanceHudTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dPerformanceHudTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dPerformanceHudTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            d2dPerformanceHudTextFormat_->SetTrimming(&trimming, nullptr);
        }
    }

    ID2D1Bitmap* BrowserPane::GetOrCreateD2DBitmap(ID2D1RenderTarget* rt, std::shared_ptr<const cache::CachedThumbnail> thumbnail) const
    {
        if (!thumbnail) return nullptr;
        const HBITMAP key = thumbnail->Bitmap();
        auto it = d2dBitmapCache_.find(key);
        if (it != d2dBitmapCache_.end())
        {
            if (it->second.pixels.lock() == thumbnail) return it->second.bitmap.Get();
            d2dBitmapCache_.erase(it);
        }

        auto bitmap = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(rt, *thumbnail);
        if (!bitmap)
        {
            return nullptr;
        }

        if (d2dBitmapCache_.size() >= 128) d2dBitmapCache_.clear();
        auto [insertIt, inserted] = d2dBitmapCache_.emplace(key, BitmapEntry{std::move(bitmap), std::move(thumbnail)});
        return insertIt->second.bitmap.Get();
    }

    void BrowserPane::D2DDrawPlaceholderState(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const
    {
        const bool hasFolder = model_ && !model_->FolderPath().empty();
        const bool hasError = hasFolder && model_->HasError();
        const bool loadingFolder = hasFolder && model_->IsEnumerating() && orderedModelIndices_.empty();
        const bool noMatches = hasFolder && !filterQuery_.empty() && orderedModelIndices_.empty();
        const bool emptyFolder = hasFolder && !hasError && !loadingFolder && !noMatches && orderedModelIndices_.empty();
        const bool showIcon = !hasError && (!hasFolder || loadingFolder || emptyFolder);
        const bool stackedEmptyState = !hasFolder && d2dPlaceholderArtBitmap_;

        std::wstring title = L"HyperBrowse";
        if (hasFolder)
        {
            if (hasError) title = L"Folder Load Failed";
            else if (loadingFolder) title = L"Loading Images";
            else if (noMatches) title = L"No Matches";
            else if (emptyFolder) title = L"Empty Folder";
        }

        const float clientWidth = size.width;
        const float clientHeight = size.height;
        const float kPanelPaddingLeft = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(12, appTextSize_));
        const float kPanelPaddingRight = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(14, appTextSize_));
        const float kPanelPaddingVertical = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(9, appTextSize_));
        const float kIconTextGap = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(15, appTextSize_));
        const float kDesiredTextBlockWidth = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(180, appTextSize_));
        const float kMinimumTextBlockWidth = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(135, appTextSize_));
        const float kTitleHeight = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(26, appTextSize_));
        const float kBodyHeight = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(32, appTextSize_));
        const float kTitleBodyGap = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(5, appTextSize_));

        const float maxPanelWidth = std::max(180.0f, clientWidth - 40.0f);
        const float maxPanelHeight = std::max(72.0f, clientHeight - 32.0f);

        float renderedIconSize = 0.0f;
        float panelWidth = std::max(180.0f, std::min(330.0f, clientWidth - 48.0f));
        float panelHeight = showIcon
            ? std::min(114.0f, std::max(90.0f, clientHeight - 48.0f))
            : std::min(96.0f, std::max(72.0f, clientHeight - 48.0f));

        if (showIcon && d2dPlaceholderArtBitmap_)
        {
            const float artWidth = static_cast<float>(d2dPlaceholderArtBitmap_->GetSize().width);
            const float iconTextWidth = stackedEmptyState ? 0.0f : kIconTextGap + kMinimumTextBlockWidth;
            const float maxIconWidth = std::max(32.0f, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - iconTextWidth);
            const float maxIconHeight = stackedEmptyState
                ? std::max(12.0f,
                           maxPanelHeight - (kPanelPaddingVertical * 2.0f) - kIconTextGap
                               - kTitleHeight - kTitleBodyGap - kBodyHeight)
                : std::max(54.0f, maxPanelHeight - (kPanelPaddingVertical * 2.0f));
            renderedIconSize = std::min({static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(kPlaceholderIconDisplaySize, appTextSize_)), artWidth, maxIconWidth, maxIconHeight});

            if (stackedEmptyState)
            {
                const float textBlockWidth = std::max(kMinimumTextBlockWidth,
                    std::min(320.0f, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight));
                panelWidth = std::min(maxPanelWidth,
                    kPanelPaddingLeft + textBlockWidth + kPanelPaddingRight);
                panelHeight = std::min(maxPanelHeight,
                    (kPanelPaddingVertical * 2.0f) + renderedIconSize + kIconTextGap
                        + kTitleHeight + kTitleBodyGap + kBodyHeight);
            }
            else
            {
                const float textBlockWidth = std::max(kMinimumTextBlockWidth,
                    std::min(kDesiredTextBlockWidth,
                             maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - renderedIconSize));
                panelWidth = std::min(maxPanelWidth,
                    kPanelPaddingLeft + renderedIconSize + kIconTextGap + textBlockWidth + kPanelPaddingRight);
                panelHeight = std::min(maxPanelHeight, (kPanelPaddingVertical * 2.0f) + renderedIconSize);
            }
        }

        const float panelLeft = (clientWidth - panelWidth) / 2.0f;
        const float panelTop = (clientHeight - panelHeight) / 2.0f;
        const D2D1_RECT_F panelRect = D2D1::RectF(panelLeft, panelTop, panelLeft + panelWidth, panelTop + panelHeight);
        const float panelCornerRadius = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(7, appTextSize_));
        const D2D1_ROUNDED_RECT roundedPanel = D2D1::RoundedRect(panelRect, panelCornerRadius, panelCornerRadius);

        if (d2dPlaceholderBrush_) rt->FillRoundedRectangle(roundedPanel, d2dPlaceholderBrush_.Get());
        if (d2dBorderBrush_) rt->DrawRoundedRectangle(roundedPanel, d2dBorderBrush_.Get(), 1.0f);

        D2D1_RECT_F titleRect{};
        D2D1_RECT_F bodyRect{};
        if (showIcon && d2dPlaceholderArtBitmap_)
        {
            const float iconX = stackedEmptyState
                ? panelLeft + (panelWidth - renderedIconSize) / 2.0f
                : panelLeft + kPanelPaddingLeft;
            const float iconY = stackedEmptyState
                ? panelTop + kPanelPaddingVertical
                : panelTop + (panelHeight - renderedIconSize) / 2.0f;
            rt->DrawBitmap(d2dPlaceholderArtBitmap_.Get(),
                           D2D1::RectF(iconX, iconY, iconX + renderedIconSize, iconY + renderedIconSize),
                           stackedEmptyState ? 0.62f : 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

            if (stackedEmptyState)
            {
                const float contentLeft = panelLeft + kPanelPaddingLeft;
                const float contentRight = panelLeft + panelWidth - kPanelPaddingRight;
                const float contentTop = iconY + renderedIconSize + kIconTextGap;
                titleRect = D2D1::RectF(contentLeft, contentTop, contentRight, contentTop + kTitleHeight);
                bodyRect = D2D1::RectF(contentLeft,
                                       titleRect.bottom + kTitleBodyGap,
                                       contentRight,
                                       titleRect.bottom + kTitleBodyGap + kBodyHeight);
            }
            else
            {
                const float contentLeft = iconX + renderedIconSize + kIconTextGap;
                const float contentRight = panelLeft + panelWidth - kPanelPaddingRight;
                const float textBlockHeight = kTitleHeight + kTitleBodyGap + kBodyHeight;
                const float contentTop = panelTop + std::max(kPanelPaddingVertical, (panelHeight - textBlockHeight) / 2.0f);
                titleRect = D2D1::RectF(contentLeft, contentTop, contentRight, contentTop + kTitleHeight);
                bodyRect = D2D1::RectF(contentLeft, titleRect.bottom + kTitleBodyGap, contentRight, titleRect.bottom + kTitleBodyGap + kBodyHeight);
            }
        }
        else
        {
            titleRect = D2D1::RectF(panelLeft + 20, panelTop + 20, panelLeft + panelWidth - 20, panelTop + 54);
            bodyRect = D2D1::RectF(panelLeft + 24, titleRect.bottom + 8, panelLeft + panelWidth - 24, panelTop + panelHeight - 22);
        }

        auto* titleFormat = d2dPlaceholderTitleFormat_.Get();
        if (titleFormat && d2dTextBrush_)
        {
            titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawText(title.c_str(), static_cast<UINT32>(title.size()), titleFormat, titleRect, d2dTextBrush_.Get());
        }

        const std::wstring text = BuildPlaceholderText();
        if (!text.empty() && d2dPlaceholderBodyFormat_ && d2dMutedTextBrush_)
        {
            auto* fmt = d2dPlaceholderBodyFormat_.Get();
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt, bodyRect, d2dMutedTextBrush_.Get());
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    void BrowserPane::D2DDrawPerformanceHud(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const
    {
        if (!rt || performanceHudText_.empty() || !d2dPerformanceHudBackgroundBrush_
            || !d2dPerformanceHudTextFormat_ || !d2dBorderBrush_ || !d2dTextBrush_)
        {
            return;
        }

        const float padding = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(10, appTextSize_));
        const float rowHeight = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(17, appTextSize_));
        const float panelWidth = std::min(static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(280, appTextSize_)),
                                          std::max(0.0f, size.width - (padding * 2.0f)));
        if (panelWidth < 120.0f)
        {
            return;
        }

        const float panelHeight = (padding * 2.0f) + (rowHeight * 5.0f);
        const float panelRight = size.width - padding;
        const float panelTop = padding;
        const D2D1_RECT_F panelRect = D2D1::RectF(
            panelRight - panelWidth,
            panelTop,
            panelRight,
            std::min(size.height - padding, panelTop + panelHeight));
        if (panelRect.bottom <= panelRect.top)
        {
            return;
        }

        const float cornerRadius = static_cast<float>(hyperbrowse::util::ScaleAppTextDimension(4, appTextSize_));
        const D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(panelRect, cornerRadius, cornerRadius);
        rt->FillRoundedRectangle(panel, d2dPerformanceHudBackgroundBrush_.Get());
        rt->DrawRoundedRectangle(panel, d2dBorderBrush_.Get(), 1.0f);

        const D2D1_RECT_F textRect = D2D1::RectF(
            panelRect.left + padding,
            panelRect.top + padding,
            panelRect.right - padding,
            panelRect.bottom - padding);
        rt->DrawText(performanceHudText_.c_str(),
                     static_cast<UINT32>(performanceHudText_.size()),
                     d2dPerformanceHudTextFormat_.Get(),
                     textRect,
                     d2dTextBrush_.Get(),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void BrowserPane::D2DDrawThumbnailCells(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const
    {
        if (d2dBackgroundBrush_)
        {
            rt->Clear(render::ToD2DColor(colors_.windowBackground));
        }

        if (orderedModelIndices_.empty())
        {
            D2DDrawPlaceholderState(rt, size);
            if (!performanceHudText_.empty())
            {
                D2DDrawPerformanceHud(rt, size);
            }
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientWidth = static_cast<int>(size.width);
        const int clientHeight = static_cast<int>(size.height);
        const int columns = ColumnsForClientWidth(clientWidth);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastRow = std::max(firstRow, (scrollOffsetY_ + clientHeight) / verticalStride + 1);
        const int firstIndex = firstRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (lastRow + 1) * columns);
        const float footerGap = thumbnailDetailsVisible_ ? static_cast<float>(std::max(6, layout.badgeGap - 1)) : 0.0f;
        float ratingStripWidth = 0.0f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dRatingBrush;
        if (thumbnailDetailsVisible_)
        {
            ratingStripWidth = render::D2DRenderer::Instance().MeasureTextWidth(
                kThumbnailRatingStars,
                d2dMetaFormat_.Get(),
                512.0f,
                128.0f);
            if (ratingStripWidth <= 0.0f)
            {
                ratingStripWidth = static_cast<float>(layout.infoHeight * 3);
            }
            ratingStripWidth += 2.0f;
            rt->CreateSolidColorBrush(render::ToD2DColor(kThumbnailRatingGold), d2dRatingBrush.GetAddressOf());
        }

        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item) continue;

            const RECT cellRectGdi = GetThumbnailCellRect(viewIndex);
            if (cellRectGdi.bottom < 0 || cellRectGdi.top > clientHeight) continue;

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const bool selected = selectedModelIndices_.contains(modelIndex);

            const D2D1_RECT_F cellRect = render::ToD2DRect(cellRectGdi);
            const D2D1_RECT_F cellStrokeRect = InsetD2DRect(cellRect, 0.5f, 0.5f);
            const float cornerRadius = static_cast<float>(layout.cellCornerRadius);
            const D2D1_ROUNDED_RECT roundedCell = D2D1::RoundedRect(cellRect, cornerRadius, cornerRadius);
            const D2D1_ROUNDED_RECT roundedCellStroke = D2D1::RoundedRect(
                cellStrokeRect,
                std::max(0.0f, cornerRadius - 0.5f),
                std::max(0.0f, cornerRadius - 0.5f));

            ID2D1SolidColorBrush* cellBrush = selected
                ? (d2dSelectedCellBrush_ ? d2dSelectedCellBrush_.Get() : d2dSurfaceBrush_.Get())
                : d2dSurfaceBrush_.Get();
            ID2D1SolidColorBrush* borderBrush = selected
                ? d2dSelectedBorderBrush_.Get()
                : d2dBorderBrush_.Get();

            if (cellBrush) rt->FillRoundedRectangle(roundedCell, cellBrush);
            if (borderBrush) rt->DrawRoundedRectangle(roundedCellStroke, borderBrush, 1.0f);

            const RECT previewRectGdi = GetThumbnailPreviewRect(cellRectGdi);
            const D2D1_RECT_F previewRect = render::ToD2DRect(previewRectGdi);
            const float previewCorner = static_cast<float>(layout.previewCornerRadius);
            const D2D1_ROUNDED_RECT roundedPreview = D2D1::RoundedRect(previewRect, previewCorner, previewCorner);
            const bool stackedPair = PairedCompanionModelIndex(modelIndex) >= 0;

            ID2D1SolidColorBrush* previewBrush = selected
                ? (d2dSelectedPreviewBrush_ ? d2dSelectedPreviewBrush_.Get() : d2dPreviewBrush_.Get())
                : (d2dPreviewBrush_ ? d2dPreviewBrush_.Get() : d2dBackgroundBrush_.Get());
            if (stackedPair)
            {
                const D2D1_RECT_F stackedPreviewRect = D2D1::RectF(
                    previewRect.left - 5.0f,
                    previewRect.top - 5.0f,
                    previewRect.right - 5.0f,
                    previewRect.bottom - 5.0f);
                const D2D1_ROUNDED_RECT stackedRoundedPreview = D2D1::RoundedRect(
                    stackedPreviewRect,
                    previewCorner,
                    previewCorner);
                if (previewBrush) rt->FillRoundedRectangle(stackedRoundedPreview, previewBrush);
                if (borderBrush) rt->DrawRoundedRectangle(stackedRoundedPreview, borderBrush, 1.0f);
            }
            if (previewBrush) rt->FillRoundedRectangle(roundedPreview, previewBrush);

            D2DDrawPreviewThumbnail(rt, previewRect, *item, selected);

            if (thumbnailDetailsVisible_ && !item->isDirectory)
            {
                ID2D1SolidColorBrush* textBrush = selected ? d2dSelectionTextBrush_.Get() : d2dTextBrush_.Get();
                ID2D1SolidColorBrush* mutedBrush = selected ? d2dSelectionTextBrush_.Get() : d2dMutedTextBrush_.Get();

                const float titleTop = previewRect.bottom + static_cast<float>(layout.titleTopGap);
                D2D1_RECT_F nameRect = D2D1::RectF(
                    cellRect.left + layout.textInset,
                    titleTop,
                    cellRect.right - layout.textInset,
                    titleTop + layout.titleHeight);

                if (d2dTitleFormat_ && textBrush)
                {
                    d2dTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    d2dTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    const std::wstring displayTitle = BuildThumbnailDisplayTitle(*item);
                    rt->DrawText(displayTitle.c_str(), static_cast<UINT32>(displayTitle.size()),
                                 d2dTitleFormat_.Get(), nameRect, textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }

                const float infoTop = cellRect.bottom - layout.infoBottomInset - layout.infoHeight;
                D2D1_RECT_F infoRect = D2D1::RectF(
                    cellRect.left + layout.textInset, infoTop,
                    cellRect.right - layout.textInset, cellRect.bottom - layout.infoBottomInset);
                const float infoWidth = std::max(0.0f, infoRect.right - infoRect.left);
                const float centeredRatingWidth = std::min(infoWidth, ratingStripWidth);
                const float ratingLeft = infoRect.left + std::max(0.0f, (infoWidth - centeredRatingWidth) / 2.0f);
                const float ratingRight = ratingLeft + centeredRatingWidth;
                const D2D1_RECT_F dimensionsRect = D2D1::RectF(
                    infoRect.left,
                    infoRect.top,
                    std::max(infoRect.left, ratingLeft - footerGap),
                    infoRect.bottom);
                const D2D1_RECT_F ratingRect = D2D1::RectF(
                    ratingLeft,
                    infoRect.top,
                    ratingRight,
                    infoRect.bottom);
                const D2D1_RECT_F typeRect = D2D1::RectF(
                    std::min(infoRect.right, ratingRight + footerGap),
                    infoRect.top,
                    infoRect.right,
                    infoRect.bottom);

                const std::wstring typeLabel = BuildDisplayFileTypeLabel(*item, modelIndex);
                const std::wstring dimensionLabel = item->isDirectory ? std::wstring{} : FormatDimensionsForItem(*item);
                const int rating = item->isDirectory ? 0 : ThumbnailRatingForItem(userMetadataStore_, *item);
                ID2D1SolidColorBrush* ratingBaseBrush = d2dMutedTextBrush_ ? d2dMutedTextBrush_.Get() : mutedBrush;

                if (d2dMetaFormat_)
                {
                    d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    if (mutedBrush && !dimensionLabel.empty())
                    {
                        rt->DrawText(dimensionLabel.c_str(), static_cast<UINT32>(dimensionLabel.size()),
                                     d2dMetaFormat_.Get(), dimensionsRect, mutedBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }

                    d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    if (ratingBaseBrush)
                    {
                        rt->DrawText(kThumbnailRatingStars.data(),
                                     static_cast<UINT32>(kThumbnailRatingStars.size()),
                                     d2dMetaFormat_.Get(), ratingRect, ratingBaseBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    if (rating > 0 && d2dRatingBrush)
                    {
                        rt->DrawText(kThumbnailRatingStars.data(),
                                     static_cast<UINT32>(rating),
                                     d2dMetaFormat_.Get(), ratingRect, d2dRatingBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }

                    if (textBrush && !typeLabel.empty())
                    {
                        d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                        rt->DrawText(typeLabel.c_str(), static_cast<UINT32>(typeLabel.size()),
                                     d2dMetaFormat_.Get(), typeRect, textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    }
                }
            }
        }

        if (rubberBandActive_ && d2dRubberBandBrush_)
        {
            D2D1_RECT_F bandRect = render::ToD2DRect(rubberBandRect_);
            rt->DrawRectangle(bandRect, d2dRubberBandBrush_.Get(), 1.0f);
        }

        if (!performanceHudText_.empty())
        {
            D2DDrawPerformanceHud(rt, size);
        }
    }

    void BrowserPane::D2DDrawPreviewThumbnail(ID2D1RenderTarget* rt, const D2D1_RECT_F& previewRect, const BrowserItem& item, bool selected) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        if (item.isDirectory)
        {
            const float previewHeight = previewRect.bottom - previewRect.top;
            const float labelHeight = static_cast<float>(std::max(24, layout.titleHeight + 8));
            const float iconSize = std::min({
                folderArt_ ? static_cast<float>(folderArt_->Width()) : 0.0f,
                std::max(0.0f, previewRect.right - previewRect.left - (layout.textInset * 2.0f)),
                previewHeight * 0.60f,
            });
            const float contentHeight = iconSize > 0.0f ? iconSize + 6.0f + labelHeight : labelHeight;
            const float contentTop = previewRect.top + std::max(4.0f, (previewHeight - contentHeight) / 2.0f);
            float labelTop = contentTop;

            if (folderArt_ && iconSize > 0.0f)
            {
                if (ID2D1Bitmap* iconBitmap = GetOrCreateD2DBitmap(rt, folderArt_))
                {
                    const float iconX = previewRect.left + ((previewRect.right - previewRect.left - iconSize) / 2.0f);
                    rt->DrawBitmap(iconBitmap,
                                   D2D1::RectF(iconX, contentTop, iconX + iconSize, contentTop + iconSize),
                                   1.0f,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    labelTop += iconSize + 6.0f;
                }
            }

            ID2D1SolidColorBrush* titleBrush = selected ? d2dSelectionTextBrush_.Get() : d2dTextBrush_.Get();
            if (d2dFolderTitleFormat_ && titleBrush)
            {
                const D2D1_RECT_F labelRect = D2D1::RectF(
                    previewRect.left + layout.textInset,
                    labelTop,
                    previewRect.right - layout.textInset,
                    labelTop + labelHeight);
                const std::wstring displayTitle = BuildThumbnailDisplayTitle(item);
                rt->DrawText(displayTitle.c_str(),
                             static_cast<UINT32>(displayTitle.size()),
                             d2dFolderTitleFormat_.Get(),
                             labelRect,
                             titleBrush,
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            return;
        }

        const int targetWidth = layout.previewWidth;
        const int targetHeight = layout.previewHeight;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);
        const auto thumbnail = thumbnailScheduler_ ? thumbnailScheduler_->FindCachedThumbnail(cacheKey) : nullptr;

        ID2D1SolidColorBrush* statusBrush = selected ? d2dSelectionTextBrush_.Get() : d2dMutedTextBrush_.Get();

        if (!thumbnailScheduler_ || !decode::CanDecodeThumbnail(item))
        {
            D2D1_RECT_F iconTextRect = D2D1::RectF(
                previewRect.left + layout.textInset,
                previewRect.top + std::max(10.0f, static_cast<float>(layout.previewInset) - 2),
                previewRect.right - layout.textInset,
                previewRect.top + std::max(34.0f, static_cast<float>(layout.previewInset + layout.metaHeight)));
            const std::wstring placeholder = decode::IsRawFileType(item.fileType) ? item.fileType : std::wstring(L"IMAGE");
            if (d2dStatusFormat_ && statusBrush)
            {
                d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                rt->DrawText(placeholder.c_str(), static_cast<UINT32>(placeholder.size()),
                             d2dStatusFormat_.Get(), iconTextRect, statusBrush);
            }
            return;
        }

        if (!thumbnail)
        {
            const decode::ThumbnailDecodeFailureKind knownFailureKind = thumbnailScheduler_->KnownFailureKind(cacheKey);
            if (knownFailureKind != decode::ThumbnailDecodeFailureKind::None)
            {
                D2DDrawUnavailableThumbnailState(rt, previewRect, knownFailureKind, selected);
                return;
            }

            const float previewHeight = previewRect.bottom - previewRect.top;
            const float iconSize = static_cast<float>(layout.loadingIconSize);
            const float iconY = previewRect.top + std::max(static_cast<float>(layout.previewInset),
                ((previewHeight - iconSize) / 2.0f) - (static_cast<float>(layout.metaHeight) / 2.0f));

            D2D1_RECT_F statusRect = D2D1::RectF(
                previewRect.left + layout.textInset,
                iconY + iconSize + std::max(6.0f, static_cast<float>(layout.metaTopGap)),
                previewRect.right - layout.textInset,
                previewRect.bottom - layout.previewInset);
            if (d2dStatusFormat_ && statusBrush)
            {
                d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawText(L"Loading thumbnail", 17, d2dStatusFormat_.Get(), statusRect, statusBrush);
            }
            return;
        }

        const auto displayImage = ImageForDisplay(cacheKey, thumbnail);
        ID2D1Bitmap* d2dBitmap = GetOrCreateD2DBitmap(rt, displayImage);
        if (!d2dBitmap)
        {
            return;
        }

        const D2D1_RECT_F imageBounds = InsetD2DRect(previewRect, 1.0f, 1.0f);
        const float thumbWidth = static_cast<float>(thumbnail->Width());
        const float thumbHeight = static_cast<float>(thumbnail->Height());
        const float drawX = imageBounds.left + ((imageBounds.right - imageBounds.left - thumbWidth) / 2.0f);
        const float drawY = imageBounds.top + ((imageBounds.bottom - imageBounds.top - thumbHeight) / 2.0f);

        rt->PushAxisAlignedClip(imageBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        hyperbrowse::render::DrawBitmapHighQuality(rt,
                   d2dBitmap,
                   D2D1::RectF(drawX, drawY, drawX + thumbWidth, drawY + thumbHeight),
                   1.0f);
        rt->PopAxisAlignedClip();
    }

    void BrowserPane::D2DDrawUnavailableThumbnailState(ID2D1RenderTarget* rt,
                                                       const D2D1_RECT_F& previewRect,
                                                       decode::ThumbnailDecodeFailureKind failureKind,
                                                       bool selected) const
    {
        if (!rt)
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        ID2D1SolidColorBrush* statusBrush = selected ? d2dSelectionTextBrush_.Get() : d2dMutedTextBrush_.Get();
        const float previewWidth = previewRect.right - previewRect.left;
        const float previewHeight = previewRect.bottom - previewRect.top;

        float iconSize = 0.0f;
        if (unavailableThumbnailArt_)
        {
            iconSize = std::min({
                static_cast<float>(kUnavailableThumbnailIconSize),
                std::max(0.0f, previewWidth - (layout.textInset * 2.0f)),
                previewHeight * 0.34f,
            });
        }

        float textTop = previewRect.top + (previewHeight * 0.34f);
        if (iconSize > 0.0f && unavailableThumbnailArt_)
        {
            if (ID2D1Bitmap* iconBitmap = GetOrCreateD2DBitmap(rt, unavailableThumbnailArt_))
            {
                const float iconX = previewRect.left + ((previewWidth - iconSize) / 2.0f);
                const float iconY = previewRect.top + std::max(10.0f, previewHeight * 0.14f);
                rt->DrawBitmap(iconBitmap,
                               D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize),
                               1.0f,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                textTop = iconY + iconSize + std::max(6.0f, static_cast<float>(layout.metaTopGap));
            }
        }

        const wchar_t* message = UnavailableThumbnailMessage(failureKind);
        if (d2dStatusFormat_ && statusBrush)
        {
            D2D1_RECT_F statusRect = D2D1::RectF(
                previewRect.left + layout.textInset,
                textTop,
                previewRect.right - layout.textInset,
                previewRect.bottom - layout.previewInset);
            d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dStatusFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dStatusFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            rt->DrawText(message,
                         static_cast<UINT32>(wcslen(message)),
                         d2dStatusFormat_.Get(),
                         statusRect,
                         statusBrush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
            d2dStatusFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
}
