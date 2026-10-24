#include "viewer/ViewerWindowInternal.h"

namespace
{
    constexpr int kPlaceholderBrandArtSize = 256;

    bool LoadViewerInfoOverlaysVisibleSetting()
    {
        HKEY key{};
        if (hyperbrowse::util::OpenSettingsRegistryKey(KEY_READ, &key) != ERROR_SUCCESS)
        {
            return true;
        }

        DWORD value = 1;
        const bool foundValue = TryReadDwordValue(key, kRegistryValueViewerInfoOverlaysVisible, &value);
        RegCloseKey(key);
        return !foundValue || value != 0;
    }

    InfoOverlayTextSize NormalizeInfoOverlayTextSize(DWORD value)
    {
        switch (static_cast<InfoOverlayTextSize>(value))
        {
        case InfoOverlayTextSize::Small:
        case InfoOverlayTextSize::Medium:
        case InfoOverlayTextSize::Large:
            return static_cast<InfoOverlayTextSize>(value);
        default:
            return InfoOverlayTextSize::Small;
        }
    }

    InfoOverlayTextSize LoadViewerInfoOverlayTextSizeSetting()
    {
        HKEY key{};
        if (hyperbrowse::util::OpenSettingsRegistryKey(KEY_READ, &key) != ERROR_SUCCESS)
        {
            return InfoOverlayTextSize::Small;
        }

        DWORD value = static_cast<DWORD>(InfoOverlayTextSize::Small);
        const bool foundValue = TryReadDwordValue(key, kRegistryValueViewerInfoOverlayTextSize, &value);
        RegCloseKey(key);
        return foundValue ? NormalizeInfoOverlayTextSize(value) : InfoOverlayTextSize::Small;
    }
}

namespace hyperbrowse::viewer
{
    bool ViewerWindow::DefaultInfoOverlaysVisible()
    {
        return LoadViewerInfoOverlaysVisibleSetting();
    }

    InfoOverlayTextSize ViewerWindow::DefaultOverlayTextSize()
    {
        return LoadViewerInfoOverlayTextSizeSetting();
    }

    bool ViewerWindow::DefaultFullMetadataVisible()
    {
        return DefaultWindowedFullMetadataVisible();
    }

    bool ViewerWindow::DefaultWindowedFullMetadataVisible()
    {
        return LoadViewerFullMetadataVisibleSetting(kRegistryValueViewerWindowedFullMetadataVisible);
    }

    bool ViewerWindow::DefaultFullScreenFullMetadataVisible()
    {
        return LoadViewerFullMetadataVisibleSetting(kRegistryValueViewerFullScreenFullMetadataVisible);
    }

    void ViewerWindow::SetDefaultInfoOverlaysVisible(bool visible)
    {
        SaveViewerInfoOverlaysVisibleSetting(visible);
    }

    void ViewerWindow::SetDefaultOverlayTextSize(InfoOverlayTextSize size)
    {
        SaveViewerInfoOverlayTextSizeSetting(size);
    }

    void ViewerWindow::SetDefaultFullMetadataVisible(bool visible)
    {
        SetDefaultWindowedFullMetadataVisible(visible);
        SetDefaultFullScreenFullMetadataVisible(visible);
    }

    void ViewerWindow::SetDefaultWindowedFullMetadataVisible(bool visible)
    {
        SaveViewerFullMetadataVisibleSetting(kRegistryValueViewerWindowedFullMetadataVisible, visible);
    }

    void ViewerWindow::SetDefaultFullScreenFullMetadataVisible(bool visible)
    {
        SaveViewerFullMetadataVisibleSetting(kRegistryValueViewerFullScreenFullMetadataVisible, visible);
    }

    ViewerWindow::ViewerWindow(HINSTANCE instance)
        : instance_(instance)
        , asyncState_(std::make_shared<AsyncState>())
        , compareAsyncState_(std::make_shared<CompareAsyncState>())
        , infoOverlaysVisible_(LoadViewerInfoOverlaysVisibleSetting())
        , infoOverlayTextSize_(LoadViewerInfoOverlayTextSizeSetting())
        , fullMetadataVisible_(LoadViewerFullMetadataVisibleSetting(kRegistryValueViewerWindowedFullMetadataVisible))
    {
        menuFont_ = CreateViewerMenuFont(appTextSize_);
        backgroundBrush_ = CreateSolidBrush(BackgroundColor(false));
        statusArt_ = util::LoadPngResourceBitmap(instance_,
                                                 IDB_HYPERBROWSE_BRAND_PNG,
                                                 kPlaceholderBrandArtSize,
                                                 kPlaceholderBrandArtSize);
        // Six workers: one for foreground image decode, up to four for adjacent prefetches,
        // one spare. This allows prefetch to degrade gracefully without starving the main
        // image load when many adjacent images need decoding during rapid delete sequences.
        backgroundExecutor_ = std::make_unique<util::BackgroundExecutor>(
            6,
            ViewerBackgroundQueueCapacity(resourceProfile_));
        displayColors_ = std::make_unique<services::DisplayColorService>(
            512ULL * 1024 * 1024, services::DisplayColorService::ProfileProvider{},
            services::DisplayColorService::Converter{}, backgroundExecutor_.get());
    }

