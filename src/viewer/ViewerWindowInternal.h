#pragma once

#include "viewer/ViewerWindow.h"

#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d2d1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <dwrite.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <exception>

#include "app/resource.h"
#include "decode/ImageDecoder.h"
#include "render/D2DRenderer.h"
#include "render/GdiText.h"
#include "services/ImageMetadataService.h"
#include "browser/ThumbnailRatingKeyPolicy.h"
#include "ui/ItemNumberNavigationPolicy.h"
#include "ui/MainWindowDialogs.h"
#include "ui/SystemTheme.h"
#include "ui/ThemeTokens.h"
#include "ui/ViewerTransitionPolicy.h"
#include "util/ResourcePng.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/SettingsRegistry.h"
#include "util/Timing.h"

// Helpers used by more than one translation unit. They were file-local before the
// source file was split, so they are inline and live in a named namespace.
namespace viewer_window_shared
{

    inline constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";
    inline constexpr wchar_t kRegistryValueViewerInfoOverlayTextSize[] = L"ViewerInfoOverlayTextSize";
    inline constexpr wchar_t kRegistryValueViewerWindowedFullMetadataVisible[] = L"ViewerWindowedFullMetadataVisible";
    inline constexpr wchar_t kRegistryValueViewerFullScreenFullMetadataVisible[] = L"ViewerFullScreenFullMetadataVisible";
    inline constexpr wchar_t kRegistryValueViewerFullMetadataVisible[] = L"ViewerFullMetadataVisible";
    inline constexpr std::size_t kViewerConservativeBackgroundQueueCapacity = 16;
    inline constexpr std::size_t kViewerBalancedBackgroundQueueCapacity = 32;
    inline constexpr std::size_t kViewerPerformanceBackgroundQueueCapacity = 64;
    inline constexpr std::size_t kViewerAggressiveBackgroundQueueCapacity = 256;
    inline constexpr double kMaximumZoomScale = 64.0;
    inline constexpr float kViewerOverlayScale = 0.90f;

    using InfoOverlayTextSize = hyperbrowse::viewer::InfoOverlayTextSize;

    struct ViewerOverlayMetrics
    {
        float nameFontSize;
        float infoFontSize;
        float bottomInfoFontSize;
        float metadataFontSize;
        float overlayWidthScale;
        float topPanelPaddingX;
        float topPanelPaddingY;
        float bottomPanelPaddingX;
        float bottomPanelPaddingY;
        float topNameHeight;
        float topInfoHeight;
        float bottomInfoHeight;
        float loadingTextInset;
        float loadingTitleHeight;
        float loadingBodyHeight;
        float loadingGap;
        float metadataPanelPadding;
        float metadataGap;
    };

    inline D2D1_RECT_F ViewerOverlayTextRect(const RECT& panel,
                                           const ViewerOverlayMetrics& metrics,
                                           hyperbrowse::viewer::OverlayPane pane)
    {
        float paddingX = metrics.metadataPanelPadding;
        float paddingY = metrics.metadataPanelPadding;
        if (pane == hyperbrowse::viewer::OverlayPane::FileInformation)
        {
            paddingX = metrics.topPanelPaddingX;
            paddingY = metrics.topPanelPaddingY;
        }
        else if (pane == hyperbrowse::viewer::OverlayPane::ImageStatus)
        {
            paddingX = metrics.bottomPanelPaddingX;
            paddingY = metrics.bottomPanelPaddingY;
        }
        return D2D1::RectF(panel.left + paddingX, panel.top + paddingY,
                          panel.right - paddingX, panel.bottom - paddingY);
    }

    inline std::wstring FormatWindowHandle(HWND hwnd)
    {
        return std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
    }

