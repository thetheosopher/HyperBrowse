#include "viewer/ViewerWindowInternal.h"

namespace
{
    constexpr bool kEnableFullImagePrefetch = false;

    struct DecodedImageResult
    {
        std::uint64_t requestId{};
        std::uint64_t navigationGeneration{};
        int index{-1};
        std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> image;
        std::wstring errorMessage;
    };

    struct PrefetchedImageResult
    {
        std::uint64_t navigationGeneration{};
        int index{-1};
        std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> image;
        std::wstring errorMessage;
    };

    struct MetadataReadyResult
    {
        std::uint64_t requestId{};
        int index{-1};
        std::shared_ptr<const hyperbrowse::services::ImageMetadata> metadata;
        std::wstring text;
    };

    std::wstring BuildMetadataOverlayText(const hyperbrowse::browser::BrowserItem& item,
                                          const hyperbrowse::services::ImageMetadata* metadata,
                                          const std::wstring& errorMessage)
    {
        std::wstring text = hyperbrowse::services::FormatImageInfoContent(
            item, std::filesystem::path(item.filePath).parent_path().wstring());
        text.append(L"\r\n\r\n");

        if (metadata)
        {
            const std::wstring expanded = hyperbrowse::services::FormatImageInfoExpanded(*metadata);
            if (!expanded.empty())
            {
                text.append(expanded);
                return text;
            }
        }

        if (!errorMessage.empty())
        {
            text.append(errorMessage);
            return text;
        }

        text.append(L"No embedded EXIF, IPTC, or XMP metadata is available for this image.");
        return text;
    }

    using NavigationCursorPolygon = std::array<POINT, 7>;

    NavigationCursorPolygon MakeBaseNavigationCursorPolygon(int width, int height)
    {
        const int paddingX = std::max(3, width / 8);
        const int paddingY = std::max(4, height / 8);
        const int centerY = height / 2;
        const int shaftHalfHeight = std::max(3, height / 10);
        const int headBaseX = std::max(paddingX + 4, width * 7 / 16);
        const int tailX = width - paddingX - 1;

        return {{
            POINT{paddingX, centerY},
            POINT{headBaseX, paddingY},
            POINT{headBaseX, centerY - shaftHalfHeight},
            POINT{tailX, centerY - shaftHalfHeight},
            POINT{tailX, centerY + shaftHalfHeight},
            POINT{headBaseX, centerY + shaftHalfHeight},
            POINT{headBaseX, height - paddingY - 1},
        }};
    }

    NavigationCursorPolygon MirrorNavigationCursorPolygon(const NavigationCursorPolygon& polygon, int width)
    {
        NavigationCursorPolygon mirrored = polygon;
        for (POINT& point : mirrored)
        {
            point.x = (width - 1) - point.x;
        }

        return mirrored;
    }

    bool PointInPolygon(const NavigationCursorPolygon& polygon, double x, double y)
    {
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const double currentX = static_cast<double>(polygon[i].x);
            const double currentY = static_cast<double>(polygon[i].y);
            const double previousX = static_cast<double>(polygon[j].x);
            const double previousY = static_cast<double>(polygon[j].y);
            const bool intersects = ((currentY > y) != (previousY > y))
                && (x < (((previousX - currentX) * (y - currentY)) / (previousY - currentY) + currentX));
            if (intersects)
            {
                inside = !inside;
            }
        }