    ViewerWindow::~ViewerWindow()
    {
        displayColors_->Shutdown();
        asyncState_->shutdown.store(true, std::memory_order_release);
        asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
        asyncState_->targetWindow.store(nullptr, std::memory_order_release);
        compareAsyncState_->sessionGeneration.fetch_add(1, std::memory_order_acq_rel);
        WaitForBackgroundTasks();

        ReleaseD2DResources();

        if (hwnd_ && IsWindow(hwnd_))
        {
            DestroyWindow(hwnd_);
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (menuFont_ && menuFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(menuFont_);
        }

        if (previousNavigationCursor_)
        {
            DestroyCursor(previousNavigationCursor_);
        }

        if (nextNavigationCursor_)
        {
            DestroyCursor(nextNavigationCursor_);
        }
    }

    bool ViewerWindow::Open(HWND owner,
                            std::vector<browser::BrowserItem> items,
                            int selectedIndex,
                            bool darkTheme,
                            HMONITOR targetMonitor)
    {
        owner_ = owner;
                            items_ = std::move(items);
        if (items_.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items_.size()))
        {
            return false;
        }

        currentIndex_ = selectedIndex;
        darkTheme_ = darkTheme;
        compareSessionActive_ = false;
        compareTiles_.clear();
        compareCandidateMetadata_.clear();
        compareSessionGeneration_++;
        compareAsyncState_->sessionGeneration.store(compareSessionGeneration_, std::memory_order_release);
        compareMode_ = false;
        compareDirection_ = CompareDirection::Next;
        ClearWraparoundMessage();
        StopSlideshow();
        StopTransition();
        ResetCachedImageSlots();
        ResetPrefetchStatistics();
        ReapCompletedBackgroundTasks();

        util::LogInfo(L"ViewerWindow::Open requested index="
            + std::to_wstring(selectedIndex)
            + L", existingHwnd=" + FormatWindowHandle(hwnd_)
            + L", isWindow=" + std::to_wstring(hwnd_ && IsWindow(hwnd_) != FALSE));

        if (hwnd_ && IsWindow(hwnd_) == FALSE)
        {
            util::LogInfo(L"ViewerWindow::Open clearing stale HWND " + FormatWindowHandle(hwnd_));
            ReleaseD2DResources();
            hwnd_ = nullptr;
            fullScreen_ = false;
            windowFitMode_ = WindowFitMode::Regular;
            hasRegularPlacementBeforeFit_ = false;
            regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
            windowedStyle_ = 0;
            windowedExStyle_ = 0;
            windowedPlacement_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
        }

        const bool reusedExistingWindow = hwnd_ != nullptr;
        if (!hwnd_)
        {
            infoOverlaysVisible_ = LoadViewerInfoOverlaysVisibleSetting();
            infoOverlayTextSize_ = LoadViewerInfoOverlayTextSizeSetting();
            fullMetadataVisible_ = LoadViewerFullMetadataVisibleSetting(kRegistryValueViewerWindowedFullMetadataVisible);
            if (!RegisterWindowClass())
            {
                return false;
            }

            hwnd_ = CreateWindowExW(
                0,
                kWindowClassName,
                L"HyperBrowse Viewer",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                1280,
                900,
                nullptr,
                nullptr,
                instance_,
                this);

            if (!hwnd_)
            {
                return false;
            }

            DragAcceptFiles(hwnd_, TRUE);
            util::LogInfo(L"ViewerWindow::Open created HWND " + FormatWindowHandle(hwnd_));
        }
        else
        {
            util::LogInfo(L"ViewerWindow::Open reusing HWND " + FormatWindowHandle(hwnd_));
        }

        SetDarkTheme(darkTheme_);
        UpdateWindowTitle();
        if (IsIconic(hwnd_))
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }

        SetFullScreen(true, targetMonitor);
        if (!reusedExistingWindow)
        {
            hasWindowedStateToRestore_ = false;
        }
        if (!SetForegroundWindow(hwnd_))
        {
            SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(hwnd_);
        }
        SetActiveWindow(hwnd_);
        SetFocus(hwnd_);
        NotifyCurrentItemChanged();
        if (!reusedExistingWindow)
        {
            displayColors_->BindTargetWindow(hwnd_);
            SetTimer(hwnd_, services::DisplayColorService::kProfilePollTimer,
                     services::DisplayColorService::kProfilePollIntervalMs, nullptr);
        }
        else
        {
            displayColors_->InvalidateImages();
        }
        RefreshColorProfile(true);
        LoadCurrentImageAsync(LoadReason::Open);
        return true;
    }

    HWND ViewerWindow::Hwnd() const noexcept
    {
        return hwnd_;
    }

    void ViewerWindow::SetColorManagementEnabled(bool enabled)
    {
        displayColors_->SetEnabled(enabled);
        RefreshColorProfile(true);
        RequestRepaint();
    }

    bool ViewerWindow::IsColorManagementEnabled() const
    {
        return displayColors_->IsEnabled();
    }

    void ViewerWindow::SetColorProfileProvider(services::DisplayColorService::ProfileProvider provider)
    {
        displayColors_->SetProfileProvider(std::move(provider));
        RefreshColorProfile(true);
    }

    services::DisplayColorService::Statistics ViewerWindow::DisplayColorStatistics() const
    {
        return displayColors_->GetStatistics();
    }

    void ViewerWindow::RefreshColorProfile(bool force)
    {
        displayColors_->RefreshMonitor(services::MonitorDeviceForWindow(hwnd_), force);
    }

    std::shared_ptr<const cache::CachedThumbnail> ViewerWindow::ImageForDisplay(
        int itemIndex, std::shared_ptr<const cache::CachedThumbnail> source) const
    {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(items_.size())) return source;
        return displayColors_->ImageForDisplay(MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(itemIndex)]), std::move(source));
    }

    bool ViewerWindow::IsOpen() const noexcept
    {
        return hwnd_ != nullptr && IsWindow(hwnd_) != FALSE;
    }

    bool ViewerWindow::IsFullScreen() const noexcept
    {
        return fullScreen_;
    }

    int ViewerWindow::CurrentIndex() const noexcept
    {
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
        {
            return compareTiles_[focusedCompareTile_].itemIndex;
        }
            return currentIndex_;
    }

    std::wstring ViewerWindow::CurrentFilePath() const
    {
        if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
        {
            return compareTiles_[focusedCompareTile_].filePath;
        }
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return {};
        }

        return items_[static_cast<std::size_t>(currentIndex_)].filePath;
    }

    int ViewerWindow::CurrentZoomPercent() const noexcept
    {
        return currentZoomPercent_;
    }

    int ViewerWindow::RotationQuarterTurns() const noexcept
    {
        return rotationQuarterTurns_;
    }

    POINT ViewerWindow::PanOffset() const noexcept
    {
        return POINT{
            static_cast<LONG>(std::lround(panOffsetX_)),
            static_cast<LONG>(std::lround(panOffsetY_)),
        };
    }

    bool ViewerWindow::AreInfoOverlaysVisible() const noexcept
    {
        return infoOverlaysVisible_;
    }

    InfoOverlayTextSize ViewerWindow::OverlayTextSize() const noexcept
    {
        return infoOverlayTextSize_;
    }

    bool ViewerWindow::IsFullMetadataVisible() const noexcept
    {
        return fullMetadataVisible_;
    }

    void ViewerWindow::StartSlideshow(UINT intervalMs)
    {
        if (!hwnd_ || items_.size() < 2)
        {
            return;
        }

        slideshowIntervalMs_ = std::max<UINT>(250, intervalMs);
        slideshowTimerId_ = SetTimer(hwnd_, 1, slideshowIntervalMs_, nullptr);
        slideshowActive_ = slideshowTimerId_ != 0;
        slideshowAdvancePending_ = false;
        if (slideshowActive_ && currentImage_ && !pendingLoadActive_)
        {
            ScheduleAdjacentPrefetch(asyncState_->navigationGeneration.load(std::memory_order_acquire));
        }
    }

    void ViewerWindow::StopSlideshow()
    {
        if (hwnd_ && slideshowTimerId_ != 0)
        {
            KillTimer(hwnd_, slideshowTimerId_);
        }

        slideshowTimerId_ = 0;
        slideshowActive_ = false;
        slideshowAdvancePending_ = false;
        slideshowNextPrefetchIndex_ = -1;
        slideshowNextPrefetchGeneration_ = 0;
        if (!manualTransitionEnabled_)
        {
            StopTransition();
        }
    }

    bool ViewerWindow::IsSlideshowActive() const noexcept
    {
        return slideshowActive_;
    }

    UINT ViewerWindow::SlideshowIntervalMs() const noexcept
    {
        return slideshowIntervalMs_;
    }

    bool ViewerWindow::ReplaceItems(std::vector<browser::BrowserItem> items, int selectedIndex)
    {
        if (items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return false;
        }

        std::vector<browser::BrowserItem> oldItems = std::move(items_);
        std::vector<CompareTileState> oldTiles = std::move(compareTiles_);
        std::vector<CompareTileMetadata> oldCandidateMetadata = std::move(compareCandidateMetadata_);
        const std::size_t oldFocusedTile = focusedCompareTile_;
        const bool keepCompareActive = compareSessionActive_;
        items_ = std::move(items);
        currentIndex_ = selectedIndex;

        if (!oldTiles.empty())
        {
            compareCandidateMetadata_.resize(items_.size());
            for (std::size_t oldIndex = 0; oldIndex < oldItems.size() && oldIndex < oldCandidateMetadata.size(); ++oldIndex)
            {
                const auto newCandidate = std::find_if(items_.begin(), items_.end(), [&](const browser::BrowserItem& item)
                {
                    return browser::FilePathsEqual(item.filePath, oldItems[oldIndex].filePath);
                });
                if (newCandidate != items_.end())
                {
                    const std::size_t newIndex = static_cast<std::size_t>(std::distance(items_.begin(), newCandidate));
                    compareCandidateMetadata_[newIndex] = oldCandidateMetadata[oldIndex];
                }
            }

            std::wstring oldFocusedPath;
            if (oldFocusedTile < oldTiles.size())
            {
                oldFocusedPath = oldTiles[oldFocusedTile].filePath;
            }
            compareTiles_.clear();
            compareTiles_.reserve(oldTiles.size());
            std::vector<bool> tileNeedsReload;
            for (CompareTileState& tile : oldTiles)
            {
                const auto newItem = std::find_if(items_.begin(), items_.end(), [&](const browser::BrowserItem& item)
                {
                    return browser::FilePathsEqual(item.filePath, tile.filePath);
                });
                if (newItem == items_.end())
                {
                    continue;
                }

                const int oldIndex = tile.itemIndex;
                const bool contentChanged = oldIndex < 0 || oldIndex >= static_cast<int>(oldItems.size())
                    || oldItems[static_cast<std::size_t>(oldIndex)].modifiedTimestampUtc != newItem->modifiedTimestampUtc
                    || oldItems[static_cast<std::size_t>(oldIndex)].fileSizeBytes != newItem->fileSizeBytes;
                tile.itemIndex = static_cast<int>(std::distance(items_.begin(), newItem));
                tile.filePath = newItem->filePath;
                tile.pendingItemIndex = -1;
                tile.pendingFilePath.clear();
                tile.loading = false;
                tile.errorMessage.clear();
                if (contentChanged)
                {
                    tile.image.reset();
                    tile.bitmap.Reset();
                }
                if (static_cast<std::size_t>(tile.itemIndex) < compareCandidateMetadata_.size())
                {
                    tile.metadata = compareCandidateMetadata_[static_cast<std::size_t>(tile.itemIndex)];
                }
                compareTiles_.push_back(std::move(tile));
                tileNeedsReload.push_back(contentChanged || !compareTiles_.back().image);
            }

            compareSessionGeneration_++;
            compareAsyncState_->sessionGeneration.store(compareSessionGeneration_, std::memory_order_release);
            for (std::size_t tileIndex = 0; tileIndex < compareAsyncState_->tileRequestGenerations.size(); ++tileIndex)
            {
                compareAsyncState_->tileRequestGenerations[tileIndex].fetch_add(1, std::memory_order_acq_rel);
            }

            if (compareTiles_.size() >= 2)
            {
                const auto focusedTile = std::find_if(compareTiles_.begin(), compareTiles_.end(), [&](const CompareTileState& tile)
                {
                    return browser::FilePathsEqual(tile.filePath, oldFocusedPath);
                });
                focusedCompareTile_ = focusedTile == compareTiles_.end()
                    ? std::min(oldFocusedTile, compareTiles_.size() - 1)
                    : static_cast<std::size_t>(std::distance(compareTiles_.begin(), focusedTile));
                compareSessionActive_ = keepCompareActive;
                for (std::size_t tileIndex = 0; tileIndex < compareTiles_.size(); ++tileIndex)
                {
                    if (tileNeedsReload[tileIndex])
                    {
                        RequestCompareTileImage(tileIndex, compareTiles_[tileIndex].itemIndex, false);
                    }
                }
                if (compareSessionActive_)
                {
                    FocusCompareTileInternal(focusedCompareTile_);
                    NotifyCurrentItemChanged();
                    UpdateWindowTitle();
                    RequestRepaint();
                    return true;
                }
            }
            else
            {
                if (compareTiles_.size() == 1)
                {
                    selectedIndex = compareTiles_.front().itemIndex;
                }
                compareTiles_.clear();
                compareCandidateMetadata_.clear();
                compareSessionActive_ = false;
                compareMode_ = false;
            }
        }

        NotifyCurrentItemChanged();
        ClearWraparoundMessage();
        if (compareMode_)
        {
            compareDirection_ = ResolveCompareDirection(compareDirection_);
            compareMode_ = ActiveCompareIndex() >= 0;
        }
        if (slideshowActive_ && items_.size() < 2)
        {
            StopSlideshow();
        }

        StopTransition();
        ResetCachedImageSlots();
        ResetPrefetchStatistics();
        ReapCompletedBackgroundTasks();
        UpdateWindowTitle();
        LoadCurrentImageAsync(LoadReason::Navigation);
        return true;
    }

    bool ViewerWindow::GetDeleteCurrentPaths(std::wstring* sourcePath, std::wstring* preferredFocusPath) const
    {
        const int activeIndex = CurrentIndex();
        if (activeIndex < 0 || activeIndex >= static_cast<int>(items_.size()))
        {
            return false;
        }

        if (sourcePath)
        {
            *sourcePath = items_[static_cast<std::size_t>(activeIndex)].filePath;
        }
        if (preferredFocusPath)
        {
            preferredFocusPath->clear();
        }

        const int preferredIndex = (activeIndex + 1 < static_cast<int>(items_.size()))
            ? activeIndex + 1
            : activeIndex - 1;
        if (preferredFocusPath && preferredIndex >= 0 && preferredIndex < static_cast<int>(items_.size()))
        {
            *preferredFocusPath = items_[static_cast<std::size_t>(preferredIndex)].filePath;
        }
        return true;
    }

    std::wstring ViewerWindow::FilingResumeTargetPathForMove() const
    {
        const int activeIndex = CurrentIndex();
        if (activeIndex < 0 || activeIndex >= static_cast<int>(items_.size()))
        {
            return {};
        }

        const int targetIndex = activeIndex > 0
            ? activeIndex - 1
            : activeIndex + 1;
        if (targetIndex < 0 || targetIndex >= static_cast<int>(items_.size()))
        {
            return {};
        }

        return items_[static_cast<std::size_t>(targetIndex)].filePath;
    }

    bool ViewerWindow::AdvanceAfterDeleteCurrent()
    {
        util::ScopedTimer functionTimer(L"ViewerWindow::AdvanceAfterDeleteCurrent");
        if (compareSessionActive_)
        {
            EndCompareSessionPromoteFocused();
        }
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return false;
        }

        // Capture whether adjacent prefetch slots already hold the images that
        // will become the new current item after the erase, so we can reuse them
        // and avoid a redundant full decode.
        const int nextIndexOld = currentIndex_ + 1;
        const int prevIndexOld = currentIndex_ - 1;
        const bool canReuseNext = nextIndexOld < static_cast<int>(items_.size())
            && nextSlot_.index == nextIndexOld
            && nextSlot_.image;
        const bool canReusePrev = prevIndexOld >= 0
            && previousSlot_.index == prevIndexOld
            && previousSlot_.image;

        items_.erase(items_.begin() + currentIndex_);
        if (items_.empty())
        {
            const HWND viewerHwnd = hwnd_;
            if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
            {
                PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
            }
            return true;
        }

        const bool wentBackward = currentIndex_ >= static_cast<int>(items_.size());
        if (wentBackward)
        {
            currentIndex_ = static_cast<int>(items_.size()) - 1;
        }
        NotifyCurrentItemChanged();
        if (compareMode_)
        {
            compareDirection_ = ResolveCompareDirection(compareDirection_);
            compareMode_ = ActiveCompareIndex() >= 0;
        }
        if (slideshowActive_ && items_.size() < 2)
        {
            StopSlideshow();
        }

        StopTransition();

        // The paint-time bitmap cache is keyed on currentIndex_ (see
        // ensureCurrentBitmap / BeginTransitionFromPending). Deleting an item
        // shifts every subsequent item down by one, so currentIndex_ can end up
        // numerically unchanged even though the image *at* that index is now a
        // completely different file. That produces a false-positive cache hit
        // that skips re-uploading the D2D bitmap, leaving the stale (deleted)
        // image on screen until some other navigation forces a refresh. Force
        // invalidation here so the next paint always re-uploads.
        d2dCurrentImageBitmap_.Reset();
        d2dCurrentImageIndex_ = -1;

        // Fast path: reuse the prefetched image that is now the current item.
        const bool reusePrefetch = (!wentBackward && canReuseNext)
                                || ( wentBackward && canReusePrev);
        util::LogInfo(L"ViewerWindow::AdvanceAfterDeleteCurrent path="
            + std::wstring(reusePrefetch ? L"FAST(prefetch-reuse)" : L"SLOW(needs-decode)")
            + L", newCurrentIndex=" + std::to_wstring(currentIndex_)
            + L", itemCount=" + std::to_wstring(items_.size()));
        if (reusePrefetch)
        {
            // Extract the image before touching the slots — sourceSlot is an alias
            // into nextSlot_/previousSlot_ and would be invalidated by clearing them.
            auto reusedImage = (!wentBackward) ? nextSlot_.image : previousSlot_.image;

            // Cancel any in-flight decode so stale results are not applied.
            asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);

            previousSlot_ = {};
            nextSlot_ = {};
            SetCurrentImageSlot(currentIndex_, std::move(reusedImage), true);
            prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.hit");
            util::RecordTiming(L"viewer.navigation", 0.0);
            PrepareForImageChange();
            loading_ = false;
            errorMessage_.clear();
            currentImage_ = currentSlot_.image;
            BeginTransitionFromPending();
            const std::uint64_t navigationGeneration =
                asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            ScheduleAdjacentPrefetch(navigationGeneration);
            if (hwnd_)
            {
                RequestRepaint();
            }
            return true;
        }

        // Slow path: no prefetched image is available yet. Mirror Navigate()'s
        // cache-miss behavior by leaving currentImage_/currentSlot_ untouched so the
        // last-displayed image keeps showing (instead of blanking to a "loading"
        // placeholder) while the new current image decodes in the background.
        // Only the now-stale adjacent prefetch slots need to be dropped.
        previousSlot_ = {};
        nextSlot_ = {};
        d2dCompareImageBitmap_.Reset();
        d2dCompareImageIndex_ = -1;
        ResetPrefetchStatistics();
        ReapCompletedBackgroundTasks();
        UpdateWindowTitle();
        LoadCurrentImageAsync(LoadReason::Navigation);
        return true;
    }

    bool ViewerWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kWindowClassName, &windowClass) != FALSE)
        {
            return true;
        }

        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &ViewerWindow::WindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kWindowClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        windowClass.hIconSm = static_cast<HICON>(
            LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE),
                       IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        return RegisterClassExW(&windowClass) != 0;
    }

    void ViewerWindow::UpdateWindowTitle() const
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        std::wstring title = L"HyperBrowse Viewer";
        const int displayIndex = CurrentIndex();
        if (displayIndex >= 0 && displayIndex < static_cast<int>(items_.size()))
        {
            title.append(L" - ");
            title.append(items_[static_cast<std::size_t>(displayIndex)].fileName);
            if (compareSessionActive_ && focusedCompareTile_ < compareTiles_.size())
            {
                title.append(L" (Tile ");
                title.append(std::to_wstring(focusedCompareTile_ + 1));
                title.append(L"/");
                title.append(std::to_wstring(compareTiles_.size()));
                title.append(L")");
            }
            else
            {
                title.append(L" (");
                title.append(std::to_wstring(displayIndex + 1));
                title.append(L"/");
                title.append(std::to_wstring(items_.size()));
                title.append(L")");
            }
        }
        if ((compareSessionActive_ && compareTiles_.size() >= 2)
            || (compareMode_ && ActiveCompareIndex() >= 0))
        {
            title.append(L" [Compare]");
        }

        SetWindowTextW(hwnd_, title.c_str());
    }
}
