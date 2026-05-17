#include "viewer/ViewerWindow.h"

#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <memory>

#include "app/resource.h"
#include "decode/ImageDecoder.h"
#include "render/D2DRenderer.h"
#include "util/ResourcePng.h"
#include "util/Diagnostics.h"
#include "util/Log.h"

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";
    constexpr int kPlaceholderBrandArtSize = 256;
    constexpr bool kEnableFullImagePrefetch = false;
    constexpr std::size_t kViewerFullImageCacheBytes = 256ULL * 1024ULL * 1024ULL;

    std::wstring FormatWindowHandle(HWND hwnd)
    {
        return std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
    }

    hyperbrowse::cache::ThumbnailCacheKey MakeViewerFullImageCacheKey(const hyperbrowse::browser::BrowserItem& item)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = item.filePath;
        key.modifiedTimestampUtc = item.modifiedTimestampUtc;
        key.targetWidth = 0;
        key.targetHeight = 0;
        return key;
    }

    hyperbrowse::cache::ThumbnailCache& ViewerFullImageCache()
    {
        static hyperbrowse::cache::ThumbnailCache cache(kViewerFullImageCacheBytes);
        return cache;
    }

    float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - (2.0f * value));
    }

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

    COLORREF BackgroundColor(bool darkTheme)
    {
        return darkTheme ? RGB(18, 21, 25) : RGB(247, 249, 252);
    }

    COLORREF TextColor(bool darkTheme)
    {
        return darkTheme ? RGB(236, 240, 244) : RGB(28, 33, 40);
    }

    COLORREF MutedTextColor(bool darkTheme)
    {
        return darkTheme ? RGB(165, 176, 188) : RGB(96, 107, 118);
    }

    COLORREF PanelFillColor(bool darkTheme)
    {
        return darkTheme ? RGB(28, 33, 39) : RGB(255, 255, 255);
    }

    COLORREF PanelBorderColor(bool darkTheme)
    {
        return darkTheme ? RGB(70, 80, 94) : RGB(206, 215, 225);
    }

    bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    bool LoadViewerInfoOverlaysVisibleSetting()
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return true;
        }

        DWORD value = 1;
        const bool foundValue = TryReadDwordValue(key, kRegistryValueViewerInfoOverlaysVisible, &value);
        RegCloseKey(key);
        return !foundValue || value != 0;
    }

    void SaveViewerInfoOverlaysVisibleSetting(bool visible)
    {
        HKEY key{};
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            kRegistryPath,
                            0,
                            nullptr,
                            0,
                            KEY_WRITE,
                            nullptr,
                            &key,
                            &disposition) != ERROR_SUCCESS)
        {
            return;
        }

        WriteDwordValue(key, kRegistryValueViewerInfoOverlaysVisible, visible ? 1UL : 0UL);
        RegCloseKey(key);
    }

}

namespace hyperbrowse::viewer
{
    ViewerWindow::ViewerWindow(HINSTANCE instance)
        : instance_(instance)
        , asyncState_(std::make_shared<AsyncState>())
        , infoOverlaysVisible_(LoadViewerInfoOverlaysVisibleSetting())
    {
        backgroundBrush_ = CreateSolidBrush(BackgroundColor(false));
        statusArt_ = util::LoadPngResourceBitmap(instance_,
                                                 IDB_HYPERBROWSE_BRAND_PNG,
                                                 kPlaceholderBrandArtSize,
                                                 kPlaceholderBrandArtSize);
        // Two workers cover the common case (one for the foreground decode, one for
        // a single adjacent prefetch). Bursty navigation cannot exceed two concurrent
        // OS threads, in contrast to the prior std::async approach.
        backgroundExecutor_ = std::make_unique<util::BackgroundExecutor>(2);
    }

    ViewerWindow::~ViewerWindow()
    {
        asyncState_->shutdown.store(true, std::memory_order_release);
        asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
        asyncState_->targetWindow = nullptr;
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
        compareMode_ = false;
        compareDirection_ = CompareDirection::Next;
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
            windowedStyle_ = 0;
            windowedExStyle_ = 0;
            windowedPlacement_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
        }

        if (!hwnd_)
        {
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
        SetForegroundWindow(hwnd_);
        LoadCurrentImageAsync(LoadReason::Open);
        return true;
    }

    HWND ViewerWindow::Hwnd() const noexcept
    {
        return hwnd_;
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
        return currentIndex_;
    }

