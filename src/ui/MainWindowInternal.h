#pragma once

#include "ui/MainWindow.h"

#include <commctrl.h>
#include <Xinput.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <richedit.h>
#include <wtsapi32.h>
#include <wrl/client.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/Application.h"
#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "browser/ThumbnailRatingKeyPolicy.h"
#include "cache/DiskThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileAssociationService.h"
#include "services/FileOperationService.h"
#include "services/ImageMetadataService.h"
#include "services/ImageCommandService.h"
#include "services/JpegTransformService.h"
#include "services/ThumbnailScheduler.h"
#include "services/UserMetadataStore.h"
#include "ui/DialogTheme.h"
#include "ui/DialogDpi.h"
#include "ui/DialogShell.h"
#include "ui/BrowserItemScopeCollector.h"
#include "ui/DiagnosticsWindow.h"
#include "ui/CommandIds.h"
#include "ui/CacheStatsPainter.h"
#include "ui/ClipboardFileTransfer.h"
#include "ui/ExternalDropTarget.h"
#include "ui/FileCommandController.h"
#include "ui/CommandBarPainter.h"
#include "ui/DetailsPanelHistogram.h"
#include "ui/DetailsPanelChromePainter.h"
#include "ui/DetailsPanelHistogramPainter.h"
#include "ui/DetailsPanelLayout.h"
#include "ui/DetailsPanelSurfacePainter.h"
#include "ui/DetailsPanelTextPainter.h"
#include "ui/DisplaySurfaceRecoveryPolicy.h"
#include "ui/FileOperationJournal.h"
#include "ui/FolderTreeDropPolicy.h"
#include "ui/FolderTreeDragController.h"
#include "ui/RightPaneHitTester.h"
#include "ui/SystemTheme.h"
#include "ui/FolderWatchChangeCoordinator.h"
#include "ui/MainWindowDialogs.h"
#include "ui/MainWindowDialogState.h"
#include "ui/SettingsLayout.h"
#include "ui/MenuMessageHandling.h"
#include "ui/QuickAccessDestinationBuilder.h"
#include "ui/QuickAccessPathList.h"
#include "ui/QuickAccessPainter.h"
#include "ui/QuickAccessShortcutEditPolicy.h"
#include "ui/QuickSendPersistence.h"
#include "ui/ShortcutCatalog.h"
#include "ui/ShellDragSource.h"
#include "ui/ShellPainter.h"
#include "ui/SelectionRatingPolicy.h"
#include "ui/SavedSearchController.h"
#include "ui/SelectedPathPersistence.h"
#include "ui/StatusBarPainter.h"
#include "ui/ToolbarIconLibrary.h"
#include "ui/WindowAsyncMessageRouter.h"
#include "ui/WindowBoundsPersistence.h"
#include "ui/BrowserPresentationPersistence.h"
#include "ui/ImageWorkflowPersistence.h"
#include "ui/ItemNumberNavigationPolicy.h"
#include "ui/PerformanceSettingsPersistence.h"
#include "ui/PerformanceHud.h"
#include "ui/PairedRawJpegResolver.h"
#include "ui/ViewerSettingsPersistence.h"
#include "ui/ViewerItemSelectionPolicy.h"
#include "ui/ViewerSynchronizer.h"
#include "render/D2DRenderer.h"
#include "util/BackgroundExecutor.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/NamedMutexLock.h"
#include "util/ResourcePng.h"
#include "util/ResourceSizing.h"
#include "util/SettingsRegistry.h"
#include "util/StringConvert.h"
#include "util/Timing.h"
#include "util/UiTextSize.h"
#include "render/GdiText.h"
#include "viewer/CompareSessionPolicy.h"
#include "viewer/ViewerWindow.h"

#include "app/resource.h"
#include "HyperBrowseBuildInfo.h"

namespace fs = std::filesystem;

using namespace hyperbrowse::ui::command_ids;

namespace hyperbrowse::ui
{
    void InitializeExperimentalSettingsAccessibility(
        dialog_detail::ExperimentalSettingsDialogState& state);

    struct PersistentThumbnailCacheMaintenanceState
    {
        std::mutex mutex;
        cache::DiskThumbnailCache::Statistics statistics;
        std::vector<std::wstring> sourceFilePaths;
        std::atomic_uint completionMessage{};
    };
}

