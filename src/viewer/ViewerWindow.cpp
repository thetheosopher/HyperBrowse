#include "viewer/ViewerWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>

#include "decode/ImageDecoder.h"
#include "util/Diagnostics.h"
#include "util/Log.h"

namespace
{
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
}

namespace hyperbrowse::viewer
{
    ViewerWindow::ViewerWindow(HINSTANCE instance)
        : instance_(instance)
        , asyncState_(std::make_shared<AsyncState>())
    {
        backgroundBrush_ = CreateSolidBrush(BackgroundColor(false));
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

    bool ViewerWindow::Open(HWND owner, std::vector<browser::BrowserItem> items, int selectedIndex, bool darkTheme)
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
                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
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
        ShowWindow(hwnd_, SW_SHOWNORMAL);
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
            InvalidateRect(hwnd_, nullptr, TRUE);
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
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
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
            InvalidateRect(hwnd_, nullptr, TRUE);
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
                    InvalidateRect(hwnd_, nullptr, TRUE);
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
                    InvalidateRect(hwnd_, nullptr, TRUE);
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
        if (!currentImage_ || factor <= 0.0)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const double baseScale = zoomMode_ == ZoomMode::Fit ? FitScaleForClient(clientRect) : customZoomScale_;
        zoomMode_ = ZoomMode::Custom;
        customZoomScale_ = std::clamp(baseScale * factor, 0.05, 16.0);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void ViewerWindow::FitToWindow()
    {
        zoomMode_ = ZoomMode::Fit;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, TRUE);
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
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    void ViewerWindow::RotateLeft()
    {
        rotationQuarterTurns_ = (rotationQuarterTurns_ + 3) % 4;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    void ViewerWindow::RotateRight()
    {
        rotationQuarterTurns_ = (rotationQuarterTurns_ + 1) % 4;
        panOffsetX_ = 0.0;
        panOffsetY_ = 0.0;
        if (hwnd_)
        {
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    void ViewerWindow::ToggleFullScreen()
    {
        if (!hwnd_)
        {
            return;
        }

        if (!fullScreen_)
        {
            windowedStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
            windowedExStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
            GetWindowPlacement(hwnd_, &windowedPlacement_);

            MONITORINFO monitorInfo{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo);
            SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_ & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU));
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_ & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
            SetWindowPos(hwnd_, HWND_TOP,
                         monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            fullScreen_ = true;
            return;
        }

        SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_);
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fullScreen_ = false;
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
            InvalidateRect(hwnd_, nullptr, TRUE);
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
            InvalidateRect(hwnd_, nullptr, TRUE);
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
                break;
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
                InvalidateRect(hwnd_, nullptr, TRUE);
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
            FillRect(hdc, &clientRect, backgroundBrush_);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, TextColor(darkTheme_));

            if (!currentImage_)
            {
                RECT textRect = clientRect;
                InflateRect(&textRect, -24, -24);
                const std::wstring messageText = loading_
                    ? L"Loading image..."
                    : (errorMessage_.empty() ? L"No image is currently loaded." : errorMessage_);
                DrawTextW(hdc, messageText.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                EndPaint(hwnd_, &paintStruct);
                return 0;
            }

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

            const int clientWidth = clientRect.right - clientRect.left;
            const int clientHeight = clientRect.bottom - clientRect.top;
            const int x = static_cast<int>(std::lround(((clientWidth - destinationWidth) / 2.0) + panOffsetX_));
            const int y = static_cast<int>(std::lround(((clientHeight - destinationHeight) / 2.0) + panOffsetY_));

            HDC bitmapDc = CreateCompatibleDC(hdc);
            HGDIOBJ oldBitmap = SelectObject(bitmapDc, currentImage_->Bitmap());
            SetStretchBltMode(hdc, HALFTONE);

            if (rotationQuarterTurns_ == 0)
            {
                StretchBlt(hdc, x, y, destinationWidth, destinationHeight, bitmapDc, 0, 0, sourceWidth, sourceHeight, SRCCOPY);
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

                PlgBlt(hdc, destination, bitmapDc, 0, 0, sourceWidth, sourceHeight, nullptr, 0, 0);
            }

            SelectObject(bitmapDc, oldBitmap);
            DeleteDC(bitmapDc);
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