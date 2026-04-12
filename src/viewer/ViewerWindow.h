#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"

namespace hyperbrowse::viewer
{
    class ViewerWindow
    {
    public:
        static constexpr UINT kZoomChangedMessage = WM_APP + 60;
        static constexpr UINT kActivityChangedMessage = WM_APP + 61;
        static constexpr UINT kClosedMessage = WM_APP + 62;

        explicit ViewerWindow(HINSTANCE instance);
        ~ViewerWindow();

        bool Open(HWND owner, std::vector<browser::BrowserItem> items, int selectedIndex, bool darkTheme);
        HWND Hwnd() const noexcept;
        bool IsOpen() const noexcept;
        bool IsFullScreen() const noexcept;
        int CurrentIndex() const noexcept;
        int CurrentZoomPercent() const noexcept;
        int RotationQuarterTurns() const noexcept;
        POINT PanOffset() const noexcept;
        void SetDarkTheme(bool enabled);

    private:
        static constexpr const wchar_t* kWindowClassName = L"HyperBrowseViewerWindow";
        static constexpr UINT kDecodedImageMessage = WM_APP + 63;
        static constexpr UINT kPrefetchImageMessage = WM_APP + 64;

        enum class ZoomMode
        {
            Fit,
            Custom,
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
        void LoadCurrentImageAsync();
        void Navigate(int delta);
        void PrepareForImageChange();
        void ResetCachedImageSlots();
        void ResetPrefetchStatistics();
        void ScheduleAdjacentPrefetch(std::uint64_t navigationGeneration);
        void StartPrefetch(int index, std::uint64_t navigationGeneration);
        void SetCurrentImageSlot(int index,
                                 std::shared_ptr<const cache::CachedThumbnail> image,
                                 bool prefetched);
        void LogPrefetchStats() const;
        void ZoomBy(double factor);
        void FitToWindow();
        void SetActualSize();
        void RotateLeft();
        void RotateRight();
        void ToggleFullScreen();
        void ResetViewState();
        double FitScaleForClient(const RECT& clientRect) const;
        double EffectiveScaleForClient(const RECT& clientRect) const;
        void NotifyZoomChanged(int zoomPercent);
        void NotifyActivityChanged(bool isActive) const;
        LRESULT HandleDecodedImageMessage(LPARAM lParam);
        LRESULT HandlePrefetchImageMessage(LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HINSTANCE instance_{};
        HWND owner_{};
        HWND hwnd_{};
        std::vector<browser::BrowserItem> items_;
        int currentIndex_{-1};
        bool darkTheme_{};
        bool loading_{};
        std::wstring errorMessage_;
        std::shared_ptr<const cache::CachedThumbnail> currentImage_;
        CachedImageSlot currentSlot_;
        CachedImageSlot previousSlot_;
        CachedImageSlot nextSlot_;
        std::shared_ptr<AsyncState> asyncState_;
        HBRUSH backgroundBrush_{};
        ZoomMode zoomMode_{ZoomMode::Fit};
        double customZoomScale_{1.0};
        int currentZoomPercent_{};
        int rotationQuarterTurns_{};
        double panOffsetX_{};
        double panOffsetY_{};
        bool panning_{};
        POINT lastPanPoint_{};
        bool fullScreen_{};
        DWORD windowedStyle_{};
        DWORD windowedExStyle_{};
        WINDOWPLACEMENT windowedPlacement_{sizeof(WINDOWPLACEMENT)};
        std::atomic_uint64_t prefetchRequestCount_{0};
        std::atomic_uint64_t prefetchCompletedCount_{0};
        std::atomic_uint64_t prefetchCancelledCount_{0};
        std::atomic_uint64_t prefetchHitCount_{0};
        std::atomic_uint64_t prefetchMissCount_{0};
    };
}