// Helpers that are used by more than one MainWindow translation unit. They were file-local
// before MainWindow.cpp was split, so they are inline and live in a named namespace.
namespace main_window_shared
{
    using hyperbrowse::ui::dialog_detail::ConsolidatedSettingsControl;
    using hyperbrowse::ui::dialog_detail::ExperimentalSettingsDialogState;
    using hyperbrowse::ui::dialog_detail::SlideshowTransitionOption;
    using hyperbrowse::ui::AdjustDialogWindowRectForDpi;
    using hyperbrowse::ui::CenterDialogInWorkArea;
    using hyperbrowse::ui::ClampDialogFrameToWorkArea;
    using hyperbrowse::ui::DialogDpiForWindow;
    using hyperbrowse::ui::DialogShellMetrics;
    using hyperbrowse::ui::MeasureDialogShellMetrics;
    using hyperbrowse::ui::PersistentThumbnailCacheMaintenanceState;
    using hyperbrowse::ui::ScaleDialogAppTextDimension;

    inline constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
    inline constexpr DWORD kDwmUseImmersiveDarkModeLegacyAttribute = 19;
    inline constexpr DWORD kDwmBorderColorAttribute = 34;
    inline constexpr DWORD kDwmCaptionColorAttribute = 35;
    inline constexpr DWORD kDwmTextColorAttribute = 36;
    inline constexpr UINT kBreadcrumbMoreControlId = 11999;
    inline constexpr UINT kBreadcrumbSegmentControlIdBase = 12000;
    inline constexpr UINT kMemoryPressureSampledMessage = WM_APP + 72;
    inline constexpr UINT kPersistentThumbnailCacheMaintenanceMessage = WM_APP + 75;
    inline constexpr UINT kAppTextSizeChangedMessage = WM_APP + 78;
    inline constexpr UINT kGamepadPollIntervalMs = 16;
    inline constexpr int kToolbarTooltipMaxWidth = 300;
    inline constexpr int kToolbarFilterEditHeight = 24;
    inline constexpr int kDetailsPanelMinWidth = 540;
    inline constexpr int kQuickAccessPanelHeaderHeight = 18;
    inline constexpr int kQuickAccessPanelRowHeight = 40;
    inline constexpr int kQuickAccessPanelRowGap = 2;
    inline constexpr int kQuickAccessPanelButtonWidth = 56;
    inline constexpr int kQuickAccessPanelButtonGap = 8;
    inline constexpr int kQuickAccessPanelButtonRightInset = 8;
    inline constexpr int kQuickAccessPanelRemoveButtonWidth = 24;
    inline constexpr int kQuickAccessPanelHeaderVerticalPadding = 4;
    inline constexpr int kQuickAccessPanelRowTextTopInset = 2;
    inline constexpr int kQuickAccessPanelRowTextGap = 2;
    inline constexpr int kQuickAccessPanelRowBottomInset = 3;
    inline constexpr int kQuickAccessPanelButtonVerticalInset = 6;
    inline constexpr int kQuickAccessPanelShortcutWidth = 24;
    inline constexpr int kQuickAccessPanelShortcutGap = 8;
    inline constexpr int kQuickAccessPanelSortButtonGap = 6;
    inline constexpr UINT kQuickAccessShortcutEditBaseId = 5200;
    inline constexpr UINT_PTR kQuickAccessSortTooltipId = static_cast<UINT_PTR>(-1);
    inline constexpr UINT_PTR kDetailsPanelHistogramTooltipId = static_cast<UINT_PTR>(-2);

    inline constexpr std::array kThumbnailSizePresets{
        hyperbrowse::browser::ThumbnailSizePreset::Pixels96,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels128,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels160,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels192,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels256,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels320,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels360,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels420,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels480,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels560,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels640,
    };
    inline constexpr int kTextInputDialogMargin = 14;
    inline constexpr int kTextInputEditHeight = 30;
    inline constexpr int kTextInputButtonWidth = 88;
    inline constexpr int kTextInputButtonHeight = 28;

    struct MemoryPressureSampleResult
    {
        bool pressureDetected{};
        bool recoveryCandidate{};
    };
    inline constexpr std::size_t kQuickAccessFolderLimit = 8;
    inline constexpr std::size_t kFavoriteDestinationLimit = hyperbrowse::ui::kQuickSendShortcutCount;
    inline constexpr UINT kSlideshowMinimumDurationMs = 250U;
    inline constexpr UINT kSlideshowMaximumDurationMs = 60000U;
    inline constexpr UINT kSlideshowMinimumTransitionDurationMs = 100U;
    inline constexpr UINT kSlideshowMaximumTransitionDurationMs = 5000U;
    inline constexpr UINT kDefaultSlideshowDurationMs = 3000U;
    inline constexpr UINT kDefaultSlideshowTransitionDurationMs = 350U;

