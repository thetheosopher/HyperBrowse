#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"
#include "util/BackgroundExecutor.h"
#include "util/ResourceSizing.h"

namespace hyperbrowse::viewer
{
    enum class CompareDirection : int
    {
        Previous = -1,
        Next = 1,
    };

    enum class MouseWheelBehavior : int
    {
        Zoom = 0,
        Navigate = 1,
    };

    enum class TransitionStyle : int
    {
        Cut = 0,
        Crossfade = 1,
        Slide = 2,
        KenBurns = 3,
    };

    class ViewerWindow
    {
    public:
        static constexpr UINT kZoomChangedMessage = WM_APP + 60;
        static constexpr UINT kActivityChangedMessage = WM_APP + 61;
        static constexpr UINT kClosedMessage = WM_APP + 62;
        static constexpr UINT kDeleteRequestedMessage = WM_APP + 65;
        static constexpr WPARAM kDeleteRequestPermanent = 0x1;

        explicit ViewerWindow(HINSTANCE instance);
        ~ViewerWindow();

        bool Open(HWND owner,
                  std::vector<browser::BrowserItem> items,
                  int selectedIndex,
                  bool darkTheme,
                  HMONITOR targetMonitor = nullptr);
        HWND Hwnd() const noexcept;
        bool IsOpen() const noexcept;
        bool IsFullScreen() const noexcept;
        int CurrentIndex() const noexcept;
        std::wstring CurrentFilePath() const;
        int CurrentZoomPercent() const noexcept;
        int RotationQuarterTurns() const noexcept;
        POINT PanOffset() const noexcept;
        bool AreInfoOverlaysVisible() const noexcept;
        void StartSlideshow(UINT intervalMs = 3000);
        void StopSlideshow();
        bool IsSlideshowActive() const noexcept;
        void SetCompareMode(bool enabled, CompareDirection direction = CompareDirection::Next);
        bool IsCompareModeEnabled() const noexcept;
        void SetMouseWheelBehavior(MouseWheelBehavior behavior) noexcept;
        void SetTransitionSettings(TransitionStyle style, UINT durationMs);
        void SetInfoOverlaysVisible(bool visible);
        void SetMemoryPressureActive(bool active);
        void SetResourceProfile(util::ResourceProfile profile) noexcept;
        void SetDarkTheme(bool enabled);
        bool ReplaceItems(std::vector<browser::BrowserItem> items, int selectedIndex);
        bool PrepareDeleteCurrent(std::wstring* sourcePath, std::wstring* preferredFocusPath);

    private:
        static constexpr const wchar_t* kWindowClassName = L"HyperBrowseViewerWindow";
        static constexpr UINT kDecodedImageMessage = WM_APP + 63;
        static constexpr UINT kPrefetchImageMessage = WM_APP + 64;

        enum class ZoomMode
        {
            Fit,
            Custom,
        };

        enum class LoadReason
        {
            Open,
            Navigation,
        };

        struct AsyncState
        {
            std::atomic_uint64_t activeRequestId{0};
            std::atomic_uint64_t navigationGeneration{0};
            std::atomic_bool shutdown{false};
            HWND targetWindow{};
        };

        struct CachedImageSlot
        {
            int index{-1};
            std::shared_ptr<const cache::CachedThumbnail> image;
            bool prefetched{};
        };

