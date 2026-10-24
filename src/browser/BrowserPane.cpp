#include "browser/BrowserPaneInternal.h"

namespace
{
    constexpr int kVisiblePriority = 0;
    constexpr int kNearVisiblePriority = 1;
    constexpr int kProactivePrefetchPriority = 2;
    constexpr int kThumbnailNearVisiblePrefetchRows = 2;
    constexpr int kThumbnailActiveScrollPrefetchRows = 1;
    constexpr int kThumbnailMinimumTopOfFolderPrefetchRows = 10;
    constexpr int kThumbnailMaximumTopOfFolderPrefetchRows = 40;
    constexpr int kThumbnailMinimumLookAheadPrefetchRows = 4;
    constexpr int kThumbnailMaximumLookAheadPrefetchRows = 20;
    constexpr int kMetadataNearVisiblePrefetchRows = 2;
    constexpr int kMetadataMinimumTopOfFolderPrefetchRows = 12;
    constexpr int kMetadataMaximumTopOfFolderPrefetchRows = 48;
    constexpr int kMetadataMinimumLookAheadPrefetchRows = 8;
    constexpr int kMetadataMaximumLookAheadPrefetchRows = 32;
    constexpr int kMinimumDetailsMetadataNearVisibleItems = 12;
    constexpr int kMinimumDetailsMetadataTopOfFolderItems = 24;
    constexpr int kMaximumDetailsMetadataTopOfFolderItems = 192;
    constexpr int kMinimumDetailsMetadataLookAheadItems = 12;
    constexpr int kMaximumDetailsMetadataLookAheadItems = 96;
    constexpr int kPlaceholderBrandArtSize = 256;

    int TopOfFolderThumbnailWarmUpMultiplier(int prefetchDepth)
    {
        return std::clamp(prefetchDepth + 2,
                          kThumbnailMinimumTopOfFolderPrefetchRows,
                          kThumbnailMaximumTopOfFolderPrefetchRows);
    }

    int LookAheadThumbnailWarmUpMultiplier(int prefetchDepth)
    {
        return std::clamp(prefetchDepth,
                          kThumbnailMinimumLookAheadPrefetchRows,
                          kThumbnailMaximumLookAheadPrefetchRows);
    }

    int TopOfFolderMetadataWarmUpMultiplier(int prefetchDepth)
    {
        return std::clamp(prefetchDepth * 2,
                          kMetadataMinimumTopOfFolderPrefetchRows,
                          kMetadataMaximumTopOfFolderPrefetchRows);
    }

    int LookAheadMetadataWarmUpMultiplier(int prefetchDepth)
    {
        return std::clamp(prefetchDepth,
                          kMetadataMinimumLookAheadPrefetchRows,
                          kMetadataMaximumLookAheadPrefetchRows);
    }

    hyperbrowse::browser::BrowserPane::ThemeColors MakeThemeColors(bool darkTheme)
    {
        const hyperbrowse::ui::ThemeVariant variant = hyperbrowse::ui::ResolveThemeVariant(darkTheme);
        const hyperbrowse::ui::ThemeTokens tokens = hyperbrowse::ui::MakeThemeTokens(variant);

        if (variant == hyperbrowse::ui::ThemeVariant::HighContrast)
        {
            return hyperbrowse::browser::BrowserPane::ThemeColors{
                tokens.windowBackground,
                tokens.surfaceBackground,
                GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_HIGHLIGHT),
                tokens.text,
                tokens.mutedText,
                tokens.border,
                tokens.accent,
                tokens.accentFill,
                tokens.accentText,
                GetSysColor(COLOR_HIGHLIGHT),
                GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_WINDOW),
            };
        }

        if (variant == hyperbrowse::ui::ThemeVariant::Dark)
        {
            return hyperbrowse::browser::BrowserPane::ThemeColors{
                RGB(18, 22, 27),
                tokens.surfaceBackground,
                RGB(23, 27, 33),
                RGB(34, 50, 68),
                tokens.text,
                RGB(152, 161, 171),
                tokens.border,
                tokens.accent,
                tokens.accentFill,
                tokens.accentText,
                RGB(138, 189, 239),
                RGB(26, 31, 37),
                RGB(27, 32, 38),
            };
        }

        return hyperbrowse::browser::BrowserPane::ThemeColors{
            RGB(236, 240, 245),
            tokens.surfaceBackground,
            RGB(228, 234, 242),
            RGB(236, 243, 251),
            tokens.text,
            RGB(96, 105, 115),
            RGB(201, 210, 220),
            tokens.accent,
            tokens.accentFill,
            tokens.accentText,
            RGB(90, 144, 214),
            RGB(245, 248, 252),
            RGB(241, 245, 250),
        };
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> CreateIconThumbnail(HICON iconHandle, int iconSize)
    {
        if (!iconHandle || iconSize <= 0)
        {
            return {};
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = iconSize;
        bitmapInfo.bmiHeader.biHeight = -iconSize;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return {};
        }

        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap)
        {
            ReleaseDC(nullptr, screenDc);
            return {};
        }

        const std::size_t bufferSize = static_cast<std::size_t>(iconSize) * static_cast<std::size_t>(iconSize) * 4U;
        if (pixels)
        {
            ZeroMemory(pixels, bufferSize);
        }

        HDC memoryDc = CreateCompatibleDC(screenDc);
        if (!memoryDc)
        {
            DeleteObject(bitmap);
            ReleaseDC(nullptr, screenDc);
            return {};
        }

        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
        DrawIconEx(memoryDc, 0, 0, iconHandle, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        SelectObject(memoryDc, oldBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);

        return std::make_shared<const hyperbrowse::cache::CachedThumbnail>(
            bitmap,
            iconSize,
            iconSize,
            bufferSize,
            iconSize,
            iconSize);
    }
}