    inline bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target);

    inline bool HasNvJpegCapability()
    {
        return hyperbrowse::decode::IsNvJpegBuildEnabled()
            && hyperbrowse::decode::IsNvJpegRuntimeAvailable();
    }

    inline std::wstring ReadWindowText(HWND hwnd)
    {
        if (!hwnd)
        {
            return {};
        }

        const int textLength = GetWindowTextLengthW(hwnd);
        std::wstring text(static_cast<std::size_t>(textLength) + 1, L'\0');
        GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
        text.resize(wcslen(text.c_str()));
        return text;
    }

    inline bool SetWindowTextIfDifferent(HWND hwnd, const std::wstring& text)
    {
        if (!hwnd)
        {
            return false;
        }

        if (ReadWindowText(hwnd) == text)
        {
            return false;
        }

        SetWindowTextW(hwnd, text.c_str());
        return true;
    }

    inline bool TryReadStringValue(HKEY key, const wchar_t* valueName, std::wstring* value)
    {
        value->clear();

        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS
            || (type != REG_SZ && type != REG_EXPAND_SZ)
            || size < sizeof(wchar_t))
        {
            return false;
        }

        std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1, L'\0');
        if (RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &size) != ERROR_SUCCESS)
        {
            return false;
        }

        *value = buffer.data();
        return true;
    }

    inline void WriteStringValue(HKEY key, const wchar_t* valueName, std::wstring_view value)
    {
        // Copy into a guaranteed null-terminated buffer; std::wstring_view is not required to be
        // null-terminated, so reading value.size() + 1 wchar_t's directly from value.data() risks
        // a heap over-read (or writing trailing garbage to the registry) for non-string callers.
        std::wstring buffer(value);
        const DWORD size = static_cast<DWORD>((buffer.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(buffer.c_str()), size);
    }

    inline void ApplyWindowFrameTheme(HWND hwnd,
                               bool useDarkMode,
                               COLORREF captionColor = CLR_INVALID,
                               COLORREF textColor = CLR_INVALID,
                               COLORREF borderColor = CLR_INVALID)
    {
        const BOOL enabled = useDarkMode ? TRUE : FALSE;
        const HRESULT result = DwmSetWindowAttribute(
            hwnd,
            kDwmUseImmersiveDarkModeAttribute,
            &enabled,
            sizeof(enabled));

        if (FAILED(result))
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmUseImmersiveDarkModeLegacyAttribute,
                &enabled,
                sizeof(enabled));
        }

        if (captionColor != CLR_INVALID)
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmCaptionColorAttribute,
                &captionColor,
                sizeof(captionColor));
        }

        if (textColor != CLR_INVALID)
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmTextColorAttribute,
                &textColor,
                sizeof(textColor));
        }

        if (borderColor != CLR_INVALID)
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmBorderColorAttribute,
                &borderColor,
                sizeof(borderColor));
        }
    }

    inline void RefreshWindowNonClientArea(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
        {
            return;
        }

        SetWindowPos(hwnd,
                     nullptr,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        DwmFlush();
    }

    inline COLORREF BlendColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
    {
        const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
        return RGB(
            (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
            (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
            (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
    }

    inline HFONT CreateDialogUiFont(int pointSize,
                             int weight,
                             hyperbrowse::util::AppTextSize size = hyperbrowse::util::kDefaultAppTextSize,
                             UINT dpi = 0)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);

        LOGFONTW logFont{};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
        {
            logFont = metrics.lfMessageFont;
        }
        else
        {
            wcscpy_s(logFont.lfFaceName, L"Segoe UI");
        }

        HDC screenDc = nullptr;
        const int dpiY = dpi != 0 ? static_cast<int>(dpi) : [&screenDc]
        {
            screenDc = GetDC(nullptr);
            const int value = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
            if (screenDc)
            {
                ReleaseDC(nullptr, screenDc);
            }
            return value;
        }();

        logFont.lfHeight = -MulDiv(hyperbrowse::util::ScaleAppTextDimension(pointSize, size), dpiY, 72);
        logFont.lfWeight = weight;
        logFont.lfCharSet = DEFAULT_CHARSET;
        logFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        return CreateFontIndirectW(&logFont);
    }

    inline HFONT CreateSystemUiFont(hyperbrowse::util::AppTextSize size, UINT dpi = 0)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) == FALSE)
        {
            return CreateDialogUiFont(9, FW_NORMAL, size, dpi);
        }

        metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
        metrics.lfMessageFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        if (dpi != 0)
        {
            HDC screenDc = GetDC(nullptr);
            const int systemDpi = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
            if (screenDc)
            {
                ReleaseDC(nullptr, screenDc);
            }
            metrics.lfMessageFont.lfHeight = MulDiv(
                metrics.lfMessageFont.lfHeight,
                static_cast<int>(dpi),
                std::max(1, systemDpi));
        }
        metrics.lfMessageFont.lfHeight = static_cast<LONG>(
            static_cast<double>(metrics.lfMessageFont.lfHeight)
            * hyperbrowse::util::AppTextSizeScale(size));
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    inline void DeleteFontIfOwned(HFONT font)
    {
        if (font && font != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(font);
        }
    }

    inline int MeasureTextBlockHeight(HFONT font,
                               std::wstring_view text,
                               int width,
                               UINT format,
                               int minimumHeight = 0)
    {
        if (width <= 0)
        {
            return minimumHeight;
        }

        std::wstring localText = text.empty() ? std::wstring(L" ") : std::wstring(text);
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return minimumHeight;
        }

        const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
        RECT bounds{0, 0, width, 0};
        DrawTextW(screenDc, localText.c_str(), -1, &bounds, format | DT_CALCRECT);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);
        const int measuredHeight = static_cast<int>(bounds.bottom - bounds.top);
        return std::max(minimumHeight, measuredHeight);
    }

    inline int MeasureDialogButtonWidth(HFONT font, std::wstring_view label, int minimumWidth)
    {
        if (label.empty())
        {
            return minimumWidth;
        }

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return minimumWidth;
        }

        const HFONT effectiveFont = font
            ? font
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ oldFont = effectiveFont ? SelectObject(screenDc, effectiveFont) : nullptr;
        const std::wstring localText(label);
        SIZE size{};
        GetTextExtentPoint32W(screenDc,
                              localText.c_str(),
                              static_cast<int>(localText.size()),
                              &size);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);

        return std::max(minimumWidth, static_cast<int>(size.cx) + 24);
    }

    using QuickAccessPanelMetrics = hyperbrowse::ui::QuickAccessLayout::Metrics;

    inline int MeasureSingleLineTextHeight(HFONT font, int minimumHeight)
    {
        return MeasureTextBlockHeight(font,
                                      L"Ag",
                                      4096,
                                      DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE,
                                      minimumHeight);
    }

    inline QuickAccessPanelMetrics BuildQuickAccessPanelMetrics(HFONT summaryFont,
                                                         HFONT bodyFont,
                                                         const hyperbrowse::ui::MenuMetrics& sizing)
    {
        const HFONT defaultGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HFONT effectiveSummaryFont = summaryFont ? summaryFont : defaultGuiFont;
        const HFONT effectiveBodyFont = bodyFont ? bodyFont : defaultGuiFont;

        QuickAccessPanelMetrics metrics;
        const auto scale = [&sizing](int value)
        {
            return sizing.ScaleDip(value);
        };
        metrics.headerHeight = scale(kQuickAccessPanelHeaderHeight);
        metrics.rowHeight = scale(kQuickAccessPanelRowHeight);
        metrics.textHorizontalInset = scale(10);
        metrics.labelTopInset = scale(kQuickAccessPanelRowTextTopInset);
        metrics.labelHeight = scale(15);
        metrics.metadataTopInset = scale(21);
        metrics.metadataBottomInset = scale(kQuickAccessPanelRowBottomInset);
        metrics.buttonHeight = scale(kTextInputButtonHeight);
        metrics.buttonTopInset = scale(kQuickAccessPanelButtonVerticalInset);
        metrics.rowGap = scale(kQuickAccessPanelRowGap);
        metrics.buttonWidth = scale(kQuickAccessPanelButtonWidth);
        metrics.buttonGap = scale(kQuickAccessPanelButtonGap);
        metrics.buttonRightInset = scale(kQuickAccessPanelButtonRightInset);
        metrics.removeButtonWidth = scale(kQuickAccessPanelRemoveButtonWidth);
        metrics.shortcutWidth = scale(kQuickAccessPanelShortcutWidth);
        metrics.shortcutGap = scale(kQuickAccessPanelShortcutGap);
        metrics.headerHeight = std::max(metrics.headerHeight,
                                        MeasureSingleLineTextHeight(effectiveSummaryFont, 0)
                                            + scale(kQuickAccessPanelHeaderVerticalPadding));
        metrics.headerHeight = std::max(metrics.headerHeight, scale(28));
        metrics.labelHeight = MeasureSingleLineTextHeight(effectiveSummaryFont, metrics.labelHeight);
        metrics.buttonHeight = std::max(metrics.buttonHeight, metrics.labelHeight);
        const int metadataHeight = MeasureSingleLineTextHeight(effectiveBodyFont, scale(14));
        metrics.metadataTopInset = metrics.labelTopInset + metrics.labelHeight + scale(kQuickAccessPanelRowTextGap);
        metrics.rowHeight = std::max(metrics.rowHeight,
                                     metrics.metadataTopInset + metadataHeight + metrics.metadataBottomInset);
        metrics.buttonWidth = std::max(metrics.buttonWidth,
                                       std::max(MeasureDialogButtonWidth(effectiveBodyFont, L"Copy", 0),
                                                MeasureDialogButtonWidth(effectiveBodyFont, L"Move", 0)));
        metrics.buttonHeight = std::min(metrics.buttonHeight,
                        std::max(0, metrics.rowHeight - (metrics.buttonTopInset * 2)));
        metrics.buttonTopInset = std::max(0, (metrics.rowHeight - metrics.buttonHeight) / 2);
        return metrics;
    }

    inline int MeasureTextWidth(HFONT font, std::wstring_view text)
    {
        if (text.empty())
        {
            return 0;
        }

        std::wstring localText(text);
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return 0;
        }

        const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
        SIZE size{};
        GetTextExtentPoint32W(screenDc, localText.c_str(), static_cast<int>(localText.size()), &size);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);
        return static_cast<int>(size.cx);
    }

    inline bool StringsEqualInsensitive(std::wstring_view lhs, std::wstring_view rhs)
    {
        return hyperbrowse::util::EqualsIgnoreCaseOrdinal(lhs, rhs);
    }

    inline void AppendLabeledLine(std::wstring* text, std::wstring_view label, std::wstring_view value)
    {
        if (!text || value.empty())
        {
            return;
        }

        text->append(label);
        text->append(value);
        text->append(L"\r\n");
    }

    inline std::wstring NormalizeFolderPath(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        while (path.size() > 3 && !path.empty() && path.back() == L'\\')
        {
            path.pop_back();
        }

        if (path.size() == 2 && path[1] == L':')
        {
            path.push_back(L'\\');
        }

        return path;
    }

    inline void CenterWindowOnOwner(HWND window, HWND ownerWindow)
    {
        const DialogShellMetrics shell = MeasureDialogShellMetrics(ownerWindow, hyperbrowse::util::kDefaultAppTextSize);
        CenterDialogInWorkArea(window, shell.workArea);
    }

    inline std::wstring TrimWhitespaceCopy(std::wstring value);

    inline bool TryParsePositiveSizeValue(const std::wstring& text, std::size_t* value)
    {
        if (!value)
        {
            return false;
        }

        const std::wstring trimmed = TrimWhitespaceCopy(text);
        if (trimmed.empty())
        {
            return false;
        }

        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long long parsed = wcstoull(trimmed.c_str(), &end, 10);
        if (errno != 0 || !end || *end != L'\0' || parsed == 0ULL)
        {
            return false;
        }

        *value = hyperbrowse::util::SaturatingCastToSizeT(parsed);
        return true;
    }

    inline constexpr std::array<SlideshowTransitionOption, 21> kSlideshowTransitionOptions = {{
        {hyperbrowse::viewer::TransitionStyle::Random, L"Random (All animated styles)"},
        {hyperbrowse::viewer::TransitionStyle::BlurCrossfade, L"Blur Crossfade"},
        {hyperbrowse::viewer::TransitionStyle::CenterWipe, L"Center Wipe"},
        {hyperbrowse::viewer::TransitionStyle::CheckerboardWipe, L"Checkerboard Wipe"},
        {hyperbrowse::viewer::TransitionStyle::ColorWash, L"Color Wash"},
        {hyperbrowse::viewer::TransitionStyle::Crossfade, L"Crossfade"},
        {hyperbrowse::viewer::TransitionStyle::DiagonalSlide, L"Diagonal Slide"},
        {hyperbrowse::viewer::TransitionStyle::FadeToBlack, L"Fade to Black"},
        {hyperbrowse::viewer::TransitionStyle::Flashbulb, L"Flashbulb"},
        {hyperbrowse::viewer::TransitionStyle::HorizontalBlinds, L"Horizontal Blinds"},
        {hyperbrowse::viewer::TransitionStyle::KenBurns, L"Ken Burns"},
        {hyperbrowse::viewer::TransitionStyle::MonochromeReveal, L"Monochrome Reveal"},
        {hyperbrowse::viewer::TransitionStyle::MotionBlur, L"Motion Blur"},
        {hyperbrowse::viewer::TransitionStyle::Cut, L"None (Cut)"},
        {hyperbrowse::viewer::TransitionStyle::Prism, L"Prism"},
        {hyperbrowse::viewer::TransitionStyle::Push, L"Push"},
        {hyperbrowse::viewer::TransitionStyle::SepiaDrift, L"Sepia Drift"},
        {hyperbrowse::viewer::TransitionStyle::Slide, L"Slide"},
        {hyperbrowse::viewer::TransitionStyle::SplitWipe, L"Split Wipe"},
        {hyperbrowse::viewer::TransitionStyle::VenetianBlinds, L"Venetian Blinds"},
        {hyperbrowse::viewer::TransitionStyle::ZoomFade, L"Zoom Fade"},
    }};

    inline int SlideshowTransitionComboIndex(hyperbrowse::viewer::TransitionStyle style)
    {
        for (std::size_t index = 0; index < kSlideshowTransitionOptions.size(); ++index)
        {
            if (kSlideshowTransitionOptions[index].style == style)
            {
                return static_cast<int>(index);
            }
        }

        return SlideshowTransitionComboIndex(hyperbrowse::viewer::TransitionStyle::Crossfade);
    }

    inline bool TryReadDialogUInt(HWND window, UINT minimum, UINT maximum, UINT* value)
    {
        if (!window || !value)
        {
            return false;
        }

        std::size_t parsedValue = 0;
        if (!TryParsePositiveSizeValue(ReadWindowText(window), &parsedValue))
        {
            return false;
        }

        if (parsedValue < minimum || parsedValue > maximum)
        {
            return false;
        }

        *value = static_cast<UINT>(parsedValue);
        return true;
    }

    inline UINT NormalizeSlideshowDuration(UINT value)
    {
        return value >= kSlideshowMinimumDurationMs && value <= kSlideshowMaximumDurationMs
            ? value
            : kDefaultSlideshowDurationMs;
    }

    inline UINT NormalizeSlideshowTransitionDuration(UINT value)
    {
        return value >= kSlideshowMinimumTransitionDurationMs && value <= kSlideshowMaximumTransitionDurationMs
            ? value
            : kDefaultSlideshowTransitionDurationMs;
    }

    inline bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
    {
        const std::wstring normalizedLeft = NormalizeFolderPath(std::wstring(lhs));
        const std::wstring normalizedRight = NormalizeFolderPath(std::wstring(rhs));
        return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
    }

    inline bool IsExistingDirectory(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return false;
        }

        std::error_code error;
        return fs::is_directory(fs::path(folderPath), error) && !error;
    }

    inline bool IsTextInputControlWindow(HWND window)
    {
        if (!window)
        {
            return false;
        }

        wchar_t className[64] = {};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) == 0)
        {
            return false;
        }

        return _wcsicmp(className, L"Edit") == 0
            || _wcsicmp(className, L"RichEdit20W") == 0
            || _wcsicmp(className, L"RichEdit50W") == 0
            || _wcsicmp(className, MSFTEDIT_CLASS) == 0;
    }

    inline bool AreFoldersOnSameDrive(std::wstring_view lhs, std::wstring_view rhs)
    {
        if (lhs.empty() || rhs.empty())
        {
            return false;
        }

        const std::wstring leftRoot = NormalizeFolderPath(fs::path(lhs).root_path().wstring());
        const std::wstring rightRoot = NormalizeFolderPath(fs::path(rhs).root_path().wstring());
        if (leftRoot.empty() || rightRoot.empty())
        {
            return false;
        }

        return FolderPathsEqual(leftRoot, rightRoot);
    }

    inline hyperbrowse::services::FileOperationType ResolveQuickAccessDropOperationType(
        const std::vector<std::wstring>& sourcePaths,
        std::wstring_view destinationFolder)
    {
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            return hyperbrowse::services::FileOperationType::Copy;
        }

        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            return hyperbrowse::services::FileOperationType::Move;
        }

        if (destinationFolder.empty() || sourcePaths.empty())
        {
            return hyperbrowse::services::FileOperationType::Copy;
        }

        const bool allSourcesOnSameDrive = std::all_of(sourcePaths.begin(), sourcePaths.end(), [&](const std::wstring& sourcePath)
        {
            return !sourcePath.empty() && AreFoldersOnSameDrive(sourcePath, destinationFolder);
        });

        return allSourcesOnSameDrive
            ? hyperbrowse::services::FileOperationType::Move
            : hyperbrowse::services::FileOperationType::Copy;
    }

    inline bool AreAllSourcePathsOnSameDrive(const std::vector<std::wstring>& sourcePaths,
                                      std::wstring_view destinationFolder)
    {
        if (destinationFolder.empty() || sourcePaths.empty())
        {
            return false;
        }

        return std::all_of(sourcePaths.begin(), sourcePaths.end(), [&](const std::wstring& sourcePath)
        {
            return !sourcePath.empty() && AreFoldersOnSameDrive(sourcePath, destinationFolder);
        });
    }

    inline bool IsValidFolderName(const std::wstring& name, std::wstring* errorMessage)
    {
        if (name.empty())
        {
            if (errorMessage) *errorMessage = L"Folder name cannot be empty.";
            return false;
        }

        if (name.size() > 255)
        {
            if (errorMessage) *errorMessage = L"Folder name is too long.";
            return false;
        }

        const wchar_t invalidChars[] = L"<>:\"/\\|?*";
        for (const wchar_t invalidChar : invalidChars)
        {
            if (name.find(invalidChar) != std::wstring::npos)
            {
                if (errorMessage) *errorMessage = L"Folder name contains invalid characters.";
                return false;
            }
        }

        if (name.back() == L' ' || name.back() == L'.')
        {
            if (errorMessage) *errorMessage = L"Folder names cannot end with a space or a period.";
            return false;
        }

        return true;
    }

    inline std::wstring BuildDeleteConfirmationMessage(std::size_t itemCount, bool permanent)
    {
        if (itemCount <= 1)
        {
            return permanent
                ? L"Permanently delete the selected image?\n\nThis cannot be undone."
                : L"Move the selected image to the Recycle Bin?";
        }

        return permanent
            ? L"Permanently delete " + std::to_wstring(itemCount) + L" selected images?\n\nThis cannot be undone."
            : L"Move " + std::to_wstring(itemCount) + L" selected images to the Recycle Bin?";
    }

    inline bool ConfirmFileDeletion(HWND ownerWindow, std::size_t itemCount, bool permanent)
    {
        const std::wstring prompt = BuildDeleteConfirmationMessage(itemCount, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete" : L"Delete",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }

    inline bool ShouldConfirmDeletion(bool permanent)
    {
        return permanent && (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
    }

    inline std::wstring BuildFileConflictContent(std::size_t conflictCount)
    {
        if (conflictCount == 1)
        {
            return L"1 selected file would use a name that already exists in the destination or is already queued for this operation.";
        }

        return std::to_wstring(conflictCount)
            + L" selected files would use names that already exist in the destination or are already queued for this operation.";
    }

    inline bool PromptForFileConflictPolicy(HWND ownerWindow,
                                     hyperbrowse::services::FileOperationType type,
                                     std::size_t conflictCount,
                                     hyperbrowse::services::FileConflictPolicy* conflictPolicy)
    {
        if (!conflictPolicy)
        {
            return false;
        }

        if (conflictCount == 0)
        {
            *conflictPolicy = hyperbrowse::services::FileConflictPolicy::PromptShell;
            return true;
        }

        TASKDIALOG_BUTTON buttons[] = {
            {1001, L"Overwrite target files\nReplace the existing destination files when names collide."},
            {1002, L"Auto-rename incoming files\nKeep both versions using numeric suffixes like photo.1.jpg and photo.2.jpg."},
        };

        const std::wstring content = BuildFileConflictContent(conflictCount);

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = ownerWindow;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.pszWindowTitle = type == hyperbrowse::services::FileOperationType::Move
            ? L"Move Conflicts"
            : L"Copy Conflicts";
        config.pszMainIcon = TD_WARNING_ICON;
        config.pszMainInstruction = L"Conflicting file names were found in the destination.";
        config.pszContent = content.c_str();
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = 1002;

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        if (FAILED(dialogResult) || clickedButton == IDCANCEL)
        {
            return false;
        }

        *conflictPolicy = clickedButton == 1001
            ? hyperbrowse::services::FileConflictPolicy::OverwriteExisting
            : hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix;
        return true;
    }

    inline bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target)
    {
        if (target.empty())
        {
            return false;
        }

        const std::wstring path(target);
        const HINSTANCE result = ShellExecuteW(ownerWindow, verb, path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    inline UINT CommandIdFromSortMode(hyperbrowse::browser::BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case hyperbrowse::browser::BrowserSortMode::FileName:
            return ID_VIEW_SORT_FILENAME;
        case hyperbrowse::browser::BrowserSortMode::ModifiedDate:
            return ID_VIEW_SORT_MODIFIED;
        case hyperbrowse::browser::BrowserSortMode::FileSize:
            return ID_VIEW_SORT_SIZE;
        case hyperbrowse::browser::BrowserSortMode::Dimensions:
            return ID_VIEW_SORT_DIMENSIONS;
        case hyperbrowse::browser::BrowserSortMode::FileType:
            return ID_VIEW_SORT_TYPE;
        case hyperbrowse::browser::BrowserSortMode::DateTaken:
            return ID_VIEW_SORT_DATETAKEN;
        case hyperbrowse::browser::BrowserSortMode::Rating:
            return ID_VIEW_SORT_RATING;
        case hyperbrowse::browser::BrowserSortMode::Tags:
            return ID_VIEW_SORT_TAGS;
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return ID_VIEW_SORT_RANDOM;
        }
    }

    inline UINT CommandIdFromThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset preset)
    {
        switch (preset)
        {
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels96:
            return ID_VIEW_THUMBNAIL_SIZE_96;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels128:
            return ID_VIEW_THUMBNAIL_SIZE_128;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels160:
            return ID_VIEW_THUMBNAIL_SIZE_160;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels256:
            return ID_VIEW_THUMBNAIL_SIZE_256;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels320:
            return ID_VIEW_THUMBNAIL_SIZE_320;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels360:
            return ID_VIEW_THUMBNAIL_SIZE_360;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels420:
            return ID_VIEW_THUMBNAIL_SIZE_420;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels480:
            return ID_VIEW_THUMBNAIL_SIZE_480;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels560:
            return ID_VIEW_THUMBNAIL_SIZE_560;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels640:
            return ID_VIEW_THUMBNAIL_SIZE_640;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels192:
        default:
            return ID_VIEW_THUMBNAIL_SIZE_192;
        }
    }

    inline UINT CommandIdFromRating(int rating)
    {
        rating = std::clamp(rating, 0, 5);
        return ID_FILE_SET_RATING_0 + static_cast<UINT>(rating);
    }

    inline std::wstring TrimWhitespaceCopy(std::wstring value)
    {
        const auto isSpace = [](wchar_t character)
        {
            return iswspace(character) != 0;
        };

        const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
        const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
        if (first >= last)
        {
            return {};
        }

        return std::wstring(first, last);
    }

    struct AlternateMonitorSearch
    {
        HMONITOR referenceMonitor{};
        HMONITOR alternateMonitor{};
    };

    inline BOOL CALLBACK CaptureAlternateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
    {
        auto* search = reinterpret_cast<AlternateMonitorSearch*>(lParam);
        if (!search)
        {
            return FALSE;
        }

        if (monitor != search->referenceMonitor)
        {
            search->alternateMonitor = monitor;
            return FALSE;
        }

        return TRUE;
    }

    inline HMONITOR FindAlternateMonitorForWindow(HWND hwnd)
    {
        if (GetSystemMetrics(SM_CMONITORS) < 2)
        {
            return nullptr;
        }

        const HMONITOR referenceMonitor = hwnd && IsWindow(hwnd)
            ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        AlternateMonitorSearch search{referenceMonitor, nullptr};
        EnumDisplayMonitors(nullptr, nullptr, &CaptureAlternateMonitor, reinterpret_cast<LPARAM>(&search));
        return search.alternateMonitor;
    }
}

using namespace main_window_shared;