    inline hyperbrowse::cache::ThumbnailCacheKey MakeViewerFullImageCacheKey(const hyperbrowse::browser::BrowserItem& item)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = item.filePath;
        key.modifiedTimestampUtc = item.modifiedTimestampUtc;
        key.targetWidth = 0;
        key.targetHeight = 0;
        return key;
    }

    inline hyperbrowse::cache::ThumbnailCache& ViewerFullImageCache()
    {
        static hyperbrowse::cache::ThumbnailCache cache(
            hyperbrowse::util::ResolveViewerFullImageCacheCapacityBytes(
                hyperbrowse::util::ResourceProfile::Balanced));
        return cache;
    }

    inline std::size_t ViewerBackgroundQueueCapacity(hyperbrowse::util::ResourceProfile profile) noexcept
    {
        switch (profile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            return kViewerConservativeBackgroundQueueCapacity;
        case hyperbrowse::util::ResourceProfile::Performance:
            return kViewerPerformanceBackgroundQueueCapacity;
        case hyperbrowse::util::ResourceProfile::Aggressive:
            return kViewerAggressiveBackgroundQueueCapacity;
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            return kViewerBalancedBackgroundQueueCapacity;
        }
    }

    inline COLORREF BackgroundColor(bool darkTheme)
    {
        if (hyperbrowse::ui::IsHighContrastEnabled())
        {
            return GetSysColor(COLOR_WINDOW);
        }
        return darkTheme ? RGB(18, 21, 25) : RGB(247, 249, 252);
    }

    inline COLORREF TextColor(bool darkTheme)
    {
        return hyperbrowse::ui::MakeThemeTokens(hyperbrowse::ui::ResolveThemeVariant(darkTheme)).text;
    }

    inline COLORREF MutedTextColor(bool darkTheme)
    {
        if (hyperbrowse::ui::IsHighContrastEnabled())
        {
            return GetSysColor(COLOR_GRAYTEXT);
        }
        return darkTheme ? RGB(165, 176, 188) : RGB(96, 107, 118);
    }

    inline COLORREF PanelFillColor(bool darkTheme)
    {
        return hyperbrowse::ui::MakeThemeTokens(hyperbrowse::ui::ResolveThemeVariant(darkTheme)).surfaceBackground;
    }

    inline COLORREF PanelBorderColor(bool darkTheme)
    {
        return hyperbrowse::ui::MakeThemeTokens(hyperbrowse::ui::ResolveThemeVariant(darkTheme)).border;
    }

    inline HFONT CreateViewerMenuFont(hyperbrowse::util::AppTextSize size)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) == FALSE)
        {
            return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
        metrics.lfMessageFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        metrics.lfMessageFont.lfHeight = static_cast<LONG>(
            std::lround(static_cast<double>(metrics.lfMessageFont.lfHeight)
                        * hyperbrowse::util::AppTextSizeScale(size)));
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    inline HFONT MenuFontOrDefault(HFONT menuFont)
    {
        return menuFont ? menuFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    inline float MetadataPanelFillAlpha(bool darkTheme)
    {
        return darkTheme ? 0.68f : 0.74f;
    }

    inline bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    inline void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    inline void SaveViewerInfoOverlaysVisibleSetting(bool visible)
    {
        HKEY key{};
        if (hyperbrowse::util::CreateSettingsRegistryKey(KEY_WRITE, &key) != ERROR_SUCCESS)
        {
            return;
        }

        WriteDwordValue(key, kRegistryValueViewerInfoOverlaysVisible, visible ? 1UL : 0UL);
        RegCloseKey(key);
    }

    inline void SaveViewerInfoOverlayTextSizeSetting(InfoOverlayTextSize size)
    {
        HKEY key{};
        if (hyperbrowse::util::CreateSettingsRegistryKey(KEY_WRITE, &key) != ERROR_SUCCESS)
        {
            return;
        }

        WriteDwordValue(key, kRegistryValueViewerInfoOverlayTextSize, static_cast<DWORD>(size));
        RegCloseKey(key);
    }

    inline bool LoadViewerFullMetadataVisibleSetting(const wchar_t* valueName)
    {
        HKEY key{};
        if (hyperbrowse::util::OpenSettingsRegistryKey(KEY_READ, &key) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD value = 0;
        bool foundValue = TryReadDwordValue(key, valueName, &value);
        if (!foundValue)
        {
            foundValue = TryReadDwordValue(key, kRegistryValueViewerFullMetadataVisible, &value);
        }
        RegCloseKey(key);
        return foundValue && value != 0;
    }

    inline void SaveViewerFullMetadataVisibleSetting(const wchar_t* valueName, bool visible)
    {
        HKEY key{};
        if (hyperbrowse::util::CreateSettingsRegistryKey(KEY_WRITE, &key) != ERROR_SUCCESS)
        {
            return;
        }

        WriteDwordValue(key, valueName, visible ? 1UL : 0UL);
        RegCloseKey(key);
    }

    inline const ViewerOverlayMetrics& ViewerOverlayMetricsForTextSize(InfoOverlayTextSize size)
    {
        static const ViewerOverlayMetrics kSmall{
            13.0f,
            11.0f,
            13.0f,
            14.0f,
            1.0f,
            14.0f,
            10.0f,
            14.0f,
            12.0f,
            24.0f,
            24.0f,
            18.0f,
            20.0f,
            34.0f,
            34.0f,
            8.0f,
            18.0f,
            10.0f,
        };
        static const ViewerOverlayMetrics kMedium{
            20.0f,
            16.0f,
            19.0f,
            18.0f,
            1.18f,
            18.0f,
            12.0f,
            16.0f,
            14.0f,
            30.0f,
            28.0f,
            26.0f,
            24.0f,
            42.0f,
            40.0f,
            10.0f,
            20.0f,
            12.0f,
        };
        static const ViewerOverlayMetrics kLarge{
            25.0f,
            19.0f,
            23.0f,
            21.0f,
            1.28f,
            20.0f,
            14.0f,
            18.0f,
            16.0f,
            34.0f,
            32.0f,
            32.0f,
            28.0f,
            48.0f,
            46.0f,
            12.0f,
            22.0f,
            14.0f,
        };

        switch (size)
        {
        case InfoOverlayTextSize::Medium:
            return kMedium;
        case InfoOverlayTextSize::Large:
            return kLarge;
        case InfoOverlayTextSize::Small:
        default:
            return kSmall;
        }
    }

    inline ViewerOverlayMetrics ViewerOverlayMetricsForDpi(UINT dpi, InfoOverlayTextSize size)
    {
        ViewerOverlayMetrics metrics = ViewerOverlayMetricsForTextSize(size);
        const float scale = (static_cast<float>(dpi == 0 ? 96 : dpi) / 96.0f) * kViewerOverlayScale;
        metrics.nameFontSize *= scale;
        metrics.infoFontSize *= scale;
        metrics.bottomInfoFontSize *= scale;
        metrics.metadataFontSize *= scale;
        metrics.topPanelPaddingX *= scale;
        metrics.topPanelPaddingY *= scale;
        metrics.bottomPanelPaddingX *= scale;
        metrics.bottomPanelPaddingY *= scale;
        metrics.topNameHeight *= scale;
        metrics.topInfoHeight *= scale;
        metrics.bottomInfoHeight *= scale;
        metrics.loadingTextInset *= scale;
        metrics.loadingTitleHeight *= scale;
        metrics.loadingBodyHeight *= scale;
        metrics.loadingGap *= scale;
        metrics.metadataPanelPadding *= scale;
        metrics.metadataGap *= scale;
        metrics.overlayWidthScale *= scale;
        return metrics;
    }
    inline ViewerOverlayMetrics ViewerOverlayMetricsForWindow(HWND hwnd, InfoOverlayTextSize size)
    {
        return ViewerOverlayMetricsForDpi(hwnd ? GetDpiForWindow(hwnd) : 96, size);
    }
}

using namespace viewer_window_shared;
