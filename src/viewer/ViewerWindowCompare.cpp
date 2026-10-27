#include "viewer/ViewerWindowInternal.h"

namespace
{

    struct CompareTileReadyResult
    {
        std::uint64_t sessionGeneration{};
        std::uint64_t requestGeneration{};
        std::size_t tileIndex{};
        int itemIndex{-1};
        std::wstring filePath;
        std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> image;
        std::wstring errorMessage;
    };
}

namespace hyperbrowse::viewer
{

    void ViewerWindow::SetCompareMode(bool enabled, CompareDirection direction)
    {
        if (!compareTiles_.empty())
        {
            if (!enabled && compareTiles_.size() >= 3)
            {
                EndCompareSessionPromoteFocused();
                return;
            }
            compareSessionActive_ = enabled;
            if (!enabled)
            {
                CaptureFocusedCompareView();
            }
            else
            {
                FocusCompareTileInternal(focusedCompareTile_);
            }
            RefreshOverlayContent();
            UpdateWindowTitle();
            if (hwnd_ && IsWindow(hwnd_) != FALSE)
            {
                RequestRepaint();
            }
            return;
        }

        if (!enabled)
        {
            compareMode_ = false;
            d2dCompareImageBitmap_.Reset();
            d2dCompareImageIndex_ = -1;
            StopTransition();
            UpdateWindowTitle();
            ClampPanOffsets();
            if (hwnd_ && IsWindow(hwnd_) != FALSE)
            {
                RequestRepaint();
            }
            return;
        }

        compareDirection_ = ResolveCompareDirection(direction);
        compareMode_ = ActiveCompareIndex() >= 0;
        d2dCompareImageBitmap_.Reset();
        d2dCompareImageIndex_ = -1;
        StopTransition();
        UpdateWindowTitle();
        ClampPanOffsets();
        if (compareMode_ && currentImage_ && !pendingLoadActive_)
        {
            ScheduleAdjacentPrefetch(asyncState_->navigationGeneration.load(std::memory_order_acquire));
        }
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
    }

    bool ViewerWindow::IsCompareModeEnabled() const noexcept
    {
        return compareSessionActive_ || compareMode_;
    }

    bool ViewerWindow::BeginCompareSession(const std::vector<int>& tileIndexes,
                                          const std::vector<CompareTileMetadata>& candidateMetadata)
    {
        if (!IsSupportedCompareTileCount(tileIndexes.size()))
        {
            return false;
        }

        std::vector<int> uniqueIndexes;
        uniqueIndexes.reserve(tileIndexes.size());
        for (const int itemIndex : tileIndexes)
        {
            if (itemIndex < 0 || itemIndex >= static_cast<int>(items_.size())
                || std::find(uniqueIndexes.begin(), uniqueIndexes.end(), itemIndex) != uniqueIndexes.end())
            {
                return false;
            }
            uniqueIndexes.push_back(itemIndex);
        }

        compareSessionGeneration_++;
        compareAsyncState_->sessionGeneration.store(compareSessionGeneration_, std::memory_order_release);
        compareTiles_.clear();
        compareTiles_.reserve(tileIndexes.size());
        compareCandidateMetadata_ = candidateMetadata;
        compareViewsSynchronized_ = true;
        compareMode_ = false;
        focusedCompareTile_ = 0;
        for (std::size_t tileIndex = 0; tileIndex < tileIndexes.size(); ++tileIndex)
        {
            CompareTileState tile;
            tile.itemIndex = tileIndexes[tileIndex];
            tile.filePath = items_[static_cast<std::size_t>(tile.itemIndex)].filePath;
            if (static_cast<std::size_t>(tile.itemIndex) < compareCandidateMetadata_.size())
            {
                tile.metadata = compareCandidateMetadata_[static_cast<std::size_t>(tile.itemIndex)];
            }
            tile.image = tile.itemIndex == currentIndex_ ? currentImage_ : nullptr;
            compareTiles_.push_back(std::move(tile));
            if (tileIndexes[tileIndex] == currentIndex_)
            {
                focusedCompareTile_ = tileIndex;
            }
        }

        compareSessionActive_ = true;
        for (std::size_t tileIndex = 0; tileIndex < compareTiles_.size(); ++tileIndex)
        {
            CompareTileState& tile = compareTiles_[tileIndex];
            if (!tile.image)
            {
                tile.image = ViewerFullImageCache().Find(MakeViewerFullImageCacheKey(
                    items_[static_cast<std::size_t>(tile.itemIndex)]));
            }
            if (!tile.image)
            {
                RequestCompareTileImage(tileIndex, tile.itemIndex, false);
            }
        }
        FocusCompareTileInternal(focusedCompareTile_);
        UpdateWindowTitle();
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
        return true;
    }

    bool ViewerWindow::HasCompareSession() const noexcept
    {
        return !compareTiles_.empty();
    }

    bool ViewerWindow::IsCompareSessionActive() const noexcept
    {
        return compareSessionActive_;
    }

    std::size_t ViewerWindow::CompareTileCount() const noexcept
    {
        return compareTiles_.size();
    }

