#include "viewer/ViewerWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>

#include "app/resource.h"
#include "decode/ImageDecoder.h"
#include "util/ResourcePng.h"
#include "util/Diagnostics.h"
#include "util/Log.h"

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";
    constexpr int kPlaceholderBrandArtSize = 256;

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
    }

    ViewerWindow::~ViewerWindow()
    {
        asyncState_->shutdown.store(true, std::memory_order_release);
        asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
        asyncState_->targetWindow = nullptr;
        WaitForBackgroundTasks();

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
        StopSlideshow();
        ResetCachedImageSlots();
        ResetPrefetchStatistics();
        ReapCompletedBackgroundTasks();

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
    }

    void ViewerWindow::StopSlideshow()
    {
        if (hwnd_ && slideshowTimerId_ != 0)
        {
            KillTimer(hwnd_, slideshowTimerId_);
        }

        slideshowTimerId_ = 0;
        slideshowActive_ = false;
    }

    bool ViewerWindow::IsSlideshowActive() const noexcept
    {
        return slideshowActive_;
    }

    void ViewerWindow::SetDarkTheme(bool enabled)
    {
        darkTheme_ = enabled;
        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        backgroundBrush_ = CreateSolidBrush(BackgroundColor(darkTheme_));
        if (hwnd_)
        {
            RequestRepaint();
        }
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
        if (!hwnd_)
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

        SetWindowTextW(hwnd_, title.c_str());
    }

    void ViewerWindow::PrepareForImageChange()
    {
        errorMessage_.clear();
        loading_ = true;
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
        currentSlot_ = {};
        previousSlot_ = {};
        nextSlot_ = {};
    }

    void ViewerWindow::ResetPrefetchStatistics()
    {
        prefetchRequestCount_.store(0, std::memory_order_release);
        prefetchCompletedCount_.store(0, std::memory_order_release);
        prefetchCancelledCount_.store(0, std::memory_order_release);
        prefetchHitCount_.store(0, std::memory_order_release);
        prefetchMissCount_.store(0, std::memory_order_release);
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
        backgroundTasks_.erase(std::remove_if(backgroundTasks_.begin(), backgroundTasks_.end(), [](std::future<void>& task)
        {
            return !task.valid() || task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        }), backgroundTasks_.end());
    }

    void ViewerWindow::WaitForBackgroundTasks()
    {
        for (std::future<void>& task : backgroundTasks_)
        {
            if (task.valid())
            {
                task.wait();
            }
        }
        backgroundTasks_.clear();
    }

    void ViewerWindow::LoadCurrentImageAsync(LoadReason reason)
    {
        PrepareForImageChange();
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
        const std::shared_ptr<AsyncState> asyncState = asyncState_;
        ReapCompletedBackgroundTasks();
        backgroundTasks_.push_back(std::async(std::launch::async, [asyncState, item, selectedIndex, requestId, navigationGeneration]()
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
        }));
    }

    void ViewerWindow::Navigate(int delta)
    {
        if (items_.empty())
        {
            return;
        }

        const int nextIndex = std::clamp(currentIndex_ + delta, 0, static_cast<int>(items_.size()) - 1);
        if (nextIndex == currentIndex_)
        {
            return;
        }

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
        currentSlot_ = {};
        currentImage_.reset();
        LoadCurrentImageAsync(LoadReason::Navigation);
    }

    void ViewerWindow::ScheduleAdjacentPrefetch(std::uint64_t navigationGeneration)
    {
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            previousSlot_ = {};
            nextSlot_ = {};
            return;
        }

        const int previousIndex = currentIndex_ - 1;
        const int nextIndex = currentIndex_ + 1;

        if (previousIndex >= 0)
        {
            if (previousSlot_.index != previousIndex || !previousSlot_.image)
            {
                previousSlot_ = {};
                StartPrefetch(previousIndex, navigationGeneration);
            }
        }
        else
        {
            previousSlot_ = {};
        }

        if (nextIndex < static_cast<int>(items_.size()))
        {
            if (nextSlot_.index != nextIndex || !nextSlot_.image)
            {
                nextSlot_ = {};
                StartPrefetch(nextIndex, navigationGeneration);
            }
        }
        else
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
        prefetchRequestCount_.fetch_add(1, std::memory_order_acq_rel);
        util::IncrementCounter(L"viewer.prefetch.request");
        ReapCompletedBackgroundTasks();
        backgroundTasks_.push_back(std::async(std::launch::async, [asyncState, item, index, navigationGeneration]()
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
        }));
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
        const double baseScale = zoomMode_ == ZoomMode::Fit ? fitScale : customZoomScale_;
        if (factor < 1.0)
        {
            const double minimumScale = std::min(baseScale, fitScale);
            const double targetScale = std::clamp(baseScale * factor, minimumScale, 16.0);
            if (std::abs(targetScale - baseScale) < 0.0001)
            {
                return;
            }

            if (std::abs(targetScale - fitScale) < 0.0001)
            {
                FitToWindow();
                return;
            }

            zoomMode_ = ZoomMode::Custom;
            customZoomScale_ = targetScale;
            RequestRepaint();
            return;
        }

        zoomMode_ = ZoomMode::Custom;
        customZoomScale_ = std::clamp(baseScale * factor, 0.05, 16.0);
        RequestRepaint();
    }

    void ViewerWindow::FitToWindow()
    {
        zoomMode_ = ZoomMode::Fit;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
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

    void ViewerWindow::ToggleInfoOverlays()
    {
        infoOverlaysVisible_ = !infoOverlaysVisible_;
        SaveViewerInfoOverlaysVisibleSetting(infoOverlaysVisible_);
        if (hwnd_)
        {
            RequestRepaint();
        }
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

        if (currentIndex_ >= static_cast<int>(items_.size()) - 1)
        {
            currentIndex_ = 0;
            currentSlot_ = {};
            currentImage_.reset();
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
    }

    double ViewerWindow::FitScaleForClient(const RECT& clientRect) const
    {
        if (!currentImage_)
        {
            return 1.0;
        }

        const int clientWidth = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
        const int clientHeight = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const double imageWidth = static_cast<double>(swapDimensions ? currentImage_->SourceHeight() : currentImage_->SourceWidth());
        const double imageHeight = static_cast<double>(swapDimensions ? currentImage_->SourceWidth() : currentImage_->SourceHeight());
        const double widthRatio = static_cast<double>(clientWidth) / std::max(1.0, imageWidth);
        const double heightRatio = static_cast<double>(clientHeight) / std::max(1.0, imageHeight);
        return std::min(widthRatio, heightRatio);
    }

    double ViewerWindow::EffectiveScaleForClient(const RECT& clientRect) const
    {
        return zoomMode_ == ZoomMode::Fit ? FitScaleForClient(clientRect) : customZoomScale_;
    }

    void ViewerWindow::RequestRepaint() const
    {
        if (hwnd_)
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
            return 0;
        }

        if (update->navigationGeneration != asyncState_->navigationGeneration.load(std::memory_order_acquire)
            || update->index != currentIndex_)
        {
            return 0;
        }

        if (pendingLoadActive_)
        {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - pendingLoadStartedAt_).count();
            util::RecordTiming(
                pendingLoadReason_ == LoadReason::Open ? L"viewer.open" : L"viewer.navigation",
                elapsedMs);
            pendingLoadActive_ = false;
        }

        SetCurrentImageSlot(update->index, std::move(update->image), false);
        errorMessage_ = std::move(update->errorMessage);
        loading_ = false;
        if (currentImage_)
        {
            ScheduleAdjacentPrefetch(update->navigationGeneration);
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

        if (update->navigationGeneration != asyncState_->navigationGeneration.load(std::memory_order_acquire))
        {
            prefetchCancelledCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.cancelled");
            return 0;
        }

        if (!update->image)
        {
            return 0;
        }

        if (update->index == currentIndex_ - 1)
        {
            previousSlot_.index = update->index;
            previousSlot_.image = std::move(update->image);
            previousSlot_.prefetched = true;
            prefetchCompletedCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.completed");
            return 0;
        }

        if (update->index == currentIndex_ + 1)
        {
            nextSlot_.index = update->index;
            nextSlot_.image = std::move(update->image);
            nextSlot_.prefetched = true;
            prefetchCompletedCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.completed");
            return 0;
        }

        prefetchCancelledCount_.fetch_add(1, std::memory_order_acq_rel);
        util::IncrementCounter(L"viewer.prefetch.cancelled");
        return 0;
    }

    LRESULT ViewerWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
            RequestRepaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            NotifyActivityChanged(LOWORD(wParam) != WA_INACTIVE);
            return 0;
        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_RIGHT:
                Navigate(+1);
                return 0;
            case VK_LEFT:
                Navigate(-1);
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
            ZoomBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1 : 0.9);
            return 0;
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
            break;
        case kDecodedImageMessage:
            return HandleDecodedImageMessage(lParam);
        case kPrefetchImageMessage:
            return HandlePrefetchImageMessage(lParam);
        case WM_PAINT:
        {
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
            SetTextColor(frameDc, TextColor(darkTheme_));

            const browser::BrowserItem* currentItem =
                (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(items_.size()))
                ? &items_[static_cast<std::size_t>(currentIndex_)]
                : nullptr;

            if (!currentImage_)
            {
                const bool showIcon = loading_ || errorMessage_.empty();
                RECT panelRect = clientRect;
                constexpr int kPanelMarginX = 24;
                constexpr int kPanelMarginY = 18;
                constexpr int kPanelPaddingLeft = 28;
                constexpr int kPanelPaddingRight = 32;
                constexpr int kPanelPaddingVertical = 16;
                constexpr int kIconTextGap = 30;
                constexpr int kDesiredTextBlockWidth = 320;
                constexpr int kMinimumTextBlockWidth = 240;
                constexpr int kTitleHeight = 34;
                constexpr int kBodyHeight = 34;
                constexpr int kTitleBodyGap = 8;

                const int maxPanelWidth = std::max(320, clientWidth - (kPanelMarginX * 2));
                const int maxPanelHeight = std::max(140, clientHeight - (kPanelMarginY * 2));
                int renderedIconSize = 0;
                int panelWidth = std::max(320, std::min(560, clientWidth - 64));
                int panelHeight = showIcon
                    ? std::min(198, std::max(152, clientHeight - 64))
                    : std::min(170, std::max(126, clientHeight - 64));
                if (showIcon && statusArt_)
                {
                    const int maxIconWidth = std::max(96, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - kMinimumTextBlockWidth);
                    const int maxIconHeight = std::max(96, maxPanelHeight - (kPanelPaddingVertical * 2));
                    renderedIconSize = std::min({statusArt_->Width(), maxIconWidth, maxIconHeight});

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

                HBRUSH panelBrush = CreateSolidBrush(PanelFillColor(darkTheme_));
                HPEN panelPen = CreatePen(PS_SOLID, 1, PanelBorderColor(darkTheme_));
                HGDIOBJ oldBrush = SelectObject(frameDc, panelBrush);
                HGDIOBJ oldPen = SelectObject(frameDc, panelPen);
                RoundRect(frameDc, panelRect.left, panelRect.top, panelRect.right, panelRect.bottom, 22, 22);
                SelectObject(frameDc, oldPen);
                SelectObject(frameDc, oldBrush);

                const std::wstring title = loading_
                    ? L"Loading Image"
                    : (errorMessage_.empty() ? L"No Image Loaded" : L"Unable to Open Image");
                const std::wstring messageText = loading_
                    ? L"Opening image..."
                    : (errorMessage_.empty()
                        ? L"Choose an image to continue."
                        : errorMessage_);

                RECT titleRect{};
                RECT bodyRect{};
                if (showIcon && statusArt_)
                {
                    const int iconX = panelRect.left + kPanelPaddingLeft;
                    const int iconY = panelRect.top + ((panelHeight - renderedIconSize) / 2);
                    util::DrawBitmapWithAlpha(frameDc, *statusArt_, iconX, iconY, renderedIconSize, renderedIconSize);

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
                }
                SetTextColor(frameDc, TextColor(darkTheme_));
                DrawTextW(frameDc, title.c_str(), -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SetTextColor(frameDc, MutedTextColor(darkTheme_));
                DrawTextW(frameDc, messageText.c_str(), -1, &bodyRect, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

                DeleteObject(panelPen);
                DeleteObject(panelBrush);
            }
            else
            {
                const double scale = EffectiveScaleForClient(clientRect);
                const int zoomPercent = std::max(1, static_cast<int>(std::lround(scale * 100.0)));
                if (zoomPercent != currentZoomPercent_)
                {
                    NotifyZoomChanged(zoomPercent);
                }

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

                if (infoOverlaysVisible_)
                {
                    HBRUSH panelBrush = CreateSolidBrush(PanelFillColor(darkTheme_));
                    HPEN panelPen = CreatePen(PS_SOLID, 1, PanelBorderColor(darkTheme_));
                    HGDIOBJ oldBrush = SelectObject(frameDc, panelBrush);
                    HGDIOBJ oldPen = SelectObject(frameDc, panelPen);

                    const std::wstring fileName = currentItem ? currentItem->fileName : std::wstring(L"Image");
                    std::wstring topLine = std::to_wstring(currentIndex_ + 1) + L" / "
                        + std::to_wstring(static_cast<int>(items_.size()));
                    if (currentItem)
                    {
                        topLine.append(L"  |  ");
                        topLine.append(currentItem->fileType);
                        topLine.append(L"  |  ");
                        topLine.append(browser::FormatByteSize(currentItem->fileSizeBytes));
                    }

                    std::wstring bottomLine = std::to_wstring(rotatedWidth) + L" x " + std::to_wstring(rotatedHeight);
                    bottomLine.append(L"  |  ");
                    bottomLine.append(std::to_wstring(zoomPercent));
                    bottomLine.append(L"%");
                    bottomLine.append(L"  |  ");
                    bottomLine.append(zoomMode_ == ZoomMode::Fit ? L"Fit" : L"Custom");

                    const int availablePanelWidth = std::max(120, clientWidth - 32);
                    const int topPanelWidth = std::min(560, availablePanelWidth);
                    const int bottomPanelWidth = std::min(320, availablePanelWidth);
                    RECT topPanel{clientRect.left + 16, clientRect.top + 16, clientRect.left + 16 + topPanelWidth, clientRect.top + 74};
                    RECT bottomPanel{clientRect.right - 16 - bottomPanelWidth, clientRect.bottom - 52, clientRect.right - 16, clientRect.bottom - 16};

                    RoundRect(frameDc, topPanel.left, topPanel.top, topPanel.right, topPanel.bottom, 16, 16);
                    RoundRect(frameDc, bottomPanel.left, bottomPanel.top, bottomPanel.right, bottomPanel.bottom, 16, 16);
                    SelectObject(frameDc, oldPen);
                    SelectObject(frameDc, oldBrush);

                    RECT nameRect{topPanel.left + 14, topPanel.top + 10, topPanel.right - 14, topPanel.top + 34};
                    RECT topInfoRect{topPanel.left + 14, topPanel.top + 34, topPanel.right - 14, topPanel.bottom - 10};
                    RECT bottomInfoRect{bottomPanel.left + 12, bottomPanel.top + 10, bottomPanel.right - 12, bottomPanel.bottom - 10};

                    SetTextColor(frameDc, TextColor(darkTheme_));
                    DrawTextW(frameDc, fileName.c_str(), -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SetTextColor(frameDc, MutedTextColor(darkTheme_));
                    DrawTextW(frameDc, topLine.c_str(), -1, &topInfoRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    DrawTextW(frameDc, bottomLine.c_str(), -1, &bottomInfoRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                    DeleteObject(panelPen);
                    DeleteObject(panelBrush);
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
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            StopSlideshow();
            if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }
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
            const HWND window = hwnd_;
            hwnd_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
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
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}