    std::wstring ViewerWindow::CurrentFilePath() const
    {
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

    void ViewerWindow::StartSlideshow(UINT intervalMs)
    {
        if (!hwnd_ || items_.size() < 2)
        {
            return;
        }

        slideshowIntervalMs_ = std::max<UINT>(1000, intervalMs);
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
    }

    bool ViewerWindow::IsSlideshowActive() const noexcept
    {
        return slideshowActive_;
    }

    void ViewerWindow::SetCompareMode(bool enabled, CompareDirection direction)
    {
        if (!enabled)
        {
            compareMode_ = false;
            d2dCompareImageBitmap_.Reset();
            d2dCompareImageIndex_ = -1;
            StopTransition();
            UpdateWindowTitle();
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
        return compareMode_;
    }

    void ViewerWindow::SetMouseWheelBehavior(MouseWheelBehavior behavior) noexcept
    {
        mouseWheelBehavior_ = behavior;
    }

    void ViewerWindow::SetTransitionSettings(TransitionStyle style, UINT durationMs)
    {
        transitionStyle_ = style;
        transitionDurationMs_ = std::clamp<UINT>(durationMs, 120U, 5000U);

        if (transitionStyle_ == TransitionStyle::Cut)
        {
            StopTransition();
        }
    }

    void ViewerWindow::SetInfoOverlaysVisible(bool visible)
    {
        if (infoOverlaysVisible_ == visible)
        {
            return;
        }

        infoOverlaysVisible_ = visible;
        SaveViewerInfoOverlaysVisibleSetting(infoOverlaysVisible_);
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::SetMemoryPressureActive(bool active)
    {
        if (memoryPressureActive_ == active)
        {
            return;
        }

        memoryPressureActive_ = active;
        util::LogInfo(L"ViewerWindow memory pressure "
            + std::wstring(memoryPressureActive_ ? L"entered" : L"cleared")
            + L"; prefetch radius=" + std::to_wstring(EffectivePrefetchRadius()));

        if (memoryPressureActive_)
        {
            ViewerFullImageCache().TrimToBytes(std::max<std::size_t>(1, kViewerFullImageCacheBytes / 2));
        }

        slideshowNextPrefetchIndex_ = -1;
        slideshowNextPrefetchGeneration_ = 0;
        if (hwnd_ && currentImage_ && !pendingLoadActive_)
        {
            const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            ScheduleAdjacentPrefetch(navigationGeneration);
        }
    }

    void ViewerWindow::SetResourceProfile(util::ResourceProfile profile) noexcept
    {
        if (resourceProfile_ == profile)
        {
            return;
        }

        resourceProfile_ = profile;
        if (hwnd_ && currentImage_ && !pendingLoadActive_)
        {
            const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            ScheduleAdjacentPrefetch(navigationGeneration);
        }
    }

    void ViewerWindow::SetDarkTheme(bool enabled)
    {
        darkTheme_ = enabled;
        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        backgroundBrush_ = CreateSolidBrush(BackgroundColor(darkTheme_));
        if (d2dRenderTarget_)
        {
            RebuildD2DBrushes();
        }
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
    }

    bool ViewerWindow::ReplaceItems(std::vector<browser::BrowserItem> items, int selectedIndex)
    {
        if (items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return false;
        }

        items_ = std::move(items);
        currentIndex_ = selectedIndex;
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

    bool ViewerWindow::PrepareDeleteCurrent(std::wstring* sourcePath, std::wstring* preferredFocusPath)
    {
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return false;
        }

        if (sourcePath)
        {
            *sourcePath = items_[static_cast<std::size_t>(currentIndex_)].filePath;
        }
        if (preferredFocusPath)
        {
            preferredFocusPath->clear();
        }

        if (items_.size() == 1)
        {
            StopSlideshow();
            StopTransition();
            ResetCachedImageSlots();
            items_.clear();
            currentIndex_ = -1;
            currentImage_.reset();
            errorMessage_.clear();
            if (hwnd_ && IsWindow(hwnd_) != FALSE)
            {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
            return true;
        }

        const int preferredIndex = (currentIndex_ + 1 < static_cast<int>(items_.size()))
            ? currentIndex_ + 1
            : currentIndex_ - 1;
        if (preferredFocusPath && preferredIndex >= 0 && preferredIndex < static_cast<int>(items_.size()))
        {
            *preferredFocusPath = items_[static_cast<std::size_t>(preferredIndex)].filePath;
        }

        items_.erase(items_.begin() + currentIndex_);
        currentIndex_ = std::clamp(currentIndex_, 0, static_cast<int>(items_.size()) - 1);
        compareMode_ = false;
        compareDirection_ = CompareDirection::Next;
        d2dCompareImageBitmap_.Reset();
        d2dCompareImageIndex_ = -1;
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
            util::LogInfo(L"ViewerWindow created D2D render target for HWND " + FormatWindowHandle(hwnd_));
            RebuildD2DBrushes();

            d2dNameFormat_ = renderer.CreateTextFormat(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            d2dInfoFormat_ = renderer.CreateTextFormat(L"Segoe UI", 11.0f, DWRITE_FONT_WEIGHT_NORMAL);

            if (d2dNameFormat_)
            {
                d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                d2dNameFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                d2dNameFormat_->SetTrimming(&trimming, nullptr);
            }

            if (d2dInfoFormat_)
            {
                d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                d2dInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                d2dInfoFormat_->SetTrimming(&trimming, nullptr);
            }

            if (statusArt_)
            {
                d2dStatusArtBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *statusArt_);
            }
        }
    }

    void ViewerWindow::ReleaseD2DResources()
    {
        if (d2dRenderTarget_)
        {
            util::LogInfo(L"ViewerWindow releasing D2D resources for HWND " + FormatWindowHandle(hwnd_));
        }

        d2dCurrentImageBitmap_.Reset();
        d2dCompareImageBitmap_.Reset();
        transitionFromBitmap_.Reset();
        pendingTransitionFromBitmap_.Reset();
        d2dStatusArtBitmap_.Reset();
        d2dNameFormat_.Reset();
        d2dInfoFormat_.Reset();
        d2dBackgroundBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dPanelFillBrush_.Reset();
        d2dPanelBorderBrush_.Reset();
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

        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(BackgroundColor(darkTheme_)), d2dBackgroundBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(TextColor(darkTheme_)), d2dTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(MutedTextColor(darkTheme_)), d2dMutedTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelFillColor(darkTheme_)), d2dPanelFillBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelBorderColor(darkTheme_)), d2dPanelBorderBrush_.GetAddressOf());
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
        if (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(items_.size()))
        {
            title.append(L" - ");
            title.append(items_[static_cast<std::size_t>(currentIndex_)].fileName);
            title.append(L" (");
            title.append(std::to_wstring(currentIndex_ + 1));
            title.append(L"/");
            title.append(std::to_wstring(items_.size()));
            title.append(L")");
        }
        if (compareMode_ && ActiveCompareIndex() >= 0)
        {
            title.append(L" [Compare]");
        }

        SetWindowTextW(hwnd_, title.c_str());
    }