    int ViewerWindow::FocusedCompareTile() const noexcept
    {
        return compareSessionActive_ ? static_cast<int>(focusedCompareTile_) : -1;
    }

    int ViewerWindow::CompareTileItemIndex(std::size_t tileIndex) const noexcept
    {
        return tileIndex < compareTiles_.size() ? compareTiles_[tileIndex].itemIndex : -1;
    }

    bool ViewerWindow::AreCompareViewsSynchronized() const noexcept
    {
        return compareViewsSynchronized_;
    }

    bool ViewerWindow::SetCompareViewsSynchronized(bool synchronized)
    {
        if (compareTiles_.empty() || compareViewsSynchronized_ == synchronized)
        {
            return false;
        }

        CaptureFocusedCompareView();
        compareViewsSynchronized_ = synchronized;
        if (synchronized)
        {
            ApplyCompareViewToTiles();
            FocusCompareTileInternal(focusedCompareTile_);
        }
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
        return true;
    }

    void ViewerWindow::UpdateCompareTileMetadata(std::wstring_view filePath, CompareTileMetadata metadata)
    {
        for (CompareTileState& tile : compareTiles_)
        {
            if (browser::FilePathsEqual(tile.filePath, filePath))
            {
                tile.metadata = metadata;
            }
        }
        for (std::size_t candidateIndex = 0; candidateIndex < items_.size(); ++candidateIndex)
        {
            if (browser::FilePathsEqual(items_[candidateIndex].filePath, filePath)
                && candidateIndex < compareCandidateMetadata_.size())
            {
                compareCandidateMetadata_[candidateIndex] = metadata;
            }
        }
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
    }

    RECT ViewerWindow::CompareTileClientRect(std::size_t tileIndex) const
    {
        RECT clientRect{};
        if (!hwnd_ || GetClientRect(hwnd_, &clientRect) == FALSE)
        {
            return {};
        }
        const UINT dpi = std::max<UINT>(96, GetDpiForWindow(hwnd_));
        const LONG gap = std::clamp<LONG>(static_cast<LONG>((static_cast<double>(dpi) / 96.0) * 16.0), 12, 32);
        const std::vector<RECT> bounds = CompareTileBounds(clientRect, compareTiles_.size(), gap);
        return tileIndex < bounds.size() ? bounds[tileIndex] : RECT{};
    }

    void ViewerWindow::RequestCompareTileImage(std::size_t tileIndex, int itemIndex, bool preserveCurrent)
    {
        if (tileIndex >= compareTiles_.size() || itemIndex < 0 || itemIndex >= static_cast<int>(items_.size()))
        {
            return;
        }

        CompareTileState& tile = compareTiles_[tileIndex];
        const browser::BrowserItem item = items_[static_cast<std::size_t>(itemIndex)];
        if (preserveCurrent)
        {
            tile.pendingItemIndex = itemIndex;
            tile.pendingFilePath = item.filePath;
        }
        else
        {
            tile.itemIndex = itemIndex;
            tile.filePath = item.filePath;
            tile.pendingItemIndex = -1;
            tile.pendingFilePath.clear();
            tile.image.reset();
            tile.bitmap.Reset();
        }

        const std::uint64_t requestGeneration = ++tile.requestGeneration;
        const std::uint64_t sessionGeneration = compareSessionGeneration_;
        compareAsyncState_->tileRequestGenerations[tileIndex].store(requestGeneration, std::memory_order_release);
        tile.loading = true;
        tile.errorMessage.clear();

        if (const auto cachedImage = ViewerFullImageCache().Find(MakeViewerFullImageCacheKey(item)))
        {
            tile.itemIndex = itemIndex;
            tile.filePath = item.filePath;
            tile.pendingItemIndex = -1;
            tile.pendingFilePath.clear();
            tile.image = cachedImage;
            tile.bitmap.Reset();
            tile.loading = false;
            if (static_cast<std::size_t>(itemIndex) < compareCandidateMetadata_.size())
            {
                tile.metadata = compareCandidateMetadata_[static_cast<std::size_t>(itemIndex)];
            }
            if (tileIndex == focusedCompareTile_)
            {
                FocusCompareTileInternal(tileIndex);
                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    PostMessageW(owner_, kContextMenuCommandMessage,
                                 kContextMenuRefreshCompareTileMetadata,
                                 reinterpret_cast<LPARAM>(hwnd_));
                }
            }
            RequestRepaint();
            return;
        }