        return inside;
    }

    HCURSOR CreateViewerNavigationCursor(bool previous)
    {
        const int width = std::max(22, (GetSystemMetrics(SM_CXCURSOR) * 7) / 10);
        const int height = std::max(22, (GetSystemMetrics(SM_CYCURSOR) * 7) / 10);

        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = width;
        header.bV5Height = -height;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;

        void* bitmapBits = nullptr;
        HDC screenDc = GetDC(nullptr);
        HBITMAP colorBitmap = CreateDIBSection(screenDc,
                                               reinterpret_cast<BITMAPINFO*>(&header),
                                               DIB_RGB_COLORS,
                                               &bitmapBits,
                                               nullptr,
                                               0);
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }
        if (!colorBitmap || !bitmapBits)
        {
            if (colorBitmap)
            {
                DeleteObject(colorBitmap);
            }
            return nullptr;
        }

        HBITMAP maskBitmap = CreateBitmap(width, height, 1, 1, nullptr);
        if (!maskBitmap)
        {
            DeleteObject(colorBitmap);
            return nullptr;
        }

        NavigationCursorPolygon polygon = MakeBaseNavigationCursorPolygon(width, height);
        if (!previous)
        {
            polygon = MirrorNavigationCursorPolygon(polygon, width);
        }

        std::vector<std::uint8_t> fillMask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (PointInPolygon(polygon, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5))
                {
                    fillMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 1;
                }
            }
        }

        auto* pixels = static_cast<std::uint32_t*>(bitmapBits);
        constexpr std::uint32_t fillColor = 0xFFF7F9FCu;
        constexpr std::uint32_t outlineColor = 0xFF11161Bu;
        std::fill(pixels, pixels + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height)), 0u);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (fillMask[index] != 0)
                {
                    pixels[index] = fillColor;
                }
            }
        }

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (fillMask[index] != 0)
                {
                    continue;
                }

                bool touchesFilledPixel = false;
                for (int offsetY = -1; offsetY <= 1 && !touchesFilledPixel; ++offsetY)
                {
                    for (int offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        const int neighborX = x + offsetX;
                        const int neighborY = y + offsetY;
                        if (neighborX < 0 || neighborY < 0 || neighborX >= width || neighborY >= height)
                        {
                            continue;
                        }

                        const std::size_t neighborIndex = static_cast<std::size_t>(neighborY) * static_cast<std::size_t>(width)
                            + static_cast<std::size_t>(neighborX);
                        if (fillMask[neighborIndex] != 0)
                        {
                            touchesFilledPixel = true;
                            break;
                        }
                    }
                }

                if (touchesFilledPixel)
                {
                    pixels[index] = outlineColor;
                }
            }
        }

        ICONINFO cursorInfo{};
        cursorInfo.fIcon = FALSE;
        cursorInfo.xHotspot = static_cast<DWORD>(polygon[0].x);
        cursorInfo.yHotspot = static_cast<DWORD>(polygon[0].y);
        cursorInfo.hbmMask = maskBitmap;
        cursorInfo.hbmColor = colorBitmap;

        HCURSOR cursor = CreateIconIndirect(&cursorInfo);
        DeleteObject(maskBitmap);
        DeleteObject(colorBitmap);
        return cursor;
    }
}

namespace hyperbrowse::viewer
{