namespace hyperbrowse::browser
{
    std::wstring BrowserSortModeToLabel(BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case BrowserSortMode::FileName:
            return L"Filename";
        case BrowserSortMode::ModifiedDate:
            return L"Modified";
        case BrowserSortMode::FileSize:
            return L"File Size";
        case BrowserSortMode::Dimensions:
            return L"Dimensions";
        case BrowserSortMode::FileType:
            return L"Type";
        case BrowserSortMode::Random:
            return L"Random";
        case BrowserSortMode::DateTaken:
            return L"Date Taken";
        case BrowserSortMode::Rating:
            return L"Rating";
        case BrowserSortMode::Tags:
            return L"Tags";
        default:
            return L"Unknown";
        }
    }

    int ThumbnailSizePresetToPixels(ThumbnailSizePreset preset)
    {
        return static_cast<int>(preset);
    }

    std::wstring ThumbnailSizePresetToLabel(ThumbnailSizePreset preset)
    {
        return std::to_wstring(ThumbnailSizePresetToPixels(preset)) + L" px";
    }

    BrowserPane::BrowserPane(HINSTANCE instance)
        : instance_(instance)
        , colors_(MakeThemeColors(false))
    {
        RecreateBackgroundServices();
        placeholderArt_ = util::LoadPngResourceBitmap(instance_,
                                                      IDB_HYPERBROWSE_BRAND_PNG,
                                                      kPlaceholderBrandArtSize,
                                                      kPlaceholderBrandArtSize);
        unavailableThumbnailArt_ = CreateIconThumbnail(LoadIconW(nullptr, IDI_WARNING), 64);
        wchar_t windowsDirectory[MAX_PATH]{};
        const UINT windowsDirectoryLength = GetWindowsDirectoryW(windowsDirectory, static_cast<UINT>(std::size(windowsDirectory)));
        const std::wstring folderIconPath = windowsDirectoryLength > 0
                && windowsDirectoryLength < std::size(windowsDirectory)
            ? std::wstring(windowsDirectory, windowsDirectoryLength)
            : std::wstring(L"C:\\Windows");
        SHFILEINFOW folderImageListInfo{};
        shellImageList_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            folderIconPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &folderImageListInfo,
            sizeof(folderImageListInfo),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
        if (shellImageList_)
        {
            folderIconIndex_ = folderImageListInfo.iIcon;
        }

        if (folderIconIndex_ >= 0)
        {
            Microsoft::WRL::ComPtr<IImageList> jumboImageList;
            if (SUCCEEDED(SHGetImageList(SHIL_JUMBO,
                                         IID_IImageList,
                                         reinterpret_cast<void**>(jumboImageList.GetAddressOf()))))
            {
                HICON jumboFolderIcon{};
                if (SUCCEEDED(jumboImageList->GetIcon(folderIconIndex_, ILD_TRANSPARENT, &jumboFolderIcon))
                    && jumboFolderIcon)
                {
                    folderArt_ = CreateIconThumbnail(jumboFolderIcon, 256);
                    DestroyIcon(jumboFolderIcon);
                }
            }
        }

        if (!folderArt_)
        {
            SHFILEINFOW folderShellInfo{};
            if (SHGetFileInfoW(folderIconPath.c_str(),
                               FILE_ATTRIBUTE_DIRECTORY,
                               &folderShellInfo,
                               sizeof(folderShellInfo),
                               SHGFI_ICON | SHGFI_LARGEICON) != 0
                && folderShellInfo.hIcon)
            {
                folderArt_ = CreateIconThumbnail(folderShellInfo.hIcon, 128);
                DestroyIcon(folderShellInfo.hIcon);
            }
        }
        RebuildThemeResources();
        RebuildThumbnailFonts();
    }

    BrowserPane::~BrowserPane()
    {
        displayColors_->BindTargetWindow(nullptr);
        displayColors_.reset();
        ReleaseD2DResources();

        if (detailsListFont_ && ownsDetailsListFont_)
        {
            DeleteObject(detailsListFont_);
        }

        ReleaseThumbnailFonts();

        ReleaseThemeResources();
    }

    bool BrowserPane::Create(HWND parent)
    {
        parent_ = parent;
        if (!RegisterClass())
        {
            return false;
        }

        hwnd_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            kClassName,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_VSCROLL,
            0,
            0,
            100,
            100,
            parent_,
            nullptr,
            instance_,
            this);

        if (hwnd_ && thumbnailScheduler_)
        {
            thumbnailScheduler_->BindTargetWindow(hwnd_);
        }
        if (hwnd_ && metadataService_)
        {
            metadataService_->BindTargetWindow(hwnd_);
        }
        if (hwnd_)
        {
            displayColors_->BindTargetWindow(hwnd_);
            SetTimer(hwnd_, services::DisplayColorService::kProfilePollTimer,
                     services::DisplayColorService::kProfilePollIntervalMs, nullptr);
            RefreshColorProfile(true);
            RebuildThumbnailFonts();
            RebuildD2DTextFormats();
        }

        return hwnd_ != nullptr;
    }

    HWND BrowserPane::Hwnd() const noexcept
    {
        return hwnd_;
    }

    void BrowserPane::SetColorManagementEnabled(bool enabled)
    {
        displayColors_->SetEnabled(enabled);
        d2dBitmapCache_.clear();
        RefreshColorProfile(true);
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        if (parent_) InvalidateRect(parent_, nullptr, FALSE);
    }

    bool BrowserPane::IsColorManagementEnabled() const
    {
        return displayColors_->IsEnabled();
    }

    void BrowserPane::SetColorProfileProvider(services::DisplayColorService::ProfileProvider provider)
    {
        displayColors_->SetProfileProvider(std::move(provider));
        RefreshColorProfile(true);
    }

    services::DisplayColorService::Statistics BrowserPane::DisplayColorStatistics() const
    {
        return displayColors_->GetStatistics();
    }

    void BrowserPane::RefreshColorProfile(bool force)
    {
        displayColors_->RefreshMonitor(services::MonitorDeviceForWindow(hwnd_), force);
    }

    std::shared_ptr<const cache::CachedThumbnail> BrowserPane::ImageForDisplay(
        const cache::ThumbnailCacheKey& key, std::shared_ptr<const cache::CachedThumbnail> source) const
    {
        return displayColors_->ImageForDisplay(key, std::move(source));
    }

    void BrowserPane::SetModel(BrowserModel* model)
    {
        if (model_ == model)
        {
            return;
        }

        model_ = model;
        displayColors_->InvalidateImages();
        ++thumbnailSessionId_;
        ++metadataSessionId_;
        HideThumbnailTooltip();
        CancelThumbnailWork();
        if (metadataService_)
        {
            metadataService_->CancelOutstanding();
        }
    }

    void BrowserPane::BeginFolderLoad()
    {
        displayColors_->InvalidateImages();
        d2dBitmapCache_.clear();
        ++thumbnailSessionId_;
        ++metadataSessionId_;
        ++thumbnailRequestEpoch_;
        HideThumbnailTooltip();
        CancelThumbnailWork();
        if (metadataService_)
        {
            metadataService_->CancelOutstanding();
        }
    }

    void BrowserPane::RefreshFromModel()
    {
        HideThumbnailTooltip();
        d2dBitmapCache_.clear();
        RebuildOrder();
        UpdateSelectionBytes();
        UpdateDetailsListView();
        SyncDetailsListSelectionFromModel();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::SetViewMode(BrowserViewMode mode)
    {
        if (viewMode_ == mode)
        {
            return;
        }

        HideThumbnailTooltip();
        viewMode_ = mode;
        LayoutChildren();
        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            ScheduleVisibleThumbnailWork();
        }
        else
        {
            CancelThumbnailWork();
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    BrowserViewMode BrowserPane::GetViewMode() const noexcept
    {
        return viewMode_;
    }

    void BrowserPane::SetSortMode(BrowserSortMode sortMode)
    {
        if (sortMode_ == sortMode)
        {
            return;
        }

        sortMode_ = sortMode;
        RebuildOrder();
        UpdateDetailsListView();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    BrowserSortMode BrowserPane::GetSortMode() const noexcept
    {
        return sortMode_;
    }

    void BrowserPane::SetSortAscending(bool ascending)
    {
        if (sortAscending_ == ascending)
        {
            return;
        }

        sortAscending_ = ascending;
        RebuildOrder();
        UpdateDetailsListView();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    bool BrowserPane::IsSortAscending() const noexcept
    {
        return sortAscending_;
    }

    void BrowserPane::SetUserMetadataStore(hyperbrowse::services::UserMetadataStore* userMetadataStore)
    {
        if (userMetadataStore_ == userMetadataStore)
        {
            return;
        }

        userMetadataStore_ = userMetadataStore;
        RebuildOrder();
        UpdateSelectionBytes();
        UpdateDetailsListView();
        SyncDetailsListSelectionFromModel();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::SetFilterQuery(std::wstring query)
    {
        query = TrimWhitespace(std::move(query));
        const std::wstring queryLower = ToLowercase(query);
        if (filterQuery_ == query && filterQueryLower_ == queryLower)
        {
            return;
        }

        filterQuery_ = std::move(query);
        filterQueryLower_ = std::move(queryLower);
        RebuildOrder();
        UpdateSelectionBytes();
        UpdateDetailsListView();
        SyncDetailsListSelectionFromModel();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    const std::wstring& BrowserPane::GetFilterQuery() const noexcept
    {
        return filterQuery_;
    }

    bool BrowserPane::HasActiveFilter() const noexcept
    {
        return !filterQuery_.empty();
    }

    std::uint64_t BrowserPane::DisplayedItemCount() const noexcept
    {
        return static_cast<std::uint64_t>(orderedModelIndices_.size());
    }

    int BrowserPane::CurrentItemNumber() const noexcept
    {
        const int currentViewIndex = PrimarySelectedViewIndex();
        return currentViewIndex < 0 ? 0 : currentViewIndex + 1;
    }

    bool BrowserPane::GoToItemNumber(int itemNumber)
    {
        if (itemNumber < 1 || itemNumber > static_cast<int>(orderedModelIndices_.size()))
        {
            return false;
        }

        const int viewIndex = itemNumber - 1;
        SelectSingleViewIndex(viewIndex);
        EnsureFocusedItemVisible();
        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            SetFocus(hwnd_);
        }
        else if (detailsList_)
        {
            SetFocus(detailsList_);
        }
        return true;
    }

    void BrowserPane::SetRawJpegStackingEnabled(bool enabled)
    {
        if (rawJpegStackingEnabled_ == enabled)
        {
            return;
        }

        rawJpegStackingEnabled_ = enabled;
        if (hwnd_)
        {
            RefreshFromModel();
        }
    }

    bool BrowserPane::IsRawJpegStackingEnabled() const noexcept
    {
        return rawJpegStackingEnabled_;
    }

    void BrowserPane::SetRawJpegDisplayPreference(RawJpegDisplayPreference preference)
    {
        if (rawJpegDisplayPreference_ == preference)
        {
            return;
        }

        rawJpegDisplayPreference_ = preference;
        if (rawJpegStackingEnabled_ && hwnd_)
        {
            RefreshFromModel();
        }
    }

    RawJpegDisplayPreference BrowserPane::GetRawJpegDisplayPreference() const noexcept
    {
        return rawJpegDisplayPreference_;
    }

    void BrowserPane::SetThumbnailSizePreset(ThumbnailSizePreset preset)
    {
        if (thumbnailSizePreset_ == preset)
        {
            return;
        }

        thumbnailSizePreset_ = preset;
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    ThumbnailSizePreset BrowserPane::GetThumbnailSizePreset() const noexcept
    {
        return thumbnailSizePreset_;
    }

    void BrowserPane::SetCompactThumbnailLayout(bool enabled)
    {
        if (compactThumbnailLayout_ == enabled)
        {
            return;
        }

        compactThumbnailLayout_ = enabled;
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    bool BrowserPane::IsCompactThumbnailLayoutEnabled() const noexcept
    {
        return compactThumbnailLayout_;
    }

    void BrowserPane::SetThumbnailDetailsVisible(bool visible)
    {
        if (thumbnailDetailsVisible_ == visible)
        {
            return;
        }

        thumbnailDetailsVisible_ = visible;
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    bool BrowserPane::AreThumbnailDetailsVisible() const noexcept
    {
        return thumbnailDetailsVisible_;
    }

    void BrowserPane::SetAppTextSize(hyperbrowse::util::AppTextSize size)
    {
        size = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(size));
        if (appTextSize_ == size)
        {
            return;
        }

        appTextSize_ = size;
        RebuildDetailsListFont();
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    hyperbrowse::util::AppTextSize BrowserPane::GetAppTextSize() const noexcept
    {
        return appTextSize_;
    }

    void BrowserPane::SetResourceProfile(hyperbrowse::util::ResourceProfile profile)
    {
        SetResourceAndCacheSettings(profile,
                                    prefetchDepthOverride_,
                                    thumbnailCacheCapacityOverrideBytes_,
                                    metadataCacheCapacityOverrideEntries_,
                                    persistentThumbnailCacheCapacityOverrideBytes_);
    }

    void BrowserPane::SetResourceAndCacheSettings(hyperbrowse::util::ResourceProfile profile,
                                                  int prefetchDepthOverride,
                                                  std::size_t thumbnailCacheCapacityBytes,
                                                  std::size_t metadataCacheCapacityEntries,
                                                  std::size_t persistentThumbnailCacheCapacityBytes)
    {
        const int normalizedPrefetchDepthOverride = prefetchDepthOverride == hyperbrowse::util::kAutomaticPrefetchDepth
            ? hyperbrowse::util::kAutomaticPrefetchDepth
            : std::clamp(prefetchDepthOverride,
                         hyperbrowse::util::kMinimumPrefetchDepth,
                         hyperbrowse::util::kMaximumPrefetchDepth);
        const bool resourceProfileChanged = resourceProfile_ != profile;
        const bool prefetchDepthChanged = prefetchDepthOverride_ != normalizedPrefetchDepthOverride;
        const bool cacheCapacityChanged = thumbnailCacheCapacityOverrideBytes_ != thumbnailCacheCapacityBytes
            || metadataCacheCapacityOverrideEntries_ != metadataCacheCapacityEntries
            || persistentThumbnailCacheCapacityOverrideBytes_ != persistentThumbnailCacheCapacityBytes;
        if (!resourceProfileChanged && !prefetchDepthChanged && !cacheCapacityChanged)
        {
            return;
        }

        resourceProfile_ = profile;
        prefetchDepthOverride_ = normalizedPrefetchDepthOverride;
        thumbnailCacheCapacityOverrideBytes_ = thumbnailCacheCapacityBytes;
        metadataCacheCapacityOverrideEntries_ = metadataCacheCapacityEntries;
        persistentThumbnailCacheCapacityOverrideBytes_ = persistentThumbnailCacheCapacityBytes;

        if (resourceProfileChanged || cacheCapacityChanged)
        {
            ++thumbnailSessionId_;
            ++metadataSessionId_;
            ++thumbnailRequestEpoch_;
            HideThumbnailTooltip();
            RecreateBackgroundServices();
            if (hwnd_)
            {
                RefreshFromModel();
            }
            return;
        }

        ScheduleVisibleThumbnailWork();
        ScheduleVisibleMetadataWork();
    }

    void BrowserPane::SetPrefetchDepthOverride(int depth)
    {
        const int normalizedDepth = depth == hyperbrowse::util::kAutomaticPrefetchDepth
            ? hyperbrowse::util::kAutomaticPrefetchDepth
            : std::clamp(depth,
                         hyperbrowse::util::kMinimumPrefetchDepth,
                         hyperbrowse::util::kMaximumPrefetchDepth);
        if (prefetchDepthOverride_ == normalizedDepth)
        {
            return;
        }

        prefetchDepthOverride_ = normalizedDepth;
        ScheduleVisibleThumbnailWork();
        ScheduleVisibleMetadataWork();
    }

    int BrowserPane::EffectivePrefetchDepth() const noexcept
    {
        if (thumbnailMemoryPressureActive_)
        {
            return hyperbrowse::util::kMinimumPrefetchDepth;
        }

        return hyperbrowse::util::ResolvePrefetchDepth(resourceProfile_, prefetchDepthOverride_);
    }

    void BrowserPane::SetPerformanceHudText(std::wstring text)
    {
        if (performanceHudText_ == text)
        {
            return;
        }

        performanceHudText_ = std::move(text);
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void BrowserPane::SetThumbnailMemoryPressureActive(bool active)
    {
        if (thumbnailMemoryPressureActive_ == active)
        {
            return;
        }

        thumbnailMemoryPressureActive_ = active;
        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->SetPressureModeEnabled(active);
            if (active)
            {
                thumbnailScheduler_->TrimCacheToBytes(std::max<std::size_t>(1, thumbnailScheduler_->CacheCapacityBytes() / 2));
            }
        }
        if (metadataService_)
        {
            metadataService_->SetPressureModeEnabled(active);
            if (active)
            {
                metadataService_->TrimCacheToEntries(std::max<std::size_t>(1, metadataService_->CacheCapacityEntries() / 2));
            }
        }
    }

    void BrowserPane::SetCacheCapacityOverrides(std::size_t thumbnailCacheCapacityBytes,
                                                std::size_t metadataCacheCapacityEntries,
                                                std::size_t persistentThumbnailCacheCapacityBytes)
    {
        SetResourceAndCacheSettings(resourceProfile_,
                                    prefetchDepthOverride_,
                                    thumbnailCacheCapacityBytes,
                                    metadataCacheCapacityEntries,
                                    persistentThumbnailCacheCapacityBytes);
    }

    void BrowserPane::SetPersistentThumbnailCacheEnabled(bool enabled)
    {
        persistentThumbnailCacheEnabled_ = enabled;
        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->SetDiskCacheEnabled(enabled);
        }
    }

    bool BrowserPane::IsPersistentThumbnailCacheEnabled() const noexcept
    {
        return persistentThumbnailCacheEnabled_;
    }

    std::size_t BrowserPane::ThumbnailCacheCapacityBytes() const noexcept
    {
        return thumbnailScheduler_ ? thumbnailScheduler_->CacheCapacityBytes() : 0;
    }

    std::size_t BrowserPane::ThumbnailCacheBytes() const
    {
        return thumbnailScheduler_ ? thumbnailScheduler_->CacheBytes() : 0;
    }

    hyperbrowse::cache::ThumbnailCache::Statistics BrowserPane::ThumbnailCacheStatistics() const
    {
        return thumbnailScheduler_ ? thumbnailScheduler_->GetCacheStatistics()
                                   : hyperbrowse::cache::ThumbnailCache::Statistics{};
    }

    hyperbrowse::services::ThumbnailScheduler::RuntimeStatistics BrowserPane::ThumbnailRuntimeStatistics() const
    {
        return thumbnailScheduler_ ? thumbnailScheduler_->GetRuntimeStatistics()
                                   : hyperbrowse::services::ThumbnailScheduler::RuntimeStatistics{};
    }

    bool BrowserPane::QueuePersistentThumbnailCacheStatistics(
        hyperbrowse::services::ThumbnailScheduler::PersistentCacheStatisticsCallback callback,
        bool includeSourceFilePaths)
    {
        return thumbnailScheduler_
            && thumbnailScheduler_->QueuePersistentCacheStatistics(std::move(callback), includeSourceFilePaths);
    }

    bool BrowserPane::QueuePersistentThumbnailCacheMaintenance(
        bool purge,
        hyperbrowse::services::ThumbnailScheduler::PersistentCacheOperationCallback callback)
    {
        return thumbnailScheduler_
            && thumbnailScheduler_->QueuePersistentCacheMaintenance(purge, std::move(callback));
    }

    std::size_t BrowserPane::MetadataCacheCapacityEntries() const noexcept
    {
        return metadataService_ ? metadataService_->CacheCapacityEntries() : 0;
    }

    std::size_t BrowserPane::MetadataCacheEntryCount() const
    {
        return metadataService_ ? metadataService_->CacheEntryCount() : 0;
    }

    hyperbrowse::services::ImageMetadataService::CacheStatistics BrowserPane::MetadataCacheStatistics() const
    {
        return metadataService_ ? metadataService_->GetCacheStatistics()
                                : hyperbrowse::services::ImageMetadataService::CacheStatistics{};
    }

    void BrowserPane::SetDarkTheme(bool enabled)
    {
        darkTheme_ = enabled;
        colors_ = MakeThemeColors(enabled);

        RebuildThemeResources();
        if (d2dRenderTarget_)
        {
            RebuildD2DBrushes();
        }
        ApplyThemeToDetailsList();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::RecreateBackgroundServices()
    {
        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->CancelOutstanding();
            thumbnailScheduler_->BindTargetWindow(nullptr);
        }
        if (metadataService_)
        {
            metadataService_->CancelOutstanding();
            metadataService_->BindTargetWindow(nullptr);
        }

        if ((thumbnailScheduler_ || metadataService_) && !serviceRetirementExecutor_)
        {
            serviceRetirementExecutor_ = std::make_unique<util::BackgroundExecutor>(2, 0, true);
        }
        if (thumbnailScheduler_
            && !serviceRetirementExecutor_->PostDestruction(std::move(thumbnailScheduler_)))
        {
            util::LogError(L"Could not queue the old thumbnail scheduler for background retirement.");
        }
        if (metadataService_
            && !serviceRetirementExecutor_->PostDestruction(std::move(metadataService_)))
        {
            util::LogError(L"Could not queue the old metadata service for background retirement.");
        }

        thumbnailScheduler_ = std::make_unique<services::ThumbnailScheduler>(
            thumbnailCacheCapacityOverrideBytes_,
            0,
            resourceProfile_,
            std::function<void()>{},
            std::function<void()>{},
            persistentThumbnailCacheCapacityOverrideBytes_);
        metadataService_ = std::make_unique<services::ImageMetadataService>(
            0,
            metadataCacheCapacityOverrideEntries_,
            resourceProfile_);

        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->SetDiskCacheEnabled(persistentThumbnailCacheEnabled_);
            if (hwnd_)
            {
                thumbnailScheduler_->BindTargetWindow(hwnd_);
            }
            thumbnailScheduler_->SetPressureModeEnabled(thumbnailMemoryPressureActive_);
            if (thumbnailMemoryPressureActive_)
            {
                thumbnailScheduler_->TrimCacheToBytes(std::max<std::size_t>(1, thumbnailScheduler_->CacheCapacityBytes() / 2));
            }
        }

        if (metadataService_ && hwnd_)
        {
            metadataService_->BindTargetWindow(hwnd_);
        }
        if (metadataService_)
        {
            metadataService_->SetPressureModeEnabled(thumbnailMemoryPressureActive_);
            if (thumbnailMemoryPressureActive_)
            {
                metadataService_->TrimCacheToEntries(std::max<std::size_t>(1, metadataService_->CacheCapacityEntries() / 2));
            }
        }
    }

    void BrowserPane::ClearSelection()
    {
        selectedModelIndices_.clear();
        rubberBandSeedSelection_.clear();
        anchorModelIndex_ = -1;
        focusedModelIndex_ = -1;
        selectedBytes_ = 0;
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    std::uint64_t BrowserPane::SelectedCount() const noexcept
    {
        return static_cast<std::uint64_t>(selectedModelIndices_.size());
    }

    std::uint64_t BrowserPane::SelectedBytes() const noexcept
    {
        return selectedBytes_;
    }

    int BrowserPane::PrimarySelectedModelIndex() const noexcept
    {
        if (focusedModelIndex_ >= 0)
        {
            return focusedModelIndex_;
        }

        if (!selectedModelIndices_.empty())
        {
            return *selectedModelIndices_.begin();
        }

        return -1;
    }

    std::vector<int> BrowserPane::OrderedModelIndicesSnapshot() const
    {
        return orderedModelIndices_;
    }

    std::vector<int> BrowserPane::OrderedSelectedModelIndicesSnapshot() const
    {
        std::vector<int> orderedSelection;
        orderedSelection.reserve(selectedModelIndices_.size());
        for (const int modelIndex : orderedModelIndices_)
        {
            if (selectedModelIndices_.contains(modelIndex))
            {
                orderedSelection.push_back(modelIndex);
            }
        }
        return orderedSelection;
    }

    std::vector<std::wstring> BrowserPane::SelectedFilePathsSnapshot() const
    {
        std::vector<std::wstring> filePaths;
        if (!model_)
        {
            return filePaths;
        }

        const auto& items = model_->Items();
        for (const int modelIndex : OrderedSelectedModelIndicesSnapshot())
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(items.size()))
            {
                filePaths.push_back(items[static_cast<std::size_t>(modelIndex)].filePath);
            }
        }

        return filePaths;
    }

    std::wstring BrowserPane::FocusedFilePathSnapshot() const
    {
        if (!model_ || focusedModelIndex_ < 0)
        {
            return {};
        }

        const auto& items = model_->Items();
        return focusedModelIndex_ < static_cast<int>(items.size())
            ? items[static_cast<std::size_t>(focusedModelIndex_)].filePath
            : std::wstring{};
    }

    void BrowserPane::RestoreSelectionByFilePaths(const std::vector<std::wstring>& filePaths, const std::wstring& focusedFilePath)
    {
        selectedModelIndices_.clear();
        anchorModelIndex_ = -1;
        focusedModelIndex_ = -1;

        if (!model_)
        {
            return;
        }

        for (const std::wstring& filePath : filePaths)
        {
            const int modelIndex = model_->FindItemIndexByPath(filePath);
            if (modelIndex >= 0)
            {
                selectedModelIndices_.insert(modelIndex);
                if (anchorModelIndex_ < 0)
                {
                    anchorModelIndex_ = modelIndex;
                }
            }
        }

        if (!focusedFilePath.empty())
        {
            focusedModelIndex_ = model_->FindItemIndexByPath(focusedFilePath);
        }
        if (focusedModelIndex_ < 0 && !selectedModelIndices_.empty())
        {
            focusedModelIndex_ = *selectedModelIndices_.begin();
        }

        PruneSelectionToVisibleItems();
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::AcknowledgeStateChangedMessage() noexcept
    {
        stateChangedMessagePending_ = false;
    }

    void BrowserPane::EnsureFocusedItemVisible()
    {
        if (!hwnd_ || !model_)
        {
            return;
        }

        int targetModelIndex = focusedModelIndex_;
        if (targetModelIndex < 0 && !selectedModelIndices_.empty())
        {
            targetModelIndex = *selectedModelIndices_.begin();
        }

        if (targetModelIndex < 0)
        {
            return;
        }

        const int viewIndex = ViewIndexFromModelIndex(targetModelIndex);
        if (viewIndex < 0)
        {
            return;
        }

        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            EnsureThumbnailViewIndexVisible(viewIndex);
            return;
        }

        if (viewMode_ == BrowserViewMode::Details && detailsList_)
        {
            ListView_EnsureVisible(detailsList_, viewIndex, FALSE);
        }
    }

    void BrowserPane::InvalidateMediaCacheForPaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        d2dBitmapCache_.clear();

        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->CancelOutstanding();
            thumbnailScheduler_->InvalidateFilePaths(filePaths);
        }
        if (metadataService_)
        {
            metadataService_->InvalidateFilePaths(filePaths);
        }

        ScheduleVisibleThumbnailWork();
    }

    std::shared_ptr<const hyperbrowse::services::ImageMetadata> BrowserPane::FindCachedMetadataForModelIndex(int modelIndex) const
    {
        if (!metadataService_ || !model_ || modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
        {
            return nullptr;
        }

        return metadataService_->FindCachedMetadata(model_->Items()[static_cast<std::size_t>(modelIndex)]);
    }

    void BrowserPane::RequestMetadataForModelIndices(const std::vector<int>& modelIndices) const
    {
        if (!metadataService_ || !model_ || modelIndices.empty())
        {
            return;
        }

        std::vector<services::MetadataWorkItem> workItems;
        workItems.reserve(modelIndices.size());
        for (const int modelIndex : modelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
            {
                continue;
            }

            const BrowserItem& item = model_->Items()[static_cast<std::size_t>(modelIndex)];
            if (item.isDirectory)
            {
                continue;
            }

            workItems.push_back(services::MetadataWorkItem{
                modelIndex,
                item,
                kVisiblePriority,
            });
        }

        if (!workItems.empty())
        {
            metadataService_->Schedule(metadataSessionId_, std::move(workItems));
        }
    }

    std::wstring BrowserPane::BuildMetadataReportForModelIndex(int modelIndex) const
    {
        if (!model_ || modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
        {
            return {};
        }

        const BrowserItem& item = model_->Items()[static_cast<std::size_t>(modelIndex)];
        if (item.isDirectory)
        {
            return L"Folders do not have image metadata.";
        }

        std::wstring errorMessage;
        const auto metadata = FindCachedMetadataForModelIndex(modelIndex)
            ? FindCachedMetadataForModelIndex(modelIndex)
            : services::ExtractImageMetadata(item, &errorMessage);
        if (!metadata)
        {
            return errorMessage.empty() ? L"No metadata is available for the selected image." : errorMessage;
        }

        return services::FormatImageMetadataReport(item, *metadata);
    }

    std::wstring BrowserPane::ThumbnailDecodeFailureMessageForModelIndex(int modelIndex) const
    {
        if (!model_ || !thumbnailScheduler_ || modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
        {
            return {};
        }

        const BrowserItem& item = model_->Items()[static_cast<std::size_t>(modelIndex)];
        if (item.isDirectory || !decode::CanDecodeThumbnail(item))
        {
            return {};
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        return thumbnailScheduler_->KnownFailureMessage(MakeThumbnailCacheKey(item, layout.previewWidth, layout.previewHeight));
    }

    bool BrowserPane::RegisterClass() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kClassName, &windowClass) != FALSE)
        {
            return true;
        }

        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &BrowserPane::WindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        return RegisterClassExW(&windowClass) != 0;
    }

    void BrowserPane::ScheduleMetadataForItem(int modelIndex, const BrowserItem& item) const
    {
        if (!metadataService_ || modelIndex < 0 || item.isDirectory)
        {
            return;
        }

        metadataService_->Schedule(metadataSessionId_, services::MetadataWorkItem{modelIndex, item, 0});
    }

    void BrowserPane::ScheduleVisibleMetadataWork() const
    {
        if (!metadataService_ || !model_ || orderedModelIndices_.empty())
        {
            return;
        }

        std::vector<services::MetadataWorkItem> workItems;
        workItems.reserve(128);

        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            if (!hwnd_)
            {
                return;
            }

            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
            const int clientHeight = clientRect.bottom - clientRect.top;
            const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
            const int verticalStride = layout.itemHeight + layout.cellPadding;
            const int firstVisibleRow = std::max(0, scrollOffsetY_ / verticalStride);
            const int lastVisibleRow = std::max(firstVisibleRow, (scrollOffsetY_ + clientHeight) / verticalStride);
            const int visibleRowCount = std::max(1, (lastVisibleRow - firstVisibleRow) + 1);
            const int prefetchDepth = EffectivePrefetchDepth();
            const int proactivePrefetchRows = scrollOffsetY_ == 0
                ? std::clamp(visibleRowCount * TopOfFolderMetadataWarmUpMultiplier(prefetchDepth),
                             kMetadataMinimumTopOfFolderPrefetchRows,
                             kMetadataMaximumTopOfFolderPrefetchRows)
                : std::clamp(visibleRowCount * LookAheadMetadataWarmUpMultiplier(prefetchDepth),
                             kMetadataMinimumLookAheadPrefetchRows,
                             kMetadataMaximumLookAheadPrefetchRows);
            const int requestStartRow = std::max(0, firstVisibleRow - kMetadataNearVisiblePrefetchRows);
            const int requestEndRow = lastVisibleRow + kMetadataNearVisiblePrefetchRows + proactivePrefetchRows;
            const int firstIndex = requestStartRow * columns;
            const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (requestEndRow + 1) * columns);

            workItems.reserve(static_cast<std::size_t>(std::max(0, lastIndex - firstIndex)));
            for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
            {
                const BrowserItem* item = ItemFromViewIndex(viewIndex);
                if (!item || item->isDirectory)
                {
                    continue;
                }

                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex < 0)
                {
                    continue;
                }

                const int row = viewIndex / columns;
                int priority = kProactivePrefetchPriority;
                if (row >= firstVisibleRow && row <= lastVisibleRow)
                {
                    priority = kVisiblePriority;
                }
                else if (row >= (firstVisibleRow - kMetadataNearVisiblePrefetchRows)
                    && row <= (lastVisibleRow + kMetadataNearVisiblePrefetchRows))
                {
                    priority = kNearVisiblePriority;
                }

                workItems.push_back(services::MetadataWorkItem{modelIndex, *item, priority});
            }
        }
        else if (viewMode_ == BrowserViewMode::Details && detailsList_)
        {
            const int topIndex = std::max(0, ListView_GetTopIndex(detailsList_));
            const int visibleCount = std::max(1, ListView_GetCountPerPage(detailsList_));
            const int nearVisibleCount = std::max(visibleCount, kMinimumDetailsMetadataNearVisibleItems);
            const int prefetchDepth = EffectivePrefetchDepth();
            const int proactivePrefetchCount = topIndex == 0
                ? std::clamp(visibleCount * TopOfFolderMetadataWarmUpMultiplier(prefetchDepth),
                             kMinimumDetailsMetadataTopOfFolderItems,
                             kMaximumDetailsMetadataTopOfFolderItems)
                : std::clamp(visibleCount * LookAheadMetadataWarmUpMultiplier(prefetchDepth),
                             kMinimumDetailsMetadataLookAheadItems,
                             kMaximumDetailsMetadataLookAheadItems);
            const int firstIndex = std::max(0, topIndex - nearVisibleCount);
            const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()),
                                           topIndex + visibleCount + nearVisibleCount + proactivePrefetchCount);
            const int visibleEndIndex = std::min(lastIndex - 1, topIndex + visibleCount - 1);
            const int nearEndIndex = std::min(lastIndex - 1, topIndex + visibleCount + nearVisibleCount - 1);

            workItems.reserve(static_cast<std::size_t>(std::max(0, lastIndex - firstIndex)));
            for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
            {
                const BrowserItem* item = ItemFromViewIndex(viewIndex);
                if (!item || item->isDirectory)
                {
                    continue;
                }

                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex < 0)
                {
                    continue;
                }

                int priority = kProactivePrefetchPriority;
                if (viewIndex >= topIndex && viewIndex <= visibleEndIndex)
                {
                    priority = kVisiblePriority;
                }
                else if (viewIndex >= firstIndex && viewIndex <= nearEndIndex)
                {
                    priority = kNearVisiblePriority;
                }

                workItems.push_back(services::MetadataWorkItem{modelIndex, *item, priority});
            }
        }

        if (!workItems.empty())
        {
            metadataService_->Schedule(metadataSessionId_, std::move(workItems));
        }
    }

    void BrowserPane::ScheduleVisibleThumbnailWork()
    {
        ScheduleVisibleMetadataWork();

        if (!thumbnailScheduler_)
        {
            return;
        }

        if (!hwnd_ || viewMode_ != BrowserViewMode::Thumbnails || !model_ || orderedModelIndices_.empty())
        {
            CancelThumbnailWork();
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstVisibleRow = std::max(0, scrollOffsetY_ / verticalStride);
        if (thumbnailScrollbarThumbTracking_
            && firstVisibleRow == lastScheduledThumbnailViewportRow_)
        {
            return;
        }
        const int lastVisibleRow = std::max(firstVisibleRow, (scrollOffsetY_ + clientHeight) / verticalStride);
        const int visibleRowCount = std::max(1, (lastVisibleRow - firstVisibleRow) + 1);
        const bool activelyScrolling = thumbnailScrollbarThumbTracking_ || smoothScrollTimerId_ != 0;
        const bool loadingFolder = model_->IsEnumerating();
        const int prefetchDepth = EffectivePrefetchDepth();
        const int proactivePrefetchRows = activelyScrolling
            ? std::clamp(prefetchDepth,
                         kThumbnailActiveScrollPrefetchRows,
                         kThumbnailMaximumLookAheadPrefetchRows)
            : (loadingFolder
                ? kThumbnailNearVisiblePrefetchRows
                : (scrollOffsetY_ == 0
                    ? std::clamp(visibleRowCount * TopOfFolderThumbnailWarmUpMultiplier(prefetchDepth),
                                 kThumbnailMinimumTopOfFolderPrefetchRows,
                                 kThumbnailMaximumTopOfFolderPrefetchRows)
                    : std::clamp(visibleRowCount * LookAheadThumbnailWarmUpMultiplier(prefetchDepth),
                                 kThumbnailMinimumLookAheadPrefetchRows,
                                 kThumbnailMaximumLookAheadPrefetchRows)));
        const int backwardPrefetchRows = thumbnailScrollDirection_ < 0
            ? proactivePrefetchRows
            : kThumbnailNearVisiblePrefetchRows;
        const int forwardPrefetchRows = thumbnailScrollDirection_ > 0
            ? proactivePrefetchRows
            : kThumbnailNearVisiblePrefetchRows;
        const int requestStartRow = std::max(0, firstVisibleRow - backwardPrefetchRows);
        const int requestEndRow = lastVisibleRow + forwardPrefetchRows;
        const int firstIndex = requestStartRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (requestEndRow + 1) * columns);
        const int targetWidth = layout.previewWidth;
        const int targetHeight = layout.previewHeight;

        std::vector<services::ThumbnailWorkItem> workItems;
        workItems.reserve(static_cast<std::size_t>(std::max(0, lastIndex - firstIndex)));
        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item || !decode::CanDecodeThumbnail(*item))
            {
                continue;
            }

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            if (modelIndex < 0)
            {
                continue;
            }

            const int row = viewIndex / columns;
            int priority = kProactivePrefetchPriority;
            if (row >= firstVisibleRow && row <= lastVisibleRow)
            {
                priority = kVisiblePriority;
            }
            else if (row >= (firstVisibleRow - kThumbnailNearVisiblePrefetchRows)
                && row <= (lastVisibleRow + kThumbnailNearVisiblePrefetchRows))
            {
                priority = kNearVisiblePriority;
            }

            const bool preferCpu = priority <= kNearVisiblePriority;

            workItems.push_back(services::ThumbnailWorkItem{
                modelIndex,
                MakeThumbnailCacheKey(*item, targetWidth, targetHeight),
                priority,
                preferCpu,
            });
        }

        ++thumbnailRequestEpoch_;
        lastScheduledThumbnailViewportRow_ = firstVisibleRow;
        thumbnailScheduler_->Schedule(thumbnailSessionId_, thumbnailRequestEpoch_, std::move(workItems));
    }

    bool BrowserPane::HasVisibleCachedThumbnail() const
    {
        if (!thumbnailScheduler_
            || !hwnd_
            || viewMode_ != BrowserViewMode::Thumbnails
            || orderedModelIndices_.empty())
        {
            return false;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstVisibleRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastVisibleRow = std::max(firstVisibleRow, (scrollOffsetY_ + clientHeight) / verticalStride);
        const int firstIndex = firstVisibleRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (lastVisibleRow + 1) * columns);

        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item || !decode::CanDecodeThumbnail(*item))
            {
                continue;
            }

            const auto cacheKey = MakeThumbnailCacheKey(*item, layout.previewWidth, layout.previewHeight);
            if (thumbnailScheduler_->FindCachedThumbnail(cacheKey))
            {
                return true;
            }
        }

        return false;
    }

    void BrowserPane::CancelThumbnailWork()
    {
        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->CancelOutstanding();
        }
    }

    void BrowserPane::InvalidateThumbnailCellForModelIndex(int modelIndex) const
    {
        if (viewMode_ != BrowserViewMode::Thumbnails || !hwnd_)
        {
            return;
        }

        const int viewIndex = ViewIndexFromModelIndex(modelIndex);
        if (viewIndex < 0)
        {
            return;
        }

        RECT invalidRect = GetThumbnailCellRect(viewIndex);
        InflateRect(&invalidRect, 1, 1);
        InvalidateRect(hwnd_, &invalidRect, FALSE);
    }
}