        const std::shared_ptr<AsyncState> asyncState = asyncState_;
        const std::shared_ptr<CompareAsyncState> compareAsyncState = compareAsyncState_;
        const auto cacheKey = MakeViewerFullImageCacheKey(item);
        if (!backgroundExecutor_
            || !backgroundExecutor_->Post([asyncState, compareAsyncState, item, cacheKey, tileIndex,
                                           itemIndex, requestGeneration, sessionGeneration]()
            {
                if (asyncState->shutdown.load(std::memory_order_acquire)
                    || compareAsyncState->sessionGeneration.load(std::memory_order_acquire) != sessionGeneration
                    || compareAsyncState->tileRequestGenerations[tileIndex].load(std::memory_order_acquire) != requestGeneration)
                {
                    return;
                }

                std::wstring errorMessage;
                std::shared_ptr<const cache::CachedThumbnail> image;
                try
                {
                    image = decode::DecodeFullImage(item, &errorMessage);
                }
                catch (const std::exception&)
                {
                    errorMessage = L"Image decoding failed unexpectedly.";
                }
                catch (...)
                {
                    errorMessage = L"Image decoding failed unexpectedly.";
                }

                if (asyncState->shutdown.load(std::memory_order_acquire)
                    || compareAsyncState->sessionGeneration.load(std::memory_order_acquire) != sessionGeneration
                    || compareAsyncState->tileRequestGenerations[tileIndex].load(std::memory_order_acquire) != requestGeneration)
                {
                    return;
                }

                auto update = std::make_unique<CompareTileReadyResult>();
                update->sessionGeneration = sessionGeneration;
                update->requestGeneration = requestGeneration;
                update->tileIndex = tileIndex;
                update->itemIndex = itemIndex;
                update->filePath = item.filePath;
                update->image = std::move(image);
                update->errorMessage = std::move(errorMessage);
                const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
                if (!targetWindow || !PostMessageW(targetWindow,
                                                   ViewerWindow::kCompareTileReadyMessage,
                                                   0,
                                                   reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }
                update.release();
            }))
        {
            tile.loading = false;
            tile.errorMessage = L"Image loading is unavailable.";
            tile.pendingItemIndex = -1;
            tile.pendingFilePath.clear();
        }

        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
    }