    void ViewerWindow::PrepareForImageChange(bool keepDisplayedImage)
    {
        DrainHistogramResults(hwnd_);
        histogram_ = {};
        histogramLoading_ = true;
        errorMessage_.clear();
        loading_ = true;
        preserveDisplayedImageWhileLoading_ = keepDisplayedImage && currentImage_;
        if (preserveDisplayedImageWhileLoading_)
        {
            if (hwnd_)
            {
                RequestInfoOverlayRepaint();
            }
            return;
        }

        ResetViewState(zoomMode_ == ZoomMode::FitHeight || zoomMode_ == ZoomMode::FitWidth);
        UpdateWindowTitle();
        NotifyZoomChanged(0);
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::ResetCachedImageSlots()
    {
        currentImage_.reset();
        d2dCurrentImageBitmap_.Reset();
        d2dCompareImageBitmap_.Reset();
        d2dCurrentImageIndex_ = -1;
        d2dCompareImageIndex_ = -1;
        currentSlot_ = {};
        previousSlot_ = {};
        nextSlot_ = {};
        ClearCurrentMetadata();
    }

    void ViewerWindow::ResetPrefetchStatistics()
    {
        prefetchRequestCount_.store(0, std::memory_order_release);
        prefetchCompletedCount_.store(0, std::memory_order_release);
        prefetchCancelledCount_.store(0, std::memory_order_release);
        prefetchHitCount_.store(0, std::memory_order_release);
        prefetchMissCount_.store(0, std::memory_order_release);
    }

    int ViewerWindow::BasePrefetchRadius() const noexcept
    {
        return util::ResolvePrefetchDepth(resourceProfile_, prefetchDepthOverride_);
    }

    int ViewerWindow::EffectivePrefetchRadius() const noexcept
    {
        return memoryPressureActive_ ? 1 : BasePrefetchRadius();
    }

    void ViewerWindow::SetCurrentImageSlot(int index,
                                           std::shared_ptr<const cache::CachedThumbnail> image,
                                           bool prefetched)
    {
        currentSlot_.index = index;
        currentSlot_.image = std::move(image);
        currentSlot_.prefetched = prefetched;
        currentImage_ = currentSlot_.image;
        ClearCurrentMetadata();
        RefreshWindowFitForCurrentImage();
        RefreshOverlayContent();
    }

    void ViewerWindow::ClearCurrentMetadata(bool invalidateRequest)
    {
        if (invalidateRequest)
        {
            metadataRequestId_.fetch_add(1, std::memory_order_acq_rel);
        }

        metadataLoading_ = false;
        currentMetadataIndex_ = -1;
        currentMetadata_.reset();
        currentMetadataText_.clear();
    }

    void ViewerWindow::LoadMetadataAsyncForIndex(int index)
    {
        if (!fullMetadataVisible_)
        {
            return;
        }

        if (index < 0 || index >= static_cast<int>(items_.size()))
        {
            ClearCurrentMetadata();
            return;
        }

        currentMetadataIndex_ = index;
        currentMetadata_.reset();
        currentMetadataText_.clear();
        metadataLoading_ = true;

        const browser::BrowserItem item = items_[static_cast<std::size_t>(index)];
        const std::uint64_t requestId = metadataRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        asyncState_->targetWindow.store(hwnd_, std::memory_order_release);

        if (!backgroundExecutor_
            || !backgroundExecutor_->Post([asyncState = asyncState_, metadataRequestId = &metadataRequestId_, item, index, requestId]()
            {
                std::wstring errorMessage;
                std::shared_ptr<const hyperbrowse::services::ImageMetadata> metadata;
                try
                {
                    metadata = hyperbrowse::services::ExtractImageMetadata(item, &errorMessage);
                }
                catch (const std::exception&)
                {
                    errorMessage = L"Metadata extraction failed unexpectedly.";
                }
                catch (...)
                {
                    errorMessage = L"Metadata extraction failed unexpectedly.";
                }

                if (asyncState->shutdown.load(std::memory_order_acquire)
                    || metadataRequestId->load(std::memory_order_acquire) != requestId)
                {
                    return;
                }

                auto update = std::make_unique<MetadataReadyResult>();
                update->requestId = requestId;
                update->index = index;
                update->metadata = std::move(metadata);
                update->text = BuildMetadataOverlayText(item, update->metadata.get(), errorMessage);

                const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
                if (!targetWindow || !PostMessageW(targetWindow, ViewerWindow::kMetadataReadyMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            }))
        {
            metadataLoading_ = false;
            currentMetadataText_ = BuildMetadataOverlayText(item, nullptr, L"Metadata loading is unavailable.");
        }

        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::ReapCompletedBackgroundTasks()
    {
        // The bounded executor handles its own backlog; nothing to reap here.
    }

    void ViewerWindow::WaitForBackgroundTasks()
    {
        // Destroying the executor joins its worker threads, ensuring no in-flight
        // decode can outlive the ViewerWindow.
        backgroundExecutor_.reset();
    }

    void ViewerWindow::LoadCurrentImageAsync(LoadReason reason)
    {
        PrepareForImageChange(reason == LoadReason::Navigation && currentImage_ != nullptr);
        pendingLoadReason_ = reason;
        pendingLoadStartedAt_ = std::chrono::steady_clock::now();
        pendingLoadActive_ = true;

        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            loading_ = false;
            pendingLoadActive_ = false;
            errorMessage_ = L"The viewer does not have a valid image selection.";
            return;
        }

        const int selectedIndex = currentIndex_;
        const browser::BrowserItem item = items_[static_cast<std::size_t>(selectedIndex)];
        const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::uint64_t requestId = asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
        asyncState_->targetWindow.store(hwnd_, std::memory_order_release);
        util::LogInfo(L"ViewerWindow::LoadCurrentImageAsync requestId="
            + std::to_wstring(requestId)
            + L", generation=" + std::to_wstring(navigationGeneration)
            + L", index=" + std::to_wstring(selectedIndex)
            + L", hwnd=" + FormatWindowHandle(hwnd_)
            + L", file=" + item.fileName);

        const auto cacheKey = MakeViewerFullImageCacheKey(item);
        if (const auto cachedImage = ViewerFullImageCache().Find(cacheKey))
        {
            util::LogInfo(L"ViewerWindow full-image cache hit for " + item.fileName);
            if (pendingLoadActive_)
            {
                const double elapsedMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - pendingLoadStartedAt_).count();
                util::RecordTiming(
                    pendingLoadReason_ == LoadReason::Open ? L"viewer.open" : L"viewer.navigation",
                    elapsedMs);
                pendingLoadActive_ = false;
            }

            SetCurrentImageSlot(selectedIndex, cachedImage, true);
            errorMessage_.clear();
            loading_ = false;
            BeginTransitionFromPending();
            ScheduleAdjacentPrefetch(navigationGeneration);
            if (hwnd_)
            {
                RequestRepaint();
            }
            return;
        }

        const std::shared_ptr<AsyncState> asyncState = asyncState_;
        ReapCompletedBackgroundTasks();
        if (backgroundExecutor_)
        {
            if (!backgroundExecutor_->Post([asyncState, item, selectedIndex, requestId, navigationGeneration]()
            {
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
                    || asyncState->activeRequestId.load(std::memory_order_acquire) != requestId)
                {
                    return;
                }

                auto update = std::make_unique<DecodedImageResult>();
                update->requestId = requestId;
                update->navigationGeneration = navigationGeneration;
                update->index = selectedIndex;
                update->image = std::move(image);
                update->errorMessage = std::move(errorMessage);

                const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
                if (!targetWindow || !PostMessageW(targetWindow, kDecodedImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            }))
            {
                loading_ = false;
                pendingLoadActive_ = false;
                errorMessage_ = L"Image loading is unavailable.";
                RefreshOverlayContent();
                if (hwnd_)
                {
                    RequestRepaint();
                }
            }
        }
    }

    void ViewerWindow::Navigate(int delta)
    {
        slideshowAdvancePending_ = false;
        ExitCompareForNavigation();

        if (items_.empty())
        {
            return;
        }

        const int itemCount = static_cast<int>(items_.size());
        int nextIndex = currentIndex_ + delta;
        bool wrapped = false;
        if (nextIndex < 0)
        {
            nextIndex = itemCount - 1;
            wrapped = true;
        }
        else if (nextIndex >= itemCount)
        {
            nextIndex = 0;
            wrapped = true;
        }

        if (nextIndex == currentIndex_)
        {
            return;
        }

        if (wrapped)
        {
            ShowWraparoundMessage(delta > 0);
        }

        NavigateToIndex(nextIndex, delta > 0);
    }

    void ViewerWindow::PromptForItemNumber()
    {
        if (items_.empty())
        {
            MessageBeep(MB_ICONWARNING);
            return;
        }

        const int itemCount = static_cast<int>(items_.size());
        const std::wstring title = L"Go to File";
        const std::wstring instruction = L"Enter a file number from 1 to " + std::to_wstring(itemCount) + L".";
        std::wstring candidate = std::to_wstring(currentIndex_ + 1);
        while (ui::PromptForSingleLineText(hwnd_,
                                           instance_,
                                           appTextSize_,
                                           darkTheme_,
                                           title,
                                           instruction,
                                           L"Go",
                                           candidate,
                                           0,
                                           -1,
                                           &candidate))
        {
            int targetIndex = -1;
            if (!ui::TryParseItemNumber(candidate, itemCount, &targetIndex))
            {
                MessageBeep(MB_ICONWARNING);
                MessageBoxW(hwnd_, instruction.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
                continue;
            }

            NavigateToIndex(targetIndex, targetIndex > currentIndex_);
            return;
        }
    }

    void ViewerWindow::NavigateToIndex(int targetIndex, bool forward, bool slideshowNavigation)
    {
        slideshowAdvancePending_ = false;
        ExitCompareForNavigation();

        if (targetIndex < 0 || targetIndex >= static_cast<int>(items_.size()) || targetIndex == currentIndex_)
        {
            return;
        }

        const int nextIndex = targetIndex;
        QueueTransitionFromCurrent(forward, slideshowNavigation);

        if (forward)
        {
            if (nextSlot_.index == nextIndex && nextSlot_.image)
            {
                previousSlot_ = currentSlot_;
                SetCurrentImageSlot(nextSlot_.index, nextSlot_.image, true);
                nextSlot_ = {};
                currentIndex_ = nextIndex;
                NotifyCurrentItemChanged();
                prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
                util::IncrementCounter(L"viewer.prefetch.hit");
                util::RecordTiming(L"viewer.navigation", 0.0);
                PrepareForImageChange();
                loading_ = false;
                errorMessage_.clear();
                currentImage_ = currentSlot_.image;
                RefreshOverlayContent();
                BeginTransitionFromPending();
                const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                ScheduleAdjacentPrefetch(navigationGeneration);
                if (hwnd_)
                {
                    RequestRepaint();
                }
                return;
            }

            previousSlot_ = currentSlot_;
            nextSlot_ = {};
        }
        else
        {
            if (previousSlot_.index == nextIndex && previousSlot_.image)
            {
                nextSlot_ = currentSlot_;
                SetCurrentImageSlot(previousSlot_.index, previousSlot_.image, true);
                previousSlot_ = {};
                currentIndex_ = nextIndex;
                NotifyCurrentItemChanged();
                prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
                util::IncrementCounter(L"viewer.prefetch.hit");
                util::RecordTiming(L"viewer.navigation", 0.0);
                PrepareForImageChange();
                loading_ = false;
                errorMessage_.clear();
                currentImage_ = currentSlot_.image;
                RefreshOverlayContent();
                BeginTransitionFromPending();
                const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                ScheduleAdjacentPrefetch(navigationGeneration);
                if (hwnd_)
                {
                    RequestRepaint();
                }
                return;
            }

            nextSlot_ = currentSlot_;
            previousSlot_ = {};
        }

        prefetchMissCount_.fetch_add(1, std::memory_order_acq_rel);
        util::IncrementCounter(L"viewer.prefetch.miss");
        currentIndex_ = nextIndex;
        NotifyCurrentItemChanged();
        LoadCurrentImageAsync(LoadReason::Navigation);
    }

    int ViewerWindow::NavigationDeltaForPoint(POINT point) const noexcept
    {
        if (!hwnd_ || items_.size() < 2)
        {
            return 0;
        }

        RECT clientRect{};
        if (GetClientRect(hwnd_, &clientRect) == FALSE)
        {
            return 0;
        }

        const int clientWidth = clientRect.right - clientRect.left;
        if (clientWidth < 8)
        {
            return 0;
        }

        const int navigationZoneWidth = std::max(1, clientWidth / 8);
        const int leftBoundary = clientRect.left + navigationZoneWidth;
        const int rightBoundary = clientRect.right - navigationZoneWidth;
        if (point.x < leftBoundary)
        {
            return -1;
        }

        if (point.x >= rightBoundary)
        {
            return +1;
        }

        return 0;
    }

    bool ViewerWindow::SetNavigationCursorForPoint(POINT point)
    {
        if (compareSessionActive_)
        {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return true;
        }

        const int navigationDelta = NavigationDeltaForPoint(point);
        if (navigationDelta < 0)
        {
            if (!previousNavigationCursor_)
            {
                previousNavigationCursor_ = CreateViewerNavigationCursor(true);
            }

            if (previousNavigationCursor_)
            {
                SetCursor(previousNavigationCursor_);
                return true;
            }
        }
        else if (navigationDelta > 0)
        {
            if (!nextNavigationCursor_)
            {
                nextNavigationCursor_ = CreateViewerNavigationCursor(false);
            }

            if (nextNavigationCursor_)
            {
                SetCursor(nextNavigationCursor_);
                return true;
            }
        }

        return false;
    }

    void ViewerWindow::ScheduleAdjacentPrefetch(std::uint64_t navigationGeneration)
    {
        const int prefetchRadius = EffectivePrefetchRadius();
        const bool slideshowAheadPrefetch = slideshowActive_ && items_.size() > 1;
        const bool comparePrefetch = compareMode_ && items_.size() > 1;
        const bool genericAdjacentPrefetch = kEnableFullImagePrefetch || prefetchRadius > 0;
        if (!genericAdjacentPrefetch && !slideshowAheadPrefetch && !comparePrefetch)
        {
            (void)navigationGeneration;
            return;
        }

        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            previousSlot_ = {};
            nextSlot_ = {};
            return;
        }

        if (slideshowAheadPrefetch && !comparePrefetch)
        {
            const int itemCount = static_cast<int>(items_.size());
            const int nextIndex = (currentIndex_ + 1) % itemCount;
            if (nextSlot_.index == nextIndex && nextSlot_.image)
            {
                slideshowNextPrefetchIndex_ = -1;
                slideshowNextPrefetchGeneration_ = 0;
            }

            if (slideshowNextPrefetchIndex_ != nextIndex
                || slideshowNextPrefetchGeneration_ != navigationGeneration)
            {
                nextSlot_ = {};
                StartPrefetch(nextIndex, navigationGeneration);
            }

            for (int offset = 2; offset <= prefetchRadius && offset < itemCount; ++offset)
            {
                const int extraIndex = (currentIndex_ + offset) % itemCount;
                StartPrefetch(extraIndex, navigationGeneration);
            }
            return;
        }

        for (int offset = 1; offset <= prefetchRadius; ++offset)
        {
            const int previousIndex = currentIndex_ - offset;
            if (previousIndex >= 0)
            {
                if (offset == 1)
                {
                    if (previousSlot_.index != previousIndex || !previousSlot_.image)
                    {
                        previousSlot_ = {};
                        StartPrefetch(previousIndex, navigationGeneration);
                    }
                }
                else
                {
                    StartPrefetch(previousIndex, navigationGeneration);
                }
            }

            const int nextIndex = currentIndex_ + offset;
            if (nextIndex < static_cast<int>(items_.size()))
            {
                if (offset == 1)
                {
                    if (nextSlot_.index != nextIndex || !nextSlot_.image)
                    {
                        nextSlot_ = {};
                        StartPrefetch(nextIndex, navigationGeneration);
                    }
                }
                else
                {
                    StartPrefetch(nextIndex, navigationGeneration);
                }
            }
        }

        if (currentIndex_ - 1 < 0)
        {
            previousSlot_ = {};
        }
        if (currentIndex_ + 1 >= static_cast<int>(items_.size()))
        {
            nextSlot_ = {};
        }
    }

    void ViewerWindow::StartPrefetch(int index, std::uint64_t navigationGeneration)
    {
        if (index < 0 || index >= static_cast<int>(items_.size()))
        {
            return;
        }

        const browser::BrowserItem item = items_[static_cast<std::size_t>(index)];
        const std::shared_ptr<AsyncState> asyncState = asyncState_;
        const bool slideshowNextPrefetch = slideshowActive_
            && items_.size() > 1
            && index == ((currentIndex_ + 1) % static_cast<int>(items_.size()));
        if (slideshowNextPrefetch)
        {
            slideshowNextPrefetchIndex_ = index;
            slideshowNextPrefetchGeneration_ = navigationGeneration;
        }

        prefetchRequestCount_.fetch_add(1, std::memory_order_acq_rel);
        util::IncrementCounter(L"viewer.prefetch.request");

        const auto cacheKey = MakeViewerFullImageCacheKey(item);
        if (const auto cachedImage = ViewerFullImageCache().Find(cacheKey))
        {
            auto update = std::make_unique<PrefetchedImageResult>();
            update->navigationGeneration = navigationGeneration;
            update->index = index;
            update->image = cachedImage;

            const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
            if (!targetWindow || !PostMessageW(targetWindow, ViewerWindow::kPrefetchImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
            {
                if (slideshowNextPrefetch)
                {
                    slideshowNextPrefetchIndex_ = -1;
                    slideshowNextPrefetchGeneration_ = 0;
                }
                return;
            }

            update.release();
            return;
        }

        ReapCompletedBackgroundTasks();
        if (!backgroundExecutor_)
        {
            return;
        }
        if (!backgroundExecutor_->Post([asyncState, item, index, navigationGeneration]()
        {
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

            if (asyncState->shutdown.load(std::memory_order_acquire))
            {
                return;
            }

            if (asyncState->navigationGeneration.load(std::memory_order_acquire) != navigationGeneration)
            {
                return;
            }

            auto update = std::make_unique<PrefetchedImageResult>();
            update->navigationGeneration = navigationGeneration;
            update->index = index;
            update->image = std::move(image);
            update->errorMessage = std::move(errorMessage);

            const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
            if (!targetWindow || !PostMessageW(targetWindow, ViewerWindow::kPrefetchImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
            {
                return;
            }

            update.release();
        }) && slideshowNextPrefetch)
        {
            slideshowNextPrefetchIndex_ = -1;
            slideshowNextPrefetchGeneration_ = 0;
        }
    }

    void ViewerWindow::LogPrefetchStats() const
    {
        const std::uint64_t hits = prefetchHitCount_.load(std::memory_order_acquire);
        const std::uint64_t misses = prefetchMissCount_.load(std::memory_order_acquire);
        const std::uint64_t requests = prefetchRequestCount_.load(std::memory_order_acquire);
        const std::uint64_t completed = prefetchCompletedCount_.load(std::memory_order_acquire);
        const std::uint64_t cancelled = prefetchCancelledCount_.load(std::memory_order_acquire);
        const double hitRate = (hits + misses) == 0
            ? 0.0
            : (static_cast<double>(hits) * 100.0) / static_cast<double>(hits + misses);

        util::LogInfo(L"Viewer prefetch stats: requests="
            + std::to_wstring(requests)
            + L", completed=" + std::to_wstring(completed)
            + L", cancelled=" + std::to_wstring(cancelled)
            + L", hits=" + std::to_wstring(hits)
            + L", misses=" + std::to_wstring(misses)
            + L", hitRate=" + std::to_wstring(hitRate) + L"%");
    }

    void ViewerWindow::AdvanceSlideshow()
    {
        if (items_.size() < 2)
        {
            return;
        }

        if (pendingLoadActive_ || transitionActive_)
        {
            return;
        }

        const int nextIndex = (currentIndex_ + 1) % static_cast<int>(items_.size());
        if (nextSlot_.index == nextIndex && nextSlot_.image)
        {
            QueueTransitionFromCurrent(true, true);
            previousSlot_ = currentSlot_;
            SetCurrentImageSlot(nextSlot_.index, nextSlot_.image, true);
            nextSlot_ = {};
            currentIndex_ = nextIndex;
            NotifyCurrentItemChanged();
            prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.hit");
            util::RecordTiming(L"viewer.navigation", 0.0);
            PrepareForImageChange();
            loading_ = false;
            errorMessage_.clear();
            currentImage_ = currentSlot_.image;
            RefreshOverlayContent();
            slideshowAdvancePending_ = false;
            const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            BeginTransitionFromPending();
            ScheduleAdjacentPrefetch(navigationGeneration);
            if (hwnd_)
            {
                RequestRepaint();
            }
            return;
        }

        const std::uint64_t activeGeneration = asyncState_->navigationGeneration.load(std::memory_order_acquire);
        if (slideshowNextPrefetchIndex_ == nextIndex && slideshowNextPrefetchGeneration_ == activeGeneration)
        {
            slideshowAdvancePending_ = true;
            return;
        }

        slideshowAdvancePending_ = false;

        if (currentIndex_ >= static_cast<int>(items_.size()) - 1)
        {
            QueueTransitionFromCurrent(true, true);
            currentIndex_ = 0;
            NotifyCurrentItemChanged();
            LoadCurrentImageAsync(LoadReason::Navigation);
            return;
        }

        NavigateToIndex(nextIndex, true, true);
    }

    void ViewerWindow::ShowWraparoundMessage(bool forward)
    {
        ShowNavigationMessage(forward ? L"Wrapped to first image" : L"Wrapped to last image");
    }

    void ViewerWindow::NavigateToBoundary(bool first)
    {
        ExitCompareForNavigation();
        if (items_.empty())
        {
            return;
        }

        const int targetIndex = first ? 0 : static_cast<int>(items_.size()) - 1;
        ShowNavigationMessage(first ? L"First image" : L"Last image");
        if (targetIndex == currentIndex_)
        {
            return;
        }

        NavigateToIndex(targetIndex, targetIndex > currentIndex_);
    }

    void ViewerWindow::ShowNavigationMessage(std::wstring message)
    {
        wraparoundMessage_ = std::move(message);
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        if (wraparoundTimerId_ != 0)
        {
            KillTimer(hwnd_, wraparoundTimerId_);
        }

        wraparoundTimerId_ = SetTimer(hwnd_, kWraparoundTimerId, kWraparoundMessageDurationMs, nullptr);
        if (wraparoundTimerId_ == 0)
        {
            wraparoundMessage_.clear();
            return;
        }

        RequestRepaint();
    }

    void ViewerWindow::ClearWraparoundMessage()
    {
        if (hwnd_ && wraparoundTimerId_ != 0)
        {
            KillTimer(hwnd_, wraparoundTimerId_);
        }

        wraparoundTimerId_ = 0;
        wraparoundMessage_.clear();
    }

    LRESULT ViewerWindow::HandleDecodedImageMessage(LPARAM lParam)
    {
        std::unique_ptr<DecodedImageResult> update(reinterpret_cast<DecodedImageResult*>(lParam));
        if (!update)
        {
            return 0;
        }

        if (update->requestId != asyncState_->activeRequestId.load(std::memory_order_acquire))
        {
            util::LogInfo(L"ViewerWindow dropping decoded image for stale requestId="
                + std::to_wstring(update->requestId)
                + L", activeRequestId="
                + std::to_wstring(asyncState_->activeRequestId.load(std::memory_order_acquire)));
            return 0;
        }

        if (update->navigationGeneration != asyncState_->navigationGeneration.load(std::memory_order_acquire)
            || update->index != currentIndex_)
        {
            util::LogInfo(L"ViewerWindow dropping decoded image for stale navigation/index. requestGeneration="
                + std::to_wstring(update->navigationGeneration)
                + L", activeGeneration="
                + std::to_wstring(asyncState_->navigationGeneration.load(std::memory_order_acquire))
                + L", decodedIndex=" + std::to_wstring(update->index)
                + L", currentIndex=" + std::to_wstring(currentIndex_));
            return 0;
        }

        util::LogInfo(L"ViewerWindow applying decoded image requestId="
            + std::to_wstring(update->requestId)
            + L", index=" + std::to_wstring(update->index)
            + L", hasImage=" + std::to_wstring(update->image != nullptr));

        if (pendingLoadActive_)
        {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - pendingLoadStartedAt_).count();
            util::RecordTiming(
                pendingLoadReason_ == LoadReason::Open ? L"viewer.open" : L"viewer.navigation",
                elapsedMs);
            pendingLoadActive_ = false;
        }

        const bool deferredSwap = preserveDisplayedImageWhileLoading_;
        preserveDisplayedImageWhileLoading_ = false;
        if (deferredSwap && !update->image && currentImage_)
        {
            loading_ = false;
            errorMessage_ = update->errorMessage.empty() ? L"Unable to load the requested image." : std::move(update->errorMessage);
            RefreshOverlayContent();
            ShowNavigationMessage(errorMessage_);
            if (hwnd_) RequestRepaint();
            return 0;
        }
        if (deferredSwap)
        {
            ResetViewState(zoomMode_ == ZoomMode::FitHeight || zoomMode_ == ZoomMode::FitWidth);
            UpdateWindowTitle();
            NotifyZoomChanged(0);
        }

        SetCurrentImageSlot(update->index, std::move(update->image), false);
        errorMessage_ = std::move(update->errorMessage);
        loading_ = false;
        RefreshOverlayContent();
        if (currentImage_)
        {
            ViewerFullImageCache().Insert(
                MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(update->index)]),
                currentImage_);
            for (std::size_t tileIndex = 0; tileIndex < compareTiles_.size(); ++tileIndex)
            {
                CompareTileState& tile = compareTiles_[tileIndex];
                if (tile.pendingItemIndex < 0 && tile.itemIndex == update->index
                    && browser::FilePathsEqual(tile.filePath, items_[static_cast<std::size_t>(update->index)].filePath))
                {
                    tile.image = currentImage_;
                    tile.bitmap.Reset();
                    tile.loading = false;
                    tile.errorMessage.clear();
                    ++tile.requestGeneration;
                    compareAsyncState_->tileRequestGenerations[tileIndex].store(tile.requestGeneration, std::memory_order_release);
                }
            }
            BeginTransitionFromPending();
            ScheduleAdjacentPrefetch(update->navigationGeneration);
        }
        else
        {
            d2dCurrentImageBitmap_.Reset();
            d2dCurrentImageIndex_ = -1;
            StopTransition();
        }
        if (hwnd_)
        {
            RequestRepaint();
        }
        return 0;
    }

    LRESULT ViewerWindow::HandleMetadataReadyMessage(LPARAM lParam)
    {
        std::unique_ptr<MetadataReadyResult> update(reinterpret_cast<MetadataReadyResult*>(lParam));
        if (!update)
        {
            return 0;
        }

        if (update->requestId != metadataRequestId_.load(std::memory_order_acquire)
            || update->index != currentMetadataIndex_)
        {
            return 0;
        }

        currentMetadata_ = std::move(update->metadata);
        currentMetadataText_ = std::move(update->text);
        metadataLoading_ = false;
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }

        return 0;
    }

    LRESULT ViewerWindow::HandlePrefetchImageMessage(LPARAM lParam)
    {
        std::unique_ptr<PrefetchedImageResult> update(reinterpret_cast<PrefetchedImageResult*>(lParam));
        if (!update)
        {
            return 0;
        }

        const bool trackedSlideshowPrefetch = slideshowNextPrefetchIndex_ == update->index
            && slideshowNextPrefetchGeneration_ == update->navigationGeneration;
        if (trackedSlideshowPrefetch)
        {
            slideshowNextPrefetchIndex_ = -1;
            slideshowNextPrefetchGeneration_ = 0;
        }

        if (update->navigationGeneration != asyncState_->navigationGeneration.load(std::memory_order_acquire))
        {
            prefetchCancelledCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.cancelled");
            return 0;
        }

        if (!update->image)
        {
            if (slideshowAdvancePending_ && trackedSlideshowPrefetch)
            {
                slideshowAdvancePending_ = false;
                AdvanceSlideshow();
            }
            return 0;
        }

        const int expectedNextIndex = slideshowActive_ && items_.size() > 1
            ? ((currentIndex_ + 1) % static_cast<int>(items_.size()))
            : (currentIndex_ + 1);

        ViewerFullImageCache().Insert(
            MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(update->index)]),
            update->image);

        if (update->index == currentIndex_ - 1)
        {
            previousSlot_.index = update->index;
            previousSlot_.image = std::move(update->image);
            previousSlot_.prefetched = true;
            prefetchCompletedCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.completed");
            if (hwnd_ && compareMode_)
            {
                RequestRepaint();
            }
            return 0;
        }

        if (update->index == expectedNextIndex)
        {
            nextSlot_.index = update->index;
            nextSlot_.image = std::move(update->image);
            nextSlot_.prefetched = true;
            prefetchCompletedCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.completed");
            if (hwnd_ && compareMode_)
            {
                RequestRepaint();
            }
            if (slideshowAdvancePending_ && slideshowActive_ && !pendingLoadActive_ && !transitionActive_)
            {
                slideshowAdvancePending_ = false;
                AdvanceSlideshow();
            }
            return 0;
        }

        prefetchCompletedCount_.fetch_add(1, std::memory_order_acq_rel);
        util::IncrementCounter(L"viewer.prefetch.completed");
        return 0;
    }
}