        bool RegisterWindowClass() const;
        void UpdateWindowTitle() const;
        void LoadCurrentImageAsync(LoadReason reason);
        void Navigate(int delta);
        void PrepareForImageChange(bool keepDisplayedImage = false);
        void ResetCachedImageSlots();
        void ResetPrefetchStatistics();
        int BasePrefetchRadius() const noexcept;
        int EffectivePrefetchRadius() const noexcept;
        void ScheduleAdjacentPrefetch(std::uint64_t navigationGeneration);
        void StartPrefetch(int index, std::uint64_t navigationGeneration);
        void SetCurrentImageSlot(int index,
                                 std::shared_ptr<const cache::CachedThumbnail> image,
                                 bool prefetched);
        void ReapCompletedBackgroundTasks();
        void WaitForBackgroundTasks();
        void LogPrefetchStats() const;
        void ZoomBy(double factor);
        void FitToWindow();
        void SetActualSize();
        void RotateLeft();
        void RotateRight();
        void ToggleCompareMode();
        void ActivateComparedImage();
        void ToggleInfoOverlays();
        HMONITOR ResolveTargetMonitor(HMONITOR preferredMonitor) const noexcept;
        void SetFullScreen(bool enabled, HMONITOR targetMonitor = nullptr);
        void ToggleFullScreen();
        void AdvanceSlideshow();
        int DisplayedImageIndex() const noexcept;
        void QueueTransitionFromCurrent(bool forward);
        void BeginTransitionFromPending();
        void StopTransition(bool clearPending = true);
        void ResetViewState();
        CompareDirection ResolveCompareDirection(CompareDirection preferred) const noexcept;
        int CompareIndexForDirection(CompareDirection direction) const noexcept;
        int ActiveCompareIndex() const noexcept;
        double FitScaleForImage(const cache::CachedThumbnail& image, const RECT& clientRect) const;
        double FitScaleForClient(const RECT& clientRect) const;
        double EffectiveScaleForClient(const RECT& clientRect) const;
        void DrawImageBitmap(ID2D1RenderTarget* renderTarget,
                     ID2D1Bitmap* bitmap,
                     const cache::CachedThumbnail& image,
                     const RECT& clientRect,
                     float opacity,
                     float scaleMultiplier,
                     float offsetX,
                     float offsetY) const;
        void RequestRepaint() const;
        void NotifyZoomChanged(int zoomPercent);
        void NotifyActivityChanged(bool isActive) const;
        void EnsureD2DRenderTarget();
        void ReleaseD2DResources();
        void RebuildD2DBrushes();
        LRESULT HandleDecodedImageMessage(LPARAM lParam);
        LRESULT HandlePrefetchImageMessage(LPARAM lParam);
        LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HINSTANCE instance_{};
        HWND owner_{};
        HWND hwnd_{};
        std::vector<browser::BrowserItem> items_;
        int currentIndex_{-1};
        bool darkTheme_{};
        bool loading_{};
        std::wstring errorMessage_;
        util::ResourceProfile resourceProfile_{util::ResourceProfile::Balanced};
        bool memoryPressureActive_{};
        std::shared_ptr<const cache::CachedThumbnail> currentImage_;
        CachedImageSlot currentSlot_;
        CachedImageSlot previousSlot_;
        CachedImageSlot nextSlot_;
        std::shared_ptr<AsyncState> asyncState_;
        HBRUSH backgroundBrush_{};
        std::shared_ptr<const cache::CachedThumbnail> statusArt_;
        ZoomMode zoomMode_{ZoomMode::Fit};
        double customZoomScale_{1.0};
        int currentZoomPercent_{};
        int rotationQuarterTurns_{};
        double panOffsetX_{};
        double panOffsetY_{};
        bool compareMode_{};
        CompareDirection compareDirection_{CompareDirection::Next};
        bool infoOverlaysVisible_{true};
        MouseWheelBehavior mouseWheelBehavior_{MouseWheelBehavior::Zoom};
        bool panning_{};
        POINT lastPanPoint_{};
        bool fullScreen_{};
        bool slideshowActive_{};
        UINT slideshowIntervalMs_{3000};
        UINT_PTR slideshowTimerId_{};
        TransitionStyle transitionStyle_{TransitionStyle::Crossfade};
        UINT transitionDurationMs_{350};
        bool transitionActive_{};
        bool transitionForward_{true};
        std::chrono::steady_clock::time_point transitionStartedAt_{};
        std::shared_ptr<const cache::CachedThumbnail> transitionFromImage_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> transitionFromBitmap_;
        int transitionFromIndex_{-1};
        std::shared_ptr<const cache::CachedThumbnail> pendingTransitionFromImage_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> pendingTransitionFromBitmap_;
        int pendingTransitionFromIndex_{-1};
        bool pendingTransitionForward_{true};
        UINT_PTR transitionTimerId_{};
        DWORD windowedStyle_{};
        DWORD windowedExStyle_{};
        WINDOWPLACEMENT windowedPlacement_{sizeof(WINDOWPLACEMENT)};
        std::atomic_uint64_t prefetchRequestCount_{0};
        std::atomic_uint64_t prefetchCompletedCount_{0};
        std::atomic_uint64_t prefetchCancelledCount_{0};
        std::atomic_uint64_t prefetchHitCount_{0};
        std::atomic_uint64_t prefetchMissCount_{0};
        int slideshowNextPrefetchIndex_{-1};
        std::uint64_t slideshowNextPrefetchGeneration_{};
        bool slideshowAdvancePending_{};
        LoadReason pendingLoadReason_{LoadReason::Navigation};
        std::chrono::steady_clock::time_point pendingLoadStartedAt_{};
        bool pendingLoadActive_{};
        bool preserveDisplayedImageWhileLoading_{};
        std::unique_ptr<hyperbrowse::util::BackgroundExecutor> backgroundExecutor_;

        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> d2dRenderTarget_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dBackgroundBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dTextBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dMutedTextBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dPanelFillBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dPanelBorderBrush_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dNameFormat_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dInfoFormat_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dCurrentImageBitmap_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dCompareImageBitmap_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dStatusArtBitmap_;
        int d2dCurrentImageIndex_{-1};
        int d2dCompareImageIndex_{-1};

        double smoothZoomTarget_{1.0};
        double smoothZoomCurrent_{1.0};
        UINT_PTR smoothZoomTimerId_{};
        static constexpr UINT_PTR kSmoothZoomTimerId = 9002;
        static constexpr UINT kSmoothZoomIntervalMs = 16;
        static constexpr UINT_PTR kTransitionTimerId = 9003;
        static constexpr UINT kTransitionIntervalMs = 16;
    };
}