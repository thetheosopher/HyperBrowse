#include "viewer/ViewerWindowInternal.h"

namespace
{

    std::size_t EffectiveViewerFullImageCacheCapacity(hyperbrowse::util::ResourceProfile profile,
                                                       bool memoryPressureActive) noexcept
    {
        const std::size_t resolvedCapacity = hyperbrowse::util::ResolveViewerFullImageCacheCapacityBytes(profile);
        return memoryPressureActive
            ? std::max<std::size_t>(1, resolvedCapacity / 2)
            : resolvedCapacity;
    }
}

namespace hyperbrowse::viewer
{

    void ViewerWindow::SetMouseWheelBehavior(MouseWheelBehavior behavior) noexcept
    {
        mouseWheelBehavior_ = behavior;
    }

    void ViewerWindow::SetKeyboardPanningInverted(bool inverted) noexcept
    {
        keyboardPanningInverted_ = inverted;
    }

    void ViewerWindow::SetEscapeKeyBehavior(EscapeKeyBehavior behavior) noexcept
    {
        escapeKeyBehavior_ = behavior;
    }

    bool ViewerWindow::IsKeyboardPanningInverted() const noexcept
    {
        return keyboardPanningInverted_;
    }

    void ViewerWindow::SetTransitionSettings(TransitionStyle style, UINT durationMs)
    {
        transitionStyle_ = style;
        transitionDurationMs_ = std::clamp<UINT>(durationMs, 100U, 5000U);
        activeTransitionStyle_ = transitionStyle_;

        if (transitionStyle_ == TransitionStyle::Cut)
        {
            StopTransition();
        }
    }

    void ViewerWindow::SetManualTransitionEnabled(bool enabled)
    {
        manualTransitionEnabled_ = enabled;
        if (!manualTransitionEnabled_ && !slideshowActive_)
        {
            StopTransition();
        }
    }

    TransitionStyle ViewerWindow::ResolveActiveTransitionStyle() noexcept
    {
        if (transitionStyle_ != TransitionStyle::Random)
        {
            return transitionStyle_;
        }

        static constexpr std::array<TransitionStyle, 19> kRandomStyles = {
            TransitionStyle::Crossfade,
            TransitionStyle::Slide,
            TransitionStyle::KenBurns,
            TransitionStyle::FadeToBlack,
            TransitionStyle::DiagonalSlide,
            TransitionStyle::Push,
            TransitionStyle::CenterWipe,
            TransitionStyle::VenetianBlinds,
            TransitionStyle::SplitWipe,
            TransitionStyle::HorizontalBlinds,
            TransitionStyle::CheckerboardWipe,
            TransitionStyle::ZoomFade,
            TransitionStyle::BlurCrossfade,
            TransitionStyle::MotionBlur,
            TransitionStyle::ColorWash,
            TransitionStyle::SepiaDrift,
            TransitionStyle::Flashbulb,
            TransitionStyle::Prism,
            TransitionStyle::MonochromeReveal,
        };
        std::uniform_int_distribution<std::size_t> distribution(0, kRandomStyles.size() - 1);
        return kRandomStyles[distribution(transitionRandomEngine_)];
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

    void ViewerWindow::SetOverlayTextSize(InfoOverlayTextSize size)
    {
        if (infoOverlayTextSize_ == size)
        {
            return;
        }

        infoOverlayTextSize_ = size;
        SaveViewerInfoOverlayTextSizeSetting(infoOverlayTextSize_);
        if (d2dRenderTarget_)
        {
            RebuildD2DTextFormats();
        }
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
    }

    void ViewerWindow::SetAppTextSize(util::AppTextSize size)
    {
        const util::AppTextSize normalized = util::NormalizeAppTextSize(static_cast<std::uint32_t>(size));
        if (appTextSize_ == normalized)
        {
            return;
        }

        if (menuFont_ && menuFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(menuFont_);
        }
        appTextSize_ = normalized;
        menuFont_ = CreateViewerMenuFont(appTextSize_);
    }

    void ViewerWindow::SetFullMetadataVisible(bool visible)
    {
        if (fullMetadataVisible_ != visible)
        {
            fullMetadataVisible_ = visible;
            SaveViewerFullMetadataVisibleSetting(
                fullScreen_ ? kRegistryValueViewerFullScreenFullMetadataVisible : kRegistryValueViewerWindowedFullMetadataVisible,
                fullMetadataVisible_);
        }

        ApplyFullMetadataVisible(fullMetadataVisible_);
    }

    void ViewerWindow::ApplyFullMetadataVisible(bool visible)
    {
        fullMetadataVisible_ = visible;
        if (fullMetadataVisible_)
        {
            LoadMetadataAsyncForIndex(currentIndex_);
        }
        else
        {
            ClearCurrentMetadata();
        }

        if (hwnd_ && IsWindow(hwnd_) != FALSE)
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

        ViewerFullImageCache().SetCapacityBytes(
            EffectiveViewerFullImageCacheCapacity(resourceProfile_, memoryPressureActive_));

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
        ViewerFullImageCache().SetCapacityBytes(
            EffectiveViewerFullImageCacheCapacity(resourceProfile_, memoryPressureActive_));
        if (backgroundExecutor_)
        {
            backgroundExecutor_->SetMaxPendingTaskCount(ViewerBackgroundQueueCapacity(resourceProfile_));
        }
        if (hwnd_ && currentImage_ && !pendingLoadActive_)
        {
            const std::uint64_t navigationGeneration = asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            ScheduleAdjacentPrefetch(navigationGeneration);
        }
    }

    void ViewerWindow::SetPrefetchDepthOverride(int depth) noexcept
    {
        const int normalizedDepth = depth == util::kAutomaticPrefetchDepth
            ? util::kAutomaticPrefetchDepth
            : std::clamp(depth, util::kMinimumPrefetchDepth, util::kMaximumPrefetchDepth);
        if (prefetchDepthOverride_ == normalizedDepth)
        {
            return;
        }

        prefetchDepthOverride_ = normalizedDepth;
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
}