    void ViewerWindow::PrepareForImageChange(bool keepDisplayedImage)
    {
        errorMessage_.clear();
        loading_ = true;
        preserveDisplayedImageWhileLoading_ = keepDisplayedImage && currentImage_;
        if (preserveDisplayedImageWhileLoading_)
        {
            return;
        }

        ResetViewState();
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
        switch (resourceProfile_)
        {
        case util::ResourceProfile::Conservative:
            return 1;
        case util::ResourceProfile::Performance:
            return 3;
        case util::ResourceProfile::Balanced:
        default:
            return 2;
        }
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
        asyncState_->targetWindow = hwnd_;
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
            backgroundExecutor_->Post([asyncState, item, selectedIndex, requestId, navigationGeneration]()
            {
                std::wstring errorMessage;
                auto image = decode::DecodeFullImage(item, &errorMessage);

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

                HWND targetWindow = asyncState->targetWindow;
                if (!targetWindow || !PostMessageW(targetWindow, kDecodedImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            });
        }
    }

    void ViewerWindow::Navigate(int delta)
    {
        slideshowAdvancePending_ = false;

        if (items_.empty())
        {
            return;
        }

        const int nextIndex = std::clamp(currentIndex_ + delta, 0, static_cast<int>(items_.size()) - 1);
        if (nextIndex == currentIndex_)
        {
            return;
        }

        QueueTransitionFromCurrent(delta > 0);

        if (delta > 0)
        {
            if (nextSlot_.index == nextIndex && nextSlot_.image)
            {
                previousSlot_ = currentSlot_;
                SetCurrentImageSlot(nextSlot_.index, nextSlot_.image, true);
                nextSlot_ = {};
                currentIndex_ = nextIndex;
                prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
                util::IncrementCounter(L"viewer.prefetch.hit");
                util::RecordTiming(L"viewer.navigation", 0.0);
                PrepareForImageChange();
                loading_ = false;
                errorMessage_.clear();
                currentImage_ = currentSlot_.image;
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
        else if (delta < 0)
        {
            if (previousSlot_.index == nextIndex && previousSlot_.image)
            {
                nextSlot_ = currentSlot_;
                SetCurrentImageSlot(previousSlot_.index, previousSlot_.image, true);
                previousSlot_ = {};
                currentIndex_ = nextIndex;
                prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
                util::IncrementCounter(L"viewer.prefetch.hit");
                util::RecordTiming(L"viewer.navigation", 0.0);
                PrepareForImageChange();
                loading_ = false;
                errorMessage_.clear();
                currentImage_ = currentSlot_.image;
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
        LoadCurrentImageAsync(LoadReason::Navigation);
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

            HWND targetWindow = asyncState->targetWindow;
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
        backgroundExecutor_->Post([asyncState, item, index, navigationGeneration]()
        {
            std::wstring errorMessage;
            auto image = decode::DecodeFullImage(item, &errorMessage);

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

            HWND targetWindow = asyncState->targetWindow;
            if (!targetWindow || !PostMessageW(targetWindow, ViewerWindow::kPrefetchImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
            {
                return;
            }

            update.release();
        });
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

    void ViewerWindow::ZoomBy(double factor)
    {
        if (!currentImage_ || factor <= 0.0 || !hwnd_)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const double fitScale = FitScaleForClient(clientRect);
        const double baseScale = smoothZoomTimerId_
            ? smoothZoomTarget_
            : (zoomMode_ == ZoomMode::Fit ? fitScale : customZoomScale_);

        double targetScale = std::clamp(baseScale * factor, std::min(baseScale, fitScale), 16.0);

        if (factor < 1.0 && std::abs(targetScale - fitScale) < 0.0001)
        {
            if (smoothZoomTimerId_)
            {
                KillTimer(hwnd_, kSmoothZoomTimerId);
                smoothZoomTimerId_ = 0;
            }
            FitToWindow();
            return;
        }

        zoomMode_ = ZoomMode::Custom;
        smoothZoomTarget_ = targetScale;
        smoothZoomCurrent_ = customZoomScale_ > 0.0 ? customZoomScale_ : fitScale;

        if (!smoothZoomTimerId_)
        {
            smoothZoomTimerId_ = SetTimer(hwnd_, kSmoothZoomTimerId, kSmoothZoomIntervalMs, nullptr);
        }
        RequestRepaint();
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
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::RotateLeft()
    {
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
        rotationQuarterTurns_ = (rotationQuarterTurns_ + 1) % 4;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (hwnd_)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::ToggleCompareMode()
    {
        SetCompareMode(!compareMode_, compareDirection_);
    }

    void ViewerWindow::ActivateComparedImage()
    {
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
            QueueTransitionFromCurrent(true);
            previousSlot_ = currentSlot_;
            SetCurrentImageSlot(nextSlot_.index, nextSlot_.image, true);
            nextSlot_ = {};
            currentIndex_ = nextIndex;
            prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.hit");
            util::RecordTiming(L"viewer.navigation", 0.0);
            PrepareForImageChange();
            loading_ = false;
            errorMessage_.clear();
            currentImage_ = currentSlot_.image;
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
            QueueTransitionFromCurrent(true);
            currentIndex_ = 0;
            LoadCurrentImageAsync(LoadReason::Navigation);
            return;
        }

        Navigate(+1);
    }

    void ViewerWindow::ResetViewState()
    {
        zoomMode_ = ZoomMode::Fit;
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

    void ViewerWindow::QueueTransitionFromCurrent(bool forward)
    {
        StopTransition(false);

        pendingTransitionFromImage_.reset();
        pendingTransitionFromBitmap_.Reset();
        pendingTransitionFromIndex_ = -1;
        pendingTransitionForward_ = forward;

        if (transitionStyle_ == TransitionStyle::Cut || transitionDurationMs_ == 0 || !currentImage_)
        {
            return;
        }

        pendingTransitionFromImage_ = currentImage_;
        pendingTransitionFromIndex_ = DisplayedImageIndex();
        if (d2dCurrentImageIndex_ == pendingTransitionFromIndex_ && d2dCurrentImageBitmap_)
        {
            pendingTransitionFromBitmap_ = d2dCurrentImageBitmap_;
        }
    }

    void ViewerWindow::BeginTransitionFromPending()
    {
        if (!hwnd_
            || transitionStyle_ == TransitionStyle::Cut
            || transitionDurationMs_ == 0
            || !pendingTransitionFromImage_
            || !currentImage_
            || pendingTransitionFromIndex_ == currentIndex_)
        {
            StopTransition();
            return;
        }

        StopTransition(false);

        transitionFromImage_ = pendingTransitionFromImage_;
        transitionFromBitmap_ = pendingTransitionFromBitmap_;
        transitionFromIndex_ = pendingTransitionFromIndex_;
        transitionForward_ = pendingTransitionForward_;

        pendingTransitionFromImage_.reset();
        pendingTransitionFromBitmap_.Reset();
        pendingTransitionFromIndex_ = -1;

        if (d2dRenderTarget_)
        {
            auto& renderer = render::D2DRenderer::Instance();
            if (!transitionFromBitmap_ && transitionFromImage_)
            {
                transitionFromBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *transitionFromImage_);
            }

            if ((!d2dCurrentImageBitmap_ || d2dCurrentImageIndex_ != currentIndex_) && currentImage_)
            {
                const auto uploadStartedAt = std::chrono::steady_clock::now();
                d2dCurrentImageBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *currentImage_);
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
        transitionFromBitmap_.Reset();
        transitionFromIndex_ = -1;
        transitionStartedAt_ = std::chrono::steady_clock::time_point{};

        if (clearPending)
        {
            pendingTransitionFromImage_.reset();
            pendingTransitionFromBitmap_.Reset();
            pendingTransitionFromIndex_ = -1;
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

    double ViewerWindow::FitScaleForClient(const RECT& clientRect) const
    {
        if (!currentImage_)
        {
            return 1.0;
        }

        return FitScaleForImage(*currentImage_, clientRect);
    }

    double ViewerWindow::EffectiveScaleForClient(const RECT& clientRect) const
    {
        return zoomMode_ == ZoomMode::Fit ? FitScaleForClient(clientRect) : customZoomScale_;
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
        const float cx = ((clientWidth - destW) / 2.0f) + offsetX;
        const float cy = ((clientHeight - destH) / 2.0f) + offsetY;

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

    void ViewerWindow::RequestRepaint() const
    {
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        }
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
        if (deferredSwap)
        {
            ResetViewState();
            UpdateWindowTitle();
            NotifyZoomChanged(0);
        }

        SetCurrentImageSlot(update->index, std::move(update->image), false);
        errorMessage_ = std::move(update->errorMessage);
        loading_ = false;
        if (currentImage_)
        {
            ViewerFullImageCache().Insert(
                MakeViewerFullImageCacheKey(items_[static_cast<std::size_t>(update->index)]),
                currentImage_);
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

    LRESULT ViewerWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
            RequestRepaint();
            return 0;
        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RequestRepaint();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            NotifyActivityChanged(LOWORD(wParam) != WA_INACTIVE);
            return 0;
        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_DELETE:
                if (owner_ && IsWindow(owner_))
                {
                    SendMessageW(owner_, kDeleteRequestedMessage, 0, 0);
                }
                return 0;
            case VK_RIGHT:
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                {
                    SetCompareMode(true, CompareDirection::Next);
                }
                else
                {
                    Navigate(+1);
                }
                return 0;
            case VK_LEFT:
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                {
                    SetCompareMode(true, CompareDirection::Previous);
                }
                else
                {
                    Navigate(-1);
                }
                return 0;
            case VK_TAB:
                ToggleInfoOverlays();
                return 0;
            case VK_ADD:
            case VK_OEM_PLUS:
                ZoomBy(1.25);
                return 0;
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                ZoomBy(0.8);
                return 0;
            case '0':
                FitToWindow();
                return 0;
            case '1':
                SetActualSize();
                return 0;
            case 'L':
                RotateLeft();
                return 0;
            case 'R':
                RotateRight();
                return 0;
            case 'C':
                ToggleCompareMode();
                return 0;
            case 'X':
                ActivateComparedImage();
                return 0;
            case VK_SPACE:
                if (IsSlideshowActive())
                {
                    StopSlideshow();
                }
                else
                {
                    StartSlideshow();
                }
                return 0;
            case VK_F11:
                ToggleFullScreen();
                return 0;
            case VK_ESCAPE:
                if (fullScreen_)
                {
                    ToggleFullScreen();
                    return 0;
                }
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return 0;
            default:
                break;
            }
            break;
        case WM_MOUSEWHEEL:
        {
            const short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int wheelSteps = static_cast<int>(wheelDelta / WHEEL_DELTA);
            int stepCount = wheelSteps < 0 ? -wheelSteps : wheelSteps;
            if (stepCount == 0)
            {
                stepCount = 1;
            }

            if (mouseWheelBehavior_ == MouseWheelBehavior::Navigate)
            {
                const int navigateDelta = wheelDelta > 0 ? -1 : +1;
                for (int step = 0; step < stepCount; ++step)
                {
                    Navigate(navigateDelta);
                }
            }
            else
            {
                const double zoomFactor = wheelDelta > 0 ? 1.1 : 0.9;
                for (int step = 0; step < stepCount; ++step)
                {
                    ZoomBy(zoomFactor);
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (currentImage_)
            {
                panning_ = true;
                lastPanPoint_ = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                SetCapture(hwnd_);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (panning_)
            {
                const POINT currentPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                panOffsetX_ += static_cast<double>(currentPoint.x - lastPanPoint_.x);
                panOffsetY_ += static_cast<double>(currentPoint.y - lastPanPoint_.y);
                lastPanPoint_ = currentPoint;
                RequestRepaint();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (panning_)
            {
                panning_ = false;
                if (GetCapture() == hwnd_)
                {
                    ReleaseCapture();
                }
            }
            return 0;
        case WM_CAPTURECHANGED:
            panning_ = false;
            return 0;
        case WM_LBUTTONDBLCLK:
            ToggleFullScreen();
            return 0;
        case WM_TIMER:
            if (wParam == slideshowTimerId_)
            {
                AdvanceSlideshow();
                return 0;
            }
            if (wParam == kTransitionTimerId && transitionTimerId_)
            {
                if (!transitionActive_ || !transitionFromImage_ || !currentImage_)
                {
                    StopTransition();
                    RequestRepaint();
                    return 0;
                }

                const double elapsedMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - transitionStartedAt_).count();
                if (elapsedMs >= static_cast<double>(transitionDurationMs_))
                {
                    StopTransition(false);
                }
                RequestRepaint();
                return 0;
            }
            if (wParam == kSmoothZoomTimerId && smoothZoomTimerId_)
            {
                const double diff = smoothZoomTarget_ - smoothZoomCurrent_;
                if (std::abs(diff) < 0.0005)
                {
                    customZoomScale_ = smoothZoomTarget_;
                    smoothZoomCurrent_ = smoothZoomTarget_;
                    KillTimer(hwnd_, kSmoothZoomTimerId);
                    smoothZoomTimerId_ = 0;
                }
                else
                {
                    smoothZoomCurrent_ += diff * 0.22;
                    customZoomScale_ = smoothZoomCurrent_;
                }
                RequestRepaint();
                return 0;
            }
            break;
        case kDecodedImageMessage:
            return HandleDecodedImageMessage(lParam);
        case kPrefetchImageMessage:
            return HandlePrefetchImageMessage(lParam);
        case WM_PAINT:
        {
            EnsureD2DRenderTarget();

            if (d2dRenderTarget_)
            {
                PAINTSTRUCT paintStruct{};
                BeginPaint(hwnd_, &paintStruct);

                render::D2DRenderer::Instance().ResizeRenderTarget(d2dRenderTarget_.Get(), hwnd_);

                d2dRenderTarget_->BeginDraw();
                const D2D1_SIZE_F size = d2dRenderTarget_->GetSize();
                const float clientWidth = size.width;
                const float clientHeight = size.height;

                d2dRenderTarget_->Clear(render::ToD2DColor(BackgroundColor(darkTheme_)));

                const int displayedImageIndex = DisplayedImageIndex();
                const browser::BrowserItem* currentItem =
                    (displayedImageIndex >= 0 && displayedImageIndex < static_cast<int>(items_.size()))
                    ? &items_[static_cast<std::size_t>(displayedImageIndex)]
                    : nullptr;

                if (!currentImage_)
                {
                    const bool showIcon = loading_ || errorMessage_.empty();
                    constexpr float kPanelPaddingLeft = 28.0f;
                    constexpr float kPanelPaddingRight = 32.0f;
                    constexpr float kPanelPaddingVertical = 16.0f;
                    constexpr float kIconTextGap = 30.0f;
                    constexpr float kDesiredTextBlockWidth = 320.0f;
                    constexpr float kMinimumTextBlockWidth = 240.0f;
                    constexpr float kTitleHeight = 34.0f;
                    constexpr float kBodyHeight = 34.0f;
                    constexpr float kTitleBodyGap = 8.0f;

                    const float maxPanelWidth = std::max(320.0f, clientWidth - 48.0f);
                    const float maxPanelHeight = std::max(140.0f, clientHeight - 36.0f);
                    float renderedIconSize = 0.0f;
                    float panelWidth = std::max(320.0f, std::min(560.0f, clientWidth - 64.0f));
                    float panelHeight = showIcon
                        ? std::min(198.0f, std::max(152.0f, clientHeight - 64.0f))
                        : std::min(170.0f, std::max(126.0f, clientHeight - 64.0f));

                    if (showIcon && d2dStatusArtBitmap_)
                    {
                        const float artW = d2dStatusArtBitmap_->GetSize().width;
                        const float maxIconW = std::max(96.0f, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - kMinimumTextBlockWidth);
                        const float maxIconH = std::max(96.0f, maxPanelHeight - (kPanelPaddingVertical * 2.0f));
                        renderedIconSize = std::min({artW, maxIconW, maxIconH});

                        const float textBlockWidth = std::max(kMinimumTextBlockWidth,
                            std::min(kDesiredTextBlockWidth,
                                     maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - renderedIconSize));
                        panelWidth = std::min(maxPanelWidth,
                            kPanelPaddingLeft + renderedIconSize + kIconTextGap + textBlockWidth + kPanelPaddingRight);
                        panelHeight = std::min(maxPanelHeight, (kPanelPaddingVertical * 2.0f) + renderedIconSize);
                    }

                    const float panelLeft = (clientWidth - panelWidth) / 2.0f;
                    const float panelTop = (clientHeight - panelHeight) / 2.0f;
                    const D2D1_RECT_F panelRect = D2D1::RectF(panelLeft, panelTop, panelLeft + panelWidth, panelTop + panelHeight);
                    const D2D1_ROUNDED_RECT roundedPanel = D2D1::RoundedRect(panelRect, 11.0f, 11.0f);

                    if (d2dPanelFillBrush_) d2dRenderTarget_->FillRoundedRectangle(roundedPanel, d2dPanelFillBrush_.Get());
                    if (d2dPanelBorderBrush_) d2dRenderTarget_->DrawRoundedRectangle(roundedPanel, d2dPanelBorderBrush_.Get(), 1.0f);

                    const std::wstring title = loading_
                        ? L"Loading Image"
                        : (errorMessage_.empty() ? L"No Image Loaded" : L"Unable to Open Image");
                    const std::wstring messageText = loading_
                        ? L"Opening image..."
                        : (errorMessage_.empty() ? L"Choose an image to continue." : errorMessage_);

                    D2D1_RECT_F titleRect{};
                    D2D1_RECT_F bodyRect{};
                    if (showIcon && d2dStatusArtBitmap_)
                    {
                        const float iconX = panelLeft + kPanelPaddingLeft;
                        const float iconY = panelTop + (panelHeight - renderedIconSize) / 2.0f;
                        d2dRenderTarget_->DrawBitmap(d2dStatusArtBitmap_.Get(),
                            D2D1::RectF(iconX, iconY, iconX + renderedIconSize, iconY + renderedIconSize),
                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

                        const float contentLeft = iconX + renderedIconSize + kIconTextGap;
                        const float contentRight = panelLeft + panelWidth - kPanelPaddingRight;
                        const float textBlockHeight = kTitleHeight + kTitleBodyGap + kBodyHeight;
                        const float contentTop = panelTop + std::max(kPanelPaddingVertical, (panelHeight - textBlockHeight) / 2.0f);
                        titleRect = D2D1::RectF(contentLeft, contentTop, contentRight, contentTop + kTitleHeight);
                        bodyRect = D2D1::RectF(contentLeft, titleRect.bottom + kTitleBodyGap, contentRight, titleRect.bottom + kTitleBodyGap + kBodyHeight);
                    }
                    else
                    {
                        titleRect = D2D1::RectF(panelLeft + 20, panelTop + 20, panelLeft + panelWidth - 20, panelTop + 54);
                        bodyRect = D2D1::RectF(panelLeft + 24, titleRect.bottom + 8, panelLeft + panelWidth - 24, panelTop + panelHeight - 22);
                    }

                    if (d2dNameFormat_ && d2dTextBrush_)
                    {
                        d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        d2dRenderTarget_->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                                                   d2dNameFormat_.Get(), titleRect, d2dTextBrush_.Get());
                    }
                    if (d2dInfoFormat_ && d2dMutedTextBrush_)
                    {
                        d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                        d2dRenderTarget_->DrawText(messageText.c_str(), static_cast<UINT32>(messageText.size()),
                                                   d2dInfoFormat_.Get(), bodyRect, d2dMutedTextBrush_.Get());
                        d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    }
                }
                else
                {
                    RECT gdiClientRect{0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight)};
                    const CompareDirection activeCompareDirection = ResolveCompareDirection(compareDirection_);
                    const int compareIndex = compareMode_ ? CompareIndexForDirection(activeCompareDirection) : -1;
                    const browser::BrowserItem* compareItem =
                        (compareIndex >= 0 && compareIndex < static_cast<int>(items_.size()))
                        ? &items_[static_cast<std::size_t>(compareIndex)]
                        : nullptr;
                    const bool compareLayout = compareMode_ && compareItem != nullptr;
                    RECT primaryRect = gdiClientRect;
                    RECT compareRect{};
                    if (compareLayout)
                    {
                        const LONG totalWidth = gdiClientRect.right - gdiClientRect.left;
                        const LONG gapWidth = std::clamp<LONG>(totalWidth / 40, 12, 24);
                        const LONG paneWidth = std::max<LONG>(1, (totalWidth - gapWidth) / 2);
                        primaryRect.right = primaryRect.left + paneWidth;
                        compareRect.left = primaryRect.right + gapWidth;
                        compareRect.top = gdiClientRect.top;
                        compareRect.right = gdiClientRect.right;
                        compareRect.bottom = gdiClientRect.bottom;
                    }

                    const double scale = EffectiveScaleForClient(primaryRect);
                    const int zoomPercent = std::max(1, static_cast<int>(std::lround(scale * 100.0)));
                    if (zoomPercent != currentZoomPercent_)
                    {
                        NotifyZoomChanged(zoomPercent);
                    }

                    auto ensureCurrentBitmap = [&]()
                    {
                        if (!currentImage_)
                        {
                            return;
                        }

                        const int displayedImageIndex = DisplayedImageIndex();
                        if (d2dCurrentImageIndex_ == displayedImageIndex && d2dCurrentImageBitmap_)
                        {
                            return;
                        }

                        const auto uploadStartedAt = std::chrono::steady_clock::now();
                        d2dCurrentImageBitmap_ = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                            d2dRenderTarget_.Get(), *currentImage_);
                        const double uploadMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - uploadStartedAt).count();
                        util::RecordTiming(L"viewer.upload.d2d", uploadMs);
                        util::LogInfo(L"ViewerWindow D2D upload ms="
                            + std::to_wstring(uploadMs)
                            + L", size=" + std::to_wstring(currentImage_->SourceWidth())
                            + L"x" + std::to_wstring(currentImage_->SourceHeight())
                            + L", bytes=" + std::to_wstring(currentImage_->ByteCount()));
                        d2dCurrentImageIndex_ = d2dCurrentImageBitmap_ ? displayedImageIndex : -1;
                    };

                    const cache::CachedThumbnail* compareImage = nullptr;
                    auto ensureCompareBitmap = [&]()
                    {
                        if (!compareLayout)
                        {
                            d2dCompareImageBitmap_.Reset();
                            d2dCompareImageIndex_ = -1;
                            return;
                        }

                        const CachedImageSlot& compareSlot = activeCompareDirection == CompareDirection::Previous
                            ? previousSlot_
                            : nextSlot_;
                        if (compareSlot.index != compareIndex || !compareSlot.image)
                        {
                            d2dCompareImageBitmap_.Reset();
                            d2dCompareImageIndex_ = -1;
                            return;
                        }

                        compareImage = compareSlot.image.get();
                        if (d2dCompareImageIndex_ == compareIndex && d2dCompareImageBitmap_)
                        {
                            return;
                        }

                        const auto uploadStartedAt = std::chrono::steady_clock::now();
                        d2dCompareImageBitmap_ = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                            d2dRenderTarget_.Get(), *compareSlot.image);
                        const double uploadMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - uploadStartedAt).count();
                        util::RecordTiming(L"viewer.upload.d2d.compare", uploadMs);
                        d2dCompareImageIndex_ = d2dCompareImageBitmap_ ? compareIndex : -1;
                    };

                    auto drawComparePlaceholder = [&](std::wstring_view text)
                    {
                        if (!compareLayout)
                        {
                            return;
                        }

                        const D2D1_RECT_F panelRect = D2D1::RectF(
                            static_cast<float>(compareRect.left + 24),
                            static_cast<float>(compareRect.top + 24),
                            static_cast<float>(compareRect.right - 24),
                            static_cast<float>(compareRect.bottom - 24));
                        const D2D1_ROUNDED_RECT roundedPanel = D2D1::RoundedRect(panelRect, 10.0f, 10.0f);
                        if (d2dPanelFillBrush_)
                        {
                            d2dRenderTarget_->FillRoundedRectangle(roundedPanel, d2dPanelFillBrush_.Get());
                        }
                        if (d2dPanelBorderBrush_)
                        {
                            d2dRenderTarget_->DrawRoundedRectangle(roundedPanel, d2dPanelBorderBrush_.Get(), 1.0f);
                        }
                        if (d2dInfoFormat_ && d2dMutedTextBrush_)
                        {
                            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            d2dRenderTarget_->DrawText(
                                text.data(),
                                static_cast<UINT32>(text.size()),
                                d2dInfoFormat_.Get(),
                                panelRect,
                                d2dMutedTextBrush_.Get());
                            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                        }
                    };

                    const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                    const int sourceWidth = currentImage_->SourceWidth();
                    const int sourceHeight = currentImage_->SourceHeight();
                    const int rotatedWidth = swapDimensions ? sourceHeight : sourceWidth;
                    const int rotatedHeight = swapDimensions ? sourceWidth : sourceHeight;
                    ensureCurrentBitmap();
                    ensureCompareBitmap();

                    const double fitScale = FitScaleForImage(*currentImage_, primaryRect);
                    const float currentScaleMultiplier = static_cast<float>(scale / std::max(0.01, fitScale));
                    bool drewTransition = false;

                    if (compareLayout && transitionActive_)
                    {
                        StopTransition();
                    }

                    if (!compareLayout && transitionActive_ && transitionFromImage_)
                    {
                        const double elapsedMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - transitionStartedAt_).count();
                        const float progress = transitionDurationMs_ == 0
                            ? 1.0f
                            : std::clamp(static_cast<float>(elapsedMs / static_cast<double>(transitionDurationMs_)), 0.0f, 1.0f);

                        if (progress >= 1.0f)
                        {
                            StopTransition(false);
                        }
                        else
                        {
                            if (!transitionFromBitmap_ && d2dRenderTarget_)
                            {
                                transitionFromBitmap_ = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(
                                    d2dRenderTarget_.Get(), *transitionFromImage_);
                            }

                            if (transitionFromBitmap_ && d2dCurrentImageBitmap_)
                            {
                                const float eased = SmoothStep(progress);
                                const float direction = transitionForward_ ? 1.0f : -1.0f;
                                const float parityDirection = (transitionFromIndex_ % 2 == 0) ? 1.0f : -1.0f;

                                switch (transitionStyle_)
                                {
                                case TransitionStyle::Crossfade:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, eased, 1.0f, 0.0f, 0.0f);
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::Slide:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    -direction * clientWidth * eased, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    direction * clientWidth * (1.0f - eased), 0.0f);
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::KenBurns:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - eased, 1.0f + (0.08f * eased),
                                                    -direction * clientWidth * 0.06f * eased,
                                                    parityDirection * clientHeight * 0.04f * eased);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, eased, 1.08f - (0.08f * eased),
                                                    direction * clientWidth * 0.06f * (1.0f - eased),
                                                    -parityDirection * clientHeight * 0.04f * (1.0f - eased));
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::Cut:
                                default:
                                    break;
                                }
                            }
                        }
                    }

                    if (!drewTransition && d2dCurrentImageBitmap_)
                    {
                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                        primaryRect, 1.0f, currentScaleMultiplier,
                                        static_cast<float>(panOffsetX_), static_cast<float>(panOffsetY_));
                        if (compareLayout)
                        {
                            if (compareImage && d2dCompareImageBitmap_)
                            {
                                DrawImageBitmap(d2dRenderTarget_.Get(), d2dCompareImageBitmap_.Get(), *compareImage,
                                                compareRect, 1.0f, currentScaleMultiplier,
                                                static_cast<float>(panOffsetX_), static_cast<float>(panOffsetY_));
                            }
                            else
                            {
                                drawComparePlaceholder(loading_ ? L"Loading compare image..." : L"Preparing adjacent image...");
                            }

                            if (d2dPanelBorderBrush_)
                            {
                                const float dividerX = static_cast<float>(primaryRect.right + ((compareRect.left - primaryRect.right) / 2));
                                d2dRenderTarget_->DrawLine(D2D1::Point2F(dividerX, 20.0f),
                                                           D2D1::Point2F(dividerX, clientHeight - 20.0f),
                                                           d2dPanelBorderBrush_.Get(),
                                                           1.0f);
                            }
                        }
                    }

                    if (infoOverlaysVisible_)
                    {
                        std::wstring fileName = currentItem ? currentItem->fileName : std::wstring(L"Image");
                        if (compareLayout && compareItem)
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

                        std::wstring bottomLine = std::to_wstring(rotatedWidth) + L" x " + std::to_wstring(rotatedHeight);
                        bottomLine.append(L"  |  ");
                        bottomLine.append(std::to_wstring(zoomPercent));
                        bottomLine.append(L"%");
                        bottomLine.append(L"  |  ");
                        bottomLine.append(zoomMode_ == ZoomMode::Fit ? L"Fit" : L"Custom");
                        if (compareLayout)
                        {
                            bottomLine.append(compareImage
                                ? L"  |  Shift+Left/Right change pair  |  C toggle  |  X swap"
                                : L"  |  Loading compare image...");
                        }

                        const float availablePanelWidth = std::max(120.0f, clientWidth - 32.0f);
                        const float topPanelWidth = std::min(compareLayout ? 760.0f : 560.0f, availablePanelWidth);
                        const float bottomPanelWidth = std::min(compareLayout ? 560.0f : 320.0f, availablePanelWidth);
                        D2D1_RECT_F topPanel = D2D1::RectF(16, 16, 16 + topPanelWidth, 74);
                        D2D1_RECT_F bottomPanel = D2D1::RectF(clientWidth - 16 - bottomPanelWidth, clientHeight - 52, clientWidth - 16, clientHeight - 16);

                        const D2D1_ROUNDED_RECT roundedTop = D2D1::RoundedRect(topPanel, 8.0f, 8.0f);
                        const D2D1_ROUNDED_RECT roundedBottom = D2D1::RoundedRect(bottomPanel, 8.0f, 8.0f);

                        if (d2dPanelFillBrush_) d2dRenderTarget_->FillRoundedRectangle(roundedTop, d2dPanelFillBrush_.Get());
                        if (d2dPanelBorderBrush_) d2dRenderTarget_->DrawRoundedRectangle(roundedTop, d2dPanelBorderBrush_.Get(), 1.0f);
                        if (d2dPanelFillBrush_) d2dRenderTarget_->FillRoundedRectangle(roundedBottom, d2dPanelFillBrush_.Get());
                        if (d2dPanelBorderBrush_) d2dRenderTarget_->DrawRoundedRectangle(roundedBottom, d2dPanelBorderBrush_.Get(), 1.0f);

                        D2D1_RECT_F nameRect = D2D1::RectF(topPanel.left + 14, topPanel.top + 10, topPanel.right - 14, topPanel.top + 34);
                        D2D1_RECT_F topInfoRect = D2D1::RectF(topPanel.left + 14, topPanel.top + 34, topPanel.right - 14, topPanel.bottom - 10);
                        D2D1_RECT_F bottomInfoRect = D2D1::RectF(bottomPanel.left + 12, bottomPanel.top + 10, bottomPanel.right - 12, bottomPanel.bottom - 10);

                        if (d2dNameFormat_ && d2dTextBrush_)
                        {
                            d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            d2dRenderTarget_->DrawText(fileName.c_str(), static_cast<UINT32>(fileName.size()),
                                                       d2dNameFormat_.Get(), nameRect, d2dTextBrush_.Get());
                        }
                        if (d2dInfoFormat_ && d2dMutedTextBrush_)
                        {
                            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            d2dRenderTarget_->DrawText(topLine.c_str(), static_cast<UINT32>(topLine.size()),
                                                       d2dInfoFormat_.Get(), topInfoRect, d2dMutedTextBrush_.Get());
                            d2dRenderTarget_->DrawText(bottomLine.c_str(), static_cast<UINT32>(bottomLine.size()),
                                                       d2dInfoFormat_.Get(), bottomInfoRect, d2dMutedTextBrush_.Get());
                        }
                    }
                }

                const HRESULT hr = d2dRenderTarget_->EndDraw();
                if (hr == D2DERR_RECREATE_TARGET)
                {
                    ReleaseD2DResources();
                }

                EndPaint(hwnd_, &paintStruct);
                return 0;
            }

            // GDI fallback path
            if (transitionActive_)
            {
                StopTransition();
            }
            PAINTSTRUCT paintStruct{};
            HDC hdc = BeginPaint(hwnd_, &paintStruct);
            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const int clientWidth = clientRect.right - clientRect.left;
            const int clientHeight = clientRect.bottom - clientRect.top;
            if (clientWidth <= 0 || clientHeight <= 0)
            {
                EndPaint(hwnd_, &paintStruct);
                return 0;
            }

            HDC frameDc = hdc;
            HDC backBufferDc = CreateCompatibleDC(hdc);
            HBITMAP backBufferBitmap = nullptr;
            HGDIOBJ oldBackBufferBitmap = nullptr;
            if (backBufferDc)
            {
                backBufferBitmap = CreateCompatibleBitmap(hdc, std::max(1, clientWidth), std::max(1, clientHeight));
                if (backBufferBitmap)
                {
                    oldBackBufferBitmap = SelectObject(backBufferDc, backBufferBitmap);
                    frameDc = backBufferDc;
                }
                else
                {
                    DeleteDC(backBufferDc);
                    backBufferDc = nullptr;
                }
            }

            FillRect(frameDc, &clientRect, backgroundBrush_);
            SetBkMode(frameDc, TRANSPARENT);

            if (currentImage_)
            {
                const double scale = EffectiveScaleForClient(clientRect);
                const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
                const int sourceWidth = currentImage_->SourceWidth();
                const int sourceHeight = currentImage_->SourceHeight();
                const int rotatedWidth = swapDimensions ? sourceHeight : sourceWidth;
                const int rotatedHeight = swapDimensions ? sourceWidth : sourceHeight;
                const int destinationWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedWidth) * scale)));
                const int destinationHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(rotatedHeight) * scale)));
                const int x = static_cast<int>(std::lround(((clientWidth - destinationWidth) / 2.0) + panOffsetX_));
                const int y = static_cast<int>(std::lround(((clientHeight - destinationHeight) / 2.0) + panOffsetY_));

                HDC bitmapDc = CreateCompatibleDC(frameDc);
                if (bitmapDc)
                {
                    HGDIOBJ oldBitmap = SelectObject(bitmapDc, currentImage_->Bitmap());
                    SetStretchBltMode(frameDc, HALFTONE);
                    SetBrushOrgEx(frameDc, 0, 0, nullptr);

                    if (rotationQuarterTurns_ == 0)
                    {
                        StretchBlt(frameDc, x, y, destinationWidth, destinationHeight, bitmapDc, 0, 0, sourceWidth, sourceHeight, SRCCOPY);
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
                        PlgBlt(frameDc, destination, bitmapDc, 0, 0, sourceWidth, sourceHeight, nullptr, 0, 0);
                    }
                    SelectObject(bitmapDc, oldBitmap);
                    DeleteDC(bitmapDc);
                }
            }