    LRESULT ViewerWindow::HandleCompareTileReadyMessage(LPARAM lParam)
    {
        std::unique_ptr<CompareTileReadyResult> update(reinterpret_cast<CompareTileReadyResult*>(lParam));
        if (!update || update->sessionGeneration != compareSessionGeneration_
            || update->tileIndex >= compareTiles_.size())
        {
            return 0;
        }

        CompareTileState& tile = compareTiles_[update->tileIndex];
        const bool pendingReplacement = tile.pendingItemIndex >= 0;
        const int expectedItemIndex = pendingReplacement ? tile.pendingItemIndex : tile.itemIndex;
        const std::wstring& expectedPath = pendingReplacement ? tile.pendingFilePath : tile.filePath;
        if (update->requestGeneration != tile.requestGeneration
            || update->requestGeneration != compareAsyncState_->tileRequestGenerations[update->tileIndex].load(std::memory_order_acquire)
            || update->itemIndex != expectedItemIndex
            || !browser::FilePathsEqual(update->filePath, expectedPath))
        {
            return 0;
        }

        tile.loading = false;
        if (pendingReplacement && !update->image)
        {
            tile.pendingItemIndex = -1;
            tile.pendingFilePath.clear();
            tile.errorMessage = update->errorMessage.empty()
                ? L"The replacement image could not be decoded."
                : std::move(update->errorMessage);
            RequestRepaint();
            return 0;
        }
        tile.errorMessage = std::move(update->errorMessage);
        if (pendingReplacement)
        {
            tile.itemIndex = update->itemIndex;
            tile.filePath = std::move(update->filePath);
            tile.pendingItemIndex = -1;
            tile.pendingFilePath.clear();
            if (static_cast<std::size_t>(tile.itemIndex) < compareCandidateMetadata_.size())
            {
                tile.metadata = compareCandidateMetadata_[static_cast<std::size_t>(tile.itemIndex)];
            }
        }
        tile.image = std::move(update->image);
        tile.bitmap.Reset();
        if (tile.image && tile.itemIndex >= 0 && tile.itemIndex < static_cast<int>(items_.size()))
        {
            ViewerFullImageCache().Insert(
                MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(tile.itemIndex)]), tile.image);
            tile.errorMessage.clear();
        }
        if (update->tileIndex == focusedCompareTile_)
        {
            FocusCompareTileInternal(update->tileIndex);
            if (owner_ && IsWindow(owner_) != FALSE)
            {
                PostMessageW(owner_, kContextMenuCommandMessage,
                             kContextMenuRefreshCompareTileMetadata,
                             reinterpret_cast<LPARAM>(hwnd_));
            }
        }
        RequestRepaint();
        return 0;
    }

    void ViewerWindow::CaptureFocusedCompareView()
    {
        if (focusedCompareTile_ >= compareTiles_.size())
        {
            return;
        }

        CompareTileState& tile = compareTiles_[focusedCompareTile_];
        if (!tile.image)
        {
            return;
        }

        const RECT tileRect = CompareTileClientRect(focusedCompareTile_);
        const double fitScale = FitScaleForImage(*tile.image, tileRect);
        double effectiveScale = fitScale;
        if (zoomMode_ == ZoomMode::FitHeight)
        {
            effectiveScale = FitHeightScaleForImage(*tile.image, tileRect);
        }
        else if (zoomMode_ == ZoomMode::FitWidth)
        {
            effectiveScale = FitWidthScaleForImage(*tile.image, tileRect);
        }
        else if (zoomMode_ == ZoomMode::Custom)
        {
            effectiveScale = customZoomScale_;
        }
        tile.zoomRatio = std::clamp(effectiveScale / std::max(0.0001, fitScale), 0.01, kMaximumZoomScale);
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const int imageWidth = swapDimensions ? tile.image->SourceHeight() : tile.image->SourceWidth();
        const int imageHeight = swapDimensions ? tile.image->SourceWidth() : tile.image->SourceHeight();
        tile.imageCenter = ImageCenterFromPan(imageWidth, imageHeight, effectiveScale, panOffsetX_, panOffsetY_);
        tile.rotationQuarterTurns = rotationQuarterTurns_;
    }

    void ViewerWindow::ApplyCompareViewToTiles()
    {
        if (focusedCompareTile_ >= compareTiles_.size())
        {
            return;
        }
        CaptureFocusedCompareView();
        const CompareTileState& source = compareTiles_[focusedCompareTile_];
        for (CompareTileState& tile : compareTiles_)
        {
            if (&tile != &source)
            {
                tile.zoomRatio = source.zoomRatio;
                tile.imageCenter = source.imageCenter;
            }
        }
    }

    void ViewerWindow::FocusCompareTileInternal(std::size_t tileIndex)
    {
        if (tileIndex >= compareTiles_.size())
        {
            return;
        }
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
        {
            CaptureFocusedCompareView();
            if (compareViewsSynchronized_ && tileIndex != focusedCompareTile_)
            {
                compareTiles_[tileIndex].zoomRatio = compareTiles_[focusedCompareTile_].zoomRatio;
                compareTiles_[tileIndex].imageCenter = compareTiles_[focusedCompareTile_].imageCenter;
            }
        }
        const bool changed = focusedCompareTile_ != tileIndex;
        focusedCompareTile_ = tileIndex;
        CompareTileState& tile = compareTiles_[tileIndex];
        if (compareViewsSynchronized_ && tileIndex < compareTiles_.size())
        {
            const CompareTileState& sharedView = compareTiles_[tileIndex];
            for (CompareTileState& otherTile : compareTiles_)
            {
                otherTile.zoomRatio = sharedView.zoomRatio;
                otherTile.imageCenter = sharedView.imageCenter;
            }
        }
        if (tile.image)
        {
            const RECT tileRect = CompareTileClientRect(tileIndex);
            const double fitScale = FitScaleForImage(*tile.image, tileRect);
            customZoomScale_ = fitScale * tile.zoomRatio;
            zoomMode_ = ZoomMode::Custom;
            rotationQuarterTurns_ = tile.rotationQuarterTurns;
            const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
            const int imageWidth = swapDimensions ? tile.image->SourceHeight() : tile.image->SourceWidth();
            const int imageHeight = swapDimensions ? tile.image->SourceWidth() : tile.image->SourceHeight();
            const POINT pan = PanFromImageCenter(imageWidth, imageHeight, customZoomScale_, tileRect, tile.imageCenter);
            panOffsetX_ = pan.x;
            panOffsetY_ = pan.y;
        }
        if (tile.image)
        {
            const RECT tileRect = CompareTileClientRect(tileIndex);
            currentZoomPercent_ = (std::max)(1, static_cast<int>(std::lround(
                FitScaleForImage(*tile.image, tileRect) * tile.zoomRatio * 100)));
        }
        RefreshOverlayContent();
        if (changed && hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            NotifyCurrentItemChanged();
            UpdateWindowTitle();
            RequestRepaint();
        }
    }

    bool ViewerWindow::CycleCompareCandidate(int direction)
    {
        if (!compareSessionActive_ || focusedCompareTile_ >= compareTiles_.size())
        {
            return false;
        }
        std::vector<int> visibleIndices;
        visibleIndices.reserve(compareTiles_.size() - 1);
        for (std::size_t tileIndex = 0; tileIndex < compareTiles_.size(); ++tileIndex)
        {
            if (tileIndex != focusedCompareTile_)
            {
                visibleIndices.push_back(compareTiles_[tileIndex].itemIndex);
                if (compareTiles_[tileIndex].pendingItemIndex >= 0)
                {
                    visibleIndices.push_back(compareTiles_[tileIndex].pendingItemIndex);
                }
            }
        }
        const CompareTileState& focusedTile = compareTiles_[focusedCompareTile_];
        const int cycleFromIndex = focusedTile.pendingItemIndex >= 0
            ? focusedTile.pendingItemIndex
            : focusedTile.itemIndex;
        const int nextIndex = NextAvailableCompareCandidate(cycleFromIndex,
                                                              visibleIndices,
                                                              static_cast<int>(items_.size()),
                                                              direction);
        if (nextIndex < 0)
        {
            return false;
        }
        CaptureFocusedCompareView();
        RequestCompareTileImage(focusedCompareTile_, nextIndex, true);
        return true;
    }

    bool ViewerWindow::CycleAdjacentCompareCandidate(int direction)
    {
        if (!compareSessionActive_ || compareTiles_.size() != 2 || direction == 0
            || focusedCompareTile_ >= compareTiles_.size())
        {
            return false;
        }

        const std::size_t replacementTile = focusedCompareTile_ == 0 ? 1 : 0;
        const int anchorIndex = compareTiles_[focusedCompareTile_].itemIndex;
        int candidateIndex = anchorIndex + (direction > 0 ? 1 : -1);
        if (candidateIndex < 0 || candidateIndex >= static_cast<int>(items_.size()))
        {
            candidateIndex = anchorIndex + (direction > 0 ? -1 : 1);
        }
        if (candidateIndex < 0 || candidateIndex >= static_cast<int>(items_.size())
            || candidateIndex == compareTiles_[replacementTile].itemIndex
            || candidateIndex == compareTiles_[replacementTile].pendingItemIndex)
        {
            return false;
        }

        CaptureFocusedCompareView();
        RequestCompareTileImage(replacementTile, candidateIndex, true);
        return true;
    }

    void ViewerWindow::EndCompareSessionPromoteFocused()
    {
        if (focusedCompareTile_ >= compareTiles_.size())
        {
            compareSessionActive_ = false;
            compareTiles_.clear();
            return;
        }
        CaptureFocusedCompareView();
        CompareTileState focusedTile = std::move(compareTiles_[focusedCompareTile_]);
        compareSessionGeneration_++;
        compareAsyncState_->sessionGeneration.store(compareSessionGeneration_, std::memory_order_release);
        for (std::size_t tileIndex = 0; tileIndex < compareAsyncState_->tileRequestGenerations.size(); ++tileIndex)
        {
            compareAsyncState_->tileRequestGenerations[tileIndex].fetch_add(1, std::memory_order_acq_rel);
        }
        compareTiles_.clear();
        compareCandidateMetadata_.clear();
        compareSessionActive_ = false;
        compareMode_ = false;
        ResetViewState();
        rotationQuarterTurns_ = focusedTile.rotationQuarterTurns;
        currentIndex_ = focusedTile.itemIndex;
        d2dCurrentImageBitmap_.Reset();
        d2dCurrentImageIndex_ = -1;
        if (focusedTile.image)
        {
            SetCurrentImageSlot(currentIndex_, focusedTile.image, false);
            currentImage_ = focusedTile.image;
            loading_ = false;
            errorMessage_.clear();
            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            customZoomScale_ = FitScaleForImage(*focusedTile.image, clientRect) * focusedTile.zoomRatio;
            zoomMode_ = ZoomMode::Custom;
            const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
            const int imageWidth = swapDimensions ? focusedTile.image->SourceHeight() : focusedTile.image->SourceWidth();
            const int imageHeight = swapDimensions ? focusedTile.image->SourceWidth() : focusedTile.image->SourceHeight();
            const POINT pan = PanFromImageCenter(imageWidth,
                                                 imageHeight,
                                                 customZoomScale_,
                                                 clientRect,
                                                 focusedTile.imageCenter);
            panOffsetX_ = pan.x;
            panOffsetY_ = pan.y;
        }
        else
        {
            LoadCurrentImageAsync(LoadReason::Navigation);
        }
        NotifyCurrentItemChanged();
        UpdateWindowTitle();
        RequestRepaint();
    }

    void ViewerWindow::DrawCompareSession(ID2D1RenderTarget* renderTarget,
                                          float clientWidth,
                                          float clientHeight)
    {
        if (!renderTarget || compareTiles_.size() < 2 || compareTiles_.size() > 4)
        {
            return;
        }

        RECT clientRect{0, 0, static_cast<LONG>(std::lround(clientWidth)), static_cast<LONG>(std::lround(clientHeight))};
        const UINT dpi = std::max<UINT>(96, GetDpiForWindow(hwnd_));
        const LONG gap = std::clamp<LONG>(static_cast<LONG>((static_cast<double>(dpi) / 96.0) * 16.0), 12, 32);
        const std::vector<RECT> bounds = CompareTileBounds(clientRect, compareTiles_.size(), gap);
        if (bounds.size() != compareTiles_.size())
        {
            return;
        }

        const int previousRotation = rotationQuarterTurns_;
        for (std::size_t tileIndex = 0; tileIndex < compareTiles_.size(); ++tileIndex)
        {
            CompareTileState& tile = compareTiles_[tileIndex];
            const RECT& tileRect = bounds[tileIndex];
            const D2D1_RECT_F tileFrame = D2D1::RectF(
                static_cast<float>(tileRect.left),
                static_cast<float>(tileRect.top),
                static_cast<float>(tileRect.right),
                static_cast<float>(tileRect.bottom));
            const auto displayPixels = ImageForDisplay(tile.itemIndex, tile.image);
            if (displayPixels && (!tile.bitmap || tile.displayPixels.lock() != displayPixels))
            {
                tile.bitmap = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                    renderTarget, *displayPixels);
                tile.displayPixels = displayPixels;
            }

            if (tile.image && tile.bitmap)
            {
                rotationQuarterTurns_ = tile.rotationQuarterTurns;
                const double fitScale = FitScaleForImage(*tile.image, tileRect);
                const double scale = fitScale * tile.zoomRatio;
                const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                const int imageWidth = swapDimensions ? tile.image->SourceHeight() : tile.image->SourceWidth();
                const int imageHeight = swapDimensions ? tile.image->SourceWidth() : tile.image->SourceHeight();
                const POINT pan = PanFromImageCenter(imageWidth,
                                                     imageHeight,
                                                     scale,
                                                     tileRect,
                                                     tile.imageCenter);
                DrawImageBitmap(renderTarget,
                                tile.bitmap.Get(),
                                *tile.image,
                                tileRect,
                                1.0f,
                                static_cast<float>(tile.zoomRatio),
                                static_cast<float>(pan.x),
                                static_cast<float>(pan.y));
            }
            else
            {
                const D2D1_RECT_F placeholder = D2D1::RectF(
                    static_cast<float>(tileRect.left + 20),
                    static_cast<float>(tileRect.top + 20),
                    static_cast<float>(tileRect.right - 20),
                    static_cast<float>(tileRect.bottom - 20));
                const D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(placeholder, 8.0f, 8.0f);
                if (d2dPanelFillBrush_)
                {
                    renderTarget->FillRoundedRectangle(panel, d2dPanelFillBrush_.Get());
                }
                if (d2dPanelBorderBrush_)
                {
                    renderTarget->DrawRoundedRectangle(panel, d2dPanelBorderBrush_.Get(), 1.0f);
                }
                const std::wstring message = tile.errorMessage.empty()
                    ? L"Loading comparison image..."
                    : tile.errorMessage;
                if (d2dInfoFormat_ && d2dMutedTextBrush_)
                {
                    d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                    renderTarget->DrawText(message.c_str(),
                                           static_cast<UINT32>(message.size()),
                                           d2dInfoFormat_.Get(),
                                           placeholder,
                                           d2dMutedTextBrush_.Get());
                    d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                }
            }

            if (d2dMetadataPanelFillBrush_)
            {
                const D2D1_RECT_F labelBackground = D2D1::RectF(
                    static_cast<float>(tileRect.left + 8),
                    static_cast<float>(tileRect.top + 8),
                    static_cast<float>(tileRect.right - 8),
                    static_cast<float>(tileRect.top + 52));
                renderTarget->FillRectangle(labelBackground, d2dMetadataPanelFillBrush_.Get());
            }
            if (tile.itemIndex >= 0 && tile.itemIndex < static_cast<int>(items_.size())
                && d2dBottomInfoFormat_ && d2dTextBrush_)
            {
                const browser::BrowserItem& item = items_[static_cast<std::size_t>(tile.itemIndex)];
                std::wstring label = item.fileName;
                label.append(L"   Rating: ");
                label.append(std::to_wstring(tile.metadata.rating));
                label.append(L"/5");
                if (!tile.metadata.tags.empty())
                {
                    label.append(L"   ");
                    label.append(tile.metadata.tags);
                }
                if (tile.loading)
                {
                    label.append(L"   Loading replacement...");
                }
                else if (!tile.errorMessage.empty() && tile.image)
                {
                    label.append(L"   Replacement failed");
                }
                const D2D1_RECT_F labelRect = D2D1::RectF(
                    static_cast<float>(tileRect.left + 16),
                    static_cast<float>(tileRect.top + 12),
                    static_cast<float>(tileRect.right - 16),
                    static_cast<float>(tileRect.top + 48));
                d2dBottomInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                renderTarget->DrawText(label.c_str(),
                                       static_cast<UINT32>(label.size()),
                                       d2dBottomInfoFormat_.Get(),
                                       labelRect,
                                       d2dTextBrush_.Get(),
                                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }

            if (d2dPanelBorderBrush_)
            {
                const float strokeWidth = tileIndex == focusedCompareTile_ ? 3.0f : 1.0f;
                renderTarget->DrawRectangle(tileFrame,
                                            d2dPanelBorderBrush_.Get(),
                                            strokeWidth);
            }
        }
        rotationQuarterTurns_ = previousRotation;

        if (d2dTextBrush_ && d2dBottomInfoFormat_)
        {
            const std::wstring syncLabel = compareViewsSynchronized_ ? L"Sync: On" : L"Sync: Off";
            const D2D1_RECT_F syncRect = D2D1::RectF(12.0f, clientHeight - 34.0f, 132.0f, clientHeight - 8.0f);
            renderTarget->DrawText(syncLabel.c_str(),
                                   static_cast<UINT32>(syncLabel.size()),
                                   d2dBottomInfoFormat_.Get(),
                                   syncRect,
                                   d2dTextBrush_.Get());
        }
    }

    void ViewerWindow::DrawCompareSessionGdi(HDC dc, const RECT& clientRect)
    {
        if (!dc || compareTiles_.size() < 2 || compareTiles_.size() > 4)
        {
            return;
        }

        const UINT dpi = std::max<UINT>(96, GetDpiForWindow(hwnd_));
        const LONG gap = std::clamp<LONG>(static_cast<LONG>((static_cast<double>(dpi) / 96.0) * 16.0), 12, 32);
        const std::vector<RECT> bounds = CompareTileBounds(clientRect, compareTiles_.size(), gap);
        if (bounds.size() != compareTiles_.size())
        {
            return;
        }

        HFONT font = MenuFontOrDefault(menuFont_);
        HGDIOBJ oldFont = SelectObject(dc, font);
        const COLORREF textColor = TextColor(darkTheme_);
        const COLORREF borderColor = PanelBorderColor(darkTheme_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor);

        HDC labelBackgroundDc = CreateCompatibleDC(dc);
        HBITMAP labelBackgroundBitmap = labelBackgroundDc ? CreateCompatibleBitmap(dc, 1, 1) : nullptr;
        HGDIOBJ oldLabelBackgroundBitmap = labelBackgroundBitmap
            ? SelectObject(labelBackgroundDc, labelBackgroundBitmap)
            : nullptr;
        if (!labelBackgroundBitmap
            || !oldLabelBackgroundBitmap
            || oldLabelBackgroundBitmap == HGDI_ERROR
            || SetPixelV(labelBackgroundDc, 0, 0, PanelFillColor(darkTheme_)) == FALSE)
        {
            if (labelBackgroundBitmap)
            {
                if (oldLabelBackgroundBitmap && oldLabelBackgroundBitmap != HGDI_ERROR)
                {
                    SelectObject(labelBackgroundDc, oldLabelBackgroundBitmap);
                }
                DeleteObject(labelBackgroundBitmap);
            }
            if (labelBackgroundDc)
            {
                DeleteDC(labelBackgroundDc);
            }
            labelBackgroundDc = nullptr;
            labelBackgroundBitmap = nullptr;
            oldLabelBackgroundBitmap = nullptr;
        }
        BLENDFUNCTION labelBackgroundBlend{};
        labelBackgroundBlend.BlendOp = AC_SRC_OVER;
        labelBackgroundBlend.SourceConstantAlpha =
            static_cast<BYTE>(std::lround(MetadataPanelFillAlpha(darkTheme_) * 255.0f));

        for (std::size_t tileIndex = 0; tileIndex < compareTiles_.size(); ++tileIndex)
        {
            CompareTileState& tile = compareTiles_[tileIndex];
            const RECT& tileRect = bounds[tileIndex];
            const int savedDc = SaveDC(dc);
            IntersectClipRect(dc, tileRect.left, tileRect.top, tileRect.right, tileRect.bottom);

            if (tile.image)
            {
                const int previousRotation = rotationQuarterTurns_;
                rotationQuarterTurns_ = tile.rotationQuarterTurns;
                const double scale = FitScaleForImage(*tile.image, tileRect) * tile.zoomRatio;
                const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                const int sourceWidth = tile.image->SourceWidth();
                const int sourceHeight = tile.image->SourceHeight();
                const int imageWidth = swapDimensions ? sourceHeight : sourceWidth;
                const int imageHeight = swapDimensions ? sourceWidth : sourceHeight;
                const POINT pan = PanFromImageCenter(imageWidth, imageHeight, scale, tileRect, tile.imageCenter);
                const int destinationWidth = std::max(1, static_cast<int>(std::lround(imageWidth * scale)));
                const int destinationHeight = std::max(1, static_cast<int>(std::lround(imageHeight * scale)));
                const int x = tileRect.left + ((tileRect.right - tileRect.left - destinationWidth) / 2) + pan.x;
                const int y = tileRect.top + ((tileRect.bottom - tileRect.top - destinationHeight) / 2) + pan.y;
                HDC bitmapDc = CreateCompatibleDC(dc);
                if (bitmapDc)
                {
                    const auto displayPixels = ImageForDisplay(tile.itemIndex, tile.image);
                    HGDIOBJ oldBitmap = SelectObject(bitmapDc, displayPixels->Bitmap());
                    SetStretchBltMode(dc, HALFTONE);
                    SetBrushOrgEx(dc, 0, 0, nullptr);
                    if (rotationQuarterTurns_ == 0)
                    {
                        StretchBlt(dc, x, y, destinationWidth, destinationHeight,
                                   bitmapDc, 0, 0, sourceWidth, sourceHeight, SRCCOPY);
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
                        PlgBlt(dc, destination, bitmapDc, 0, 0, sourceWidth, sourceHeight, nullptr, 0, 0);
                    }
                    SelectObject(bitmapDc, oldBitmap);
                    DeleteDC(bitmapDc);
                }
                rotationQuarterTurns_ = previousRotation;
            }
            else
            {
                std::wstring message = tile.errorMessage.empty()
                    ? L"Loading comparison image..."
                    : tile.errorMessage;
                RECT placeholder = tileRect;
                InflateRect(&placeholder, -20, -20);
                DrawTextW(dc, message.c_str(), static_cast<int>(message.size()), &placeholder,
                          DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            if (tile.itemIndex >= 0 && tile.itemIndex < static_cast<int>(items_.size()))
            {
                const browser::BrowserItem& item = items_[static_cast<std::size_t>(tile.itemIndex)];
                std::wstring label = item.fileName + L"   Rating: "
                    + std::to_wstring(tile.metadata.rating) + L"/5";
                if (!tile.metadata.tags.empty())
                {
                    label.append(L"   " + tile.metadata.tags);
                }
                if (tile.loading)
                {
                    label.append(L"   Loading replacement...");
                }
                else if (!tile.errorMessage.empty() && tile.image)
                {
                    label.append(L"   Replacement failed");
                }
                const RECT labelBackground{
                    tileRect.left + 8, tileRect.top + 8, tileRect.right - 8, tileRect.top + 52};
                if (labelBackgroundDc && labelBackgroundBitmap)
                {
                    AlphaBlend(dc,
                               labelBackground.left,
                               labelBackground.top,
                               labelBackground.right - labelBackground.left,
                               labelBackground.bottom - labelBackground.top,
                               labelBackgroundDc,
                               0,
                               0,
                               1,
                               1,
                               labelBackgroundBlend);
                }
                RECT labelRect{tileRect.left + 12, tileRect.top + 12, tileRect.right - 12, tileRect.top + 48};
                DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            HPEN borderPen = CreatePen(PS_SOLID, tileIndex == focusedCompareTile_ ? 3 : 1, borderColor);
            HGDIOBJ oldPen = SelectObject(dc, borderPen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, tileRect.left, tileRect.top, tileRect.right, tileRect.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(borderPen);
            RestoreDC(dc, savedDc);
        }

        const std::wstring syncLabel = compareViewsSynchronized_ ? L"Sync: On" : L"Sync: Off";
        RECT syncRect{clientRect.left + 12, clientRect.bottom - 34, clientRect.left + 132, clientRect.bottom - 8};
        DrawTextW(dc, syncLabel.c_str(), static_cast<int>(syncLabel.size()), &syncRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (labelBackgroundBitmap)
        {
            SelectObject(labelBackgroundDc, oldLabelBackgroundBitmap);
            DeleteObject(labelBackgroundBitmap);
        }
        if (labelBackgroundDc)
        {
            DeleteDC(labelBackgroundDc);
        }
        SelectObject(dc, oldFont);
    }

    CompareDirection ViewerWindow::ResolveCompareDirection(CompareDirection preferred) const noexcept
    {
        if (CompareIndexForDirection(preferred) >= 0)
        {
            return preferred;
        }

        const CompareDirection alternate = preferred == CompareDirection::Next
            ? CompareDirection::Previous
            : CompareDirection::Next;
        if (CompareIndexForDirection(alternate) >= 0)
        {
            return alternate;
        }

        return preferred;
    }

    int ViewerWindow::CompareIndexForDirection(CompareDirection direction) const noexcept
    {
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return -1;
        }

        const int compareIndex = currentIndex_ + static_cast<int>(direction);
        return compareIndex >= 0 && compareIndex < static_cast<int>(items_.size())
            ? compareIndex
            : -1;
    }

    int ViewerWindow::ActiveCompareIndex() const noexcept
    {
        return CompareIndexForDirection(ResolveCompareDirection(compareDirection_));
    }

    void ViewerWindow::ExitCompareForNavigation()
    {
        if (compareSessionActive_)
        {
            EndCompareSessionPromoteFocused();
            return;
        }
        if (compareTiles_.empty())
        {
            return;
        }

        compareSessionGeneration_++;
        compareAsyncState_->sessionGeneration.store(compareSessionGeneration_, std::memory_order_release);
        for (std::size_t tileIndex = 0; tileIndex < compareAsyncState_->tileRequestGenerations.size(); ++tileIndex)
        {
            compareAsyncState_->tileRequestGenerations[tileIndex].fetch_add(1, std::memory_order_acq_rel);
        }
        compareTiles_.clear();
        compareCandidateMetadata_.clear();
        compareMode_ = false;
        UpdateWindowTitle();
    }

    void ViewerWindow::ToggleCompareMode()
    {
        if (HasCompareSession())
        {
            if (compareTiles_.size() >= 3)
            {
                EndCompareSessionPromoteFocused();
            }
            else
            {
                SetCompareMode(!compareSessionActive_, compareDirection_);
            }
            return;
        }
        SetCompareMode(!compareMode_, compareDirection_);
    }

    void ViewerWindow::ActivateComparedImage()
    {
        if (compareSessionActive_ && !compareTiles_.empty())
        {
            CaptureFocusedCompareView();
            if (focusedCompareTile_ > 0)
            {
                std::rotate(compareTiles_.begin(),
                            compareTiles_.begin() + static_cast<std::ptrdiff_t>(focusedCompareTile_),
                            compareTiles_.end());
                focusedCompareTile_ = 0;
            }
            currentIndex_ = compareTiles_.front().itemIndex;
            FocusCompareTileInternal(0);
            NotifyCurrentItemChanged();
            RequestRepaint();
            return;
        }
        if (!compareMode_)
        {
            return;
        }

        const CompareDirection activeDirection = ResolveCompareDirection(compareDirection_);
        if (CompareIndexForDirection(activeDirection) < 0)
        {
            return;
        }

        compareDirection_ = activeDirection == CompareDirection::Next
            ? CompareDirection::Previous
            : CompareDirection::Next;
        Navigate(activeDirection == CompareDirection::Next ? +1 : -1);
    }
}
