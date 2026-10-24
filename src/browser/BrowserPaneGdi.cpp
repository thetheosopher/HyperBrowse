#include "browser/BrowserPaneInternal.h"

namespace
{

    const wchar_t* UnavailableThumbnailTooltipText(hyperbrowse::decode::ThumbnailDecodeFailureKind failureKind)
    {
        switch (failureKind)
        {
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut:
            return L"Thumbnail: generation timed out.";
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed:
        default:
            return L"Thumbnail: generation failed.";
        }
    }

    std::wstring ToUppercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });
        return value;
    }

    std::wstring BuildCameraLabel(const hyperbrowse::services::ImageMetadata& metadata)
    {
        if (!metadata.cameraMake.empty() && !metadata.cameraModel.empty())
        {
            return metadata.cameraMake + L" " + metadata.cameraModel;
        }

        return !metadata.cameraModel.empty() ? metadata.cameraModel : metadata.cameraMake;
    }
}

namespace hyperbrowse::browser
{

    void BrowserPane::DrawPlaceholderState(HDC hdc, const RECT& clientRect) const
    {
        FillRect(hdc, &clientRect, backgroundBrush_ ? backgroundBrush_ : surfaceBrush_);

        const bool hasFolder = model_ && !model_->FolderPath().empty();
        const bool hasError = hasFolder && model_->HasError();
        const bool loadingFolder = hasFolder && model_->IsEnumerating() && orderedModelIndices_.empty();
        const bool noMatches = hasFolder && !filterQuery_.empty() && orderedModelIndices_.empty();
        const bool emptyFolder = hasFolder && !hasError && !loadingFolder && !noMatches && orderedModelIndices_.empty();
        const bool showIcon = !hasError && (!hasFolder || loadingFolder || emptyFolder);

        std::wstring title = L"HyperBrowse";
        if (hasFolder)
        {
            if (hasError)
            {
                title = L"Folder Load Failed";
            }
            else if (loadingFolder)
            {
                title = L"Loading Images";
            }
            else if (noMatches)
            {
                title = L"No Matches";
            }
            else if (emptyFolder)
            {
                title = L"Empty Folder";
            }
        }

        RECT panelRect = clientRect;
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        constexpr int kPanelMarginX = 20;
        constexpr int kPanelMarginY = 16;
        const int kPanelPaddingLeft = hyperbrowse::util::ScaleAppTextDimension(12, appTextSize_);
        const int kPanelPaddingRight = hyperbrowse::util::ScaleAppTextDimension(14, appTextSize_);
        const int kPanelPaddingVertical = hyperbrowse::util::ScaleAppTextDimension(9, appTextSize_);
        const int kIconTextGap = hyperbrowse::util::ScaleAppTextDimension(15, appTextSize_);
        const int kDesiredTextBlockWidth = hyperbrowse::util::ScaleAppTextDimension(180, appTextSize_);
        const int kMinimumTextBlockWidth = hyperbrowse::util::ScaleAppTextDimension(135, appTextSize_);
        const int kTitleHeight = hyperbrowse::util::ScaleAppTextDimension(26, appTextSize_);
        const int kBodyHeight = hyperbrowse::util::ScaleAppTextDimension(32, appTextSize_);
        const int kTitleBodyGap = hyperbrowse::util::ScaleAppTextDimension(5, appTextSize_);

        const int maxPanelWidth = std::max(180, clientWidth - (kPanelMarginX * 2));
        const int maxPanelHeight = std::max(72, clientHeight - (kPanelMarginY * 2));

        int renderedIconSize = 0;
        int panelWidth = std::max(180, std::min(330, clientWidth - 48));
        int panelHeight = showIcon
            ? std::min(114, std::max(90, clientHeight - 48))
            : std::min(96, std::max(72, clientHeight - 48));
        if (showIcon && placeholderArt_)
        {
            const int maxIconWidth = std::max(54, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - kMinimumTextBlockWidth);
            const int maxIconHeight = std::max(54, maxPanelHeight - (kPanelPaddingVertical * 2));
            renderedIconSize = std::min({hyperbrowse::util::ScaleAppTextDimension(kPlaceholderIconDisplaySize, appTextSize_), placeholderArt_->Width(), maxIconWidth, maxIconHeight});

            const int textBlockWidth = std::max(kMinimumTextBlockWidth,
                                                std::min(kDesiredTextBlockWidth,
                                                         maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - renderedIconSize));
            panelWidth = std::min(maxPanelWidth,
                                  kPanelPaddingLeft + renderedIconSize + kIconTextGap + textBlockWidth + kPanelPaddingRight);
            panelHeight = std::min(maxPanelHeight, (kPanelPaddingVertical * 2) + renderedIconSize);
        }
        panelRect.left = clientRect.left + ((clientWidth - panelWidth) / 2);
        panelRect.top = clientRect.top + ((clientHeight - panelHeight) / 2);
        panelRect.right = panelRect.left + panelWidth;
        panelRect.bottom = panelRect.top + panelHeight;

        HGDIOBJ oldBrush = SelectObject(hdc, placeholderBrush_ ? placeholderBrush_ : surfaceBrush_);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen_ ? borderPen_ : GetStockObject(BLACK_PEN));
        const int panelCornerRadius = hyperbrowse::util::ScaleAppTextDimension(12, appTextSize_);
        RoundRect(hdc, panelRect.left, panelRect.top, panelRect.right, panelRect.bottom, panelCornerRadius, panelCornerRadius);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);

        const std::wstring text = BuildPlaceholderText();
        RECT titleRect{};
        RECT bodyRect{};
        if (showIcon && placeholderArt_)
        {
            const int iconX = panelRect.left + kPanelPaddingLeft;
            const int iconY = panelRect.top + ((panelHeight - renderedIconSize) / 2);
            util::DrawBitmapWithAlpha(hdc, *placeholderArt_, iconX, iconY, renderedIconSize, renderedIconSize);

            const int contentLeft = iconX + renderedIconSize + kIconTextGap;
            const int contentRight = panelRect.right - kPanelPaddingRight;
            const int textBlockHeight = kTitleHeight + kTitleBodyGap + kBodyHeight;
            const int contentTop = panelRect.top + std::max(kPanelPaddingVertical,
                                                            (panelHeight - textBlockHeight) / 2);
            titleRect = RECT{contentLeft, contentTop, contentRight, contentTop + kTitleHeight};
            bodyRect = RECT{contentLeft, titleRect.bottom + kTitleBodyGap, contentRight, titleRect.bottom + kTitleBodyGap + kBodyHeight};
        }
        else
        {
            titleRect = RECT{panelRect.left + 20, panelRect.top + 20, panelRect.right - 20, panelRect.top + 54};
            bodyRect = RECT{panelRect.left + 24, titleRect.bottom + 8, panelRect.right - 24, panelRect.bottom - 22};
            if (text.empty())
            {
                titleRect.top = panelRect.top + ((panelRect.bottom - panelRect.top - 34) / 2);
                titleRect.bottom = titleRect.top + 34;
            }
        }

        render::DrawGdiText(hdc,
                            placeholderTitleFont_,
                            title.c_str(),
                            -1,
                            titleRect,
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                            colors_.text,
                            colors_.placeholderBackground);

        if (!text.empty())
        {
            render::DrawGdiText(hdc,
                                placeholderBodyFont_,
                                text.c_str(),
                                -1,
                                bodyRect,
                                DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX,
                                colors_.mutedText,
                                colors_.placeholderBackground);
        }
    }

    void BrowserPane::DrawThumbnailCells(HDC hdc, const RECT& clientRect) const
    {
        FillRect(hdc, &clientRect, backgroundBrush_ ? backgroundBrush_ : surfaceBrush_);
        if (orderedModelIndices_.empty())
        {
            DrawPlaceholderState(hdc, clientRect);
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastRow = std::max(firstRow, (scrollOffsetY_ + static_cast<int>(clientRect.bottom - clientRect.top)) / verticalStride + 1);
        const int firstIndex = firstRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (lastRow + 1) * columns);
        const int footerGap = thumbnailDetailsVisible_ ? std::max(6, layout.badgeGap - 1) : 0;
        int ratingStripWidth = 0;
        if (thumbnailDetailsVisible_)
        {
            HGDIOBJ measureFont = thumbnailMetaFont_
                ? SelectObject(hdc, thumbnailMetaFont_)
                : static_cast<HGDIOBJ>(nullptr);
            SIZE ratingExtent{};
            if (GetTextExtentPoint32W(hdc,
                                      kThumbnailRatingStars.data(),
                                      static_cast<int>(kThumbnailRatingStars.size()),
                                      &ratingExtent) != FALSE)
            {
                ratingStripWidth = ratingExtent.cx;
            }
            if (measureFont)
            {
                SelectObject(hdc, measureFont);
            }
            if (ratingStripWidth <= 0)
            {
                ratingStripWidth = layout.infoHeight * 3;
            }
            ratingStripWidth += 2;
        }

        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item)
            {
                continue;
            }

            const RECT cellRect = GetThumbnailCellRect(viewIndex);
            if (cellRect.bottom < clientRect.top || cellRect.top > clientRect.bottom)
            {
                continue;
            }

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const bool selected = selectedModelIndices_.contains(modelIndex);

            const HBRUSH cellBrush = selected
                ? (selectedCellBrush_ ? selectedCellBrush_ : surfaceBrush_)
                : surfaceBrush_;
            HGDIOBJ pen = selected
                ? static_cast<HGDIOBJ>(selectedBorderPen_ ? selectedBorderPen_ : GetStockObject(BLACK_PEN))
                : static_cast<HGDIOBJ>(borderPen_ ? borderPen_ : GetStockObject(BLACK_PEN));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, cellBrush);
            RoundRect(hdc,
                      cellRect.left,
                      cellRect.top,
                      cellRect.right,
                      cellRect.bottom,
                      layout.cellCornerRadius,
                      layout.cellCornerRadius);

            RECT previewRect = GetThumbnailPreviewRect(cellRect);
            const HBRUSH previewBrush = selected
                ? (selectedPreviewBrush_ ? selectedPreviewBrush_ : previewBrush_)
                : (previewBrush_ ? previewBrush_ : backgroundBrush_);
            SelectObject(hdc, previewBrush);
            if (PairedCompanionModelIndex(modelIndex) >= 0)
            {
                RECT stackedPreviewRect = previewRect;
                OffsetRect(&stackedPreviewRect, -5, -5);
                RoundRect(hdc,
                          stackedPreviewRect.left,
                          stackedPreviewRect.top,
                          stackedPreviewRect.right,
                          stackedPreviewRect.bottom,
                          layout.previewCornerRadius,
                          layout.previewCornerRadius);
            }
            RoundRect(hdc,
                      previewRect.left,
                      previewRect.top,
                      previewRect.right,
                      previewRect.bottom,
                      layout.previewCornerRadius,
                      layout.previewCornerRadius);

            SetTextColor(hdc, selected ? colors_.selectionText : colors_.text);
            DrawPreviewThumbnail(hdc, previewRect, *item, selected);

            if (thumbnailDetailsVisible_ && !item->isDirectory)
            {
                const COLORREF cellBackground = selected ? colors_.accentFill : colors_.surfaceBackground;
                const int titleTop = previewRect.bottom + layout.titleTopGap;
                RECT nameRect{cellRect.left + layout.textInset,
                              titleTop,
                              cellRect.right - layout.textInset,
                              titleTop + layout.titleHeight};
                const std::wstring displayTitle = BuildThumbnailDisplayTitle(*item);
                render::DrawGdiText(hdc,
                                    thumbnailTitleFont_,
                                    displayTitle.c_str(),
                                    -1,
                                    nameRect,
                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                    selected ? colors_.selectionText : colors_.text,
                                    cellBackground);

                RECT infoRect{cellRect.left + layout.textInset,
                              cellRect.bottom - layout.infoBottomInset - layout.infoHeight,
                              cellRect.right - layout.textInset,
                              cellRect.bottom - layout.infoBottomInset};
                const LONG infoWidth = std::max<LONG>(0, infoRect.right - infoRect.left);
                const LONG centeredRatingWidth = std::min(infoWidth, static_cast<LONG>(ratingStripWidth));
                const LONG ratingLeft = infoRect.left + std::max<LONG>(0, (infoWidth - centeredRatingWidth) / 2);
                const LONG ratingRight = ratingLeft + centeredRatingWidth;
                RECT dimensionsRect{infoRect.left,
                                    infoRect.top,
                                    std::max(infoRect.left, ratingLeft - static_cast<LONG>(footerGap)),
                                    infoRect.bottom};
                RECT ratingRect{ratingLeft,
                                infoRect.top,
                                ratingRight,
                                infoRect.bottom};
                RECT typeRect{std::min(infoRect.right, ratingRight + static_cast<LONG>(footerGap)),
                              infoRect.top,
                              infoRect.right,
                              infoRect.bottom};
                const std::wstring typeLabel = BuildDisplayFileTypeLabel(*item, modelIndex);
                const std::wstring dimensionLabel = item->isDirectory ? std::wstring{} : FormatDimensionsForItem(*item);
                const int rating = item->isDirectory ? 0 : ThumbnailRatingForItem(userMetadataStore_, *item);
                render::DrawGdiText(hdc,
                                    thumbnailMetaFont_,
                                    dimensionLabel.c_str(),
                                    -1,
                                    dimensionsRect,
                                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                    selected ? colors_.selectionText : colors_.mutedText,
                                    cellBackground);

                render::DrawGdiText(hdc,
                                    thumbnailMetaFont_,
                                    kThumbnailRatingStars.data(),
                                    static_cast<int>(kThumbnailRatingStars.size()),
                                    ratingRect,
                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                    colors_.mutedText,
                                    cellBackground);
                if (rating > 0)
                {
                    render::DrawGdiText(hdc,
                                        thumbnailMetaFont_,
                                        kThumbnailRatingStars.data(),
                                        rating,
                                        ratingRect,
                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                        kThumbnailRatingGold,
                                        cellBackground);
                }

                if (typeRect.right > typeRect.left)
                {
                    render::DrawGdiText(hdc,
                                        thumbnailMetaFont_,
                                        typeLabel.c_str(),
                                        -1,
                                        typeRect,
                                        DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                        selected ? colors_.selectionText : colors_.text,
                                        cellBackground);
                }
            }

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
        }

        if (rubberBandActive_)
        {
            HGDIOBJ pen = static_cast<HGDIOBJ>(rubberBandPen_ ? rubberBandPen_ : GetStockObject(BLACK_PEN));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, rubberBandRect_.left, rubberBandRect_.top, rubberBandRect_.right, rubberBandRect_.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
        }
    }

    void BrowserPane::DrawPreviewThumbnail(HDC hdc, const RECT& previewRect, const BrowserItem& item, bool selected) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        if (item.isDirectory)
        {
            const int previewHeight = previewRect.bottom - previewRect.top;
            const int labelHeight = std::max(24, layout.titleHeight + 8);
            const int iconSize = std::min({
                folderArt_ ? folderArt_->Width() : 0,
                std::max(0, static_cast<int>(previewRect.right - previewRect.left) - (layout.textInset * 2)),
                static_cast<int>(previewHeight * 0.60f),
            });
            const int contentHeight = iconSize > 0 ? iconSize + 6 + labelHeight : labelHeight;
            const int contentTop = previewRect.top + std::max(4, (previewHeight - contentHeight) / 2);
            int labelTop = contentTop;

            if (folderArt_ && iconSize > 0)
            {
                util::DrawBitmapWithAlpha(hdc,
                                          *folderArt_,
                                          previewRect.left + ((previewRect.right - previewRect.left - iconSize) / 2),
                                          contentTop,
                                          iconSize,
                                          iconSize);
                labelTop += iconSize + 6;
            }

            RECT folderRect{
                previewRect.left + layout.textInset,
                labelTop,
                previewRect.right - layout.textInset,
                labelTop + labelHeight,
            };
            const std::wstring displayTitle = BuildThumbnailDisplayTitle(item);
            render::DrawGdiText(hdc,
                                folderTitleFont_,
                                displayTitle.c_str(),
                                -1,
                                folderRect,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                selected ? colors_.selectionText : colors_.text,
                                selected ? colors_.selectedPreviewBackground : colors_.previewBackground);
            return;
        }

        const int targetWidth = layout.previewWidth;
        const int targetHeight = layout.previewHeight;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);
        const auto thumbnail = thumbnailScheduler_ ? thumbnailScheduler_->FindCachedThumbnail(cacheKey) : nullptr;

        if (!thumbnailScheduler_ || !decode::CanDecodeThumbnail(item))
        {
            RECT iconTextRect{previewRect.left + layout.textInset,
                              previewRect.top + std::max(10, layout.previewInset - 2),
                              previewRect.right - layout.textInset,
                              previewRect.top + std::max(34, layout.previewInset + layout.metaHeight)};
            const std::wstring placeholder = decode::IsRawFileType(item.fileType) ? item.fileType : std::wstring(L"IMAGE");
            render::DrawGdiText(hdc,
                                thumbnailStatusFont_,
                                placeholder.c_str(),
                                -1,
                                iconTextRect,
                                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX,
                                selected ? colors_.selectionText : colors_.mutedText,
                                selected ? colors_.selectedPreviewBackground : colors_.previewBackground);
            return;
        }

        if (!thumbnail)
        {
            const decode::ThumbnailDecodeFailureKind knownFailureKind = thumbnailScheduler_->KnownFailureKind(cacheKey);
            if (knownFailureKind != decode::ThumbnailDecodeFailureKind::None)
            {
                DrawUnavailableThumbnailState(hdc, previewRect, knownFailureKind, selected);
                return;
            }

            const int previewWidth = previewRect.right - previewRect.left;
            const int previewHeight = previewRect.bottom - previewRect.top;
            const int iconSize = layout.loadingIconSize;
            const int iconX = previewRect.left + ((previewWidth - iconSize) / 2);
            const int iconY = previewRect.top + std::max(layout.previewInset,
                                                         ((previewHeight - iconSize) / 2) - (layout.metaHeight / 2));
            if (HCURSOR waitCursor = LoadCursorW(nullptr, IDC_WAIT))
            {
                DrawIconEx(hdc, iconX, iconY, waitCursor, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            }

            RECT statusRect{previewRect.left + layout.textInset,
                            iconY + iconSize + std::max(6, layout.metaTopGap),
                            previewRect.right - layout.textInset,
                            previewRect.bottom - layout.previewInset};
            render::DrawGdiText(hdc,
                                thumbnailStatusFont_,
                                L"Loading thumbnail",
                                -1,
                                statusRect,
                                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                selected ? colors_.selectionText : colors_.mutedText,
                                selected ? colors_.selectedPreviewBackground : colors_.previewBackground);
            return;
        }

        const int drawX = previewRect.left + (((previewRect.right - previewRect.left) - thumbnail->Width()) / 2);
        const int drawY = previewRect.top + (((previewRect.bottom - previewRect.top) - thumbnail->Height()) / 2);
        HDC bitmapDc = CreateCompatibleDC(hdc);
        const auto displayImage = ImageForDisplay(cacheKey, thumbnail);
        HGDIOBJ oldBitmap = SelectObject(bitmapDc, displayImage->Bitmap());

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(hdc,
                   drawX,
                   drawY,
                   thumbnail->Width(),
                   thumbnail->Height(),
                   bitmapDc,
                   0,
                   0,
                   thumbnail->Width(),
                   thumbnail->Height(),
                   blend);

        SelectObject(bitmapDc, oldBitmap);
        DeleteDC(bitmapDc);
    }

    void BrowserPane::DrawUnavailableThumbnailState(HDC hdc,
                                                    const RECT& previewRect,
                                                    decode::ThumbnailDecodeFailureKind failureKind,
                                                    bool selected) const
    {
        if (!hdc)
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int previewWidth = previewRect.right - previewRect.left;
        const int previewHeight = previewRect.bottom - previewRect.top;
        int textTop = previewRect.top + (previewHeight / 3);

        if (unavailableThumbnailArt_)
        {
            const int iconSize = std::min({
                kUnavailableThumbnailIconSize,
                std::max(0, previewWidth - (layout.textInset * 2)),
                static_cast<int>(previewHeight * 0.34f),
            });
            if (iconSize > 0)
            {
                const int iconX = previewRect.left + ((previewWidth - iconSize) / 2);
                const int iconY = previewRect.top + std::max(10, static_cast<int>(previewHeight * 0.14f));
                util::DrawBitmapWithAlpha(hdc, *unavailableThumbnailArt_, iconX, iconY, iconSize, iconSize);
                textTop = iconY + iconSize + std::max(6, layout.metaTopGap);
            }
        }

        const wchar_t* message = UnavailableThumbnailMessage(failureKind);
        RECT statusRect{previewRect.left + layout.textInset,
                        textTop,
                        previewRect.right - layout.textInset,
                        previewRect.bottom - layout.previewInset};
        render::DrawGdiText(hdc,
                            thumbnailStatusFont_,
                            message,
                            -1,
                            statusRect,
                            DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX,
                            selected ? colors_.selectionText : colors_.mutedText,
                            selected ? colors_.selectedPreviewBackground : colors_.previewBackground);
    }

    std::wstring BrowserPane::BuildListText(int viewIndex, int subItem) const
    {
        const BrowserItem* item = ItemFromViewIndex(viewIndex);
        if (!item)
        {
            return L"";
        }

        if (item->isDirectory)
        {
            switch (subItem)
            {
            case 0:
                return item->fileName;
            case 1:
                return L"Folder";
            case 3:
                return item->modifiedDate;
            default:
                return L"";
            }
        }

        switch (subItem)
        {
        case 0:
            return item->fileName;
        case 1:
        {
            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            return BuildDisplayFileTypeLabel(*item, modelIndex);
        }
        case 2:
            return FormatByteSize(item->fileSizeBytes);
        case 3:
            return item->modifiedDate;
        case 4:
        {
            if (item->imageWidth > 0 && item->imageHeight > 0)
            {
                return FormatDimensionsForItem(*item);
            }

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const auto metadata = modelIndex >= 0 ? FindCachedMetadataForModelIndex(modelIndex) : nullptr;
            if (metadata && metadata->imageWidth > 0 && metadata->imageHeight > 0)
            {
                return FormatDimensions(metadata->imageWidth, metadata->imageHeight);
            }

            if (modelIndex >= 0)
            {
                ScheduleMetadataForItem(modelIndex, *item);
            }
            return L"...";
        }
        case 5:
        case 6:
        case 7:
        {
            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const auto metadata = modelIndex >= 0 ? FindCachedMetadataForModelIndex(modelIndex) : nullptr;
            if (!metadata)
            {
                if (modelIndex >= 0)
                {
                    ScheduleMetadataForItem(modelIndex, *item);
                }
                return L"...";
            }

            if (subItem == 5)
            {
                return metadata->dateTaken;
            }
            if (subItem == 6)
            {
                return BuildCameraLabel(*metadata);
            }
            return metadata->title;
        }
        default:
            return L"";
        }
    }

    std::wstring BrowserPane::BuildDisplayFileTypeLabel(const BrowserItem& item, int modelIndex) const
    {
        std::wstring label = ToUppercase(item.fileType);
        const int companionModelIndex = PairedCompanionModelIndex(modelIndex);
        if (!model_ || companionModelIndex < 0)
        {
            return label;
        }

        const auto& items = model_->Items();
        if (companionModelIndex >= static_cast<int>(items.size()))
        {
            return label;
        }

        label.push_back(L'+');
        label.append(ToUppercase(items[static_cast<std::size_t>(companionModelIndex)].fileType));
        return label;
    }

    std::wstring BrowserPane::BuildThumbnailTooltipText(int viewIndex) const
    {
        const BrowserItem* item = ItemFromViewIndex(viewIndex);
        if (!item)
        {
            return {};
        }

        std::wstring tooltip = item->fileName;
        tooltip.append(L"\r\nType: ");
        tooltip.append(ToUppercase(item->fileType));
        if (item->isDirectory)
        {
            tooltip.append(L"\r\nModified: ");
            tooltip.append(item->modifiedDate);
            return tooltip;
        }

        tooltip.append(L"\r\nDimensions: ");
        tooltip.append(FormatDimensionsForItem(*item));
        tooltip.append(L"\r\nSize: ");
        tooltip.append(FormatByteSize(item->fileSizeBytes));
        tooltip.append(L"\r\nModified: ");
        tooltip.append(item->modifiedDate);

        if (thumbnailScheduler_ && decode::CanDecodeThumbnail(*item))
        {
            const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
            const auto cacheKey = MakeThumbnailCacheKey(*item, layout.previewWidth, layout.previewHeight);
            const decode::ThumbnailDecodeFailureKind failureKind = thumbnailScheduler_->KnownFailureKind(cacheKey);
            if (failureKind != decode::ThumbnailDecodeFailureKind::None)
            {
                tooltip.append(L"\r\n");
                tooltip.append(UnavailableThumbnailTooltipText(failureKind));
                const std::wstring failureMessage = thumbnailScheduler_->KnownFailureMessage(cacheKey);
                if (!failureMessage.empty())
                {
                    tooltip.append(L"\r\nDecode detail: ");
                    tooltip.append(failureMessage);
                }
            }
        }

        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        const auto metadata = modelIndex >= 0 ? FindCachedMetadataForModelIndex(modelIndex) : nullptr;
        if (metadata)
        {
            const std::wstring cameraLabel = BuildCameraLabel(*metadata);
            if (!cameraLabel.empty())
            {
                tooltip.append(L"\r\nCamera: ");
                tooltip.append(cameraLabel);
            }
            if (!metadata->dateTaken.empty())
            {
                tooltip.append(L"\r\nTaken: ");
                tooltip.append(metadata->dateTaken);
            }
        }
        else if (modelIndex >= 0)
        {
            ScheduleMetadataForItem(modelIndex, *item);
        }

        return tooltip;
    }

    void BrowserPane::UpdateThumbnailTooltip(POINT point, WPARAM wParam, LPARAM lParam)
    {
        if (!thumbnailTooltip_)
        {
            return;
        }

        if (!trackingMouseLeave_)
        {
            TRACKMOUSEEVENT trackMouseEvent{};
            trackMouseEvent.cbSize = sizeof(trackMouseEvent);
            trackMouseEvent.dwFlags = TME_LEAVE;
            trackMouseEvent.hwndTrack = hwnd_;
            if (TrackMouseEvent(&trackMouseEvent) != FALSE)
            {
                trackingMouseLeave_ = true;
            }
        }

        const int viewIndex = HitTestThumbnailItem(point);
        if (viewIndex != hoveredTooltipViewIndex_)
        {
            hoveredTooltipViewIndex_ = viewIndex;

            TOOLINFOW toolInfo{};
            toolInfo.cbSize = sizeof(toolInfo);
            toolInfo.hwnd = hwnd_;
            toolInfo.uId = 1;
            toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
            if (viewIndex >= 0)
            {
                toolInfo.rect = GetThumbnailCellRect(viewIndex);
            }
            else
            {
                SetRectEmpty(&toolInfo.rect);
            }

            SendMessageW(thumbnailTooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            SendMessageW(thumbnailTooltip_, TTM_POP, 0, 0);
        }

        MSG relayMessage{};
        relayMessage.hwnd = hwnd_;
        relayMessage.message = WM_MOUSEMOVE;
        relayMessage.wParam = wParam;
        relayMessage.lParam = lParam;
        SendMessageW(thumbnailTooltip_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }

    void BrowserPane::HideThumbnailTooltip()
    {
        if (!thumbnailTooltip_)
        {
            hoveredTooltipViewIndex_ = -1;
            trackingMouseLeave_ = false;
            return;
        }

        hoveredTooltipViewIndex_ = -1;
        trackingMouseLeave_ = false;

        TOOLINFOW toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.hwnd = hwnd_;
        toolInfo.uId = 1;
        toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
        SetRectEmpty(&toolInfo.rect);
        SendMessageW(thumbnailTooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        SendMessageW(thumbnailTooltip_, TTM_POP, 0, 0);
    }

    std::wstring BrowserPane::BuildPlaceholderText() const
    {
        if (!model_ || model_->FolderPath().empty())
        {
            return L"Select a folder to begin browsing.";
        }

        if (model_->HasError())
        {
            return L"Unable to load this folder.\r\n\r\n" + model_->ErrorMessage();
        }

        if (model_->IsEnumerating() && orderedModelIndices_.empty())
        {
            return L"Scanning folder...";
        }

        if (!filterQuery_.empty() && orderedModelIndices_.empty())
        {
            return L"No images match \"" + filterQuery_ + L"\".";
        }

        return orderedModelIndices_.empty()
            ? L"No supported images found."
            : L"";
    }
}