            if (backBufferDc)
            {
                BitBlt(hdc, 0, 0, clientWidth, clientHeight, backBufferDc, 0, 0, SRCCOPY);
                SelectObject(backBufferDc, oldBackBufferBitmap);
                DeleteObject(backBufferBitmap);
                DeleteDC(backBufferDc);
            }

            EndPaint(hwnd_, &paintStruct);
            return 0;
        }
        case WM_CLOSE:
            util::LogInfo(L"ViewerWindow WM_CLOSE hwnd=" + FormatWindowHandle(hwnd));
            asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
            asyncState_->targetWindow = nullptr;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            util::LogInfo(L"ViewerWindow WM_DESTROY hwnd=" + FormatWindowHandle(hwnd));
            StopSlideshow();
            StopTransition();
            memoryPressureActive_ = false;
            if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }
            ReleaseD2DResources();
            asyncState_->targetWindow = nullptr;
            fullScreen_ = false;
            windowedStyle_ = 0;
            windowedExStyle_ = 0;
            windowedPlacement_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
            LogPrefetchStats();
            NotifyActivityChanged(false);
            NotifyZoomChanged(0);
            if (owner_)
            {
                PostMessageW(owner_, kClosedMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
            }
            return 0;
        case WM_NCDESTROY:
        {
            util::LogInfo(L"ViewerWindow WM_NCDESTROY hwnd=" + FormatWindowHandle(hwnd));
            const HWND window = hwnd;
            if (hwnd_ == window)
            {
                hwnd_ = nullptr;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK ViewerWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ViewerWindow* self = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<ViewerWindow*>(createStruct->lpCreateParams);
            self->hwnd_ = hwnd;
            self->asyncState_->targetWindow = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}