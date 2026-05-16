#include "ui/MainWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <richedit.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "decode/ImageDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderTreeEnumerationService.h"
#include "services/FolderWatchService.h"
#include "services/ImageMetadataService.h"
#include "services/JpegTransformService.h"
#include "services/ThumbnailScheduler.h"
#include "services/UserMetadataStore.h"
#include "ui/DiagnosticsWindow.h"
#include "ui/ToolbarIconLibrary.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/ResourcePng.h"
#include "util/StringConvert.h"
#include "util/Timing.h"
#include "viewer/ViewerWindow.h"

#include "app/resource.h"
#include "HyperBrowseBuildInfo.h"

namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueLeftPaneWidth[] = L"LeftPaneWidth";
    constexpr wchar_t kRegistryValueBrowserMode[] = L"BrowserMode";
    constexpr wchar_t kRegistryValueThemeMode[] = L"ThemeMode";
    constexpr wchar_t kRegistryValueNvJpegEnabled[] = L"NvJpegEnabled";
    constexpr wchar_t kRegistryValueLibRawOutOfProcessEnabled[] = L"LibRawOutOfProcessEnabled";
    constexpr wchar_t kRegistryValueThumbnailSizePreset[] = L"ThumbnailSizePreset";
    constexpr wchar_t kRegistryValueCompactThumbnailLayout[] = L"CompactThumbnailLayout";
    constexpr wchar_t kRegistryValueThumbnailDetailsVisible[] = L"ThumbnailDetailsVisible";
    constexpr wchar_t kRegistryValueSelectedFolderPath[] = L"SelectedFolderPath";
    constexpr wchar_t kRegistryValueSortMode[] = L"SortMode";
    constexpr wchar_t kRegistryValueSortAscending[] = L"SortAscending";
    constexpr wchar_t kRegistryValueSlideshowInterval[] = L"SlideshowIntervalMs";
    constexpr wchar_t kRegistryValueSlideshowTransitionStyle[] = L"SlideshowTransitionStyle";
    constexpr wchar_t kRegistryValueSlideshowTransitionDuration[] = L"SlideshowTransitionDurationMs";
    constexpr wchar_t kRegistryValueDetailsStripVisible[] = L"DetailsStripVisible";
    constexpr wchar_t kRegistryValueViewerMouseWheelBehavior[] = L"ViewerMouseWheelBehavior";
    constexpr wchar_t kRegistryValueRecentFolders[] = L"RecentFolders";
    constexpr wchar_t kRegistryValueRecentDestinationFolders[] = L"RecentDestinationFolders";
    constexpr wchar_t kRegistryValueFavoriteDestinationFolders[] = L"FavoriteDestinationFolders";
    constexpr wchar_t kRegistryValueRawJpegPairedOperationsEnabled[] = L"RawJpegPairedOperationsEnabled";
    constexpr wchar_t kRegistryValuePersistentThumbnailCacheEnabled[] = L"PersistentThumbnailCacheEnabled";

    constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacyAttribute = 19;

    constexpr UINT ID_FILE_OPEN_FOLDER = 1000;
    constexpr UINT ID_FILE_REFRESH_TREE = 1001;
    constexpr UINT ID_FILE_EXIT = 1002;
    constexpr UINT ID_FILE_IMAGE_INFORMATION = 1003;
    constexpr UINT ID_FILE_OPEN_SELECTED = 1004;
    constexpr UINT ID_FILE_COPY_SELECTION = 1005;
    constexpr UINT ID_FILE_MOVE_SELECTION = 1006;
    constexpr UINT ID_FILE_DELETE_SELECTION = 1007;
    constexpr UINT ID_FILE_DELETE_SELECTION_PERMANENT = 1008;
    constexpr UINT ID_FILE_REVEAL_IN_EXPLORER = 1009;
    constexpr UINT ID_FILE_BATCH_CONVERT_SELECTION_JPEG = 1010;
    constexpr UINT ID_FILE_BATCH_CONVERT_SELECTION_PNG = 1011;
    constexpr UINT ID_FILE_BATCH_CONVERT_SELECTION_TIFF = 1012;
    constexpr UINT ID_FILE_BATCH_CONVERT_FOLDER_JPEG = 1013;
    constexpr UINT ID_FILE_BATCH_CONVERT_FOLDER_PNG = 1014;
    constexpr UINT ID_FILE_BATCH_CONVERT_FOLDER_TIFF = 1015;
    constexpr UINT ID_FILE_BATCH_CONVERT_CANCEL = 1016;
    constexpr UINT ID_FILE_ROTATE_JPEG_LEFT = 1017;
    constexpr UINT ID_FILE_ROTATE_JPEG_RIGHT = 1018;
    constexpr UINT ID_FILE_OPEN_CONTAINING_FOLDER = 1019;
    constexpr UINT ID_FILE_COPY_PATH = 1020;
    constexpr UINT ID_FILE_PROPERTIES = 1021;
    constexpr UINT ID_FILE_VIEW_ON_SECONDARY_MONITOR = 1022;
    constexpr UINT ID_FILE_RENAME_SELECTED = 1023;
    constexpr UINT ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION = 1024;
    constexpr UINT ID_FILE_COMPARE_SELECTED = 1025;
    constexpr UINT ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS = 1026;
    constexpr UINT ID_FILE_SET_RATING_0 = 1080;
    constexpr UINT ID_FILE_SET_RATING_1 = 1081;
    constexpr UINT ID_FILE_SET_RATING_2 = 1082;
    constexpr UINT ID_FILE_SET_RATING_3 = 1083;
    constexpr UINT ID_FILE_SET_RATING_4 = 1084;
    constexpr UINT ID_FILE_SET_RATING_5 = 1085;
    constexpr UINT ID_FILE_EDIT_TAGS = 1086;
    constexpr UINT ID_FILE_OPEN_RECENT_FOLDER_BASE = 1030;
    constexpr UINT ID_FILE_OPEN_RECENT_FOLDER_LAST = 1037;
    constexpr UINT ID_FILE_COPY_SELECTION_BROWSE = 1040;
    constexpr UINT ID_FILE_COPY_SELECTION_FAVORITE_BASE = 1041;
    constexpr UINT ID_FILE_COPY_SELECTION_FAVORITE_LAST = 1048;
    constexpr UINT ID_FILE_COPY_SELECTION_RECENT_BASE = 1051;
    constexpr UINT ID_FILE_COPY_SELECTION_RECENT_LAST = 1058;
    constexpr UINT ID_FILE_MOVE_SELECTION_BROWSE = 1060;
    constexpr UINT ID_FILE_MOVE_SELECTION_FAVORITE_BASE = 1061;
    constexpr UINT ID_FILE_MOVE_SELECTION_FAVORITE_LAST = 1068;
    constexpr UINT ID_FILE_MOVE_SELECTION_RECENT_BASE = 1071;
    constexpr UINT ID_FILE_MOVE_SELECTION_RECENT_LAST = 1078;
    constexpr UINT ID_VIEW_THUMBNAILS = 2001;
    constexpr UINT ID_VIEW_DETAILS = 2002;
    constexpr UINT ID_VIEW_RECURSIVE = 2003;
    constexpr UINT ID_VIEW_THEME_LIGHT = 2101;
    constexpr UINT ID_VIEW_THEME_DARK = 2102;
    constexpr UINT ID_VIEW_NVJPEG_ACCELERATION = 2103;
    constexpr UINT ID_VIEW_LIBRAW_OUT_OF_PROCESS = 2104;
    constexpr UINT ID_VIEW_PERSISTENT_THUMBNAIL_CACHE = 2105;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_96 = 2110;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_128 = 2111;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_160 = 2112;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_192 = 2113;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_256 = 2114;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_320 = 2115;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_360 = 2116;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_420 = 2117;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_480 = 2118;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_560 = 2119;
    constexpr UINT ID_VIEW_THUMBNAIL_SIZE_640 = 2120;
    constexpr UINT ID_VIEW_THUMBNAIL_LAYOUT_COMPACT = 2121;
    constexpr UINT ID_VIEW_THUMBNAIL_DETAILS = 2122;
    constexpr UINT ID_VIEW_SORT_FILENAME = 2201;
    constexpr UINT ID_VIEW_SORT_MODIFIED = 2202;
    constexpr UINT ID_VIEW_SORT_SIZE = 2203;
    constexpr UINT ID_VIEW_SORT_DIMENSIONS = 2204;
    constexpr UINT ID_VIEW_SORT_TYPE = 2205;
    constexpr UINT ID_VIEW_SORT_RANDOM = 2206;
    constexpr UINT ID_VIEW_SORT_DATETAKEN = 2207;
    constexpr UINT ID_VIEW_SORT_DIRECTION = 2208;
    constexpr UINT ID_VIEW_DETAILS_STRIP = 2209;
    constexpr UINT ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM = 2210;
    constexpr UINT ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE = 2211;
    constexpr UINT ID_VIEW_SLIDESHOW_SELECTION = 2301;
    constexpr UINT ID_VIEW_SLIDESHOW_FOLDER = 2302;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_CUT = 2303;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_CROSSFADE = 2304;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_SLIDE = 2305;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_KEN_BURNS = 2306;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_DURATION_200 = 2311;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_DURATION_350 = 2312;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_DURATION_500 = 2313;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_DURATION_800 = 2314;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_DURATION_1200 = 2315;
    constexpr UINT ID_VIEW_SLIDESHOW_TRANSITION_DURATION_2000 = 2316;
    constexpr UINT ID_ACTION_SORT_MENU = 2401;
    constexpr UINT ID_ACTION_THUMBNAIL_SIZE_MENU = 2402;
    constexpr UINT ID_ACTION_THEME_MENU = 2403;
    constexpr UINT ID_ACTION_FILTER_EDIT = 2404;
    constexpr UINT ID_HELP_ABOUT = 9001;
    constexpr UINT ID_HELP_DIAGNOSTICS_SNAPSHOT = 9002;
    constexpr UINT ID_HELP_DIAGNOSTICS_RESET = 9003;
    constexpr UINT ID_ABOUT_OPEN_GITHUB = 9101;
    constexpr UINT ID_ABOUT_OPEN_SUPPORT = 9102;

    constexpr int kActionStripPaddingX = 8;
    constexpr int kActionStripPaddingY = 6;
    constexpr int kToolbarItemSize = 32;
    constexpr int kToolbarIconSize = 18;
    constexpr int kToolbarDropdownChevronSize = 10;
    constexpr int kToolbarSeparatorWidth = 9;
    constexpr int kToolbarSeparatorGap = 4;
    constexpr int kFilterEditMinWidth = 160;
    constexpr int kDetailsStripHeight = 22;
    constexpr int kDetailsPanelPreferredWidth = 340;
    constexpr int kDetailsPanelMargin = 14;
    constexpr int kDetailsPanelHistogramHeight = 88;
    constexpr int kDetailsPanelSectionGap = 12;
    constexpr int kDetailsPanelTextTopGap = 14;
    constexpr int kDetailsPanelHistogramBins = 64;
    constexpr std::size_t kIncrementalFolderWatchEventLimit = 64;
    constexpr std::size_t kIncrementalFileOperationPathLimit = 64;
    constexpr wchar_t kTextInputDialogClassName[] = L"HyperBrowseTextInputDialog";
    constexpr int kTextInputDialogWidth = 440;
    constexpr int kTextInputDialogHeight = 160;
    constexpr int kTextInputDialogMargin = 14;
    constexpr int kTextInputEditHeight = 24;
    constexpr int kTextInputButtonWidth = 88;
    constexpr int kTextInputButtonHeight = 28;
    constexpr int kTextInputEditControlId = 100;
    constexpr wchar_t kAboutDialogClassName[] = L"HyperBrowseAboutDialog";
    constexpr wchar_t kAboutDialogGitHubLabel[] = L"GitHub Project";
    constexpr wchar_t kAboutDialogSupportLabel[] = L"Buy Me A Coffee";
    constexpr wchar_t kAboutDialogGitHubUrl[] = L"https://github.com/thetheosopher/HyperBrowse";
    constexpr wchar_t kAboutDialogSupportUrl[] = L"https://buymeacoffee.com/theosopher";
    constexpr int kAboutDialogWidth = 1180;
    constexpr int kAboutDialogHeight = 720;
    constexpr int kAboutDialogMargin = 36;
    constexpr int kAboutDialogHeaderHeight = 224;
    constexpr int kAboutDialogFooterHeight = 96;
    constexpr int kAboutDialogButtonWidth = 104;
    constexpr int kAboutDialogButtonHeight = 38;
    constexpr std::size_t kQuickAccessFolderLimit = 8;
    constexpr int kAboutDialogLinkButtonWidth = 172;
    constexpr int kAboutDialogSupportButtonWidth = 196;
    constexpr int kAboutDialogButtonGap = 12;
    constexpr int kAboutDialogBrandArtSize = 152;

    hyperbrowse::cache::ThumbnailCacheKey MakeThumbnailCacheKey(const hyperbrowse::browser::BrowserItem& item,
                                                                int targetWidth,
                                                                int targetHeight)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = item.filePath;
        key.modifiedTimestampUtc = item.modifiedTimestampUtc;
        key.targetWidth = targetWidth;
        key.targetHeight = targetHeight;
        return key;
    }

    struct TextInputDialogState
    {
        HWND ownerWindow{};
        HWND editWindow{};
        HWND okButton{};
        std::wstring title;
        std::wstring instruction;
        std::wstring confirmLabel;
        std::wstring initialText;
        std::wstring resultText;
        int selectionStart{};
        int selectionEnd{};
        bool accepted{};
        bool done{};
    };

    struct AboutDialogState
    {
        HWND ownerWindow{};
        HWND githubButton{};
        HWND supportButton{};
        HWND okButton{};
        int githubButtonWidth{};
        int supportButtonWidth{};
        HINSTANCE instance{};
        HFONT titleFont{};
        HFONT subtitleFont{};
        HFONT bodyFont{};
        HFONT footerFont{};
        HICON heroIcon{};
        HICON windowIcon{};
        bool darkMode{};
        bool done{};
        COLORREF background{};
        COLORREF headerBackground{};
        COLORREF footerBackground{};
        COLORREF panelBackground{};
        COLORREF border{};
        COLORREF text{};
        COLORREF mutedText{};
        COLORREF accent{};
        std::wstring title;
        std::wstring subtitle;
        std::wstring intro;
        std::wstring bodyHeading;
        std::wstring bodyContent;
        std::wstring footer;
        std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> brandArt;
    };

    bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target);

    bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    bool HasNvJpegCapability()
    {
        return hyperbrowse::decode::IsNvJpegBuildEnabled()
            && hyperbrowse::decode::IsNvJpegRuntimeAvailable();
    }

    void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    std::wstring ReadWindowText(HWND hwnd)
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

    bool SetWindowTextIfDifferent(HWND hwnd, const std::wstring& text)
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

    bool SetWindowEnabledIfDifferent(HWND hwnd, bool enabled)
    {
        if (!hwnd)
        {
            return false;
        }

        const bool currentlyEnabled = IsWindowEnabled(hwnd) != FALSE;
        if (currentlyEnabled == enabled)
        {
            return false;
        }

        EnableWindow(hwnd, enabled ? TRUE : FALSE);
        return true;
    }

    void RedrawWindowNoErase(HWND hwnd)
    {
        if (!hwnd)
        {
            return;
        }

        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }

    void RemoveAllMenuItems(HMENU menu)
    {
        if (!menu)
        {
            return;
        }

        while (GetMenuItemCount(menu) > 0)
        {
            DeleteMenu(menu, 0, MF_BYPOSITION);
        }
    }

    bool TryReadStringValue(HKEY key, const wchar_t* valueName, std::wstring* value)
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

    void WriteStringValue(HKEY key, const wchar_t* valueName, std::wstring_view value)
    {
        // Copy into a guaranteed null-terminated buffer; std::wstring_view is not required to be
        // null-terminated, so reading value.size() + 1 wchar_t's directly from value.data() risks
        // a heap over-read (or writing trailing garbage to the registry) for non-string callers.
        std::wstring buffer(value);
        const DWORD size = static_cast<DWORD>((buffer.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(buffer.c_str()), size);
    }

    void ApplyWindowFrameTheme(HWND hwnd, bool useDarkMode)
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
    }

    COLORREF BlendColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
    {
        const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
        return RGB(
            (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
            (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
            (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
    }

    void AlphaBlendBitmap(HDC targetDC, HDC scratchDC, HBITMAP bitmap, int x, int y, int width, int height)
    {
        if (!targetDC || !scratchDC || !bitmap || width <= 0 || height <= 0)
        {
            return;
        }

        const HGDIOBJ oldBitmap = SelectObject(scratchDC, bitmap);
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(targetDC, x, y, width, height, scratchDC, 0, 0, width, height, blend);
        SelectObject(scratchDC, oldBitmap);
    }

    std::wstring GetFolderDisplayName(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return L"No Folder";
        }

        const fs::path path(folderPath);
        const std::wstring leaf = path.filename().wstring();
        return leaf.empty() ? std::wstring(folderPath) : leaf;
    }

    std::wstring FormatFolderShortcutMenuLabel(std::wstring_view folderPath)
    {
        const std::wstring displayName = GetFolderDisplayName(folderPath);
        if (displayName.empty() || displayName == folderPath)
        {
            return std::wstring(folderPath);
        }

        return displayName + L" (" + std::wstring(folderPath) + L")";
    }

    int CurrentCalendarYear()
    {
        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);
        return static_cast<int>(localTime.wYear);
    }

    HFONT CreateDialogUiFont(int pointSize, int weight)
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

        HDC screenDc = GetDC(nullptr);
        const int dpiY = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        logFont.lfHeight = -MulDiv(pointSize, dpiY, 72);
        logFont.lfWeight = weight;
        logFont.lfCharSet = DEFAULT_CHARSET;
        logFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        return CreateFontIndirectW(&logFont);
    }

    void DeleteFontIfOwned(HFONT font)
    {
        if (font && font != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(font);
        }
    }

    int MeasureTextBlockHeight(HFONT font,
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

    int MeasureTextWidth(HFONT font, std::wstring_view text)
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

    int MeasureAboutDialogLinkButtonWidth(HFONT font, std::wstring_view firstLabel, std::wstring_view secondLabel)
    {
        const int textWidth = std::max(MeasureTextWidth(font, firstLabel), MeasureTextWidth(font, secondLabel));
        return std::max(180, textWidth + 40);
    }

    int MeasureAboutDialogClientHeight(const AboutDialogState& state)
    {
        const int contentRight = kAboutDialogWidth - kAboutDialogMargin;
        const int artLeft = contentRight - kAboutDialogBrandArtSize;
        const int iconLeft = kAboutDialogMargin;
        const int iconSize = 48;
        const int textLeft = iconLeft + iconSize + 20;
        const int textRight = artLeft - 28;
        const int textWidth = std::max(320, textRight - textLeft);

        const int titleTop = kAboutDialogMargin - 2;
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 44);
        const int subtitleTop = titleTop + titleHeight + 10;
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int introTop = subtitleTop + subtitleHeight + 10;
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 40);
        const int artFrameBottom = (kAboutDialogMargin - 4) + kAboutDialogBrandArtSize + 10;
        const int headerHeight = std::max(kAboutDialogHeaderHeight,
                                          std::max(artFrameBottom + kAboutDialogMargin - 8,
                                                   introTop + introHeight + kAboutDialogMargin - 8));

        const int bodyWidth = kAboutDialogWidth - (kAboutDialogMargin * 2);
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int bodyTextHeight = MeasureTextBlockHeight(state.bodyFont, state.bodyContent, bodyWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int bodyHeight = 24 + headingHeight + 12 + bodyTextHeight + 26;

        const int footerActionWidth = state.githubButtonWidth + state.supportButtonWidth + kAboutDialogButtonWidth + (kAboutDialogButtonGap * 2);
        const int footerTextWidth = std::max(320, kAboutDialogWidth - (kAboutDialogMargin * 2) - footerActionWidth - 20);
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(kAboutDialogFooterHeight, std::max(footerTextHeight + 34, kAboutDialogButtonHeight + 36));

        return headerHeight + bodyHeight + footerHeight;
    }

    int MeasureAboutDialogFooterActionWidth(const AboutDialogState& state)
    {
        return state.githubButtonWidth + state.supportButtonWidth + kAboutDialogButtonWidth + (kAboutDialogButtonGap * 2);
    }

    int MeasureAboutDialogFooterTextWidth(const AboutDialogState& state, int clientWidth)
    {
        return std::max(320, clientWidth - (kAboutDialogMargin * 2) - MeasureAboutDialogFooterActionWidth(state) - 20);
    }

    int AboutDialogFooterButtonsLeft(const RECT& clientRect, const AboutDialogState& state)
    {
        const int buttonsLeft = static_cast<int>(clientRect.right) - kAboutDialogMargin - MeasureAboutDialogFooterActionWidth(state);
        return std::max<int>(kAboutDialogMargin, buttonsLeft);
    }

    int AboutDialogFooterButtonsTop(const RECT& clientRect)
    {
        return clientRect.bottom - kAboutDialogMargin - kAboutDialogButtonHeight;
    }

    const wchar_t* GetAboutDialogLinkLabel(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return kAboutDialogGitHubLabel;
        case ID_ABOUT_OPEN_SUPPORT:
            return kAboutDialogSupportLabel;
        default:
            return L"";
        }
    }

    const wchar_t* GetAboutDialogLinkTarget(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return kAboutDialogGitHubUrl;
        case ID_ABOUT_OPEN_SUPPORT:
            return kAboutDialogSupportUrl;
        default:
            return nullptr;
        }
    }

    const wchar_t* GetAboutDialogLinkFailureMessage(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return L"Failed to open the HyperBrowse GitHub project.";
        case ID_ABOUT_OPEN_SUPPORT:
            return L"Failed to open the Buy Me A Coffee page.";
        default:
            return L"Failed to open the selected link.";
        }
    }

    COLORREF GetAboutDialogSupportAccent(bool darkMode)
    {
        return darkMode ? RGB(255, 214, 126) : RGB(145, 78, 16);
    }

    struct AboutDialogLinkPalette
    {
        COLORREF fill{};
        COLORREF border{};
        COLORREF text{};
    };

    AboutDialogLinkPalette BuildAboutDialogLinkPalette(UINT controlId, const AboutDialogState& state, UINT itemState)
    {
        const bool supportButton = controlId == ID_ABOUT_OPEN_SUPPORT;
        const bool pressed = (itemState & ODS_SELECTED) != 0;
        const bool hot = (itemState & ODS_HOTLIGHT) != 0;
        const bool disabled = (itemState & ODS_DISABLED) != 0;

        const COLORREF accent = supportButton ? GetAboutDialogSupportAccent(state.darkMode) : state.accent;
        int fillMixAmount = supportButton
            ? (state.darkMode ? 34 : 18)
            : (state.darkMode ? 28 : 10);
        if (hot)
        {
            fillMixAmount += supportButton ? 10 : 8;
        }
        if (pressed)
        {
            fillMixAmount += supportButton ? 18 : 14;
        }
        fillMixAmount = std::min(fillMixAmount, 96);

        AboutDialogLinkPalette palette;
        if (supportButton)
        {
            const COLORREF baseFill = state.darkMode ? RGB(88, 61, 19) : RGB(255, 245, 219);
            palette.fill = BlendColor(baseFill, accent, static_cast<BYTE>(fillMixAmount));
            palette.text = state.darkMode ? RGB(255, 236, 194) : RGB(112, 62, 15);
        }
        else
        {
            const COLORREF baseFill = state.darkMode ? state.footerBackground : RGB(255, 255, 255);
            palette.fill = BlendColor(baseFill, accent, static_cast<BYTE>(fillMixAmount));
            palette.text = accent;
        }

        palette.border = BlendColor(accent, state.border, supportButton ? 22 : 28);

        if (disabled)
        {
            palette.fill = BlendColor(palette.fill, state.footerBackground, 120);
            palette.border = BlendColor(palette.border, state.border, 120);
            palette.text = state.mutedText;
        }

        return palette;
    }

    void OpenAboutDialogLink(HWND hwnd, UINT controlId)
    {
        const wchar_t* target = GetAboutDialogLinkTarget(controlId);
        if (!target)
        {
            return;
        }

        if (!LaunchShellTarget(hwnd, L"open", target))
        {
            MessageBoxW(hwnd, GetAboutDialogLinkFailureMessage(controlId), L"About HyperBrowse", MB_OK | MB_ICONERROR);
        }
    }

    void DrawAboutDialogLinkButton(const DRAWITEMSTRUCT& drawItem, const AboutDialogState& state)
    {
        RECT buttonRect{};
        GetClientRect(drawItem.hwndItem, &buttonRect);

        const HBRUSH footerBrush = CreateSolidBrush(state.footerBackground);
        FillRect(drawItem.hDC, &buttonRect, footerBrush);
        DeleteObject(footerBrush);

        const AboutDialogLinkPalette palette = BuildAboutDialogLinkPalette(drawItem.CtlID, state, drawItem.itemState);
        RECT pillRect = buttonRect;
        InflateRect(&pillRect, -1, -1);

        const HBRUSH fillBrush = CreateSolidBrush(palette.fill);
        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.border);
        const HGDIOBJ oldBrush = SelectObject(drawItem.hDC, fillBrush);
        const HGDIOBJ oldPen = SelectObject(drawItem.hDC, borderPen);
        RoundRect(drawItem.hDC, pillRect.left, pillRect.top, pillRect.right, pillRect.bottom, 16, 16);
        SelectObject(drawItem.hDC, oldPen);
        SelectObject(drawItem.hDC, oldBrush);
        DeleteObject(borderPen);
        DeleteObject(fillBrush);

        SetBkMode(drawItem.hDC, TRANSPARENT);
        SetTextColor(drawItem.hDC, palette.text);
        const HGDIOBJ oldFont = SelectObject(drawItem.hDC,
                                             state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        RECT textRect = pillRect;
        InflateRect(&textRect, -12, -6);
        DrawTextW(drawItem.hDC,
                  GetAboutDialogLinkLabel(drawItem.CtlID),
                  -1,
                  &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(drawItem.hDC, oldFont);

        if ((drawItem.itemState & ODS_FOCUS) != 0)
        {
            RECT focusRect = pillRect;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(drawItem.hDC, &focusRect);
        }
    }

    void LayoutAboutDialogControls(HWND hwnd, const AboutDialogState& state)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int buttonTop = AboutDialogFooterButtonsTop(client);
        int buttonLeft = AboutDialogFooterButtonsLeft(client, state);

        if (state.githubButton)
        {
            MoveWindow(state.githubButton,
                       buttonLeft,
                       buttonTop,
                       state.githubButtonWidth,
                       kAboutDialogButtonHeight,
                       TRUE);
            buttonLeft += state.githubButtonWidth + kAboutDialogButtonGap;
        }

        if (state.supportButton)
        {
            MoveWindow(state.supportButton,
                       buttonLeft,
                       buttonTop,
                       state.supportButtonWidth,
                       kAboutDialogButtonHeight,
                       TRUE);
            buttonLeft += state.supportButtonWidth + kAboutDialogButtonGap;
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton,
                       buttonLeft,
                       buttonTop,
                       kAboutDialogButtonWidth,
                       kAboutDialogButtonHeight,
                       TRUE);
        }
    }

    bool StringsEqualInsensitive(std::wstring_view lhs, std::wstring_view rhs)
    {
        return hyperbrowse::util::EqualsIgnoreCaseOrdinal(lhs, rhs);
    }

    bool IsJpegFileType(std::wstring_view fileType)
    {
        return StringsEqualInsensitive(fileType, L"jpg")
            || StringsEqualInsensitive(fileType, L"jpeg")
            || StringsEqualInsensitive(fileType, L".jpg")
            || StringsEqualInsensitive(fileType, L".jpeg");
    }

    std::wstring BuildCameraSummaryLabel(const hyperbrowse::services::ImageMetadata& metadata)
    {
        if (!metadata.cameraMake.empty() && !metadata.cameraModel.empty())
        {
            return metadata.cameraMake + L" " + metadata.cameraModel;
        }

        return !metadata.cameraModel.empty() ? metadata.cameraModel : metadata.cameraMake;
    }

    bool IsCuratedDetailsProperty(std::wstring_view canonicalName)
    {
        return canonicalName == L"System.Image.Dimensions"
            || canonicalName == L"System.Image.HorizontalSize"
            || canonicalName == L"System.Image.VerticalSize"
            || canonicalName == L"System.Photo.CameraManufacturer"
            || canonicalName == L"System.Photo.CameraModel"
            || canonicalName == L"System.Photo.DateTaken"
            || canonicalName == L"System.Photo.ExposureTime"
            || canonicalName == L"System.Photo.FNumber"
            || canonicalName == L"System.Photo.ISOSpeed"
            || canonicalName == L"System.Photo.FocalLength"
            || canonicalName == L"System.Title"
            || canonicalName == L"System.Author"
            || canonicalName == L"System.Keywords"
            || canonicalName == L"System.Comment";
    }

    void AppendLabeledLine(std::wstring* text, std::wstring_view label, std::wstring_view value)
    {
        if (!text || value.empty())
        {
            return;
        }

        text->append(label);
        text->append(value);
        text->append(L"\r\n");
    }

    bool HasEquivalentDisplayedProperty(const std::vector<hyperbrowse::services::MetadataPropertyEntry>& properties,
                                       const hyperbrowse::services::MetadataPropertyEntry& candidate)
    {
        return std::any_of(properties.begin(),
                           properties.end(),
                           [&](const hyperbrowse::services::MetadataPropertyEntry& property)
                           {
                               return StringsEqualInsensitive(property.displayName, candidate.displayName)
                                   && StringsEqualInsensitive(property.value, candidate.value);
                           });
    }

    std::wstring BuildSingleSelectionSummary(const hyperbrowse::browser::BrowserItem& item)
    {
        std::wstring summary = item.fileType;

        const std::wstring dimensions = hyperbrowse::browser::FormatDimensionsForItem(item);
        if (!dimensions.empty() && dimensions != L"...")
        {
            if (!summary.empty())
            {
                summary.append(L" | ");
            }
            summary.append(dimensions);
        }

        const std::wstring fileSize = hyperbrowse::browser::FormatByteSize(item.fileSizeBytes);
        if (!fileSize.empty())
        {
            if (!summary.empty())
            {
                summary.append(L" | ");
            }
            summary.append(fileSize);
        }

        return summary;
    }

    template <typename Getter>
    bool TryGetCommonItemString(const std::vector<hyperbrowse::browser::BrowserItem>& items,
                                Getter getter,
                                std::wstring* commonValue)
    {
        if (!commonValue || items.empty())
        {
            return false;
        }

        const std::wstring first = getter(items.front());
        if (first.empty() || first == L"...")
        {
            return false;
        }

        for (std::size_t index = 1; index < items.size(); ++index)
        {
            const std::wstring candidate = getter(items[index]);
            if (candidate.empty() || candidate == L"..." || !StringsEqualInsensitive(first, candidate))
            {
                return false;
            }
        }

        *commonValue = first;
        return true;
    }

    template <typename Getter>
    bool TryGetCommonMetadataString(const std::vector<std::shared_ptr<const hyperbrowse::services::ImageMetadata>>& metadataList,
                                    Getter getter,
                                    std::wstring* commonValue)
    {
        if (!commonValue || metadataList.empty() || !metadataList.front())
        {
            return false;
        }

        const std::wstring first = getter(*metadataList.front());
        if (first.empty())
        {
            return false;
        }

        for (std::size_t index = 1; index < metadataList.size(); ++index)
        {
            if (!metadataList[index])
            {
                return false;
            }

            const std::wstring candidate = getter(*metadataList[index]);
            if (candidate.empty() || !StringsEqualInsensitive(first, candidate))
            {
                return false;
            }
        }

        *commonValue = first;
        return true;
    }

    bool TryGetCommonDimensions(const std::vector<hyperbrowse::browser::BrowserItem>& items,
                                std::wstring* commonValue)
    {
        if (!commonValue || items.empty())
        {
            return false;
        }

        const int firstWidth = items.front().imageWidth;
        const int firstHeight = items.front().imageHeight;
        if (firstWidth <= 0 || firstHeight <= 0)
        {
            return false;
        }

        for (std::size_t index = 1; index < items.size(); ++index)
        {
            if (items[index].imageWidth != firstWidth || items[index].imageHeight != firstHeight)
            {
                return false;
            }
        }

        *commonValue = hyperbrowse::browser::FormatDimensions(firstWidth, firstHeight);
        return true;
    }

    std::vector<hyperbrowse::services::MetadataPropertyEntry> FindCommonMetadataProperties(
        const std::vector<std::shared_ptr<const hyperbrowse::services::ImageMetadata>>& metadataList)
    {
        if (metadataList.empty() || !metadataList.front())
        {
            return {};
        }

        std::vector<hyperbrowse::services::MetadataPropertyEntry> commonProperties;
        for (const hyperbrowse::services::MetadataPropertyEntry& property : metadataList.front()->properties)
        {
            if (property.value.empty())
            {
                continue;
            }

            bool isCommon = true;
            for (std::size_t index = 1; index < metadataList.size(); ++index)
            {
                if (!metadataList[index])
                {
                    isCommon = false;
                    break;
                }

                const auto propertyIt = std::find_if(metadataList[index]->properties.begin(),
                                                     metadataList[index]->properties.end(),
                                                     [&](const hyperbrowse::services::MetadataPropertyEntry& candidate)
                                                     {
                                                         return candidate.canonicalName == property.canonicalName
                                                             && StringsEqualInsensitive(candidate.value, property.value);
                                                     });
                if (propertyIt == metadataList[index]->properties.end())
                {
                    isCommon = false;
                    break;
                }
            }

            if (isCommon)
            {
                commonProperties.push_back(property);
            }
        }

        return commonProperties;
    }

    std::wstring CompactSortLabel(hyperbrowse::browser::BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case hyperbrowse::browser::BrowserSortMode::FileName:
            return L"Name";
        case hyperbrowse::browser::BrowserSortMode::ModifiedDate:
            return L"Date";
        case hyperbrowse::browser::BrowserSortMode::FileSize:
            return L"Size";
        case hyperbrowse::browser::BrowserSortMode::Dimensions:
            return L"Pixels";
        case hyperbrowse::browser::BrowserSortMode::FileType:
            return L"Type";
        case hyperbrowse::browser::BrowserSortMode::DateTaken:
            return L"Taken";
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return L"Random";
        }
    }

    std::wstring NormalizeFolderPath(std::wstring path)
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

    std::wstring RewritePathPrefix(std::wstring_view path, std::wstring_view oldPrefix, std::wstring_view newPrefix)
    {
        if (!hyperbrowse::browser::PathHasPrefix(path, oldPrefix))
        {
            return std::wstring(path);
        }

        std::wstring rewrittenPath(newPrefix);
        std::wstring suffix(std::wstring(path).substr(oldPrefix.size()));
        if (!rewrittenPath.empty() && !suffix.empty() && rewrittenPath.back() == L'\\' && suffix.front() == L'\\')
        {
            suffix.erase(suffix.begin());
        }

        rewrittenPath.append(suffix);
        return NormalizeFolderPath(std::move(rewrittenPath));
    }

    int DefaultRenameSelectionEnd(std::wstring_view leafName, bool isFile)
    {
        if (!isFile)
        {
            return static_cast<int>(leafName.size());
        }

        const fs::path leafPath(leafName);
        const std::wstring stem = leafPath.stem().wstring();
        const std::wstring extension = leafPath.extension().wstring();
        if (!stem.empty() && !extension.empty())
        {
            return static_cast<int>(stem.size());
        }

        return static_cast<int>(leafName.size());
    }

    void CenterWindowOnOwner(HWND window, HWND ownerWindow)
    {
        RECT ownerRect{};
        RECT dialogRect{};
        const HWND referenceWindow = ownerWindow ? ownerWindow : GetDesktopWindow();
        GetWindowRect(referenceWindow, &ownerRect);
        GetWindowRect(window, &dialogRect);

        const int width = dialogRect.right - dialogRect.left;
        const int height = dialogRect.bottom - dialogRect.top;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    LRESULT CALLBACK TextInputDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const int clientWidth = kTextInputDialogWidth;
            const int clientHeight = kTextInputDialogHeight;
            const int contentWidth = clientWidth - (kTextInputDialogMargin * 2);
            const int instructionHeight = 34;
            const int buttonTop = clientHeight - kTextInputDialogMargin - kTextInputButtonHeight;
            const int cancelLeft = clientWidth - kTextInputDialogMargin - kTextInputButtonWidth;
            const int okLeft = cancelLeft - 8 - kTextInputButtonWidth;

            HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                kTextInputDialogMargin,
                kTextInputDialogMargin,
                contentWidth,
                instructionHeight,
                hwnd,
                nullptr,
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->editWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->initialText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                kTextInputDialogMargin,
                kTextInputDialogMargin + instructionHeight + 6,
                contentWidth,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextInputEditControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                state->confirmLabel.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                okLeft,
                buttonTop,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                cancelLeft,
                buttonTop,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            if (instructionWindow) SendMessageW(instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (state->editWindow) SendMessageW(state->editWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (state->okButton) SendMessageW(state->okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancelButton) SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            if (state->editWindow)
            {
                SendMessageW(state->editWindow,
                             EM_SETSEL,
                             static_cast<WPARAM>(state->selectionStart),
                             static_cast<LPARAM>(state->selectionEnd));
            }

            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state && state->editWindow)
            {
                SetFocus(state->editWindow);
                return FALSE;
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == IDOK)
            {
                const int length = state->editWindow ? GetWindowTextLengthW(state->editWindow) : 0;
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                if (state->editWindow)
                {
                    GetWindowTextW(state->editWindow, text.data(), static_cast<int>(text.size()));
                }
                text.resize(wcslen(text.c_str()));
                state->resultText = std::move(text);
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void PaintAboutDialog(HDC hdc, const RECT& clientRect, const AboutDialogState& state)
    {
        const int contentRight = clientRect.right - kAboutDialogMargin;
        const int artLeft = contentRight - kAboutDialogBrandArtSize;
        const int artTop = kAboutDialogMargin - 4;
        const int iconLeft = kAboutDialogMargin;
        const int iconTop = kAboutDialogMargin + 2;
        const int iconSize = 48;
        const int textLeft = iconLeft + iconSize + 20;
        const int textRight = artLeft - 28;
        const int textWidth = std::max(320, textRight - textLeft);

        const int titleTop = kAboutDialogMargin - 2;
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 44);
        const int subtitleTop = titleTop + titleHeight + 10;
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int introTop = subtitleTop + subtitleHeight + 10;
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 40);
        const int artFrameBottom = artTop + kAboutDialogBrandArtSize + 10;
        const int headerHeight = std::max(kAboutDialogHeaderHeight,
                                          std::max(artFrameBottom + kAboutDialogMargin - 8,
                                                   introTop + introHeight + kAboutDialogMargin - 8));

        const int bodyWidth = clientRect.right - (kAboutDialogMargin * 2);
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int footerTextWidth = MeasureAboutDialogFooterTextWidth(state, clientRect.right);
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(kAboutDialogFooterHeight, std::max(footerTextHeight + 34, kAboutDialogButtonHeight + 36));

        const RECT headerRect{clientRect.left, clientRect.top, clientRect.right, clientRect.top + headerHeight};
        const RECT footerRect{clientRect.left, clientRect.bottom - footerHeight, clientRect.right, clientRect.bottom};
        const RECT bodyRect{clientRect.left, headerRect.bottom, clientRect.right, footerRect.top};

        HBRUSH backgroundBrush = CreateSolidBrush(state.background);
        FillRect(hdc, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        HBRUSH headerBrush = CreateSolidBrush(state.headerBackground);
        FillRect(hdc, &headerRect, headerBrush);
        DeleteObject(headerBrush);

        HBRUSH footerBrush = CreateSolidBrush(state.footerBackground);
        FillRect(hdc, &footerRect, footerBrush);
        DeleteObject(footerBrush);

        HBRUSH bodyBrush = CreateSolidBrush(state.panelBackground);
        FillRect(hdc, &bodyRect, bodyBrush);
        DeleteObject(bodyBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, state.border);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, clientRect.left, headerRect.bottom - 1, nullptr);
        LineTo(hdc, clientRect.right, headerRect.bottom - 1);
        MoveToEx(hdc, clientRect.left, footerRect.top, nullptr);
        LineTo(hdc, clientRect.right, footerRect.top);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        SetBkMode(hdc, TRANSPARENT);

        if (state.heroIcon)
        {
            DrawIconEx(hdc, iconLeft, iconTop, state.heroIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        }

        if (state.brandArt)
        {
            RECT artFrame{artLeft - 10, artTop - 10, artLeft + kAboutDialogBrandArtSize + 10, artTop + kAboutDialogBrandArtSize + 10};
            HBRUSH artPanelBrush = CreateSolidBrush(BlendColor(state.headerBackground, state.background, state.darkMode ? 42 : 18));
            HPEN artBorderPen = CreatePen(PS_SOLID, 1, state.border);
            HGDIOBJ oldBrush = SelectObject(hdc, artPanelBrush);
            HGDIOBJ oldArtPen = SelectObject(hdc, artBorderPen);
            RoundRect(hdc, artFrame.left, artFrame.top, artFrame.right, artFrame.bottom, 18, 18);
            SelectObject(hdc, oldArtPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(artBorderPen);
            DeleteObject(artPanelBrush);

            hyperbrowse::util::DrawBitmapWithAlpha(hdc, *state.brandArt, artLeft, artTop, kAboutDialogBrandArtSize, kAboutDialogBrandArtSize);
        }

        RECT titleRect{textLeft, titleTop, textRight, titleTop + titleHeight};
        RECT subtitleRect{textLeft, subtitleTop, textRight, subtitleTop + subtitleHeight};
        RECT introRect{textLeft, introTop, textRight, introTop + introHeight};

        HGDIOBJ oldFont = SelectObject(hdc, state.titleFont ? state.titleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, state.text);
        DrawTextW(hdc, state.title.c_str(), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, state.accent);
        DrawTextW(hdc, state.subtitle.c_str(), -1, &subtitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, state.mutedText);
        DrawTextW(hdc,
                  state.intro.c_str(),
                  -1,
                  &introRect,
                  DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

        RECT headingRect{kAboutDialogMargin, bodyRect.top + 24, clientRect.right - kAboutDialogMargin, bodyRect.top + 24 + headingHeight};
        SetTextColor(hdc, state.accent);
        DrawTextW(hdc, state.bodyHeading.c_str(), -1, &headingRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        RECT bodyTextRect{kAboutDialogMargin, headingRect.bottom + 12, clientRect.right - kAboutDialogMargin, footerRect.top - 18};
        SetTextColor(hdc, state.text);
        DrawTextW(hdc,
                  state.bodyContent.c_str(),
                  -1,
                  &bodyTextRect,
                  DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

        RECT footerTextRect{kAboutDialogMargin,
                    footerRect.top + 18,
                    std::max(kAboutDialogMargin + 200, AboutDialogFooterButtonsLeft(clientRect, state) - 20),
                    footerRect.bottom - 16};
        SelectObject(hdc, state.footerFont ? state.footerFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, state.mutedText);
        DrawTextW(hdc,
                  state.footer.c_str(),
                  -1,
                  &footerTextRect,
                  DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

        SelectObject(hdc, oldFont);
    }

    LRESULT CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            state->githubButton = CreateWindowExW(
                0,
                L"BUTTON",
                kAboutDialogGitHubLabel,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0,
                0,
                state->githubButtonWidth,
                kAboutDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ABOUT_OPEN_GITHUB)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->supportButton = CreateWindowExW(
                0,
                L"BUTTON",
                kAboutDialogSupportLabel,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0,
                0,
                state->supportButtonWidth,
                kAboutDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ABOUT_OPEN_SUPPORT)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kAboutDialogButtonWidth,
                kAboutDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            if (state->githubButton)
            {
                SendMessageW(state->githubButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->subtitleFont ? state->subtitleFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            if (state->supportButton)
            {
                SendMessageW(state->supportButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->subtitleFont ? state->subtitleFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            if (state->okButton)
            {
                SendMessageW(state->okButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->bodyFont ? state->bodyFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }

            if (state->windowIcon)
            {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(state->windowIcon));
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(state->windowIcon));
            }

            ApplyWindowFrameTheme(hwnd, state->darkMode);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            LayoutAboutDialogControls(hwnd, *state);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutAboutDialogControls(hwnd, *state);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state && state->okButton)
            {
                SetFocus(state->okButton);
                return FALSE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            if (!state)
            {
                break;
            }

            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            PaintAboutDialog(hdc, clientRect, *state);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM:
            if (state && (wParam == ID_ABOUT_OPEN_GITHUB || wParam == ID_ABOUT_OPEN_SUPPORT))
            {
                DrawAboutDialogLinkButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), *state);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_ABOUT_OPEN_GITHUB || LOWORD(wParam) == ID_ABOUT_OPEN_SUPPORT)
            {
                OpenAboutDialogLink(hwnd, LOWORD(wParam));
                return 0;
            }
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForSingleLineText(HWND ownerWindow,
                                 HINSTANCE instance,
                                 const std::wstring& title,
                                 const std::wstring& instruction,
                                 const std::wstring& confirmLabel,
                                 const std::wstring& initialText,
                                 int selectionStart,
                                 int selectionEnd,
                                 std::wstring* resultText)
    {
        if (!resultText)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kTextInputDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &TextInputDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kTextInputDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        TextInputDialogState state;
        state.ownerWindow = ownerWindow;
        state.title = title;
        state.instruction = instruction;
        state.confirmLabel = confirmLabel;
        state.initialText = initialText;
        state.selectionStart = selectionStart;
        state.selectionEnd = selectionEnd;

        RECT windowRect{0, 0, kTextInputDialogWidth, kTextInputDialogHeight};
        AdjustWindowRectEx(&windowRect, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kTextInputDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            return false;
        }

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        if (!state.accepted)
        {
            return false;
        }

        *resultText = state.resultText;
        return true;
    }

    bool IsValidRenameLeafName(std::wstring_view leafName, std::wstring* errorMessage)
    {
        if (leafName.empty())
        {
            if (errorMessage) *errorMessage = L"The name cannot be empty.";
            return false;
        }

        if (leafName == L"." || leafName == L"..")
        {
            if (errorMessage) *errorMessage = L"The name is not valid.";
            return false;
        }

        if (std::any_of(leafName.begin(), leafName.end(), [](wchar_t character)
        {
            return character < 32 || wcschr(L"<>:\"/\\|?*", character) != nullptr;
        }))
        {
            if (errorMessage) *errorMessage = L"The name contains characters that Windows does not allow.";
            return false;
        }

        if (!leafName.empty() && (leafName.back() == L' ' || leafName.back() == L'.'))
        {
            if (errorMessage) *errorMessage = L"Names cannot end with a space or a period.";
            return false;
        }

        return true;
    }

    bool PromptForRenameLeafName(HWND ownerWindow,
                                 HINSTANCE instance,
                                 std::wstring title,
                                 std::wstring instruction,
                                 std::wstring currentLeafName,
                                 bool isFile,
                                 std::wstring* renamedLeafName)
    {
        if (!renamedLeafName)
        {
            return false;
        }

        std::wstring candidate = currentLeafName;
        const int selectionEnd = DefaultRenameSelectionEnd(currentLeafName, isFile);
        while (PromptForSingleLineText(ownerWindow,
                                       instance,
                                       title,
                                       instruction,
                                       L"Rename",
                                       candidate,
                                       0,
                                       selectionEnd,
                                       &candidate))
        {
            std::wstring errorMessage;
            if (!IsValidRenameLeafName(candidate, &errorMessage))
            {
                MessageBoxW(ownerWindow, errorMessage.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
                continue;
            }

            if (candidate == currentLeafName)
            {
                return false;
            }

            *renamedLeafName = candidate;
            return true;
        }

        return false;
    }

    bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
    {
        const std::wstring normalizedLeft = NormalizeFolderPath(std::wstring(lhs));
        const std::wstring normalizedRight = NormalizeFolderPath(std::wstring(rhs));
        return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
    }

    bool IsExistingDirectory(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return false;
        }

        std::error_code error;
        return fs::is_directory(fs::path(folderPath), error) && !error;
    }

    bool InsertFolderPath(std::vector<std::wstring>* paths,
                          std::wstring folderPath,
                          std::size_t maxCount,
                          bool moveToFront)
    {
        if (!paths)
        {
            return false;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        if (folderPath.empty())
        {
            return false;
        }

        const auto existing = std::find_if(paths->begin(), paths->end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });

        if (existing != paths->end())
        {
            if (!moveToFront)
            {
                return false;
            }

            if (existing == paths->begin())
            {
                return false;
            }

            paths->erase(existing);
        }

        if (moveToFront)
        {
            paths->insert(paths->begin(), std::move(folderPath));
        }
        else if (paths->size() < maxCount)
        {
            paths->push_back(std::move(folderPath));
        }
        else
        {
            return false;
        }

        if (paths->size() > maxCount)
        {
            paths->resize(maxCount);
        }

        return true;
    }

    std::vector<std::wstring> DeserializeFolderPathList(std::wstring_view serialized, std::size_t maxCount)
    {
        std::vector<std::wstring> paths;
        std::wstring current;
        for (const wchar_t character : serialized)
        {
            if (character == L'\r')
            {
                continue;
            }

            if (character == L'\n')
            {
                InsertFolderPath(&paths, std::move(current), maxCount, false);
                current.clear();
                continue;
            }

            current.push_back(character);
        }

        InsertFolderPath(&paths, std::move(current), maxCount, false);
        return paths;
    }

    std::wstring SerializeFolderPathList(const std::vector<std::wstring>& paths)
    {
        std::wstring serialized;
        for (std::size_t index = 0; index < paths.size(); ++index)
        {
            if (index > 0)
            {
                serialized.push_back(L'\n');
            }

            serialized.append(paths[index]);
        }

        return serialized;
    }

    bool CopyTextToClipboard(HWND ownerWindow, std::wstring_view text)
    {
        if (!OpenClipboard(ownerWindow))
        {
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            return false;
        }

        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!buffer)
        {
            CloseClipboard();
            return false;
        }

        void* locked = GlobalLock(buffer);
        if (!locked)
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(locked)[text.size()] = L'\0';
        GlobalUnlock(buffer);

        if (!SetClipboardData(CF_UNICODETEXT, buffer))
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

    std::wstring BuildDeleteConfirmationMessage(std::size_t itemCount, bool permanent)
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

    bool ConfirmFileDeletion(HWND ownerWindow, std::size_t itemCount, bool permanent)
    {
        const std::wstring prompt = BuildDeleteConfirmationMessage(itemCount, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete" : L"Delete",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }

    std::wstring BuildFolderDeleteConfirmationMessage(std::wstring_view folderPath, bool permanent)
    {
        const std::wstring folderLabel = GetFolderDisplayName(folderPath);
        return permanent
            ? L"Permanently delete the folder \"" + folderLabel + L"\"?\n\nThis cannot be undone."
            : L"Move the folder \"" + folderLabel + L"\" to the Recycle Bin?";
    }

    bool ConfirmFolderDeletion(HWND ownerWindow, std::wstring_view folderPath, bool permanent)
    {
        const std::wstring prompt = BuildFolderDeleteConfirmationMessage(folderPath, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete Folder" : L"Delete Folder",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }

    std::wstring FindExistingFolderAncestor(fs::path candidate)
    {
        std::error_code error;
        while (!candidate.empty())
        {
            if (fs::is_directory(candidate, error) && !error)
            {
                return NormalizeFolderPath(candidate.wstring());
            }

            error.clear();
            const fs::path parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }

            candidate = parent;
        }

        return {};
    }

    std::wstring JoinLines(const std::vector<std::wstring>& lines)
    {
        std::wstring combined;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (index > 0)
            {
                combined.append(L"\r\n");
            }
            combined.append(lines[index]);
        }
        return combined;
    }

    std::wstring BuildFileConflictContent(std::size_t conflictCount)
    {
        if (conflictCount == 1)
        {
            return L"1 selected file would use a name that already exists in the destination or is already queued for this operation.";
        }

        return std::to_wstring(conflictCount)
            + L" selected files would use names that already exist in the destination or are already queued for this operation.";
    }

    bool PromptForFileConflictPolicy(HWND ownerWindow,
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

    bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target)
    {
        if (target.empty())
        {
            return false;
        }

        const std::wstring path(target);
        const HINSTANCE result = ShellExecuteW(ownerWindow, verb, path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    bool RevealPathsInExplorer(const std::vector<std::wstring>& selectedPaths)
    {
        if (selectedPaths.empty())
        {
            return false;
        }

        std::vector<std::wstring> revealPaths;
        revealPaths.push_back(selectedPaths.front());

        const std::wstring primaryParent = NormalizeFolderPath(fs::path(selectedPaths.front()).parent_path().wstring());
        bool sameParent = !primaryParent.empty();
        for (const std::wstring& path : selectedPaths)
        {
            if (!FolderPathsEqual(primaryParent, fs::path(path).parent_path().wstring()))
            {
                sameParent = false;
                break;
            }
        }

        if (sameParent)
        {
            revealPaths = selectedPaths;
        }

        PIDLIST_ABSOLUTE folderPidl = ILCreateFromPathW(primaryParent.c_str());
        if (!folderPidl)
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCUITEMID_CHILD> childPidls;
        itemPidls.reserve(revealPaths.size());
        childPidls.reserve(revealPaths.size());
        for (const std::wstring& path : revealPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }

            itemPidls.push_back(itemPidl);
            childPidls.push_back(ILFindLastID(itemPidl));
        }

        const HRESULT result = SHOpenFolderAndSelectItems(folderPidl,
                                                          static_cast<UINT>(childPidls.size()),
                                                          childPidls.empty() ? nullptr : childPidls.data(),
                                                          0);

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }
        ILFree(folderPidl);

        return SUCCEEDED(result);
    }

    struct ShellTreeItemInfo
    {
        std::wstring displayName;
        int iconIndex{};
        int openIconIndex{};
    };

    std::wstring FormatDriveDisplayName(const std::wstring& rootPath)
    {
        wchar_t volumeName[MAX_PATH]{};
        if (GetVolumeInformationW(
            rootPath.c_str(),
            volumeName,
            static_cast<DWORD>(std::size(volumeName)),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            0) != FALSE
            && volumeName[0] != L'\0')
        {
            std::wstring label = volumeName;
            label.append(L" (");
            label.append(rootPath.substr(0, 2));
            label.append(L")");
            return label;
        }

        return rootPath;
    }

    std::wstring TryGetKnownFolderPath(REFKNOWNFOLDERID folderId)
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return NormalizeFolderPath(std::move(path));
    }

    ShellTreeItemInfo QueryShellTreeItemInfo(const std::wstring& folderPath)
    {
        ShellTreeItemInfo info;

        SHFILEINFOW shellInfo{};
        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
        {
            info.displayName = shellInfo.szDisplayName;
            info.iconIndex = shellInfo.iIcon;
        }

        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_OPENICON | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
        {
            info.openIconIndex = shellInfo.iIcon;
        }
        else
        {
            info.openIconIndex = info.iconIndex;
        }

        if (info.displayName.empty())
        {
            const fs::path path(folderPath);
            const std::wstring normalizedRoot = NormalizeFolderPath(path.root_path().wstring());
            info.displayName = FolderPathsEqual(normalizedRoot, folderPath)
                ? FormatDriveDisplayName(folderPath)
                : GetFolderDisplayName(folderPath);
        }

        return info;
    }

    hyperbrowse::browser::BrowserSortMode SortModeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_SORT_FILENAME:
            return hyperbrowse::browser::BrowserSortMode::FileName;
        case ID_VIEW_SORT_MODIFIED:
            return hyperbrowse::browser::BrowserSortMode::ModifiedDate;
        case ID_VIEW_SORT_SIZE:
            return hyperbrowse::browser::BrowserSortMode::FileSize;
        case ID_VIEW_SORT_DIMENSIONS:
            return hyperbrowse::browser::BrowserSortMode::Dimensions;
        case ID_VIEW_SORT_TYPE:
            return hyperbrowse::browser::BrowserSortMode::FileType;
        case ID_VIEW_SORT_DATETAKEN:
            return hyperbrowse::browser::BrowserSortMode::DateTaken;
        case ID_VIEW_SORT_RANDOM:
        default:
            return hyperbrowse::browser::BrowserSortMode::Random;
        }
    }

    UINT CommandIdFromSortMode(hyperbrowse::browser::BrowserSortMode sortMode)
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
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return ID_VIEW_SORT_RANDOM;
        }
    }

    bool TryParseThumbnailSizePreset(DWORD value, hyperbrowse::browser::ThumbnailSizePreset* preset)
    {
        if (!preset)
        {
            return false;
        }

        switch (value)
        {
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels96):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels96;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels128):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels128;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels160):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels160;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels192):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels192;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels256):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels256;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels320):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels320;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels360):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels360;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels420):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels420;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels480):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels480;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels560):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels560;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels640):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels640;
            return true;
        default:
            return false;
        }
    }

    hyperbrowse::browser::ThumbnailSizePreset ThumbnailSizePresetFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_THUMBNAIL_SIZE_96:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels96;
        case ID_VIEW_THUMBNAIL_SIZE_128:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels128;
        case ID_VIEW_THUMBNAIL_SIZE_160:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels160;
        case ID_VIEW_THUMBNAIL_SIZE_256:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels256;
        case ID_VIEW_THUMBNAIL_SIZE_320:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels320;
        case ID_VIEW_THUMBNAIL_SIZE_360:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels360;
        case ID_VIEW_THUMBNAIL_SIZE_420:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels420;
        case ID_VIEW_THUMBNAIL_SIZE_480:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels480;
        case ID_VIEW_THUMBNAIL_SIZE_560:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels560;
        case ID_VIEW_THUMBNAIL_SIZE_640:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels640;
        case ID_VIEW_THUMBNAIL_SIZE_192:
        default:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels192;
        }
    }

    UINT CommandIdFromThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset preset)
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

    hyperbrowse::viewer::TransitionStyle TransitionStyleFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_SLIDESHOW_TRANSITION_CUT:
            return hyperbrowse::viewer::TransitionStyle::Cut;
        case ID_VIEW_SLIDESHOW_TRANSITION_SLIDE:
            return hyperbrowse::viewer::TransitionStyle::Slide;
        case ID_VIEW_SLIDESHOW_TRANSITION_KEN_BURNS:
            return hyperbrowse::viewer::TransitionStyle::KenBurns;
        case ID_VIEW_SLIDESHOW_TRANSITION_CROSSFADE:
        default:
            return hyperbrowse::viewer::TransitionStyle::Crossfade;
        }
    }

    UINT CommandIdFromTransitionStyle(hyperbrowse::viewer::TransitionStyle style)
    {
        switch (style)
        {
        case hyperbrowse::viewer::TransitionStyle::Cut:
            return ID_VIEW_SLIDESHOW_TRANSITION_CUT;
        case hyperbrowse::viewer::TransitionStyle::Slide:
            return ID_VIEW_SLIDESHOW_TRANSITION_SLIDE;
        case hyperbrowse::viewer::TransitionStyle::KenBurns:
            return ID_VIEW_SLIDESHOW_TRANSITION_KEN_BURNS;
        case hyperbrowse::viewer::TransitionStyle::Crossfade:
        default:
            return ID_VIEW_SLIDESHOW_TRANSITION_CROSSFADE;
        }
    }

    bool IsTransitionStyleCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_SLIDESHOW_TRANSITION_CUT
            && commandId <= ID_VIEW_SLIDESHOW_TRANSITION_KEN_BURNS;
    }

    UINT TransitionDurationFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_SLIDESHOW_TRANSITION_DURATION_200:
            return 200;
        case ID_VIEW_SLIDESHOW_TRANSITION_DURATION_500:
            return 500;
        case ID_VIEW_SLIDESHOW_TRANSITION_DURATION_800:
            return 800;
        case ID_VIEW_SLIDESHOW_TRANSITION_DURATION_1200:
            return 1200;
        case ID_VIEW_SLIDESHOW_TRANSITION_DURATION_2000:
            return 2000;
        case ID_VIEW_SLIDESHOW_TRANSITION_DURATION_350:
        default:
            return 350;
        }
    }

    UINT CommandIdFromTransitionDuration(UINT durationMs)
    {
        if (durationMs <= 275)
        {
            return ID_VIEW_SLIDESHOW_TRANSITION_DURATION_200;
        }
        if (durationMs <= 425)
        {
            return ID_VIEW_SLIDESHOW_TRANSITION_DURATION_350;
        }
        if (durationMs <= 650)
        {
            return ID_VIEW_SLIDESHOW_TRANSITION_DURATION_500;
        }
        if (durationMs <= 1000)
        {
            return ID_VIEW_SLIDESHOW_TRANSITION_DURATION_800;
        }
        if (durationMs <= 1600)
        {
            return ID_VIEW_SLIDESHOW_TRANSITION_DURATION_1200;
        }
        return ID_VIEW_SLIDESHOW_TRANSITION_DURATION_2000;
    }

    bool IsTransitionDurationCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_SLIDESHOW_TRANSITION_DURATION_200
            && commandId <= ID_VIEW_SLIDESHOW_TRANSITION_DURATION_2000;
    }

    hyperbrowse::viewer::MouseWheelBehavior ViewerMouseWheelBehaviorFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE:
            return hyperbrowse::viewer::MouseWheelBehavior::Navigate;
        case ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM:
        default:
            return hyperbrowse::viewer::MouseWheelBehavior::Zoom;
        }
    }

    UINT CommandIdFromViewerMouseWheelBehavior(hyperbrowse::viewer::MouseWheelBehavior behavior)
    {
        switch (behavior)
        {
        case hyperbrowse::viewer::MouseWheelBehavior::Navigate:
            return ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE;
        case hyperbrowse::viewer::MouseWheelBehavior::Zoom:
        default:
            return ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM;
        }
    }

    bool IsViewerMouseWheelBehaviorCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM
            && commandId <= ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE;
    }

    bool IsCommandInRange(UINT commandId, UINT firstCommandId, UINT lastCommandId)
    {
        return commandId >= firstCommandId && commandId <= lastCommandId;
    }

    int RatingFromCommandId(UINT commandId)
    {
        if (commandId < ID_FILE_SET_RATING_0 || commandId > ID_FILE_SET_RATING_5)
        {
            return 0;
        }

        return static_cast<int>(commandId - ID_FILE_SET_RATING_0);
    }

    bool IsRatingCommand(UINT commandId)
    {
        return commandId >= ID_FILE_SET_RATING_0 && commandId <= ID_FILE_SET_RATING_5;
    }

    UINT CommandIdFromRating(int rating)
    {
        rating = std::clamp(rating, 0, 5);
        return ID_FILE_SET_RATING_0 + static_cast<UINT>(rating);
    }

    std::wstring FormatRatingForDisplay(int rating)
    {
        if (rating <= 0)
        {
            return L"Unrated";
        }

        return std::to_wstring(std::clamp(rating, 0, 5)) + L"/5";
    }

    bool IsJpegBrowserItem(const hyperbrowse::browser::BrowserItem& item)
    {
        return hyperbrowse::decode::IsWicFileType(item.fileType)
            && (_wcsicmp(item.fileType.c_str(), L"JPG") == 0 || _wcsicmp(item.fileType.c_str(), L"JPEG") == 0);
    }

    struct AlternateMonitorSearch
    {
        HMONITOR referenceMonitor{};
        HMONITOR alternateMonitor{};
    };

    BOOL CALLBACK CaptureAlternateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
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

    HMONITOR FindAlternateMonitorForWindow(HWND hwnd)
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

    HMONITOR ResolveViewerMonitor(HWND hwnd, bool preferSecondaryMonitor)
    {
        if (preferSecondaryMonitor)
        {
            return FindAlternateMonitorForWindow(hwnd);
        }

        if (hwnd && IsWindow(hwnd))
        {
            return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        }

        return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
}

namespace hyperbrowse::ui
{
    MainWindow::MainWindow(HINSTANCE instance)
        : instance_(instance)
        , browserModel_(std::make_unique<browser::BrowserModel>())
        , browserPaneController_(std::make_unique<browser::BrowserPane>(instance))
        , batchConvertService_(std::make_unique<services::BatchConvertService>())
        , fileOperationService_(std::make_unique<services::FileOperationService>())
        , folderEnumerationService_(std::make_unique<services::FolderEnumerationService>())
        , folderTreeEnumerationService_(std::make_unique<services::FolderTreeEnumerationService>())
        , folderWatchService_(std::make_unique<services::FolderWatchService>())
        , userMetadataStore_(std::make_unique<services::UserMetadataStore>())
        , diagnosticsWindow_(std::make_unique<DiagnosticsWindow>(instance))
        , viewerWindow_(std::make_unique<viewer::ViewerWindow>(instance))
        , slideshowTransitionStyle_(viewer::TransitionStyle::Crossfade)
    {
    }

    MainWindow::~MainWindow()
    {
        if (folderEnumerationService_)
        {
            folderEnumerationService_->Cancel();
        }

        if (folderTreeEnumerationService_)
        {
            folderTreeEnumerationService_->CancelAll();
        }

        if (folderWatchService_)
        {
            folderWatchService_->Stop();
        }

        if (batchConvertService_)
        {
            batchConvertService_->Cancel();
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (actionFieldBrush_)
        {
            DeleteObject(actionFieldBrush_);
        }

        if (detailsPanelBrush_)
        {
            DeleteObject(detailsPanelBrush_);
        }

        DeleteFontIfOwned(detailsPanelTitleFont_);
        DeleteFontIfOwned(detailsPanelSummaryFont_);
        DeleteFontIfOwned(detailsPanelBodyFont_);

        if (detailsPanelRichEditModule_)
        {
            FreeLibrary(detailsPanelRichEditModule_);
            detailsPanelRichEditModule_ = nullptr;
        }

        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
        }
    }

    bool MainWindow::Create()
    {
        util::ScopedTimer timer{L"MainWindow::Create"};

        if (!RegisterWindowClass())
        {
            return false;
        }

        LoadWindowState();
        ApplyViewerMouseWheelSetting();
        ApplyViewerTransitionSettings();

        hwnd_ = CreateWindowExW(
            0,
            kWindowClassName,
            L"HyperBrowse",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1400,
            900,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_)
        {
            util::LogLastError(L"CreateWindowExW(MainWindow)");
            return false;
        }

        if (!CreateAccelerators() || !CreateMenuBar() || !CreateChildWindows())
        {
            return false;
        }

        ApplyPersistentThumbnailCacheSetting();
        ApplyTheme();
        UpdateMenuState();
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        return true;
    }

    void MainWindow::Show(int nCmdShow) const
    {
        ShowWindow(hwnd_, nCmdShow);
        UpdateWindow(hwnd_);
    }

    bool MainWindow::TranslateAcceleratorMessage(MSG* message) const
    {
        return hwnd_ && accelerators_ && message && TranslateAcceleratorW(hwnd_, accelerators_, message) != 0;
    }

    bool MainWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MainWindow::WindowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        wc.hIconSm = static_cast<HICON>(
            LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE),
                       IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

        return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool MainWindow::CreateAccelerators()
    {
        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
            accelerators_ = nullptr;
        }

        ACCEL accelerators[] = {
            {FVIRTKEY | FCONTROL, static_cast<WORD>('O'), ID_FILE_OPEN_FOLDER},
            {FVIRTKEY, VK_F5, ID_FILE_REFRESH_TREE},
            {FVIRTKEY, VK_F2, ID_FILE_RENAME_SELECTED},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('I'), ID_FILE_IMAGE_INFORMATION},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('C'), ID_FILE_COPY_PATH},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('E'), ID_FILE_REVEAL_IN_EXPLORER},
            {FVIRTKEY | FALT, VK_RETURN, ID_FILE_PROPERTIES},
            {FVIRTKEY, VK_DELETE, ID_FILE_DELETE_SELECTION},
            {FVIRTKEY | FSHIFT, VK_DELETE, ID_FILE_DELETE_SELECTION_PERMANENT},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('1'), ID_VIEW_THUMBNAILS},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('2'), ID_VIEW_DETAILS},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('R'), ID_VIEW_RECURSIVE},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('S'), ID_VIEW_SLIDESHOW_SELECTION},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('F'), ID_VIEW_SLIDESHOW_FOLDER},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('D'), ID_HELP_DIAGNOSTICS_SNAPSHOT},
            {FVIRTKEY | FCONTROL | FSHIFT, static_cast<WORD>('X'), ID_HELP_DIAGNOSTICS_RESET},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('L'), ID_VIEW_THEME_LIGHT},
            {FVIRTKEY | FCONTROL, static_cast<WORD>('D'), ID_VIEW_THEME_DARK},
        };

        accelerators_ = CreateAcceleratorTableW(accelerators, static_cast<int>(std::size(accelerators)));
        return accelerators_ != nullptr;
    }

    bool MainWindow::CreateMenuBar()
    {
        menu_ = CreateMenu();
        fileMenu_ = CreatePopupMenu();
        openRecentFolderMenu_ = CreatePopupMenu();
        copySelectionToMenu_ = CreatePopupMenu();
        moveSelectionToMenu_ = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU batchConvertFolderMenu = CreatePopupMenu();
        HMENU ratingMenu = CreatePopupMenu();
        HMENU viewMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        HMENU thumbnailSizeMenu = CreatePopupMenu();
        HMENU slideshowTransitionMenu = CreatePopupMenu();
        HMENU slideshowTransitionDurationMenu = CreatePopupMenu();
        HMENU viewerMouseWheelMenu = CreatePopupMenu();
        HMENU themeMenu = CreatePopupMenu();
        HMENU helpMenu = CreatePopupMenu();

        if (!menu_ || !fileMenu_ || !openRecentFolderMenu_ || !copySelectionToMenu_ || !moveSelectionToMenu_ || !batchConvertSelectionMenu || !batchConvertFolderMenu || !ratingMenu || !viewMenu || !sortMenu || !thumbnailSizeMenu || !slideshowTransitionMenu || !slideshowTransitionDurationMenu || !viewerMouseWheelMenu || !themeMenu || !helpMenu)
        {
            return false;
        }

        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_FOLDER, L"&Open Folder...\tCtrl+O");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(openRecentFolderMenu_), L"Open &Recent Folder");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree\tF5");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION, L"Add Current Folder to Favorite &Destinations");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_COMPARE_SELECTED, L"&Compare Selected");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_VIEW_ON_SECONDARY_MONITOR, L"View on Secondary &Monitor");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information\tCtrl+I");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer\tCtrl+E");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h\tCtrl+Shift+C");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties\tAlt+Enter");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_0, L"&Clear Rating");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_1, L"&1 Star");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_2, L"&2 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_3, L"&3 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_4, L"&4 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_5, L"&5 Stars");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set &Rating");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_EDIT_TAGS, L"Edit &Tags...");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(copySelectionToMenu_), L"Cop&y Selection To");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(moveSelectionToMenu_), L"Mo&ve Selection To");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS, L"Include Paired &RAW+JPEG");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete\tDel");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently\tShift+Del");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_JPEG, L"Folder to JPE&G");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_PNG, L"Folder to P&NG");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_TIFF, L"Folder to TIF&F");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertFolderMenu), L"Batch Convert &Folder");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_BATCH_CONVERT_CANCEL, L"&Cancel Batch Convert");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_EXIT, L"E&xit");

        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode\tCtrl+1");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode\tCtrl+2");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_RECURSIVE, L"&Recursive Browsing\tCtrl+R");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date &Taken");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"&Descending");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_96, L"&96 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_128, L"1&28 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_160, L"1&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_192, L"1&92 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_256, L"2&56 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_320, L"3&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_360, L"3&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_420, L"4&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_480, L"4&80 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_560, L"5&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_640, L"6&40 px");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(thumbnailSizeMenu), L"Thumbnail Si&ze");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAIL_DETAILS, L"Show Thumbnail &Details");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS_STRIP, L"Show File &Details Panel");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_FILE_COMPARE_SELECTED, L"Compare &Selected");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"Slideshow from &Selection\tCtrl+Shift+S");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_SLIDESHOW_FOLDER, L"Slideshow from &Folder\tCtrl+Shift+F");
        AppendMenuW(slideshowTransitionMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_CUT, L"&Cut");
        AppendMenuW(slideshowTransitionMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_CROSSFADE, L"&Crossfade");
        AppendMenuW(slideshowTransitionMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_SLIDE, L"S&lide");
        AppendMenuW(slideshowTransitionMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_KEN_BURNS, L"&Ken Burns");
        AppendMenuW(slideshowTransitionMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(slideshowTransitionDurationMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_DURATION_200, L"&200 ms");
        AppendMenuW(slideshowTransitionDurationMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_DURATION_350, L"&350 ms");
        AppendMenuW(slideshowTransitionDurationMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_DURATION_500, L"&500 ms");
        AppendMenuW(slideshowTransitionDurationMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_DURATION_800, L"&800 ms");
        AppendMenuW(slideshowTransitionDurationMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_DURATION_1200, L"1&200 ms");
        AppendMenuW(slideshowTransitionDurationMenu, MF_STRING, ID_VIEW_SLIDESHOW_TRANSITION_DURATION_2000, L"2&000 ms");
        AppendMenuW(slideshowTransitionMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(slideshowTransitionDurationMenu), L"&Duration");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(slideshowTransitionMenu), L"Slideshow &Transition");
        AppendMenuW(viewerMouseWheelMenu, MF_STRING, ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM, L"&Zoom");
        AppendMenuW(viewerMouseWheelMenu, MF_STRING, ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE, L"&Next/Previous Image");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(viewerMouseWheelMenu), L"Viewer Mouse &Wheel");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(themeMenu, MF_STRING, ID_VIEW_THEME_LIGHT, L"&Light\tCtrl+L");
        AppendMenuW(themeMenu, MF_STRING, ID_VIEW_THEME_DARK, L"&Dark\tCtrl+D");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"&Theme");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_PERSISTENT_THUMBNAIL_CACHE, L"Persistent Thumbnail &Cache");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NVJPEG_ACCELERATION, L"Enable &NVIDIA JPEG Acceleration");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_LIBRAW_OUT_OF_PROCESS, L"Use Out-of-Process &LibRaw Fallback");

        AppendMenuW(helpMenu, MF_STRING, ID_HELP_ABOUT, L"&About");
        AppendMenuW(helpMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_DIAGNOSTICS_SNAPSHOT, L"Diagnostics &Snapshot\tCtrl+Shift+D");
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_DIAGNOSTICS_RESET, L"Reset Diagnostics\tCtrl+Shift+X");

        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu_), L"&File");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

        return SetMenu(hwnd_, menu_) != FALSE;
    }

    bool MainWindow::CreateChildWindows()
    {
        const HFONT defaultGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        detailsPanelTitleFont_ = CreateDialogUiFont(11, FW_SEMIBOLD);
        detailsPanelSummaryFont_ = CreateDialogUiFont(9, FW_NORMAL);
        detailsPanelBodyFont_ = CreateDialogUiFont(9, FW_NORMAL);
        if (!detailsPanelTitleFont_) detailsPanelTitleFont_ = defaultGuiFont;
        if (!detailsPanelSummaryFont_) detailsPanelSummaryFont_ = defaultGuiFont;
        if (!detailsPanelBodyFont_) detailsPanelBodyFont_ = defaultGuiFont;

        toolbarIconLibrary_ = std::make_unique<ToolbarIconLibrary>();
        if (toolbarIconLibrary_ && !toolbarIconLibrary_->Initialize())
        {
            util::LogError(L"Toolbar SVG icon library failed to initialize.");
        }

        InitToolbarItems();

        filterEdit_ = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 120, kToolbarItemSize - 8,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION_FILTER_EDIT)),
            instance_,
            nullptr);

        if (filterEdit_)
        {
            SendMessageW(filterEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);
            SendMessageW(filterEdit_, EM_LIMITTEXT, 260, 0);
            SendMessageW(filterEdit_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Filter filenames"));
        }

        tooltipControl_ = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd_,
            nullptr,
            instance_,
            nullptr);
        if (tooltipControl_)
        {
            SendMessageW(tooltipControl_, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELPARAM(400, 0));
            SendMessageW(tooltipControl_, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELPARAM(100, 0));
            SendMessageW(tooltipControl_, TTM_SETMAXTIPWIDTH, 0, 300);

            for (int index = 0; index < static_cast<int>(toolbarItems_.size()); ++index)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.uFlags = TTF_SUBCLASS;
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = static_cast<UINT_PTR>(index);
                toolInfo.rect = toolbarItems_[static_cast<std::size_t>(index)].rect;
                toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }
        }

        treePane_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_TREEVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        statusBar_ = CreateWindowExW(
            0,
            STATUSCLASSNAMEW,
            nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!detailsPanelRichEditModule_)
        {
            detailsPanelRichEditModule_ = LoadLibraryW(L"Msftedit.dll");
        }

        const DWORD detailsPanelTextStyle = WS_CHILD | (detailsStripVisible_ ? WS_VISIBLE : 0) | WS_VSCROLL
            | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL;
        detailsPanelText_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            detailsPanelRichEditModule_ ? MSFTEDIT_CLASS : L"EDIT",
            L"",
            detailsPanelTextStyle,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!detailsPanelText_ && detailsPanelRichEditModule_)
        {
            FreeLibrary(detailsPanelRichEditModule_);
            detailsPanelRichEditModule_ = nullptr;
            detailsPanelText_ = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                detailsPanelTextStyle,
                0, 0, 100, 100,
                hwnd_,
                nullptr,
                instance_,
                nullptr);
        }

        if (!filterEdit_ || !treePane_ || !statusBar_ || !detailsPanelText_)
        {
            util::LogLastError(L"CreateChildWindows");
            return false;
        }

        SendMessageW(detailsPanelText_, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
        RefreshDetailsPanelBodyPresentation();

        detailsPanelThumbnailScheduler_ = std::make_unique<services::ThumbnailScheduler>();
        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->BindTargetWindow(hwnd_);
        }

        if (!browserPaneController_ || !browserPaneController_->Create(hwnd_))
        {
            util::LogError(L"Failed to create the browser pane control");
            return false;
        }

        browserPane_ = browserPaneController_->Hwnd();

        SendMessageW(treePane_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);

        SHFILEINFOW shellInfo{};
        treeImageList_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            L"C:\\",
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
        if (treeImageList_)
        {
            ImageList_SetBkColor(treeImageList_, CLR_NONE);
            TreeView_SetImageList(treePane_, treeImageList_, TVSIL_NORMAL);
        }

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        ApplyThumbnailDisplaySettings();
        decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
        decode::SetLibRawOutOfProcessEnabled(libRawOutOfProcessEnabled_);

        InitializeFolderTree();
        if (!startupFolderPath_.empty())
        {
            std::error_code error;
            if (fs::is_directory(fs::path(startupFolderPath_), error) && !error)
            {
                LoadFolderAsync(startupFolderPath_);
            }
        }
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateToolbarItemStates();
        UpdateDetailsPanel();
        LayoutChildren();
        return true;
    }

    void MainWindow::ApplyThumbnailDisplaySettings()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetThumbnailSizePreset(thumbnailSizePreset_);
        browserPaneController_->SetCompactThumbnailLayout(true);
        browserPaneController_->SetThumbnailDetailsVisible(thumbnailDetailsVisible_);
    }

    void MainWindow::InitializeFolderTree()
    {
        if (!treePane_)
        {
            return;
        }

        if (folderTreeEnumerationService_)
        {
            folderTreeEnumerationService_->CancelAll();
        }

        pendingFolderTreeEnumerationItems_.clear();
        pendingTreeSelectionPath_.clear();

        suppressTreeSelectionChange_ = true;
        TreeView_DeleteAllItems(treePane_);
        folderTreeNodes_.clear();
        PopulateSpecialFolderRoots();
        PopulateDriveRoots();
        suppressTreeSelectionChange_ = false;
        ShowSelectedFolderInTree();
    }

    void MainWindow::PopulateSpecialFolderRoots()
    {
        const KNOWNFOLDERID specialFolderIds[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Pictures,
        };

        for (const KNOWNFOLDERID& specialFolderId : specialFolderIds)
        {
            const std::wstring folderPath = TryGetKnownFolderPath(specialFolderId);
            if (folderPath.empty())
            {
                continue;
            }

            std::error_code error;
            if (!fs::is_directory(fs::path(folderPath), error) || error)
            {
                continue;
            }

            if (!FindChildFolderTreeItem(nullptr, folderPath))
            {
                InsertFolderTreeItem(TVI_ROOT, folderPath);
            }
        }
    }

    void MainWindow::PopulateDriveRoots()
    {
        const DWORD driveMask = GetLogicalDrives();
        for (wchar_t driveLetter = L'A'; driveLetter <= L'Z'; ++driveLetter)
        {
            const DWORD driveBit = 1UL << (driveLetter - L'A');
            if ((driveMask & driveBit) == 0)
            {
                continue;
            }

            std::wstring drivePath;
            drivePath.push_back(driveLetter);
            drivePath.append(L":\\");

            const UINT driveType = GetDriveTypeW(drivePath.c_str());
            if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN)
            {
                continue;
            }

            if (!FindChildFolderTreeItem(nullptr, drivePath))
            {
                InsertFolderTreeItem(TVI_ROOT, drivePath);
            }
        }
    }

    void MainWindow::RefreshFolderTree()
    {
        InitializeFolderTree();
    }

    HTREEITEM MainWindow::InsertFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath)
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        const ShellTreeItemInfo shellInfo = QueryShellTreeItemInfo(normalizedPath);

        auto nodeData = std::make_unique<FolderTreeNodeData>();
        nodeData->path = normalizedPath;
        nodeData->childrenLoaded = false;
        nodeData->childrenLoading = false;
        nodeData->childEnumerationRequestId = 0;
        FolderTreeNodeData* rawNodeData = nodeData.get();
        folderTreeNodes_.push_back(std::move(nodeData));

        TVINSERTSTRUCTW item{};
        item.hParent = parentItem;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
        item.item.pszText = const_cast<LPWSTR>(shellInfo.displayName.c_str());
        item.item.iImage = shellInfo.iconIndex;
        item.item.iSelectedImage = shellInfo.openIconIndex;
        item.item.lParam = reinterpret_cast<LPARAM>(rawNodeData);

        const HTREEITEM insertedItem = TreeView_InsertItem(treePane_, &item);
        if (insertedItem)
        {
            AddFolderTreePlaceholder(insertedItem);
        }

        return insertedItem;
    }

    void MainWindow::AddFolderTreePlaceholder(HTREEITEM parentItem)
    {
        if (!treePane_ || !parentItem)
        {
            return;
        }

        HTREEITEM childItem = TreeView_GetChild(treePane_, parentItem);
        while (childItem)
        {
            if (!GetFolderTreeNodeData(childItem))
            {
                return;
            }

            childItem = TreeView_GetNextSibling(treePane_, childItem);
        }

        TVINSERTSTRUCTW placeholder{};
        placeholder.hParent = parentItem;
        placeholder.hInsertAfter = TVI_LAST;
        placeholder.item.mask = TVIF_TEXT;
        placeholder.item.pszText = const_cast<LPWSTR>(L"");
        TreeView_InsertItem(treePane_, &placeholder);
    }

    void MainWindow::RequestFolderTreeChildren(HTREEITEM item)
    {
        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->childrenLoaded || nodeData->childrenLoading || !folderTreeEnumerationService_)
        {
            return;
        }

        nodeData->childrenLoading = true;
        const std::uint64_t requestId = folderTreeEnumerationService_->EnumerateChildDirectoriesAsync(hwnd_, nodeData->path);
        nodeData->childEnumerationRequestId = requestId;
        pendingFolderTreeEnumerationItems_[requestId] = item;
    }

    void MainWindow::ApplyFolderTreeChildren(HTREEITEM item, std::vector<std::wstring> childFolderPaths)
    {
        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData)
        {
            return;
        }

        nodeData->childrenLoaded = true;
        nodeData->childrenLoading = false;
        nodeData->childEnumerationRequestId = 0;

        HTREEITEM childItem = TreeView_GetChild(treePane_, item);
        while (childItem)
        {
            HTREEITEM nextSibling = TreeView_GetNextSibling(treePane_, childItem);
            TreeView_DeleteItem(treePane_, childItem);
            childItem = nextSibling;
        }

        for (const std::wstring& childFolderPath : childFolderPaths)
        {
            InsertFolderTreeItem(item, childFolderPath);
        }
    }

    void MainWindow::ShowSelectedFolderInTree()
    {
        if (!treePane_ || !browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        SelectFolderInTree(browserModel_->FolderPath());
    }

    void MainWindow::SelectFolderInTree(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty())
        {
            return;
        }

        pendingTreeSelectionPath_ = NormalizeFolderPath(folderPath);
        ContinueSelectingFolderInTree();
    }

    void MainWindow::ContinueSelectingFolderInTree()
    {
        if (!treePane_ || pendingTreeSelectionPath_.empty())
        {
            return;
        }

        const std::wstring normalizedPath = pendingTreeSelectionPath_;
        HTREEITEM currentItem = FindChildFolderTreeItem(nullptr, normalizedPath);
        if (currentItem)
        {
            suppressTreeSelectionChange_ = true;
            TreeView_SelectItem(treePane_, currentItem);
            TreeView_EnsureVisible(treePane_, currentItem);
            suppressTreeSelectionChange_ = false;
            pendingTreeSelectionPath_.clear();
            return;
        }

        const fs::path targetPath(normalizedPath);
        const std::wstring rootPath = NormalizeFolderPath(targetPath.root_path().wstring());
        if (rootPath.empty())
        {
            return;
        }

        currentItem = FindChildFolderTreeItem(nullptr, rootPath);
        if (!currentItem)
        {
            return;
        }

        if (!FolderPathsEqual(normalizedPath, rootPath))
        {
            fs::path currentPath(rootPath);
            for (const auto& segment : targetPath.relative_path())
            {
                if (segment.empty())
                {
                    continue;
                }

                currentPath /= segment;
                TreeView_Expand(treePane_, currentItem, TVE_EXPAND);

                FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
                if (!nodeData)
                {
                    return;
                }

                if (!nodeData->childrenLoaded)
                {
                    RequestFolderTreeChildren(currentItem);
                    return;
                }

                currentItem = FindChildFolderTreeItem(currentItem, currentPath.wstring());
                if (!currentItem)
                {
                    pendingTreeSelectionPath_.clear();
                    return;
                }
            }
        }

        suppressTreeSelectionChange_ = true;
        TreeView_SelectItem(treePane_, currentItem);
        TreeView_EnsureVisible(treePane_, currentItem);
        suppressTreeSelectionChange_ = false;
        pendingTreeSelectionPath_.clear();
    }

    HTREEITEM MainWindow::FindChildFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath) const
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        HTREEITEM currentItem = parentItem
            ? TreeView_GetChild(treePane_, parentItem)
            : TreeView_GetRoot(treePane_);
        while (currentItem)
        {
            FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
            if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
            {
                return currentItem;
            }

            currentItem = TreeView_GetNextSibling(treePane_, currentItem);
        }

        return nullptr;
    }

    HTREEITEM MainWindow::FindFolderTreeItemByPath(const std::wstring& folderPath) const
    {
        if (!treePane_ || folderPath.empty())
        {
            return nullptr;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM parentItem) -> HTREEITEM
        {
            HTREEITEM currentItem = parentItem
                ? TreeView_GetChild(treePane_, parentItem)
                : TreeView_GetRoot(treePane_);
            while (currentItem)
            {
                FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
                if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
                {
                    return currentItem;
                }

                if (HTREEITEM descendant = findItem(currentItem))
                {
                    return descendant;
                }

                currentItem = TreeView_GetNextSibling(treePane_, currentItem);
            }

            return nullptr;
        };

        return findItem(nullptr);
    }

    void MainWindow::InsertFolderTreeFolderIfParentLoaded(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty())
        {
            return;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        if (FindFolderTreeItemByPath(normalizedPath))
        {
            return;
        }

        const std::wstring parentPath = NormalizeFolderPath(fs::path(normalizedPath).parent_path().wstring());
        if (parentPath.empty())
        {
            return;
        }

        const HTREEITEM parentItem = FindFolderTreeItemByPath(parentPath);
        if (!parentItem)
        {
            return;
        }

        FolderTreeNodeData* parentNodeData = GetFolderTreeNodeData(parentItem);
        if (!parentNodeData || !parentNodeData->childrenLoaded)
        {
            return;
        }

        InsertFolderTreeItem(parentItem, normalizedPath);
    }

    MainWindow::FolderTreeNodeData* MainWindow::GetFolderTreeNodeData(HTREEITEM item) const
    {
        if (!treePane_ || !item)
        {
            return nullptr;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_PARAM;
        treeItem.hItem = item;
        if (TreeView_GetItem(treePane_, &treeItem) == FALSE)
        {
            return nullptr;
        }

        return reinterpret_cast<FolderTreeNodeData*>(treeItem.lParam);
    }

    std::wstring MainWindow::GetSelectedFolderTreePath() const
    {
        if (!treePane_)
        {
            return {};
        }

        const HTREEITEM selectedItem = TreeView_GetSelection(treePane_);
        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(selectedItem);
        return nodeData ? nodeData->path : std::wstring{};
    }

    LRESULT MainWindow::OnFolderTreeNotify(LPARAM lParam)
    {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (!header || header->hwndFrom != treePane_)
        {
            return 0;
        }

        switch (header->code)
        {
        case NM_RCLICK:
            return OnFolderTreeRightClick();
        case TVN_ITEMEXPANDINGW:
            return OnFolderTreeItemExpanding(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_SELCHANGEDW:
            return OnFolderTreeSelectionChanged(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        default:
            return 0;
        }
    }

    LRESULT MainWindow::OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView)
    {
        if (suppressTreeSelectionChange_)
        {
            return 0;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(treeView.itemNew.hItem);
        if (!nodeData)
        {
            return 0;
        }

        if (!browserModel_ || !FolderPathsEqual(browserModel_->FolderPath(), nodeData->path))
        {
            LoadFolderAsync(nodeData->path);
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView)
    {
        if ((treeView.action & TVE_EXPAND) != 0)
        {
            RequestFolderTreeChildren(treeView.itemNew.hItem);
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeRightClick()
    {
        if (!treePane_)
        {
            return 0;
        }

        POINT screenPoint{};
        if (!GetCursorPos(&screenPoint))
        {
            return 0;
        }

        POINT clientPoint = screenPoint;
        ScreenToClient(treePane_, &clientPoint);

        TVHITTESTINFO hitTest{};
        hitTest.pt = clientPoint;
        const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
        if (!item || (hitTest.flags & TVHT_ONITEM) == 0)
        {
            return 0;
        }

        if (!GetFolderTreeNodeData(item) || !TreeView_GetParent(treePane_, item))
        {
            return 0;
        }

        ShowFolderTreeContextMenu(screenPoint, item);
        return 0;
    }

    void MainWindow::LayoutChildren()
    {
        if (!hwnd_ || !treePane_ || !browserPane_ || !statusBar_)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);

        SendMessageW(statusBar_, WM_SIZE, 0, 0);

        RECT statusRect{};
        GetWindowRect(statusBar_, &statusRect);
        const int statusHeight = statusRect.bottom - statusRect.top;

        const int clientWidth = client.right - client.left;
        const int desiredDetailsPanelWidth = detailsStripVisible_
            ? std::min(kDetailsPanelPreferredWidth,
                       std::max(0, clientWidth - kMinLeftPaneWidth - kMinRightPaneWidth - kSplitterWidth))
            : 0;
        const int clientHeight = std::max(0, static_cast<int>(client.bottom - client.top) - statusHeight - kActionStripHeight);
        const int contentTop = kActionStripHeight;

        const int maxLeft = std::max(kMinLeftPaneWidth,
                                     clientWidth - desiredDetailsPanelWidth - kMinRightPaneWidth - kSplitterWidth);
        leftPaneWidth_ = std::clamp(leftPaneWidth_, kMinLeftPaneWidth, maxLeft);

        const int browserWidth = std::max(kMinRightPaneWidth,
                                          clientWidth - leftPaneWidth_ - kSplitterWidth - desiredDetailsPanelWidth);
        const int detailsPanelWidth = std::max(0, clientWidth - leftPaneWidth_ - kSplitterWidth - browserWidth);

        MoveWindow(treePane_, 0, contentTop, leftPaneWidth_, clientHeight, TRUE);
        MoveWindow(browserPane_, leftPaneWidth_ + kSplitterWidth, contentTop,
                   browserWidth, clientHeight, TRUE);

        detailsPanelRect_ = RECT{leftPaneWidth_ + kSplitterWidth + browserWidth,
                                 contentTop,
                                 clientWidth,
                                 contentTop + clientHeight};
        detailsPanelHistogramRect_ = RECT{};

        if (detailsPanelText_)
        {
            if (detailsStripVisible_ && detailsPanelWidth > 0)
            {
                const int innerLeft = detailsPanelRect_.left + kDetailsPanelMargin;
                const int innerRight = detailsPanelRect_.right - kDetailsPanelMargin;
                const int innerWidth = std::max(0, innerRight - innerLeft);
                const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
                const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                               title,
                                                               innerWidth,
                                                               DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                               22);
                const int summaryHeight = detailsPanelSummaryText_.empty()
                    ? 0
                    : MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                             detailsPanelSummaryText_,
                                             innerWidth,
                                             DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                             18);

                int top = detailsPanelRect_.top + kDetailsPanelMargin + titleHeight + 6;
                if (summaryHeight > 0)
                {
                    top += summaryHeight + 8;
                }

                if (detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_)
                {
                    detailsPanelHistogramRect_ = RECT{
                        innerLeft,
                        top,
                        innerRight,
                        top + kDetailsPanelHistogramHeight,
                    };
                    top = detailsPanelHistogramRect_.bottom + kDetailsPanelTextTopGap;
                }

                const int availableTextHeight = static_cast<int>(detailsPanelRect_.bottom - kDetailsPanelMargin - top);
                const int textHeight = std::max(0, availableTextHeight);
                MoveWindow(detailsPanelText_, innerLeft, top, innerWidth, textHeight, TRUE);
                ShowWindow(detailsPanelText_, SW_SHOW);
            }
            else
            {
                ShowWindow(detailsPanelText_, SW_HIDE);
                detailsPanelRect_ = RECT{};
            }
        }

        LayoutToolbar();

        RECT splitterRect{leftPaneWidth_, kActionStripHeight, leftPaneWidth_ + kSplitterWidth, client.bottom};
        InvalidateRect(hwnd_, &splitterRect, FALSE);
        if (detailsStripVisible_)
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }

        UpdateStatusText();
    }

    void MainWindow::UpdateStatusText() const
    {
        if (!statusBar_)
        {
            return;
        }

        RECT statusBarRect{};
        GetClientRect(statusBar_, &statusBarRect);
        const int firstPartWidth = statusBarRect.right > statusBarRect.left
            ? (statusBarRect.right - statusBarRect.left) / 2
            : 420;
        int parts[] = {firstPartWidth, -1};
        SendMessageW(statusBar_, SB_SETPARTS, static_cast<WPARAM>(std::size(parts)), reinterpret_cast<LPARAM>(parts));

        const std::uint64_t folderCount = browserModel_ && !browserModel_->FolderPath().empty()
            ? browserModel_->TotalCount()
            : 0;
        const std::uint64_t folderBytes = browserModel_ && !browserModel_->FolderPath().empty()
            ? browserModel_->TotalBytes()
            : 0;
        const std::wstring folderText = L"Folder: " + std::to_wstring(folderCount)
            + L" files | " + browser::FormatByteSize(folderBytes);

        const std::uint64_t selectedCount = browserPaneController_ ? browserPaneController_->SelectedCount() : 0;
        const std::uint64_t selectedBytes = browserPaneController_ ? browserPaneController_->SelectedBytes() : 0;
        std::wstring selectionText = L"Selected: " + std::to_wstring(selectedCount)
            + L" items | " + browser::FormatByteSize(selectedBytes);
        if (rawJpegPairedOperationsEnabled_)
        {
            std::size_t pairedCompanionCount = 0;
            SelectedFileOperationPathsSnapshot(&pairedCompanionCount);
            selectionText.append(L"  |  Paired RAW+JPEG On");
            if (pairedCompanionCount > 0)
            {
                selectionText.append(L" (+");
                selectionText.append(std::to_wstring(pairedCompanionCount));
                selectionText.push_back(L')');
            }
        }

        SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(folderText.c_str()));
        SendMessageW(statusBar_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(selectionText.c_str()));
    }

    void MainWindow::RecordRecentFolder(std::wstring folderPath)
    {
        if (InsertFolderPath(&recentFolders_, std::move(folderPath), kQuickAccessFolderLimit, true) && menu_)
        {
            UpdateMenuState();
        }
    }

    void MainWindow::RecordRecentDestination(std::wstring folderPath)
    {
        if (InsertFolderPath(&recentDestinationFolders_, std::move(folderPath), kQuickAccessFolderLimit, true) && menu_)
        {
            UpdateMenuState();
        }
    }

    void MainWindow::RefreshQuickAccessMenus()
    {
        if (!fileMenu_ || !openRecentFolderMenu_ || !copySelectionToMenu_ || !moveSelectionToMenu_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool allowMutatingFileCommands = browserPaneController_ && browserPaneController_->SelectedCount() > 0 && !fileOperationActive_;
        const std::wstring toggleLabel = hasFolder && IsFavoriteDestination(browserModel_->FolderPath())
            ? L"Remove Current Folder from Favorite &Destinations"
            : L"Add Current Folder to Favorite &Destinations";
        ModifyMenuW(fileMenu_,
                    ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                    MF_BYCOMMAND | MF_STRING,
                    ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                    toggleLabel.c_str());

        RemoveAllMenuItems(openRecentFolderMenu_);
        if (recentFolders_.empty())
        {
            AppendMenuW(openRecentFolderMenu_, MF_STRING | MF_GRAYED, 0, L"(No recent folders)");
        }
        else
        {
            const std::size_t recentCount = std::min<std::size_t>(recentFolders_.size(), ID_FILE_OPEN_RECENT_FOLDER_LAST - ID_FILE_OPEN_RECENT_FOLDER_BASE + 1);
            for (std::size_t index = 0; index < recentCount; ++index)
            {
                const std::wstring label = FormatFolderShortcutMenuLabel(recentFolders_[index]);
                AppendMenuW(openRecentFolderMenu_, MF_STRING, ID_FILE_OPEN_RECENT_FOLDER_BASE + static_cast<UINT>(index), label.c_str());
            }
        }

        const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
        const auto populateDestinationMenu = [&](HMENU menu,
                                                 UINT browseCommandId,
                                                 UINT favoriteBaseCommandId,
                                                 UINT favoriteLastCommandId,
                                                 UINT recentBaseCommandId,
                                                 UINT recentLastCommandId)
        {
            RemoveAllMenuItems(menu);
            AppendMenuW(menu,
                        MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                        browseCommandId,
                        L"Choose &Folder...");

            const std::size_t favoriteCapacity = favoriteLastCommandId - favoriteBaseCommandId + 1;
            const std::size_t recentCapacity = recentLastCommandId - recentBaseCommandId + 1;
            const std::size_t favoriteCount = std::min<std::size_t>(favoriteDestinationFolders_.size(), favoriteCapacity);
            const std::size_t recentCount = std::min<std::size_t>(recentDestinationPaths.size(), recentCapacity);
            if (favoriteCount == 0 && recentCount == 0)
            {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(No favorite or recent destinations)");
                return;
            }

            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            if (favoriteCount > 0)
            {
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Favorite Destinations");
                for (std::size_t index = 0; index < favoriteCount; ++index)
                {
                    const std::wstring label = FormatFolderShortcutMenuLabel(favoriteDestinationFolders_[index]);
                    AppendMenuW(menu,
                                MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                                favoriteBaseCommandId + static_cast<UINT>(index),
                                label.c_str());
                }
            }

            if (recentCount > 0)
            {
                if (favoriteCount > 0)
                {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                }

                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Recent Destinations");
                for (std::size_t index = 0; index < recentCount; ++index)
                {
                    const std::wstring label = FormatFolderShortcutMenuLabel(recentDestinationPaths[index]);
                    AppendMenuW(menu,
                                MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                                recentBaseCommandId + static_cast<UINT>(index),
                                label.c_str());
                }
            }
        };

        populateDestinationMenu(copySelectionToMenu_,
                                ID_FILE_COPY_SELECTION_BROWSE,
                                ID_FILE_COPY_SELECTION_FAVORITE_BASE,
                                ID_FILE_COPY_SELECTION_FAVORITE_LAST,
                                ID_FILE_COPY_SELECTION_RECENT_BASE,
                                ID_FILE_COPY_SELECTION_RECENT_LAST);
        populateDestinationMenu(moveSelectionToMenu_,
                                ID_FILE_MOVE_SELECTION_BROWSE,
                                ID_FILE_MOVE_SELECTION_FAVORITE_BASE,
                                ID_FILE_MOVE_SELECTION_FAVORITE_LAST,
                                ID_FILE_MOVE_SELECTION_RECENT_BASE,
                                ID_FILE_MOVE_SELECTION_RECENT_LAST);
    }

    void MainWindow::ApplyDetailsPanelText(std::wstring title, std::wstring summary, std::wstring body)
    {
        detailsPanelTitleText_ = std::move(title);
        detailsPanelSummaryText_ = std::move(summary);
        detailsPanelBodyText_ = std::move(body);

        if (detailsPanelText_)
        {
            const bool changed = SetWindowTextIfDifferent(detailsPanelText_, detailsPanelBodyText_);
            RefreshDetailsPanelBodyPresentation();
            if (changed)
            {
                SendMessageW(detailsPanelText_, EM_SETSEL, 0, 0);
                SendMessageW(detailsPanelText_, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
            }
        }

        if (hwnd_ && !IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
    }

    void MainWindow::RefreshDetailsPanelBodyPresentation()
    {
        if (!detailsPanelText_ || !detailsPanelRichEditModule_)
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        SendMessageW(detailsPanelText_, EM_SETBKGNDCOLOR, FALSE, palette.paneBackground);
        SendMessageW(detailsPanelText_, WM_SETREDRAW, FALSE, 0);

        CHARRANGE allText{};
        allText.cpMin = 0;
        allText.cpMax = -1;
        SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allText));

        CHARFORMAT2W baseFormat{};
        baseFormat.cbSize = sizeof(baseFormat);
        baseFormat.dwMask = CFM_BOLD | CFM_COLOR;
        baseFormat.dwEffects = 0;
        baseFormat.crTextColor = palette.mutedText;
        SendMessageW(detailsPanelText_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&baseFormat));

        const LRESULT lineCount = SendMessageW(detailsPanelText_, EM_GETLINECOUNT, 0, 0);
        for (LONG lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            const LONG lineStart = static_cast<LONG>(SendMessageW(detailsPanelText_, EM_LINEINDEX, lineIndex, 0));
            if (lineStart < 0)
            {
                continue;
            }

            const LONG lineLength = static_cast<LONG>(SendMessageW(detailsPanelText_, EM_LINELENGTH, lineStart, 0));
            if (lineLength <= 0)
            {
                continue;
            }

            std::vector<wchar_t> lineBuffer(static_cast<std::size_t>(lineLength) + 2, L'\0');
            *reinterpret_cast<WORD*>(lineBuffer.data()) = static_cast<WORD>(lineBuffer.size() - 1);
            const LRESULT copiedChars = SendMessageW(detailsPanelText_,
                                                     EM_GETLINE,
                                                     static_cast<WPARAM>(lineIndex),
                                                     reinterpret_cast<LPARAM>(lineBuffer.data()));
            if (copiedChars <= 0)
            {
                continue;
            }

            std::wstring_view line(lineBuffer.data(), static_cast<std::size_t>(copiedChars));
            LONG emphasisStart = lineStart;
            LONG emphasisEnd = lineStart + static_cast<LONG>(line.size());

            const std::size_t colon = line.find(L':');
            if (colon != std::wstring::npos)
            {
                std::size_t valueOffset = colon + 1;
                while (valueOffset < line.size() && iswspace(line[valueOffset]) != 0)
                {
                    ++valueOffset;
                }

                if (valueOffset >= line.size())
                {
                    continue;
                }

                emphasisStart = lineStart + static_cast<LONG>(valueOffset);
            }

            CHARRANGE emphasisRange{};
            emphasisRange.cpMin = emphasisStart;
            emphasisRange.cpMax = emphasisEnd;
            SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&emphasisRange));

            CHARFORMAT2W emphasisFormat{};
            emphasisFormat.cbSize = sizeof(emphasisFormat);
            emphasisFormat.dwMask = CFM_BOLD | CFM_COLOR;
            emphasisFormat.dwEffects = CFE_BOLD;
            emphasisFormat.crTextColor = palette.text;
            SendMessageW(detailsPanelText_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&emphasisFormat));
        }

        CHARRANGE resetSelection{};
        resetSelection.cpMin = 0;
        resetSelection.cpMax = 0;
        SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&resetSelection));
        SendMessageW(detailsPanelText_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(detailsPanelText_, nullptr, TRUE);
    }

    void MainWindow::ResetDetailsPanelHistogram()
    {
        detailsPanelHistogramPath_.clear();
        detailsPanelHistogramModifiedTimestampUtc_ = 0;
        detailsPanelHistogramModelIndex_ = -1;
        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = false;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->CancelOutstanding();
        }
    }

    void MainWindow::RequestDetailsPanelHistogram(const browser::BrowserItem& item, int modelIndex)
    {
        if (!detailsPanelThumbnailScheduler_ || !decode::CanDecodeThumbnail(item) || modelIndex < 0)
        {
            ResetDetailsPanelHistogram();
            return;
        }

        const int targetWidth = 192;
        const int targetHeight = 128;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);

        if (detailsPanelHistogramModelIndex_ == modelIndex
            && detailsPanelHistogramModifiedTimestampUtc_ == item.modifiedTimestampUtc
            && StringsEqualInsensitive(detailsPanelHistogramPath_, item.filePath)
            && (detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_))
        {
            return;
        }

        detailsPanelHistogramPath_ = item.filePath;
        detailsPanelHistogramModifiedTimestampUtc_ = item.modifiedTimestampUtc;
        detailsPanelHistogramModelIndex_ = modelIndex;

        if (const auto cachedThumbnail = detailsPanelThumbnailScheduler_->FindCachedThumbnail(cacheKey))
        {
            ApplyDetailsPanelHistogram(*cachedThumbnail);
            LayoutChildren();
            return;
        }

        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = true;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        ++detailsPanelThumbnailRequestEpoch_;
        detailsPanelThumbnailScheduler_->Schedule(detailsPanelThumbnailSessionId_,
                                                 detailsPanelThumbnailRequestEpoch_,
                                                 {services::ThumbnailWorkItem{modelIndex, cacheKey, 0, true}});
        LayoutChildren();
    }

    void MainWindow::ApplyDetailsPanelHistogram(const cache::CachedThumbnail& thumbnail)
    {
        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = false;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        BITMAP bitmap{};
        if (!thumbnail.Bitmap() || GetObjectW(thumbnail.Bitmap(), sizeof(bitmap), &bitmap) == 0)
        {
            return;
        }

        const int bitmapWidth = bitmap.bmWidth;
        const int bitmapHeight = std::abs(bitmap.bmHeight);
        if (bitmapWidth <= 0 || bitmapHeight <= 0)
        {
            return;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = bitmapWidth;
        bitmapInfo.bmiHeader.biHeight = -bitmapHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        std::vector<RGBQUAD> pixels(static_cast<std::size_t>(bitmapWidth) * static_cast<std::size_t>(bitmapHeight));
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return;
        }

        const int copiedScanLines = GetDIBits(screenDc,
                                              thumbnail.Bitmap(),
                                              0,
                                              static_cast<UINT>(bitmapHeight),
                                              pixels.data(),
                                              &bitmapInfo,
                                              DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);
        if (copiedScanLines == 0)
        {
            return;
        }

        for (const RGBQUAD& pixel : pixels)
        {
            const std::size_t redIndex = std::min<std::size_t>(kDetailsPanelHistogramBins - 1, (pixel.rgbRed * kDetailsPanelHistogramBins) / 256);
            const std::size_t greenIndex = std::min<std::size_t>(kDetailsPanelHistogramBins - 1, (pixel.rgbGreen * kDetailsPanelHistogramBins) / 256);
            const std::size_t blueIndex = std::min<std::size_t>(kDetailsPanelHistogramBins - 1, (pixel.rgbBlue * kDetailsPanelHistogramBins) / 256);

            detailsPanelHistogramRed_[redIndex] += 1;
            detailsPanelHistogramGreen_[greenIndex] += 1;
            detailsPanelHistogramBlue_[blueIndex] += 1;
        }

        for (std::size_t index = 0; index < kDetailsPanelHistogramBins; ++index)
        {
            detailsPanelHistogramPeak_ = std::max(detailsPanelHistogramPeak_, detailsPanelHistogramRed_[index]);
            detailsPanelHistogramPeak_ = std::max(detailsPanelHistogramPeak_, detailsPanelHistogramGreen_[index]);
            detailsPanelHistogramPeak_ = std::max(detailsPanelHistogramPeak_, detailsPanelHistogramBlue_[index]);
        }

        detailsPanelHistogramVisible_ = detailsPanelHistogramPeak_ > 0;
    }

    void MainWindow::UpdateDetailsPanel()
    {
        if (!detailsStripVisible_)
        {
            ResetDetailsPanelHistogram();
            return;
        }

        if (!browserPaneController_ || !browserModel_)
        {
            ApplyDetailsPanelText(L"File Details", L"Select one or more images to inspect metadata.", L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        const std::vector<int> selectedModelIndices = browserPaneController_->OrderedSelectedModelIndicesSnapshot();
        if (selectedModelIndices.empty())
        {
            ApplyDetailsPanelText(L"File Details",
                                  L"Select one or more images to inspect camera, EXIF, and other image metadata.",
                                  L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        const auto& items = browserModel_->Items();
        std::vector<browser::BrowserItem> selectedItems;
        selectedItems.reserve(selectedModelIndices.size());
        std::vector<std::shared_ptr<const services::ImageMetadata>> metadataList;
        metadataList.reserve(selectedModelIndices.size());
        bool allMetadataLoaded = true;
        std::uint64_t selectedBytes = 0;

        for (const int modelIndex : selectedModelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
            {
                continue;
            }

            selectedItems.push_back(items[static_cast<std::size_t>(modelIndex)]);
            selectedBytes += items[static_cast<std::size_t>(modelIndex)].fileSizeBytes;
            const auto metadata = browserPaneController_->FindCachedMetadataForModelIndex(modelIndex);
            metadataList.push_back(metadata);
            allMetadataLoaded = allMetadataLoaded && static_cast<bool>(metadata);
        }

        browserPaneController_->RequestMetadataForModelIndices(selectedModelIndices);

        if (selectedItems.empty())
        {
            ApplyDetailsPanelText(L"File Details", L"Select one or more images to inspect metadata.", L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        if (selectedItems.size() == 1)
        {
            const browser::BrowserItem& item = selectedItems.front();
            const auto metadata = metadataList.front();

            const std::wstring summary = BuildSingleSelectionSummary(item);

            std::wstring body = metadata
                ? services::FormatImageMetadataReport(item, *metadata)
                : L"Loading detailed metadata...";

            if (userMetadataStore_)
            {
                const services::UserMetadataEntry metadataEntry = userMetadataStore_->EntryForPath(item.filePath);
                if (metadataEntry.rating > 0 || !metadataEntry.tags.empty())
                {
                    if (!body.empty())
                    {
                        body.append(L"\r\n\r\n");
                    }
                    body.append(L"User Metadata\r\n");
                    if (metadataEntry.rating > 0)
                    {
                        AppendLabeledLine(&body, L"Rating: ", FormatRatingForDisplay(metadataEntry.rating));
                    }
                    if (!metadataEntry.tags.empty())
                    {
                        AppendLabeledLine(&body, L"Tags: ", metadataEntry.tags);
                    }
                }
            }

            ApplyDetailsPanelText(item.fileName, std::move(summary), std::move(body));
            RequestDetailsPanelHistogram(item, selectedModelIndices.front());
            LayoutChildren();
            return;
        }

        std::wstring body;
        body.reserve(2048);
        body.append(L"Common Attributes\r\n");
        bool hasCommonAttributes = false;

        std::wstring commonValue;
        if (TryGetCommonItemString(selectedItems, [](const browser::BrowserItem& item) { return item.fileType; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Type: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonDimensions(selectedItems, &commonValue))
        {
            AppendLabeledLine(&body, L"Dimensions: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return BuildCameraSummaryLabel(metadata); }, &commonValue))
        {
            AppendLabeledLine(&body, L"Camera: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.dateTaken; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Date Taken: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.exposureTime; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Exposure: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.fNumber; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Aperture: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.isoSpeed; }, &commonValue))
        {
            AppendLabeledLine(&body, L"ISO: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.focalLength; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Focal Length: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.title; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Title: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.author; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Author: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.keywords; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Keywords: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.comment; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Comment: ", commonValue);
            hasCommonAttributes = true;
        }

        bool wroteSelectionMetadataHeader = false;
        if (userMetadataStore_)
        {
            bool firstRating = true;
            int selectionRating = 0;
            bool mixedRating = false;
            bool firstTags = true;
            std::wstring selectionTags;
            bool mixedTags = false;

            for (const browser::BrowserItem& item : selectedItems)
            {
                const services::UserMetadataEntry metadataEntry = userMetadataStore_->EntryForPath(item.filePath);
                const int candidateRating = std::clamp(metadataEntry.rating, 0, 5);
                if (firstRating)
                {
                    selectionRating = candidateRating;
                    firstRating = false;
                }
                else if (candidateRating != selectionRating)
                {
                    mixedRating = true;
                }

                if (firstTags)
                {
                    selectionTags = metadataEntry.tags;
                    firstTags = false;
                }
                else if (!util::EqualsIgnoreCaseOrdinal(selectionTags, metadataEntry.tags))
                {
                    mixedTags = true;
                }
            }

            const bool showRating = mixedRating || selectionRating > 0;
            const bool showTags = mixedTags || !selectionTags.empty();
            if (showRating || showTags)
            {
                body.append(L"\r\nSelection Metadata\r\n");
                wroteSelectionMetadataHeader = true;
            }
            if (showRating)
            {
                AppendLabeledLine(&body, L"Rating: ", mixedRating ? std::wstring(L"Mixed") : FormatRatingForDisplay(selectionRating));
            }
            if (showTags)
            {
                AppendLabeledLine(&body, L"Tags: ", mixedTags ? std::wstring(L"Mixed") : selectionTags);
            }
        }

        if (allMetadataLoaded)
        {
            bool wroteAdditionalHeader = false;
            std::vector<services::MetadataPropertyEntry> renderedCommonProperties;
            for (const services::MetadataPropertyEntry& property : FindCommonMetadataProperties(metadataList))
            {
                if (property.value.empty() || IsCuratedDetailsProperty(property.canonicalName))
                {
                    continue;
                }

                if (HasEquivalentDisplayedProperty(renderedCommonProperties, property))
                {
                    continue;
                }

                if (!wroteAdditionalHeader)
                {
                    body.append(L"\r\nCommon Additional Metadata\r\n");
                    wroteAdditionalHeader = true;
                }
                AppendLabeledLine(&body, property.displayName + L": ", property.value);
                renderedCommonProperties.push_back(property);
                hasCommonAttributes = true;
            }
        }
        else
        {
            body.append(L"\r\nLoading detailed metadata for the current selection...\r\n");
        }

        if (!hasCommonAttributes && !wroteSelectionMetadataHeader)
        {
            body.append(L"No common file or metadata attributes are shared by the current selection.");
        }

        ApplyDetailsPanelText(std::to_wstring(selectedItems.size()) + L" Files Selected",
                              std::to_wstring(selectedItems.size()) + L" items | " + browser::FormatByteSize(selectedBytes),
                              std::move(body));
        ResetDetailsPanelHistogram();
        LayoutChildren();
    }

    void MainWindow::PaintDetailsPanel(HDC hdc, const RECT& clientRect) const
    {
        (void)clientRect;
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelRect_))
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        FillRect(hdc,
                 &detailsPanelRect_,
                 detailsPanelBrush_ ? detailsPanelBrush_ : (backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)));

        HPEN borderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, detailsPanelRect_.left, detailsPanelRect_.top, nullptr);
        LineTo(hdc, detailsPanelRect_.left, detailsPanelRect_.bottom);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        const int innerLeft = detailsPanelRect_.left + kDetailsPanelMargin;
        const int innerRight = detailsPanelRect_.right - kDetailsPanelMargin;
        const int innerWidth = std::max(0, innerRight - innerLeft);

        const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
        const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                       title,
                                                       innerWidth,
                                                       DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                       22);
        RECT titleRect{innerLeft, detailsPanelRect_.top + kDetailsPanelMargin, innerRight, detailsPanelRect_.top + kDetailsPanelMargin + titleHeight};

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, detailsPanelTitleFont_ ? detailsPanelTitleFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SetTextColor(hdc, palette.text);
        DrawTextW(hdc, title.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);

        int summaryTop = titleRect.bottom + 6;
        if (!detailsPanelSummaryText_.empty())
        {
            const int summaryHeight = MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                                             detailsPanelSummaryText_,
                                                             innerWidth,
                                                             DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                             18);
            RECT summaryRect{innerLeft, summaryTop, innerRight, summaryTop + summaryHeight};
            SelectObject(hdc, detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
            SetTextColor(hdc, palette.mutedText);
            DrawTextW(hdc, detailsPanelSummaryText_.c_str(), -1, &summaryRect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
        }

        if ((detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_) && !IsRectEmpty(&detailsPanelHistogramRect_))
        {
            const COLORREF histogramBackground = BlendColor(palette.actionFieldBackground, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 24 : 12);
            HBRUSH histogramBrush = CreateSolidBrush(histogramBackground);
            FillRect(hdc, &detailsPanelHistogramRect_, histogramBrush);
            DeleteObject(histogramBrush);

            HPEN histogramBorderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
            oldPen = SelectObject(hdc, histogramBorderPen);
            MoveToEx(hdc, detailsPanelHistogramRect_.left, detailsPanelHistogramRect_.top, nullptr);
            LineTo(hdc, detailsPanelHistogramRect_.right, detailsPanelHistogramRect_.top);
            LineTo(hdc, detailsPanelHistogramRect_.right, detailsPanelHistogramRect_.bottom);
            LineTo(hdc, detailsPanelHistogramRect_.left, detailsPanelHistogramRect_.bottom);
            LineTo(hdc, detailsPanelHistogramRect_.left, detailsPanelHistogramRect_.top);
            SelectObject(hdc, oldPen);
            DeleteObject(histogramBorderPen);

            RECT histogramTextRect = detailsPanelHistogramRect_;
            InflateRect(&histogramTextRect, -8, -8);
            SelectObject(hdc, detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));

            if (detailsPanelHistogramLoading_)
            {
                SetTextColor(hdc, palette.mutedText);
                DrawTextW(hdc, L"Loading histogram...", -1, &histogramTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else if (!detailsPanelHistogramVisible_ || detailsPanelHistogramPeak_ == 0)
            {
                SetTextColor(hdc, palette.mutedText);
                DrawTextW(hdc, L"Histogram unavailable", -1, &histogramTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else
            {
                const int chartLeft = detailsPanelHistogramRect_.left + 6;
                const int chartTop = detailsPanelHistogramRect_.top + 6;
                const int chartRight = detailsPanelHistogramRect_.right - 6;
                const int chartBottom = detailsPanelHistogramRect_.bottom - 6;
                const int chartWidth = std::max(1, chartRight - chartLeft);
                const int chartHeight = std::max(1, chartBottom - chartTop);

                auto drawChannel = [&](const std::array<std::uint32_t, 64>& values, COLORREF color)
                {
                    HPEN channelPen = CreatePen(PS_SOLID, 1, color);
                    HGDIOBJ oldChannelPen = SelectObject(hdc, channelPen);
                    for (int index = 0; index < kDetailsPanelHistogramBins; ++index)
                    {
                        const int x = chartLeft + MulDiv(index, chartWidth - 1, kDetailsPanelHistogramBins - 1);
                        const int valueHeight = detailsPanelHistogramPeak_ > 0
                            ? MulDiv(static_cast<int>(values[static_cast<std::size_t>(index)]), chartHeight - 1, static_cast<int>(detailsPanelHistogramPeak_))
                            : 0;
                        const int y = chartBottom - valueHeight;
                        if (index == 0)
                        {
                            MoveToEx(hdc, x, y, nullptr);
                        }
                        else
                        {
                            LineTo(hdc, x, y);
                        }
                    }
                    SelectObject(hdc, oldChannelPen);
                    DeleteObject(channelPen);
                };

                drawChannel(detailsPanelHistogramRed_, RGB(224, 98, 92));
                drawChannel(detailsPanelHistogramGreen_, RGB(112, 188, 102));
                drawChannel(detailsPanelHistogramBlue_, RGB(92, 150, 232));
            }
        }

        SelectObject(hdc, oldFont);
    }

    void MainWindow::RefreshBrowserPane()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->SetSortMode(sortMode_);
        browserPaneController_->SetSortAscending(sortAscending_);
        browserPaneController_->RefreshFromModel();
        UpdateDetailsPanel();
    }

    void MainWindow::OpenItemInViewer(int modelIndex, bool preferSecondaryMonitor)
    {
        if (!browserModel_ || !browserPaneController_ || !viewerWindow_)
        {
            return;
        }

        const auto& modelItems = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
        {
            return;
        }

        std::vector<int> orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        std::vector<browser::BrowserItem> viewerItems;
        viewerItems.reserve(orderedModelIndices.size());
        int selectedViewerIndex = -1;
        for (int orderedModelIndex : orderedModelIndices)
        {
            if (orderedModelIndex < 0 || orderedModelIndex >= static_cast<int>(modelItems.size()))
            {
                continue;
            }

            viewerItems.push_back(modelItems[static_cast<std::size_t>(orderedModelIndex)]);
            if (orderedModelIndex == modelIndex)
            {
                selectedViewerIndex = static_cast<int>(viewerItems.size()) - 1;
            }
        }

        if (selectedViewerIndex < 0)
        {
            return;
        }

        OpenItemsInViewer(std::move(viewerItems), selectedViewerIndex, false, preferSecondaryMonitor);
    }

    bool MainWindow::OpenItemsInViewer(std::vector<browser::BrowserItem> items,
                                       int selectedIndex,
                                       bool startSlideshow,
                                       bool preferSecondaryMonitor)
    {
        if (!viewerWindow_ || items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return false;
        }

        const HMONITOR targetMonitor = ResolveViewerMonitor(hwnd_, preferSecondaryMonitor);
        if (preferSecondaryMonitor && !targetMonitor)
        {
            MessageBoxW(hwnd_,
                        L"A secondary monitor is not currently available.",
                        L"View on Secondary Monitor",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        ApplyViewerMouseWheelSetting();
        ApplyViewerTransitionSettings();
        if (viewerWindow_->Open(hwnd_, std::move(items), selectedIndex, themeMode_ == ThemeMode::Dark, targetMonitor))
        {
            if (startSlideshow)
            {
                viewerWindow_->StartSlideshow(slideshowIntervalMs_);
            }
            viewerWindowActive_ = true;
            UpdateStatusText();
            return true;
        }

        return false;
    }

    std::vector<browser::BrowserItem> MainWindow::CollectItemsForScope(bool selectionScope) const
    {
        std::vector<browser::BrowserItem> items;
        if (!browserModel_)
        {
            return items;
        }

        const auto& modelItems = browserModel_->Items();
        if (selectionScope && browserPaneController_)
        {
            for (const int modelIndex : browserPaneController_->OrderedSelectedModelIndicesSnapshot())
            {
                if (modelIndex >= 0 && modelIndex < static_cast<int>(modelItems.size()))
                {
                    items.push_back(modelItems[static_cast<std::size_t>(modelIndex)]);
                }
            }
            return items;
        }

        std::vector<int> orderedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedModelIndicesSnapshot()
            : std::vector<int>{};
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        for (const int modelIndex : orderedModelIndices)
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(modelItems.size()))
            {
                items.push_back(modelItems[static_cast<std::size_t>(modelIndex)]);
            }
        }

        return items;
    }

    std::vector<std::wstring> MainWindow::SelectedFileOperationPathsSnapshot(std::size_t* pairedCompanionCount) const
    {
        if (pairedCompanionCount)
        {
            *pairedCompanionCount = 0;
        }

        if (!browserPaneController_)
        {
            return {};
        }

        std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (!rawJpegPairedOperationsEnabled_ || !browserModel_ || selectedPaths.empty())
        {
            return selectedPaths;
        }

        const auto& modelItems = browserModel_->Items();
        std::vector<std::wstring> expandedPaths = selectedPaths;
        for (const std::wstring& selectedPath : selectedPaths)
        {
            const int modelIndex = browserModel_->FindItemIndexByPath(selectedPath);
            if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
            {
                continue;
            }

            const browser::BrowserItem& selectedItem = modelItems[static_cast<std::size_t>(modelIndex)];
            const bool selectedIsRaw = decode::IsRawFileType(selectedItem.fileType);
            const bool selectedIsJpeg = IsJpegFileType(selectedItem.fileType);
            if (!selectedIsRaw && !selectedIsJpeg)
            {
                continue;
            }

            const fs::path selectedFsPath(selectedItem.filePath);
            const std::wstring selectedParent = selectedFsPath.parent_path().wstring();
            const std::wstring selectedStem = selectedFsPath.stem().wstring();
            for (const browser::BrowserItem& candidate : modelItems)
            {
                if (browser::FilePathsEqual(candidate.filePath, selectedItem.filePath))
                {
                    continue;
                }

                if (!FolderPathsEqual(fs::path(candidate.filePath).parent_path().wstring(), selectedParent)
                    || !StringsEqualInsensitive(fs::path(candidate.filePath).stem().wstring(), selectedStem))
                {
                    continue;
                }

                const bool candidateIsRaw = decode::IsRawFileType(candidate.fileType);
                const bool candidateIsJpeg = IsJpegFileType(candidate.fileType);
                if (!((selectedIsRaw && candidateIsJpeg) || (selectedIsJpeg && candidateIsRaw)))
                {
                    continue;
                }

                const auto existing = std::find_if(expandedPaths.begin(), expandedPaths.end(), [&](const std::wstring& existingPath)
                {
                    return browser::FilePathsEqual(existingPath, candidate.filePath);
                });
                if (existing == expandedPaths.end())
                {
                    expandedPaths.push_back(candidate.filePath);
                }
            }
        }

        if (pairedCompanionCount && expandedPaths.size() > selectedPaths.size())
        {
            *pairedCompanionCount = expandedPaths.size() - selectedPaths.size();
        }

        return expandedPaths;
    }

    void MainWindow::OpenFolder()
    {
        std::wstring folderPath;
        if (!ChooseFolder(&folderPath) || folderPath.empty())
        {
            return;
        }

        LoadFolderAsync(std::move(folderPath));
    }

    bool MainWindow::ChooseFolder(std::wstring* folderPath) const
    {
        if (!folderPath)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,
                                          nullptr,
                                          CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result) || !dialog)
        {
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        result = dialog->Show(hwnd_);
        if (FAILED(result))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellItem> shellItem;
        result = dialog->GetResult(&shellItem);
        if (FAILED(result) || !shellItem)
        {
            return false;
        }

        PWSTR rawFolderPath = nullptr;
        result = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &rawFolderPath);
        if (FAILED(result) || !rawFolderPath)
        {
            return false;
        }

        *folderPath = rawFolderPath;
        CoTaskMemFree(rawFolderPath);
        return true;
    }

    bool MainWindow::HasSelectedJpegItems() const
    {
        for (const browser::BrowserItem& item : CollectItemsForScope(true))
        {
            if (IsJpegBrowserItem(item))
            {
                return true;
            }
        }

        return false;
    }

    void MainWindow::StartCompareSelected()
    {
        if (!browserModel_ || !browserPaneController_ || !viewerWindow_)
        {
            return;
        }

        if (browserPaneController_->SelectedCount() != 2)
        {
            MessageBoxW(hwnd_, L"Select exactly two images to compare.", L"Compare Selected", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(true);
        if (items.size() != 2)
        {
            return;
        }

        int selectedIndex = 0;
        const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
        const auto& modelItems = browserModel_->Items();
        if (primaryModelIndex >= 0 && primaryModelIndex < static_cast<int>(modelItems.size()))
        {
            const std::wstring& primaryPath = modelItems[static_cast<std::size_t>(primaryModelIndex)].filePath;
            if (browser::FilePathsEqual(items[1].filePath, primaryPath))
            {
                selectedIndex = 1;
            }
        }

        const viewer::CompareDirection compareDirection = selectedIndex == 0
            ? viewer::CompareDirection::Next
            : viewer::CompareDirection::Previous;
        if (OpenItemsInViewer(std::move(items), selectedIndex, false))
        {
            viewerWindow_->SetCompareMode(true, compareDirection);
        }
    }

    bool MainWindow::IsFavoriteDestination(std::wstring_view folderPath) const
    {
        return std::any_of(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });
    }

    std::vector<std::wstring> MainWindow::RecentDestinationShortcutPaths() const
    {
        std::vector<std::wstring> paths;
        for (const std::wstring& recentPath : recentDestinationFolders_)
        {
            if (std::any_of(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& favoritePath)
            {
                return FolderPathsEqual(favoritePath, recentPath);
            }))
            {
                continue;
            }

            InsertFolderPath(&paths, recentPath, kQuickAccessFolderLimit, false);
        }

        return paths;
    }

    void MainWindow::ToggleCurrentFolderFavoriteDestination()
    {
        if (!browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        const std::wstring folderPath = NormalizeFolderPath(browserModel_->FolderPath());
        const auto existing = std::find_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });

        if (existing != favoriteDestinationFolders_.end())
        {
            favoriteDestinationFolders_.erase(existing);
        }
        else
        {
            InsertFolderPath(&favoriteDestinationFolders_, folderPath, kQuickAccessFolderLimit, true);
        }

        UpdateMenuState();
    }

    void MainWindow::StartSelectionFileOperationToDestination(services::FileOperationType type, std::wstring destinationFolder)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_,
                        L"Select one or more images first.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const services::FileConflictPlan conflictPlan = services::PlanDestinationConflicts(
            sourcePaths,
            destinationFolder,
            services::FileConflictPolicy::OverwriteExisting);

        services::FileConflictPolicy conflictPolicy = services::FileConflictPolicy::PromptShell;
        std::vector<std::wstring> targetLeafNames;
        if (!PromptForFileConflictPolicy(hwnd_, type, conflictPlan.conflictCount, &conflictPolicy))
        {
            return;
        }

        if (conflictPolicy == services::FileConflictPolicy::AutoRenameNumericSuffix)
        {
            targetLeafNames = services::PlanDestinationConflicts(
                sourcePaths,
                destinationFolder,
                conflictPolicy).targetLeafNames;
        }

        StartFileOperation(type,
                           std::vector<std::wstring>(sourcePaths),
                           std::move(destinationFolder),
                           conflictPolicy,
                           std::move(targetLeafNames));
    }

    void MainWindow::ShowBrowserContextMenu(POINT screenPoint)
    {
        if (!hwnd_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSingleSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 1;
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool allowMutatingFileCommands = hasSelection && !fileOperationActive_;
        const int commonSelectionRating = hasSelection ? CommonSelectionRating() : -1;
        const bool allowRenameSelected = hasSingleSelection && !fileOperationActive_;
        const bool hasSecondaryMonitor = FindAlternateMonitorForWindow(hwnd_) != nullptr;

        HMENU menu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU ratingMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        if (!menu || !batchConvertSelectionMenu || !ratingMenu || !sortMenu)
        {
            if (sortMenu)
            {
                DestroyMenu(sortMenu);
            }
            if (ratingMenu)
            {
                DestroyMenu(ratingMenu);
            }
            if (batchConvertSelectionMenu)
            {
                DestroyMenu(batchConvertSelectionMenu);
            }
            if (menu)
            {
                DestroyMenu(menu);
            }
            return;
        }

        AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
        AppendMenuW(menu, MF_STRING, ID_FILE_COMPARE_SELECTED, L"&Compare Selected");
        AppendMenuW(menu, MF_STRING, ID_FILE_VIEW_ON_SECONDARY_MONITOR, L"View on Secondary &Monitor");
        AppendMenuW(menu, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information");
        AppendMenuW(menu, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer");
        AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
        AppendMenuW(menu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h");
        AppendMenuW(menu, MF_STRING, ID_FILE_RENAME_SELECTED, L"Re&name...");
        AppendMenuW(menu, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_0, L"&Clear Rating");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_1, L"&1 Star");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_2, L"&2 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_3, L"&3 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_4, L"&4 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_5, L"&5 Stars");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set &Rating");
        AppendMenuW(menu, MF_STRING, ID_FILE_EDIT_TAGS, L"Edit &Tags...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_FILE_COPY_SELECTION, L"Cop&y Selection...");
        AppendMenuW(menu, MF_STRING, ID_FILE_MOVE_SELECTION, L"Mo&ve Selection...");
        AppendMenuW(menu, MF_STRING, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS, L"Include Paired &RAW+JPEG");
        AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete");
        AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"Slideshow from &Selection");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
        AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
        AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode");
        AppendMenuW(menu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date &Taken");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"&Descending");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_FOLDER, L"Open &Folder...");
        AppendMenuW(menu, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree");

        EnableMenuItem(menu, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_COMPARE_SELECTED,
                   MF_BYCOMMAND | ((browserPaneController_ && browserPaneController_->SelectedCount() == 2) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_VIEW_ON_SECONDARY_MONITOR,
                   MF_BYCOMMAND | ((hasSelection && hasSecondaryMonitor) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_COPY_PATH, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_RENAME_SELECTED, MF_BYCOMMAND | (allowRenameSelected ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_PROPERTIES, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_EDIT_TAGS, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        for (UINT ratingCommandId = ID_FILE_SET_RATING_0; ratingCommandId <= ID_FILE_SET_RATING_5; ++ratingCommandId)
        {
            EnableMenuItem(ratingMenu, ratingCommandId, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(ratingMenu,
                          ratingCommandId,
                          MF_BYCOMMAND | (commonSelectionRating >= 0 && ratingCommandId == CommandIdFromRating(commonSelectionRating)
                              ? MF_CHECKED
                              : MF_UNCHECKED));
        }
        EnableMenuItem(menu, ID_FILE_COPY_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_MOVE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS,
                  MF_BYCOMMAND | (rawJpegPairedOperationsEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu, ID_FILE_DELETE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_DELETE_SELECTION_PERMANENT, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, ID_VIEW_DETAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_FILENAME, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_MODIFIED, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_SIZE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_DIMENSIONS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_TYPE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_RANDOM, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(sortMenu, ID_VIEW_SORT_DATETAKEN, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            menu,
            ID_VIEW_THUMBNAILS,
            ID_VIEW_DETAILS,
            browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
            MF_BYCOMMAND);

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        const bool sortAscending = browserPaneController_
            ? browserPaneController_->IsSortAscending()
            : true;
        CheckMenuRadioItem(
            sortMenu,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_RANDOM,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);
        CheckMenuItem(
            sortMenu,
            ID_VIEW_SORT_DIRECTION,
            MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

        SetForegroundWindow(hwnd_);
        TrackPopupMenu(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            screenPoint.x,
            screenPoint.y,
            0,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void MainWindow::ShowFolderTreeContextMenu(POINT screenPoint, HTREEITEM item)
    {
        if (!hwnd_ || !treePane_ || !item)
        {
            return;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->path.empty() || !TreeView_GetParent(treePane_, item))
        {
            return;
        }

        const std::wstring folderPath = nodeData->path;
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        constexpr UINT kRenameFolderCommandId = 1;
        constexpr UINT kDeleteFolderCommandId = 2;
        constexpr UINT kDeleteFolderPermanentCommandId = 3;

        AppendMenuW(menu, MF_STRING, kRenameFolderCommandId, L"Re&name Folder...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kDeleteFolderCommandId, L"&Delete Folder");
        AppendMenuW(menu, MF_STRING, kDeleteFolderPermanentCommandId, L"Delete Folder &Permanently");

        const UINT enableState = fileOperationActive_ ? MF_GRAYED : MF_ENABLED;
        EnableMenuItem(menu, kRenameFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kDeleteFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kDeleteFolderPermanentCommandId, MF_BYCOMMAND | enableState);

        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);

        switch (commandId)
        {
        case kRenameFolderCommandId:
            StartFolderTreeRename(folderPath);
            break;
        case kDeleteFolderCommandId:
            StartFolderTreeDelete(folderPath, false);
            break;
        case kDeleteFolderPermanentCommandId:
            StartFolderTreeDelete(folderPath, true);
            break;
        default:
            break;
        }
    }

    void MainWindow::ShowDiagnosticsSnapshot()
    {
        if (!diagnosticsWindow_)
        {
            return;
        }

        diagnosticsWindow_->Show(
            hwnd_,
            decode::DescribeJpegAccelerationState(),
            decode::DescribeRawDecodingState(),
            browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"),
            util::CaptureDiagnosticsSnapshot(),
            themeMode_ == ThemeMode::Dark);
        util::LogInfo(L"Opened diagnostics snapshot window.");
    }

    void MainWindow::ShowAboutDialog() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kAboutDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &AboutDialogProc;
            windowClass.hInstance = instance_;
            windowClass.lpszClassName = kAboutDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return;
            }
        }

        const ThemePalette palette = GetThemePalette();

        AboutDialogState state;
        state.ownerWindow = hwnd_;
        state.instance = instance_;
        state.darkMode = themeMode_ == ThemeMode::Dark;
        state.background = palette.windowBackground;
        state.headerBackground = BlendColor(palette.actionStripBackground, palette.accent, state.darkMode ? 28 : 18);
        state.footerBackground = BlendColor(palette.paneBackground, palette.windowBackground, state.darkMode ? 38 : 58);
        state.panelBackground = palette.paneBackground;
        state.border = palette.actionStripBorder;
        state.text = palette.text;
        state.mutedText = palette.mutedText;
        state.accent = palette.accent;
        state.title = hyperbrowse::build::kDisplayName;
        state.subtitle = L"High-performance native image browser for Windows";
        state.intro = L"High-performance browsing and viewing for large Windows image folders.";
        state.bodyHeading = L"What sets it apart";
        state.bodyContent =
            L"- Native Win32 shell tuned for fast startup, compact chrome, and direct file-system browsing.\r\n"
            L"- Async folder enumeration, incremental folder watching, and responsive refresh in large image collections.\r\n"
            L"- Virtualized thumbnail and details views with filtering, metadata-aware sorting, and efficient multi-selection workflows.\r\n"
            L"- Broad format coverage across JPEG, PNG, GIF, TIFF, and major RAW camera formats with graceful fallback behavior.\r\n"
            L"- Dedicated viewer window with slideshow playback, transition effects, overlays, full-screen viewing, and background prefetch.\r\n"
            L"- Practical photographer workflows including batch convert, lossless JPEG orientation adjustment, and Explorer-friendly file operations.\r\n"
            L"- Optional NVIDIA-accelerated JPEG decoding when available, while preserving correctness on systems without GPU acceleration.";
        state.footer =
            L"Copyright (c) "
            + std::to_wstring(CurrentCalendarYear())
            + L" Michael A. McCloskey\r\nLicensed under the MIT License.";
        state.brandArt = util::LoadPngResourceBitmap(instance_, IDB_HYPERBROWSE_BRAND_PNG, kAboutDialogBrandArtSize, kAboutDialogBrandArtSize);
        state.heroIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        state.windowIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        state.titleFont = CreateDialogUiFont(21, FW_BOLD);
        state.subtitleFont = CreateDialogUiFont(11, FW_SEMIBOLD);
        state.bodyFont = CreateDialogUiFont(10, FW_NORMAL);
        state.footerFont = CreateDialogUiFont(9, FW_NORMAL);

        if (!state.titleFont) state.titleFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.subtitleFont) state.subtitleFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.bodyFont) state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.footerFont) state.footerFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        state.githubButtonWidth = MeasureAboutDialogLinkButtonWidth(state.subtitleFont,
                                         kAboutDialogGitHubLabel,
                                         kAboutDialogSupportLabel);
        state.supportButtonWidth = state.githubButtonWidth;

        const int aboutClientHeight = std::max(kAboutDialogHeight, MeasureAboutDialogClientHeight(state));
        RECT windowRect{0, 0, kAboutDialogWidth, aboutClientHeight};
        AdjustWindowRectEx(&windowRect,
                           WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                           FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (hwnd_)
        {
            EnableWindow(hwnd_, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kAboutDialogClassName,
            L"About HyperBrowse",
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            hwnd_,
            nullptr,
            instance_,
            &state);

        if (!dialogWindow)
        {
            if (hwnd_)
            {
                EnableWindow(hwnd_, TRUE);
            }
            DeleteFontIfOwned(state.titleFont);
            DeleteFontIfOwned(state.subtitleFont);
            DeleteFontIfOwned(state.bodyFont);
            DeleteFontIfOwned(state.footerFont);
            if (state.heroIcon) DestroyIcon(state.heroIcon);
            if (state.windowIcon) DestroyIcon(state.windowIcon);
            return;
        }

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (hwnd_)
        {
            EnableWindow(hwnd_, TRUE);
            SetForegroundWindow(hwnd_);
            SetActiveWindow(hwnd_);
        }

        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.subtitleFont);
        DeleteFontIfOwned(state.bodyFont);
        DeleteFontIfOwned(state.footerFont);
        if (state.heroIcon) DestroyIcon(state.heroIcon);
        if (state.windowIcon) DestroyIcon(state.windowIcon);
    }

    void MainWindow::ResetDiagnosticsState()
    {
        util::ResetDiagnostics();
        util::LogInfo(L"Diagnostics timings and counters were reset.");

        if (diagnosticsWindow_ && diagnosticsWindow_->IsOpen())
        {
            diagnosticsWindow_->Show(
                hwnd_,
                decode::DescribeJpegAccelerationState(),
                decode::DescribeRawDecodingState(),
                browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"),
                util::CaptureDiagnosticsSnapshot(),
                themeMode_ == ThemeMode::Dark);
        }

        MessageBoxW(hwnd_, L"Diagnostics timings and counters were reset.", L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ShowImageInformation()
    {
        if (!browserPaneController_)
        {
            return;
        }

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        if (modelIndex < 0)
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Image Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& items = browserModel_->Items();
        if (modelIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        const browser::BrowserItem& item = items[static_cast<std::size_t>(modelIndex)];
        std::wstring errorMessage;
        const auto metadata = browserPaneController_->FindCachedMetadataForModelIndex(modelIndex)
            ? browserPaneController_->FindCachedMetadataForModelIndex(modelIndex)
            : services::ExtractImageMetadata(item, &errorMessage);

        if (!metadata)
        {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"No metadata is available for the selected image." : errorMessage.c_str(),
                        L"Image Information",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring content = services::FormatImageInfoContent(item);
        const std::wstring expanded = services::FormatImageInfoExpanded(*metadata);

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = hwnd_;
        config.hInstance = instance_;
        config.dwFlags = TDF_EXPAND_FOOTER_AREA;
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        config.pszWindowTitle = L"Image Information";
        config.pszMainIcon = MAKEINTRESOURCEW(IDI_HYPERBROWSE);
        config.pszMainInstruction = item.fileName.c_str();
        config.pszContent = content.c_str();
        config.pszExpandedInformation = expanded.c_str();
        config.pszCollapsedControlText = L"Show EXIF / IPTC details";
        config.pszExpandedControlText = L"Hide EXIF / IPTC details";

        TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
    }

    void MainWindow::StartCopySelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartSelectionFileOperationToDestination(services::FileOperationType::Copy, std::move(destinationFolder));
    }

    void MainWindow::StartRenameSelectedImage()
    {
        if (!browserModel_ || !browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedCount() != 1)
        {
            MessageBoxW(hwnd_, L"Select a single image to rename.", L"Rename Image", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        const auto& items = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        const browser::BrowserItem& item = items[static_cast<std::size_t>(modelIndex)];
        std::wstring renamedLeafName;
        if (!PromptForRenameLeafName(hwnd_,
                                     instance_,
                                     L"Rename Image",
                                     L"Enter a new file name.",
                                     item.fileName,
                                     true,
                                     &renamedLeafName))
        {
            return;
        }

        StartFileOperation(services::FileOperationType::Rename,
                           {item.filePath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {renamedLeafName});
    }

    void MainWindow::StartMoveSelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Move Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartSelectionFileOperationToDestination(services::FileOperationType::Move, std::move(destinationFolder));
    }

    void MainWindow::StartDeleteSelection(bool permanent)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", permanent ? L"Permanent Delete" : L"Delete", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const bool bypassConfirmation = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!bypassConfirmation && !ConfirmFileDeletion(hwnd_, sourcePaths.size(), permanent))
        {
            return;
        }

        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           std::vector<std::wstring>(sourcePaths),
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::StartFolderTreeRename(std::wstring folderPath)
    {
        if (folderPath.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        std::error_code error;
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_, L"The selected folder is no longer available.", L"Rename Folder", MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        const std::wstring currentLeafName = fs::path(folderPath).filename().wstring();
        std::wstring renamedLeafName;
        if (!PromptForRenameLeafName(hwnd_,
                                     instance_,
                                     L"Rename Folder",
                                     L"Enter a new folder name.",
                                     currentLeafName,
                                     false,
                                     &renamedLeafName))
        {
            return;
        }

        activeTreeFolderRenamePath_ = folderPath;
        StartFileOperation(services::FileOperationType::Rename,
                           {folderPath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {renamedLeafName});
    }

    void MainWindow::StartFolderTreeDelete(std::wstring folderPath, bool permanent)
    {
        if (folderPath.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));

        std::error_code error;
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        permanent ? L"Permanent Delete Folder" : L"Delete Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        const bool bypassConfirmation = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!bypassConfirmation && !ConfirmFolderDeletion(hwnd_, folderPath, permanent))
        {
            return;
        }

        activeTreeFolderOperationPath_ = folderPath;
        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           {folderPath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::StartFileOperation(services::FileOperationType type,
                                        std::vector<std::wstring> sourcePaths,
                                        std::wstring destinationFolder,
                                        services::FileConflictPolicy conflictPolicy,
                                        std::vector<std::wstring> targetLeafNames)
    {
        if (!fileOperationService_ || sourcePaths.empty() || fileOperationActive_)
        {
            return;
        }

        activeFileOperationLabel_ = services::FileOperationTypeToActivityLabel(type);
        activeFileOperationLabel_.append(L" ");
        activeFileOperationLabel_.append(std::to_wstring(sourcePaths.size()));
        activeFileOperationLabel_.append(L" item(s)");
        fileOperationActive_ = true;
        activeFileOperationRequestId_ = fileOperationService_->Start(
            hwnd_,
            hwnd_,
            type,
            std::move(sourcePaths),
            std::move(destinationFolder),
            conflictPolicy,
            std::move(targetLeafNames));
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::RevealSelectedInExplorer() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Reveal in Explorer", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!RevealPathsInExplorer(selectedPaths))
        {
            MessageBoxW(hwnd_, L"Failed to reveal the selected items in Explorer.", L"Reveal in Explorer", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::OpenSelectedContainingFolder() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            if (!selectedPaths.empty())
            {
                targetPath = selectedPaths.front();
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Open Containing Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring containingFolder = fs::path(targetPath).parent_path().wstring();
        if (!LaunchShellTarget(hwnd_, L"open", containingFolder))
        {
            MessageBoxW(hwnd_, L"Failed to open the containing folder.", L"Open Containing Folder", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectedPathsToClipboard() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Path", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!CopyTextToClipboard(hwnd_, JoinLines(selectedPaths)))
        {
            MessageBoxW(hwnd_, L"Failed to copy the selected file paths to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::ShowSelectedFileProperties() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            if (!selectedPaths.empty())
            {
                targetPath = selectedPaths.front();
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Properties", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!LaunchShellTarget(hwnd_, L"properties", targetPath))
        {
            MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::SetSelectionRating(int rating)
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Set Rating", MB_OK | MB_ICONINFORMATION);
            return;
        }

        userMetadataStore_->SetRating(selectedPaths, rating);
        UpdateDetailsPanel();
        UpdateMenuState();
    }

    void MainWindow::EditSelectionTags()
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Edit Tags", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring initialTags;
        bool firstEntry = true;
        bool mixedTags = false;
        for (const std::wstring& path : selectedPaths)
        {
            const std::wstring& tags = userMetadataStore_->EntryForPath(path).tags;
            if (firstEntry)
            {
                initialTags = tags;
                firstEntry = false;
                continue;
            }

            if (!util::EqualsIgnoreCaseOrdinal(initialTags, tags))
            {
                mixedTags = true;
                break;
            }
        }

        std::wstring editedTags = mixedTags ? std::wstring{} : initialTags;
        if (!PromptForSingleLineText(hwnd_,
                                     instance_,
                                     L"Edit Tags",
                                     L"Enter comma-separated tags for the selected images. Leave blank to clear tags.",
                                     L"Apply",
                                     editedTags,
                                     0,
                                     -1,
                                     &editedTags))
        {
            return;
        }

        userMetadataStore_->SetTags(selectedPaths, editedTags);
        UpdateDetailsPanel();
        UpdateMenuState();
    }

    int MainWindow::CommonSelectionRating() const
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return -1;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            return -1;
        }

        int commonRating = std::clamp(userMetadataStore_->EntryForPath(selectedPaths.front()).rating, 0, 5);
        for (std::size_t index = 1; index < selectedPaths.size(); ++index)
        {
            const int candidateRating = std::clamp(userMetadataStore_->EntryForPath(selectedPaths[index]).rating, 0, 5);
            if (candidateRating != commonRating)
            {
                return -1;
            }
        }

        return commonRating;
    }

    void MainWindow::StartSlideshow(bool selectionScope)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Slideshow",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        TASKDIALOG_BUTTON buttons[] = {
            {1001, L"1 second"},
            {1002, L"2 seconds"},
            {1003, L"3 seconds"},
            {1005, L"5 seconds"},
            {1010, L"10 seconds"},
        };

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = hwnd_;
        config.dwFlags = TDF_USE_COMMAND_LINKS;
        config.pszWindowTitle = L"Slideshow";
        config.pszMainInstruction = L"Choose slideshow interval";
        config.pszContent = L"Press Space in the viewer to pause/resume.";
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = slideshowIntervalMs_ <= 1000 ? 1001
            : slideshowIntervalMs_ <= 2000 ? 1002
            : slideshowIntervalMs_ <= 3000 ? 1003
            : slideshowIntervalMs_ <= 5000 ? 1005
            : 1010;

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        if (FAILED(dialogResult) || clickedButton == IDCANCEL)
        {
            return;
        }

        switch (clickedButton)
        {
        case 1001: slideshowIntervalMs_ = 1000; break;
        case 1002: slideshowIntervalMs_ = 2000; break;
        case 1003: slideshowIntervalMs_ = 3000; break;
        case 1005: slideshowIntervalMs_ = 5000; break;
        case 1010: slideshowIntervalMs_ = 10000; break;
        default:   slideshowIntervalMs_ = 3000; break;
        }

        int selectedIndex = 0;
        if (!selectionScope)
        {
            const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
            const auto orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
            for (int index = 0; index < static_cast<int>(orderedModelIndices.size()); ++index)
            {
                if (orderedModelIndices[static_cast<std::size_t>(index)] == primaryModelIndex)
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        OpenItemsInViewer(std::move(items), selectedIndex, true);
    }

    void MainWindow::StartBatchConvert(bool selectionScope, services::BatchConvertFormat format)
    {
        if (!batchConvertService_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Batch Convert",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring outputFolder;
        if (!ChooseFolder(&outputFolder) || outputFolder.empty())
        {
            return;
        }

        batchConvertOutputFolder_ = outputFolder;
        batchConvertCompleted_ = 0;
        batchConvertTotal_ = items.size();
        batchConvertFailed_ = 0;
        batchConvertCurrentFile_.clear();
        batchConvertActive_ = true;
        activeBatchConvertRequestId_ = batchConvertService_->Start(hwnd_, std::move(items), outputFolder, format);
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::AdjustSelectedJpegOrientation(int quarterTurnsDelta)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const std::vector<browser::BrowserItem> items = CollectItemsForScope(true);
        if (items.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more JPEG images first.", L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

        std::vector<std::wstring> updatedPaths;
        std::size_t successCount = 0;
        std::size_t failureCount = 0;
        for (const browser::BrowserItem& item : items)
        {
            if (!decode::IsWicFileType(item.fileType) || (_wcsicmp(item.fileType.c_str(), L"JPG") != 0 && _wcsicmp(item.fileType.c_str(), L"JPEG") != 0))
            {
                continue;
            }

            std::wstring errorMessage;
            if (services::AdjustJpegOrientation(item.filePath, quarterTurnsDelta, &errorMessage))
            {
                ++successCount;
                updatedPaths.push_back(item.filePath);
                browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(fs::path(item.filePath)));
            }
            else
            {
                ++failureCount;
            }
        }

        if (!updatedPaths.empty())
        {
            browserPaneController_->InvalidateMediaCacheForPaths(updatedPaths);
            RefreshBrowserPane();
            browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
        }

        std::wstring summary = L"Updated orientation metadata for " + std::to_wstring(successCount) + L" JPEG file(s).";
        if (failureCount > 0)
        {
            summary.append(L"\nFailed: ");
            summary.append(std::to_wstring(failureCount));
            summary.append(L".");
        }
        MessageBoxW(hwnd_, summary.c_str(), L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ApplyCompletedFileOperation(const services::FileOperationUpdate& update)
    {
        fileOperationActive_ = false;
        activeFileOperationLabel_.clear();

        const std::wstring deferredFolderWatchReloadPath = pendingFolderWatchReloadPath_;
        const bool deferredFolderWatchTreeRefresh = pendingFolderWatchTreeRefresh_;
        pendingFolderWatchReloadPath_.clear();
        pendingFolderWatchTreeRefresh_ = false;

        const std::wstring treeFolderOperationPath = activeTreeFolderOperationPath_;
        activeTreeFolderOperationPath_.clear();
        const std::wstring treeFolderRenamePath = activeTreeFolderRenamePath_;
        activeTreeFolderRenamePath_.clear();
        const bool treeFolderDeleteOperation = !treeFolderOperationPath.empty()
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);
        const bool treeFolderDeleteSucceeded = treeFolderDeleteOperation
            && std::any_of(update.succeededSourcePaths.begin(), update.succeededSourcePaths.end(), [&](const std::wstring& sourcePath)
            {
                return FolderPathsEqual(sourcePath, treeFolderOperationPath);
            });

        bool refreshFolderTree = treeFolderDeleteSucceeded;
        std::wstring fallbackFolderPath;
        if (treeFolderDeleteSucceeded
            && browserModel_
            && !browserModel_->FolderPath().empty()
            && browser::PathHasPrefix(browserModel_->FolderPath(), treeFolderOperationPath))
        {
            fallbackFolderPath = FindExistingFolderAncestor(fs::path(treeFolderOperationPath).parent_path());
        }

        std::wstring treeFolderReloadPath;
        if (!treeFolderRenamePath.empty() && update.type == services::FileOperationType::Rename)
        {
            const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
            for (std::size_t index = 0; index < renamePairCount; ++index)
            {
                if (!FolderPathsEqual(update.succeededSourcePaths[index], treeFolderRenamePath))
                {
                    continue;
                }

                refreshFolderTree = true;
                if (browserModel_ && !browserModel_->FolderPath().empty())
                {
                    if (browser::PathHasPrefix(browserModel_->FolderPath(), treeFolderRenamePath))
                    {
                        treeFolderReloadPath = RewritePathPrefix(
                            browserModel_->FolderPath(),
                            treeFolderRenamePath,
                            update.createdPaths[index]);
                    }
                    else if (browserModel_->IsRecursive()
                        && browser::PathHasPrefix(treeFolderRenamePath, browserModel_->FolderPath()))
                    {
                        treeFolderReloadPath = browserModel_->FolderPath();
                    }
                }
                break;
            }
        }

        if (userMetadataStore_)
        {
            userMetadataStore_->ApplyFileOperationUpdate(update.type, update.succeededSourcePaths, update.createdPaths);
        }

        bool reloadCurrentFolder = browserModel_
            && !browserModel_->FolderPath().empty()
            && !deferredFolderWatchReloadPath.empty()
            && FolderPathsEqual(browserModel_->FolderPath(), deferredFolderWatchReloadPath);

        if (!treeFolderReloadPath.empty())
        {
            reloadCurrentFolder = true;
        }

        if (!reloadCurrentFolder && browserModel_ && !browserModel_->FolderPath().empty())
        {
            const std::size_t affectedCount = update.succeededSourcePaths.size() + update.createdPaths.size();
            if (affectedCount >= kIncrementalFileOperationPathLimit)
            {
                const auto pathAffectsCurrentScope = [&](const std::wstring& path)
                {
                    return IsPathInCurrentScope(path)
                        || browser::PathHasPrefix(browserModel_->FolderPath(), path);
                };

                reloadCurrentFolder = std::any_of(
                    update.createdPaths.begin(),
                    update.createdPaths.end(),
                    pathAffectsCurrentScope)
                    || std::any_of(
                        update.succeededSourcePaths.begin(),
                        update.succeededSourcePaths.end(),
                        pathAffectsCurrentScope);
            }
        }

        bool modelChanged = false;
        if (!reloadCurrentFolder && browserModel_ && browserPaneController_)
        {
            std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

            if (update.type == services::FileOperationType::Rename)
            {
                const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
                for (std::size_t index = 0; index < renamePairCount; ++index)
                {
                    const std::wstring& sourcePath = update.succeededSourcePaths[index];
                    const std::wstring& createdPath = update.createdPaths[index];
                    for (std::wstring& selectedPath : selectedPaths)
                    {
                        if (browser::FilePathsEqual(selectedPath, sourcePath))
                        {
                            selectedPath = createdPath;
                        }
                    }

                    if (browser::FilePathsEqual(focusedPath, sourcePath))
                    {
                        focusedPath = createdPath;
                    }
                }
            }

            auto upsertVisiblePath = [&](const std::wstring& path)
            {
                if (!IsPathInCurrentScope(path))
                {
                    return;
                }

                const fs::path filePath(path);
                if (!browser::IsSupportedImageExtension(filePath.extension().wstring()))
                {
                    return;
                }

                std::error_code error;
                if (!fs::is_regular_file(filePath, error) || error)
                {
                    return;
                }

                modelChanged = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(filePath)) || modelChanged;
            };

            switch (update.type)
            {
            case services::FileOperationType::Copy:
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::Move:
                for (const std::wstring& sourcePath : update.succeededSourcePaths)
                {
                    modelChanged = browserModel_->RemoveItemByPath(sourcePath) || modelChanged;
                }
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::DeleteRecycleBin:
            case services::FileOperationType::DeletePermanent:
                for (const std::wstring& sourcePath : update.succeededSourcePaths)
                {
                    modelChanged = ((treeFolderDeleteOperation && FolderPathsEqual(sourcePath, treeFolderOperationPath))
                        ? browserModel_->RemoveItemsByPathPrefix(sourcePath)
                        : browserModel_->RemoveItemByPath(sourcePath)) || modelChanged;
                }
                break;
            case services::FileOperationType::Rename:
            {
                const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
                for (std::size_t index = 0; index < renamePairCount; ++index)
                {
                    const std::wstring& sourcePath = update.succeededSourcePaths[index];
                    const std::wstring& createdPath = update.createdPaths[index];
                    if (!treeFolderRenamePath.empty() && FolderPathsEqual(sourcePath, treeFolderRenamePath))
                    {
                        continue;
                    }

                    modelChanged = browserModel_->RemoveItemByPath(sourcePath) || modelChanged;
                    upsertVisiblePath(createdPath);
                }
                break;
            }
            default:
                break;
            }

            std::vector<std::wstring> affectedPaths = update.succeededSourcePaths;
            affectedPaths.insert(affectedPaths.end(), update.createdPaths.begin(), update.createdPaths.end());
            if (!affectedPaths.empty())
            {
                browserPaneController_->InvalidateMediaCacheForPaths(affectedPaths);
            }

            if (modelChanged && fallbackFolderPath.empty())
            {
                RefreshBrowserPane();
                browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
                UpdateWindowTitle();
            }
        }

        if ((update.type == services::FileOperationType::Copy || update.type == services::FileOperationType::Move)
            && !update.destinationFolder.empty())
        {
            std::error_code destinationError;
            if (fs::is_directory(fs::path(update.destinationFolder), destinationError) && !destinationError)
            {
                if (!update.succeededSourcePaths.empty() || !update.createdPaths.empty())
                {
                    RecordRecentDestination(update.destinationFolder);
                }
                InsertFolderTreeFolderIfParentLoaded(update.destinationFolder);
            }
        }

        if (refreshFolderTree || deferredFolderWatchTreeRefresh)
        {
            RefreshFolderTree();
        }

        if (!fallbackFolderPath.empty())
        {
            LoadFolderAsync(fallbackFolderPath);
        }
        else if (!treeFolderReloadPath.empty())
        {
            LoadFolderAsync(treeFolderReloadPath);
        }
        else if (reloadCurrentFolder && browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
        }

        UpdateStatusText();
        UpdateMenuState();

        if (!update.message.empty() && update.failedCount > 0)
        {
            MessageBoxW(hwnd_, update.message.c_str(), L"File Operation", MB_OK | MB_ICONWARNING);
        }
    }

    bool MainWindow::IsPathInCurrentScope(std::wstring_view path) const
    {
        if (!browserModel_ || browserModel_->FolderPath().empty() || path.empty())
        {
            return false;
        }

        if (browserModel_->IsRecursive())
        {
            return browser::PathHasPrefix(path, browserModel_->FolderPath());
        }

        return FolderPathsEqual(fs::path(path).parent_path().wstring(), browserModel_->FolderPath());
    }

    void MainWindow::ApplyFolderWatchChanges(const services::FolderWatchUpdate& update)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        if (update.requiresFullReload)
        {
            if (fileOperationActive_)
            {
                pendingFolderWatchReloadPath_ = update.folderPath.empty() ? browserModel_->FolderPath() : update.folderPath;
                pendingFolderWatchTreeRefresh_ = true;
                return;
            }

            RefreshFolderTree();
            LoadFolderAsync(update.folderPath.empty() ? browserModel_->FolderPath() : update.folderPath);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();
        std::vector<std::wstring> invalidatedPaths;
        std::vector<std::wstring> foldersToInsertIntoTree;
        bool changed = false;
        bool refreshFolderTree = false;
        bool preferAsyncReload = update.events.size() >= kIncrementalFolderWatchEventLimit;

        auto isExistingDirectory = [](const std::wstring& path)
        {
            if (path.empty())
            {
                return false;
            }

            std::error_code error;
            return fs::is_directory(fs::path(path), error) && !error;
        };

        auto upsertFromPath = [&](const std::wstring& path)
        {
            std::error_code error;
            const fs::path watchedPath(path);
            if (fs::is_regular_file(watchedPath, error) && !error)
            {
                if (browser::IsSupportedImageExtension(watchedPath.extension().wstring()))
                {
                    changed = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(watchedPath)) || changed;
                    invalidatedPaths.push_back(path);
                }
                return;
            }

            if (!recursiveBrowsingEnabled_ || !fs::is_directory(watchedPath, error) || error)
            {
                return;
            }

            preferAsyncReload = true;
        };

        for (const services::FolderWatchEvent& event : update.events)
        {
            if (event.kind == services::FolderWatchEventKind::Added && isExistingDirectory(event.path))
            {
                foldersToInsertIntoTree.push_back(event.path);
                if (recursiveBrowsingEnabled_)
                {
                    preferAsyncReload = true;
                }
            }

            if (event.kind == services::FolderWatchEventKind::Removed
                && FindFolderTreeItemByPath(event.path))
            {
                refreshFolderTree = true;
            }

            if (event.kind == services::FolderWatchEventKind::Renamed)
            {
                if (!event.oldPath.empty() && FindFolderTreeItemByPath(event.oldPath))
                {
                    refreshFolderTree = true;
                }

                if (isExistingDirectory(event.path))
                {
                    foldersToInsertIntoTree.push_back(event.path);
                    if (recursiveBrowsingEnabled_)
                    {
                        preferAsyncReload = true;
                    }
                }
            }

            switch (event.kind)
            {
            case services::FolderWatchEventKind::Added:
            case services::FolderWatchEventKind::Modified:
                upsertFromPath(event.path);
                break;
            case services::FolderWatchEventKind::Removed:
                changed = browserModel_->RemoveItemByPath(event.path) || changed;
                changed = browserModel_->RemoveItemsByPathPrefix(event.path) || changed;
                invalidatedPaths.push_back(event.path);
                break;
            case services::FolderWatchEventKind::Renamed:
            {
                const bool renamed = browserModel_->ReplacePathPrefix(event.oldPath, event.path);
                changed = renamed || changed;
                invalidatedPaths.push_back(event.oldPath);
                invalidatedPaths.push_back(event.path);
                if (!renamed)
                {
                    changed = browserModel_->RemoveItemByPath(event.oldPath) || changed;
                    changed = browserModel_->RemoveItemsByPathPrefix(event.oldPath) || changed;
                    upsertFromPath(event.path);
                }
                break;
            }
            default:
                break;
            }
        }

        if (preferAsyncReload)
        {
            if (fileOperationActive_)
            {
                pendingFolderWatchReloadPath_ = browserModel_->FolderPath();
                pendingFolderWatchTreeRefresh_ = pendingFolderWatchTreeRefresh_
                    || refreshFolderTree;
                return;
            }

            if (refreshFolderTree)
            {
                RefreshFolderTree();
            }
            else
            {
                for (const std::wstring& folderPath : foldersToInsertIntoTree)
                {
                    InsertFolderTreeFolderIfParentLoaded(folderPath);
                }
            }

            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        if (!changed && invalidatedPaths.empty())
        {
            if (refreshFolderTree)
            {
                RefreshFolderTree();
            }
            else
            {
                for (const std::wstring& folderPath : foldersToInsertIntoTree)
                {
                    InsertFolderTreeFolderIfParentLoaded(folderPath);
                }
            }
            return;
        }

        browserPaneController_->InvalidateMediaCacheForPaths(invalidatedPaths);
        RefreshBrowserPane();
        browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
        if (refreshFolderTree)
        {
            RefreshFolderTree();
        }
        else
        {
            for (const std::wstring& folderPath : foldersToInsertIntoTree)
            {
                InsertFolderTreeFolderIfParentLoaded(folderPath);
            }
        }
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::UpdateMenuState()
    {
        if (!menu_)
        {
            return;
        }

        RefreshQuickAccessMenus();

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasCompareSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 2;
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool hasSecondaryMonitor = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        const bool allowMetadataEdit = hasSelection && !fileOperationActive_;
        const int commonSelectionRating = hasSelection ? CommonSelectionRating() : -1;
        const browser::ThumbnailSizePreset thumbnailSizePreset = browserPaneController_
            ? browserPaneController_->GetThumbnailSizePreset()
            : thumbnailSizePreset_;
        const bool thumbnailDetailsVisible = browserPaneController_
            ? browserPaneController_->AreThumbnailDetailsVisible()
            : thumbnailDetailsVisible_;

        EnableMenuItem(menu_, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COMPARE_SELECTED, MF_BYCOMMAND | (hasCompareSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_VIEW_ON_SECONDARY_MONITOR,
                   MF_BYCOMMAND | ((hasSelection && hasSecondaryMonitor) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_PATH, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_PROPERTIES, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_EDIT_TAGS, MF_BYCOMMAND | (allowMetadataEdit ? MF_ENABLED : MF_GRAYED));
        for (UINT ratingCommandId = ID_FILE_SET_RATING_0; ratingCommandId <= ID_FILE_SET_RATING_5; ++ratingCommandId)
        {
            EnableMenuItem(menu_, ratingCommandId, MF_BYCOMMAND | (allowMetadataEdit ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(menu_,
                          ratingCommandId,
                          MF_BYCOMMAND | (commonSelectionRating >= 0 && ratingCommandId == CommandIdFromRating(commonSelectionRating)
                              ? MF_CHECKED
                              : MF_UNCHECKED));
        }
        EnableMenuItem(menu_, ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu_, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS,
                  MF_BYCOMMAND | (rawJpegPairedOperationsEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu_, ID_FILE_COPY_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_MOVE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION_PERMANENT,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_JPEG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_PNG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_TIFF,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_FOLDER, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAILS,
            ID_VIEW_DETAILS,
            browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
            MF_BYCOMMAND);

        CheckMenuItem(
            menu_,
            ID_VIEW_RECURSIVE,
            MF_BYCOMMAND | (recursiveBrowsingEnabled_ ? MF_CHECKED : MF_UNCHECKED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THEME_LIGHT,
            ID_VIEW_THEME_DARK,
            themeMode_ == ThemeMode::Light ? ID_VIEW_THEME_LIGHT : ID_VIEW_THEME_DARK,
            MF_BYCOMMAND);

        CheckMenuItem(
            menu_,
            ID_VIEW_NVJPEG_ACCELERATION,
            MF_BYCOMMAND | ((nvJpegEnabled_ && HasNvJpegCapability()) ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(
            menu_,
            ID_VIEW_NVJPEG_ACCELERATION,
            MF_BYCOMMAND | (HasNvJpegCapability() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(
            menu_,
            ID_VIEW_LIBRAW_OUT_OF_PROCESS,
            MF_BYCOMMAND | ((libRawOutOfProcessEnabled_ && decode::IsLibRawBuildEnabled()) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_PERSISTENT_THUMBNAIL_CACHE,
            MF_BYCOMMAND | (persistentThumbnailCacheEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(
            menu_,
            ID_VIEW_LIBRAW_OUT_OF_PROCESS,
            MF_BYCOMMAND | (decode::IsLibRawBuildEnabled() ? MF_ENABLED : MF_GRAYED));
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAIL_SIZE_96,
            ID_VIEW_THUMBNAIL_SIZE_640,
            CommandIdFromThumbnailSizePreset(thumbnailSizePreset),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_THUMBNAIL_DETAILS,
            MF_BYCOMMAND | (thumbnailDetailsVisible ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_DETAILS_STRIP,
            MF_BYCOMMAND | (detailsStripVisible_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_SLIDESHOW_TRANSITION_CUT,
            ID_VIEW_SLIDESHOW_TRANSITION_KEN_BURNS,
            CommandIdFromTransitionStyle(slideshowTransitionStyle_),
            MF_BYCOMMAND);
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_SLIDESHOW_TRANSITION_DURATION_200,
            ID_VIEW_SLIDESHOW_TRANSITION_DURATION_2000,
            CommandIdFromTransitionDuration(slideshowTransitionDurationMs_),
            MF_BYCOMMAND);
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM,
            ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE,
            CommandIdFromViewerMouseWheelBehavior(viewerMouseWheelBehavior_),
            MF_BYCOMMAND);
        EnableMenuItem(
            menu_,
            ID_FILE_BATCH_CONVERT_CANCEL,
            MF_BYCOMMAND | (batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        const bool sortAscending = browserPaneController_
            ? browserPaneController_->IsSortAscending()
            : true;
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_RANDOM,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_SORT_DIRECTION,
            MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

        if (hwnd_)
        {
            DrawMenuBar(hwnd_);
        }

        UpdateToolbarItemStates();
    }

    void MainWindow::UpdateToolbarItemStates()
    {
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasCompareSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 2;
        const bool selectionActionsEnabled = hasSelection && !fileOperationActive_;
        const bool sizeEnabled = browserMode_ == BrowserMode::Thumbnails;

        for (auto& item : toolbarItems_)
        {
            switch (item.commandId)
            {
            case ID_VIEW_RECURSIVE:
                item.checked = recursiveBrowsingEnabled_;
                break;
            case ID_VIEW_THUMBNAILS:
                item.checked = browserMode_ == BrowserMode::Thumbnails;
                break;
            case ID_VIEW_DETAILS:
                item.checked = browserMode_ == BrowserMode::Details;
                break;
            case ID_ACTION_THUMBNAIL_SIZE_MENU:
                item.enabled = sizeEnabled;
                break;
            case ID_FILE_COMPARE_SELECTED:
                item.enabled = hasCompareSelection;
                break;
            case ID_FILE_COPY_SELECTION:
            case ID_FILE_MOVE_SELECTION:
            case ID_FILE_DELETE_SELECTION:
                item.enabled = selectionActionsEnabled;
                break;
            default:
                break;
            }
        }

        InvalidateToolbarStrip();
    }

    void MainWindow::InvalidateToolbarStrip()
    {
        if (!hwnd_)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        RECT stripRect{0, 0, client.right, kActionStripHeight};
        InvalidateRect(hwnd_, &stripRect, FALSE);
    }

    void MainWindow::UpdateWindowTitle() const
    {
        if (!hwnd_)
        {
            return;
        }

        std::wstring title = L"HyperBrowse";
        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            title.append(L" - ");
            title.append(browserModel_->FolderPath());
        }
        SetWindowTextW(hwnd_, title.c_str());
    }

    void MainWindow::ApplyTheme()
    {
        const ThemePalette palette = GetThemePalette();

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
            backgroundBrush_ = nullptr;
        }

        if (actionFieldBrush_)
        {
            DeleteObject(actionFieldBrush_);
            actionFieldBrush_ = nullptr;
        }

        if (detailsPanelBrush_)
        {
            DeleteObject(detailsPanelBrush_);
            detailsPanelBrush_ = nullptr;
        }

        backgroundBrush_ = CreateSolidBrush(palette.windowBackground);
        actionFieldBrush_ = CreateSolidBrush(palette.actionFieldBackground);
        detailsPanelBrush_ = CreateSolidBrush(palette.paneBackground);

        if (hwnd_)
        {
            ApplyWindowFrameTheme(hwnd_, themeMode_ == ThemeMode::Dark);
        }

        if (treePane_)
        {
            if (treeImageList_)
            {
                ImageList_SetBkColor(treeImageList_, CLR_NONE);
            }
            TreeView_SetBkColor(treePane_, palette.paneBackground);
            TreeView_SetTextColor(treePane_, palette.text);
            TreeView_SetLineColor(treePane_, palette.treeLine);
            InvalidateRect(treePane_, nullptr, TRUE);
        }

        if (browserPaneController_)
        {
            browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (viewerWindow_)
        {
            viewerWindow_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (diagnosticsWindow_)
        {
            diagnosticsWindow_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        RefreshDetailsPanelBodyPresentation();

        if (hwnd_)
        {
            InvalidateRect(detailsPanelText_, nullptr, TRUE);
            SetWindowPos(
                hwnd_,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
    }

    void MainWindow::ApplyViewerMouseWheelSetting()
    {
        if (viewerWindow_)
        {
            viewerWindow_->SetMouseWheelBehavior(viewerMouseWheelBehavior_);
        }
    }

    void MainWindow::ApplyViewerTransitionSettings()
    {
        if (viewerWindow_)
        {
            viewerWindow_->SetTransitionSettings(slideshowTransitionStyle_, slideshowTransitionDurationMs_);
        }
    }

    void MainWindow::ApplyPersistentThumbnailCacheSetting()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetPersistentThumbnailCacheEnabled(persistentThumbnailCacheEnabled_);
        }

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->SetDiskCacheEnabled(persistentThumbnailCacheEnabled_);
        }
    }

    void MainWindow::LoadWindowState()
    {
        // Always start non-recursive so restoring the last folder cannot trigger an expensive drive-wide scan.
        recursiveBrowsingEnabled_ = false;

        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            DWORD value = 0;

            if (TryReadDwordValue(key, kRegistryValueLeftPaneWidth, &value))
            {
                leftPaneWidth_ = static_cast<int>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueBrowserMode, &value) && value <= static_cast<DWORD>(BrowserMode::Details))
            {
                browserMode_ = static_cast<BrowserMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueThemeMode, &value) && value <= static_cast<DWORD>(ThemeMode::Dark))
            {
                themeMode_ = static_cast<ThemeMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueNvJpegEnabled, &value))
            {
                nvJpegEnabled_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueLibRawOutOfProcessEnabled, &value))
            {
                libRawOutOfProcessEnabled_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueThumbnailSizePreset, &value))
            {
                TryParseThumbnailSizePreset(value, &thumbnailSizePreset_);
            }

            if (TryReadDwordValue(key, kRegistryValueThumbnailDetailsVisible, &value))
            {
                thumbnailDetailsVisible_ = value != 0;
            }

            TryReadStringValue(key, kRegistryValueSelectedFolderPath, &startupFolderPath_);

            std::wstring serializedPaths;
            if (TryReadStringValue(key, kRegistryValueRecentFolders, &serializedPaths))
            {
                recentFolders_ = DeserializeFolderPathList(serializedPaths, kQuickAccessFolderLimit);
            }

            if (TryReadStringValue(key, kRegistryValueRecentDestinationFolders, &serializedPaths))
            {
                recentDestinationFolders_ = DeserializeFolderPathList(serializedPaths, kQuickAccessFolderLimit);
            }

            if (TryReadStringValue(key, kRegistryValueFavoriteDestinationFolders, &serializedPaths))
            {
                favoriteDestinationFolders_ = DeserializeFolderPathList(serializedPaths, kQuickAccessFolderLimit);
            }

            if (TryReadDwordValue(key, kRegistryValueSortMode, &value) && value <= static_cast<DWORD>(browser::BrowserSortMode::DateTaken))
            {
                sortMode_ = static_cast<browser::BrowserSortMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueSortAscending, &value))
            {
                sortAscending_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueSlideshowInterval, &value) && value >= 1000 && value <= 60000)
            {
                slideshowIntervalMs_ = static_cast<UINT>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueSlideshowTransitionStyle, &value)
                && value <= static_cast<DWORD>(viewer::TransitionStyle::KenBurns))
            {
                slideshowTransitionStyle_ = static_cast<viewer::TransitionStyle>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueSlideshowTransitionDuration, &value)
                && value >= 120
                && value <= 5000)
            {
                slideshowTransitionDurationMs_ = static_cast<UINT>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueDetailsStripVisible, &value))
            {
                detailsStripVisible_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueViewerMouseWheelBehavior, &value)
                && value <= static_cast<DWORD>(viewer::MouseWheelBehavior::Navigate))
            {
                viewerMouseWheelBehavior_ = static_cast<viewer::MouseWheelBehavior>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueRawJpegPairedOperationsEnabled, &value))
            {
                rawJpegPairedOperationsEnabled_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValuePersistentThumbnailCacheEnabled, &value))
            {
                persistentThumbnailCacheEnabled_ = value != 0;
            }

            RegCloseKey(key);
        }

        leftPaneWidth_ = std::max(leftPaneWidth_, kMinLeftPaneWidth);
        startupFolderPath_ = NormalizeFolderPath(std::move(startupFolderPath_));
    }

    void MainWindow::SaveWindowState() const
    {
        HKEY key{};
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) == ERROR_SUCCESS)
        {
            const std::wstring selectedFolderPath = !GetSelectedFolderTreePath().empty()
                ? GetSelectedFolderTreePath()
                : (browserModel_ ? browserModel_->FolderPath() : std::wstring{});

            WriteDwordValue(key, kRegistryValueLeftPaneWidth, static_cast<DWORD>(leftPaneWidth_));
            WriteDwordValue(key, kRegistryValueBrowserMode, static_cast<DWORD>(browserMode_));
            WriteDwordValue(key, kRegistryValueThemeMode, static_cast<DWORD>(themeMode_));
            RegDeleteValueW(key, L"RecursiveBrowsing");
            WriteDwordValue(key, kRegistryValueNvJpegEnabled, nvJpegEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueLibRawOutOfProcessEnabled, libRawOutOfProcessEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueThumbnailSizePreset, static_cast<DWORD>(thumbnailSizePreset_));
            RegDeleteValueW(key, kRegistryValueCompactThumbnailLayout);
            WriteDwordValue(key, kRegistryValueThumbnailDetailsVisible, thumbnailDetailsVisible_ ? 1UL : 0UL);
            WriteStringValue(key, kRegistryValueSelectedFolderPath, selectedFolderPath);
            WriteStringValue(key, kRegistryValueRecentFolders, SerializeFolderPathList(recentFolders_));
            WriteStringValue(key, kRegistryValueRecentDestinationFolders, SerializeFolderPathList(recentDestinationFolders_));
            WriteStringValue(key, kRegistryValueFavoriteDestinationFolders, SerializeFolderPathList(favoriteDestinationFolders_));
            if (browserPaneController_)
            {
                WriteDwordValue(key, kRegistryValueSortMode, static_cast<DWORD>(browserPaneController_->GetSortMode()));
                WriteDwordValue(key, kRegistryValueSortAscending, browserPaneController_->IsSortAscending() ? 1UL : 0UL);
            }
            WriteDwordValue(key, kRegistryValueSlideshowInterval, static_cast<DWORD>(slideshowIntervalMs_));
            WriteDwordValue(key, kRegistryValueSlideshowTransitionStyle, static_cast<DWORD>(slideshowTransitionStyle_));
            WriteDwordValue(key, kRegistryValueSlideshowTransitionDuration, static_cast<DWORD>(slideshowTransitionDurationMs_));
            WriteDwordValue(key, kRegistryValueDetailsStripVisible, detailsStripVisible_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueViewerMouseWheelBehavior, static_cast<DWORD>(viewerMouseWheelBehavior_));
            WriteDwordValue(key, kRegistryValueRawJpegPairedOperationsEnabled, rawJpegPairedOperationsEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValuePersistentThumbnailCacheEnabled, persistentThumbnailCacheEnabled_ ? 1UL : 0UL);
            RegCloseKey(key);
        }
    }

    void MainWindow::LoadFolderAsync(std::wstring folderPath)
    {
        if (folderPath.empty() || !browserModel_ || !folderEnumerationService_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));

        util::LogInfo(L"Queueing folder enumeration for " + folderPath);
        if (folderWatchService_)
        {
            folderWatchService_->Stop();
            activeFolderWatchRequestId_ = 0;
        }
        browserModel_->Reset(folderPath, recursiveBrowsingEnabled_);
        if (browserPaneController_)
        {
            browserPaneController_->ClearSelection();
        }
        ShowSelectedFolderInTree();
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        activeEnumerationRequestId_ = folderEnumerationService_->EnumerateFolderAsync(hwnd_, std::move(folderPath), recursiveBrowsingEnabled_);
    }

    LRESULT MainWindow::OnFolderEnumerationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderEnumerationUpdate> update(
            reinterpret_cast<services::FolderEnumerationUpdate*>(lParam));
        if (!update || !browserModel_ || update->requestId != activeEnumerationRequestId_)
        {
            return 0;
        }

        switch (update->kind)
        {
        case services::FolderEnumerationUpdateKind::Batch:
            browserModel_->AppendItems(std::move(update->items), update->totalCount, update->totalBytes);
            break;
        case services::FolderEnumerationUpdateKind::Completed:
            browserModel_->Complete();
            util::LogInfo(L"Completed folder enumeration for " + update->folderPath);
            RecordRecentFolder(update->folderPath);
            if (folderWatchService_)
            {
                activeFolderWatchRequestId_ = folderWatchService_->StartWatching(hwnd_, update->folderPath, browserModel_->IsRecursive());
            }
            break;
        case services::FolderEnumerationUpdateKind::Failed:
            browserModel_->Fail(update->message);
            util::LogError(update->message);
            break;
        default:
            break;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        return 0;
    }

    LRESULT MainWindow::OnFolderTreeEnumerationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderTreeEnumerationUpdate> update(
            reinterpret_cast<services::FolderTreeEnumerationUpdate*>(lParam));
        if (!update)
        {
            return 0;
        }

        const auto pendingItem = pendingFolderTreeEnumerationItems_.find(update->requestId);
        if (pendingItem == pendingFolderTreeEnumerationItems_.end())
        {
            return 0;
        }

        const HTREEITEM item = pendingItem->second;
        pendingFolderTreeEnumerationItems_.erase(pendingItem);

        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->childEnumerationRequestId != update->requestId)
        {
            return 0;
        }

        switch (update->kind)
        {
        case services::FolderTreeEnumerationUpdateKind::Completed:
            ApplyFolderTreeChildren(item, std::move(update->childFolders));
            ContinueSelectingFolderInTree();
            return 0;
        case services::FolderTreeEnumerationUpdateKind::Failed:
            nodeData->childrenLoading = false;
            nodeData->childEnumerationRequestId = 0;
            nodeData->childrenLoaded = false;
            AddFolderTreePlaceholder(item);
            util::LogError(update->message);
            return 0;
        default:
            return 0;
        }
    }

    LRESULT MainWindow::OnFolderWatchMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderWatchUpdate> update(
            reinterpret_cast<services::FolderWatchUpdate*>(lParam));
        if (!update || update->requestId != activeFolderWatchRequestId_)
        {
            return 0;
        }

        ApplyFolderWatchChanges(*update);
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateDetailsPanel();
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        OpenItemInViewer(static_cast<int>(lParam));
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ShowBrowserContextMenu(screenPoint);
        return 0;
    }

    LRESULT MainWindow::OnBatchConvertMessage(LPARAM lParam)
    {
        std::unique_ptr<services::BatchConvertUpdate> update(
            reinterpret_cast<services::BatchConvertUpdate*>(lParam));
        if (!update || update->requestId != activeBatchConvertRequestId_)
        {
            return 0;
        }

        batchConvertCompleted_ = update->completedCount;
        batchConvertTotal_ = update->totalCount;
        batchConvertFailed_ = update->failedCount;
        batchConvertCurrentFile_ = update->currentFileName;
        batchConvertOutputFolder_ = update->outputFolder;

        if (update->finished)
        {
            batchConvertActive_ = false;
            UpdateMenuState();

            if (!update->cancelled && update->failedCount > 0)
            {
                std::wstring summary;
                if (!update->message.empty())
                {
                    summary = update->message;
                    summary.append(L"\n\n");
                }

                summary.append(L"Batch conversion completed. Converted ");
                summary.append(std::to_wstring(update->completedCount - update->failedCount));
                summary.append(L" of ");
                summary.append(std::to_wstring(update->totalCount));
                summary.append(L" image(s).\nFailures: ");
                summary.append(std::to_wstring(update->failedCount));
                summary.append(L".");
                MessageBoxW(hwnd_, summary.c_str(), L"Batch Convert", MB_OK | MB_ICONWARNING);
            }
        }

        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnDetailsPanelThumbnailMessage(LPARAM lParam)
    {
        std::unique_ptr<services::ThumbnailReadyUpdate> update(
            reinterpret_cast<services::ThumbnailReadyUpdate*>(lParam));
        if (!update
            || !detailsPanelThumbnailScheduler_
            || update->sessionId != detailsPanelThumbnailSessionId_
            || update->requestEpoch != detailsPanelThumbnailRequestEpoch_
            || update->modelIndex != detailsPanelHistogramModelIndex_
            || !StringsEqualInsensitive(update->cacheKey.filePath, detailsPanelHistogramPath_)
            || update->cacheKey.modifiedTimestampUtc != detailsPanelHistogramModifiedTimestampUtc_)
        {
            return 0;
        }

        detailsPanelHistogramLoading_ = false;
        if (update->success)
        {
            if (const auto thumbnail = detailsPanelThumbnailScheduler_->FindCachedThumbnail(update->cacheKey))
            {
                ApplyDetailsPanelHistogram(*thumbnail);
            }
        }
        else
        {
            detailsPanelHistogramVisible_ = false;
            detailsPanelHistogramPeak_ = 0;
        }

        LayoutChildren();
        if (hwnd_ && !IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
        return 0;
    }

    LRESULT MainWindow::OnFileOperationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FileOperationUpdate> update(
            reinterpret_cast<services::FileOperationUpdate*>(lParam));
        if (!update || update->requestId != activeFileOperationRequestId_)
        {
            return 0;
        }

        ApplyCompletedFileOperation(*update);
        return 0;
    }

    LRESULT MainWindow::OnViewerZoomMessage(LPARAM lParam)
    {
        viewerZoomPercent_ = static_cast<int>(lParam);
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerActivityMessage(LPARAM lParam)
    {
        viewerWindowActive_ = lParam != 0;
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerClosedMessage()
    {
        viewerWindowActive_ = false;
        viewerZoomPercent_ = 0;
        UpdateStatusText();
        return 0;
    }

    bool MainWindow::HandleCommand(UINT commandId)
    {
        if (IsCommandInRange(commandId, ID_FILE_OPEN_RECENT_FOLDER_BASE, ID_FILE_OPEN_RECENT_FOLDER_LAST))
        {
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_OPEN_RECENT_FOLDER_BASE);
            if (index < recentFolders_.size())
            {
                const std::wstring folderPath = recentFolders_[index];
                if (!IsExistingDirectory(folderPath))
                {
                    MessageBoxW(hwnd_, L"The selected recent folder is no longer available.", L"Open Recent Folder", MB_OK | MB_ICONINFORMATION);
                }
                else
                {
                    LoadFolderAsync(folderPath);
                }
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_COPY_SELECTION_FAVORITE_BASE, ID_FILE_COPY_SELECTION_FAVORITE_LAST))
        {
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_COPY_SELECTION_FAVORITE_BASE);
            if (index < favoriteDestinationFolders_.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Copy, favoriteDestinationFolders_[index]);
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_COPY_SELECTION_RECENT_BASE, ID_FILE_COPY_SELECTION_RECENT_LAST))
        {
            const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_COPY_SELECTION_RECENT_BASE);
            if (index < recentDestinationPaths.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Copy, recentDestinationPaths[index]);
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_MOVE_SELECTION_FAVORITE_BASE, ID_FILE_MOVE_SELECTION_FAVORITE_LAST))
        {
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_MOVE_SELECTION_FAVORITE_BASE);
            if (index < favoriteDestinationFolders_.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Move, favoriteDestinationFolders_[index]);
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_MOVE_SELECTION_RECENT_BASE, ID_FILE_MOVE_SELECTION_RECENT_LAST))
        {
            const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_MOVE_SELECTION_RECENT_BASE);
            if (index < recentDestinationPaths.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Move, recentDestinationPaths[index]);
            }
            return true;
        }

        if (IsViewerMouseWheelBehaviorCommand(commandId))
        {
            viewerMouseWheelBehavior_ = ViewerMouseWheelBehaviorFromCommandId(commandId);
            ApplyViewerMouseWheelSetting();
            UpdateMenuState();
            return true;
        }

        if (IsRatingCommand(commandId))
        {
            SetSelectionRating(RatingFromCommandId(commandId));
            return true;
        }

        if (IsTransitionStyleCommand(commandId))
        {
            slideshowTransitionStyle_ = TransitionStyleFromCommandId(commandId);
            ApplyViewerTransitionSettings();
            UpdateMenuState();
            return true;
        }

        if (IsTransitionDurationCommand(commandId))
        {
            slideshowTransitionDurationMs_ = TransitionDurationFromCommandId(commandId);
            ApplyViewerTransitionSettings();
            UpdateMenuState();
            return true;
        }

        switch (commandId)
        {
        case ID_FILE_OPEN_FOLDER:
            OpenFolder();
            return true;
        case ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION:
            ToggleCurrentFolderFavoriteDestination();
            return true;
        case ID_FILE_EXIT:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return true;
        case ID_FILE_REFRESH_TREE:
            RefreshFolderTree();
            return true;
        case ID_FILE_OPEN_SELECTED:
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1);
            return true;
        case ID_FILE_COMPARE_SELECTED:
            StartCompareSelected();
            return true;
        case ID_FILE_VIEW_ON_SECONDARY_MONITOR:
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1, true);
            return true;
        case ID_FILE_IMAGE_INFORMATION:
            ShowImageInformation();
            return true;
        case ID_FILE_COPY_SELECTION:
            StartCopySelection();
            return true;
        case ID_FILE_COPY_SELECTION_BROWSE:
            StartCopySelection();
            return true;
        case ID_FILE_RENAME_SELECTED:
            StartRenameSelectedImage();
            return true;
        case ID_FILE_MOVE_SELECTION:
            StartMoveSelection();
            return true;
        case ID_FILE_MOVE_SELECTION_BROWSE:
            StartMoveSelection();
            return true;
        case ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS:
            rawJpegPairedOperationsEnabled_ = !rawJpegPairedOperationsEnabled_;
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_FILE_DELETE_SELECTION:
            StartDeleteSelection(false);
            return true;
        case ID_FILE_DELETE_SELECTION_PERMANENT:
            StartDeleteSelection(true);
            return true;
        case ID_FILE_REVEAL_IN_EXPLORER:
            RevealSelectedInExplorer();
            return true;
        case ID_FILE_OPEN_CONTAINING_FOLDER:
            OpenSelectedContainingFolder();
            return true;
        case ID_FILE_COPY_PATH:
            CopySelectedPathsToClipboard();
            return true;
        case ID_FILE_PROPERTIES:
            ShowSelectedFileProperties();
            return true;
        case ID_FILE_EDIT_TAGS:
            EditSelectionTags();
            return true;
        case ID_FILE_ROTATE_JPEG_LEFT:
            AdjustSelectedJpegOrientation(-1);
            return true;
        case ID_FILE_ROTATE_JPEG_RIGHT:
            AdjustSelectedJpegOrientation(+1);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_JPEG:
            StartBatchConvert(true, services::BatchConvertFormat::Jpeg);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_PNG:
            StartBatchConvert(true, services::BatchConvertFormat::Png);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_TIFF:
            StartBatchConvert(true, services::BatchConvertFormat::Tiff);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_JPEG:
            StartBatchConvert(false, services::BatchConvertFormat::Jpeg);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_PNG:
            StartBatchConvert(false, services::BatchConvertFormat::Png);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_TIFF:
            StartBatchConvert(false, services::BatchConvertFormat::Tiff);
            return true;
        case ID_FILE_BATCH_CONVERT_CANCEL:
            if (batchConvertService_)
            {
                batchConvertService_->Cancel();
                batchConvertActive_ = false;
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_THUMBNAILS:
            SetBrowserMode(BrowserMode::Thumbnails);
            return true;
        case ID_VIEW_DETAILS:
            SetBrowserMode(BrowserMode::Details);
            return true;
        case ID_ACTION_SORT_MENU:
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                if (toolbarItems_[static_cast<std::size_t>(i)].commandId == ID_ACTION_SORT_MENU)
                {
                    ShowDropdownForItem(i);
                    break;
                }
            }
            return true;
        }
        case ID_ACTION_THUMBNAIL_SIZE_MENU:
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                if (toolbarItems_[static_cast<std::size_t>(i)].commandId == ID_ACTION_THUMBNAIL_SIZE_MENU)
                {
                    ShowDropdownForItem(i);
                    break;
                }
            }
            return true;
        }
        case ID_ACTION_THEME_MENU:
            // Theme is now toggled via menu; no dropdown button
            return true;
        case ID_VIEW_RECURSIVE:
            ToggleRecursiveBrowsing();
            return true;
        case ID_VIEW_NVJPEG_ACCELERATION:
            if (HasNvJpegCapability())
            {
                nvJpegEnabled_ = !nvJpegEnabled_;
                decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_LIBRAW_OUT_OF_PROCESS:
            if (decode::IsLibRawBuildEnabled())
            {
                libRawOutOfProcessEnabled_ = !libRawOutOfProcessEnabled_;
                decode::SetLibRawOutOfProcessEnabled(libRawOutOfProcessEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_PERSISTENT_THUMBNAIL_CACHE:
            persistentThumbnailCacheEnabled_ = !persistentThumbnailCacheEnabled_;
            ApplyPersistentThumbnailCacheSetting();
            UpdateMenuState();
            return true;
        case ID_VIEW_THUMBNAIL_SIZE_96:
        case ID_VIEW_THUMBNAIL_SIZE_128:
        case ID_VIEW_THUMBNAIL_SIZE_160:
        case ID_VIEW_THUMBNAIL_SIZE_192:
        case ID_VIEW_THUMBNAIL_SIZE_256:
        case ID_VIEW_THUMBNAIL_SIZE_320:
        case ID_VIEW_THUMBNAIL_SIZE_360:
        case ID_VIEW_THUMBNAIL_SIZE_420:
        case ID_VIEW_THUMBNAIL_SIZE_480:
        case ID_VIEW_THUMBNAIL_SIZE_560:
        case ID_VIEW_THUMBNAIL_SIZE_640:
            thumbnailSizePreset_ = ThumbnailSizePresetFromCommandId(commandId);
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_VIEW_THUMBNAIL_DETAILS:
            thumbnailDetailsVisible_ = !thumbnailDetailsVisible_;
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_VIEW_DETAILS_STRIP:
            detailsStripVisible_ = !detailsStripVisible_;
            if (detailsPanelText_)
            {
                ShowWindow(detailsPanelText_, detailsStripVisible_ ? SW_SHOW : SW_HIDE);
            }
            LayoutChildren();
            UpdateDetailsPanel();
            UpdateMenuState();
            return true;
        case ID_VIEW_SORT_FILENAME:
        case ID_VIEW_SORT_MODIFIED:
        case ID_VIEW_SORT_SIZE:
        case ID_VIEW_SORT_DIMENSIONS:
        case ID_VIEW_SORT_TYPE:
        case ID_VIEW_SORT_DATETAKEN:
        case ID_VIEW_SORT_RANDOM:
            if (browserPaneController_)
            {
                browserPaneController_->SetSortMode(SortModeFromCommandId(commandId));
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_SORT_DIRECTION:
            if (browserPaneController_)
            {
                browserPaneController_->SetSortAscending(!browserPaneController_->IsSortAscending());
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_THEME_LIGHT:
            SetThemeMode(ThemeMode::Light);
            return true;
        case ID_VIEW_THEME_DARK:
            SetThemeMode(ThemeMode::Dark);
            return true;
        case ID_VIEW_SLIDESHOW_SELECTION:
            StartSlideshow(true);
            return true;
        case ID_VIEW_SLIDESHOW_FOLDER:
            StartSlideshow(false);
            return true;
        case ID_HELP_ABOUT:
            ShowAboutDialog();
            return true;
        case ID_HELP_DIAGNOSTICS_SNAPSHOT:
            ShowDiagnosticsSnapshot();
            return true;
        case ID_HELP_DIAGNOSTICS_RESET:
            ResetDiagnosticsState();
            return true;
        default:
            break;
        }

        return false;
    }

    void MainWindow::SetBrowserMode(BrowserMode mode)
    {
        if (browserMode_ == mode)
        {
            return;
        }

        browserMode_ = mode;
        util::LogInfo(browserMode_ == BrowserMode::Thumbnails
            ? L"Switched shell to thumbnail mode"
            : L"Switched shell to details mode");

        if (browserPaneController_)
        {
            browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
                ? browser::BrowserViewMode::Thumbnails
                : browser::BrowserViewMode::Details);
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    void MainWindow::ToggleRecursiveBrowsing()
    {
        recursiveBrowsingEnabled_ = !recursiveBrowsingEnabled_;
        util::LogInfo(recursiveBrowsingEnabled_
            ? L"Enabled recursive browsing for folder enumeration"
            : L"Disabled recursive browsing for folder enumeration");
        UpdateMenuState();

        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::SetThemeMode(ThemeMode themeMode)
    {
        if (themeMode_ == themeMode)
        {
            return;
        }

        themeMode_ = themeMode;
        util::LogInfo(themeMode_ == ThemeMode::Dark
            ? L"Switched shell to dark theme"
            : L"Switched shell to light theme");
        ApplyTheme();
        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    MainWindow::ThemePalette MainWindow::GetThemePalette() const
    {
        switch (themeMode_)
        {
        case ThemeMode::Dark:
            return ThemePalette{
                RGB(24, 28, 32),
                RGB(34, 38, 43),
                RGB(234, 238, 242),
                RGB(140, 148, 158),
                RGB(96, 102, 110),
                RGB(78, 84, 92),
                RGB(29, 33, 38),
                RGB(74, 82, 92),
                RGB(21, 25, 30),
                RGB(112, 169, 227),
                RGB(47, 68, 92),
                RGB(244, 248, 252),
            };
        case ThemeMode::Light:
        default:
            return ThemePalette{
                RGB(243, 245, 248),
                RGB(255, 255, 255),
                RGB(32, 36, 40),
                RGB(128, 136, 148),
                RGB(198, 204, 212),
                RGB(210, 215, 223),
                RGB(249, 250, 252),
                RGB(210, 215, 223),
                RGB(255, 255, 255),
                RGB(54, 114, 186),
                RGB(220, 233, 247),
                RGB(25, 35, 50),
            };
        }
    }

    void MainWindow::InitToolbarItems()
    {
        toolbarItems_.clear();

        auto addIcon = [this](UINT cmdId, std::string iconName, std::wstring tip,
                              ToolbarItemKind kind = ToolbarItemKind::IconButton,
                              ToolbarAlignment align = ToolbarAlignment::Left)
        {
            ToolbarItem item;
            item.commandId = cmdId;
            item.iconName = std::move(iconName);
            item.tooltip = std::move(tip);
            item.kind = kind;
            item.alignment = align;
            toolbarItems_.push_back(std::move(item));
        };

        auto addSeparator = [this](ToolbarAlignment align = ToolbarAlignment::Left)
        {
            ToolbarItem sep;
            sep.kind = ToolbarItemKind::Separator;
            sep.alignment = align;
            toolbarItems_.push_back(std::move(sep));
        };

        // Left group 1: Navigation
    addIcon(ID_FILE_OPEN_FOLDER, "open-folder", L"Open Folder (Ctrl+O)");
    addIcon(ID_VIEW_RECURSIVE, "recursive", L"Recursive Browsing (Ctrl+R)", ToolbarItemKind::IconToggle);

        addSeparator();

        // Left group 2: View mode
        addIcon(ID_VIEW_THUMBNAILS, "view-grid", L"Thumbnail Mode (Ctrl+1)", ToolbarItemKind::IconToggle);
        addIcon(ID_VIEW_DETAILS, "view-list", L"Details Mode (Ctrl+2)", ToolbarItemKind::IconToggle);

        addSeparator();

        // Left group 3: Display controls (dropdowns)
        addIcon(ID_ACTION_SORT_MENU, "sort", L"Sort By", ToolbarItemKind::IconDropdown);
        addIcon(ID_ACTION_THUMBNAIL_SIZE_MENU, "thumbnail-size", L"Thumbnail Size", ToolbarItemKind::IconDropdown);

        addSeparator();

        // Filter placeholder (will be positioned in LayoutToolbar)
        {
            ToolbarItem filterItem;
            filterItem.kind = ToolbarItemKind::FilterEdit;
            filterItem.alignment = ToolbarAlignment::Left;
            toolbarItems_.push_back(std::move(filterItem));
        }

        addSeparator(ToolbarAlignment::Right);

        // Right group: Selection actions
        addIcon(ID_FILE_COMPARE_SELECTED, "compare", L"Compare Selected", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_COPY_SELECTION, "copy", L"Copy Selection", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_MOVE_SELECTION, "move", L"Move Selection", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_DELETE_SELECTION, "delete", L"Delete (Del)", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
    }

    void MainWindow::LayoutToolbar()
    {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientWidth = client.right - client.left;
        const int itemTop = kActionStripPaddingY;

        // Lay out left-aligned items first, then right-aligned items, then fill filter gap.
        int leftCursor = kActionStripPaddingX;
        int rightCursor = clientWidth - kActionStripPaddingX;
        int filterItemIndex = -1;

        // Left pass
        for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
        {
            auto& item = toolbarItems_[static_cast<std::size_t>(i)];
            if (item.alignment != ToolbarAlignment::Left)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                item.rect = RECT{leftCursor + kToolbarSeparatorGap, itemTop,
                                 leftCursor + kToolbarSeparatorGap + 1, itemTop + kToolbarItemSize};
                leftCursor += kToolbarSeparatorWidth;
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterEdit)
            {
                filterItemIndex = i;
                continue;
            }

            item.rect = RECT{leftCursor, itemTop, leftCursor + kToolbarItemSize, itemTop + kToolbarItemSize};
            leftCursor += kToolbarItemSize + 2;
        }

        // Right pass (iterate in reverse so rightmost items stay rightmost)
        for (int i = static_cast<int>(toolbarItems_.size()) - 1; i >= 0; --i)
        {
            auto& item = toolbarItems_[static_cast<std::size_t>(i)];
            if (item.alignment != ToolbarAlignment::Right)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                rightCursor -= kToolbarSeparatorWidth;
                item.rect = RECT{rightCursor + kToolbarSeparatorGap, itemTop,
                                 rightCursor + kToolbarSeparatorGap + 1, itemTop + kToolbarItemSize};
                continue;
            }

            rightCursor -= kToolbarItemSize;
            item.rect = RECT{rightCursor, itemTop, rightCursor + kToolbarItemSize, itemTop + kToolbarItemSize};
            rightCursor -= 2;
        }

        // Place filter edit control in the gap
        if (filterItemIndex >= 0 && filterEdit_)
        {
            const int filterLeft = leftCursor + 6;
            const int filterRight = rightCursor - 6;
            const int filterWidth = std::max(0, filterRight - filterLeft);
            toolbarItems_[static_cast<std::size_t>(filterItemIndex)].rect =
                RECT{filterLeft, itemTop, filterLeft + filterWidth, itemTop + kToolbarItemSize};
            MoveWindow(filterEdit_, filterLeft + 10, itemTop + 7, std::max(0, filterWidth - 20), kToolbarItemSize - 14, TRUE);
        }

        // Update tooltip rects
        if (tooltipControl_)
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = static_cast<UINT_PTR>(i);
                toolInfo.rect = toolbarItems_[static_cast<std::size_t>(i)].rect;
                SendMessageW(tooltipControl_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }
        }

        // Invalidate the strip area
        RECT stripRect{0, 0, clientWidth, kActionStripHeight};
        InvalidateRect(hwnd_, &stripRect, FALSE);
    }

    void MainWindow::PaintToolbar(HDC hdc, const RECT& stripRect)
    {
        const ThemePalette palette = GetThemePalette();
        HDC iconDC = toolbarIconLibrary_ ? CreateCompatibleDC(hdc) : nullptr;

        // Fill strip background
        const HBRUSH stripBrush = CreateSolidBrush(palette.actionStripBackground);
        FillRect(hdc, &stripRect, stripBrush);
        DeleteObject(stripBrush);

        // Bottom border
        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        const HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, stripRect.left, stripRect.bottom - 1, nullptr);
        LineTo(hdc, stripRect.right, stripRect.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
        {
            const auto& item = toolbarItems_[static_cast<std::size_t>(i)];

            if (item.kind == ToolbarItemKind::Separator)
            {
                const HPEN sepPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
                const HGDIOBJ savedPen = SelectObject(hdc, sepPen);
                MoveToEx(hdc, item.rect.left, item.rect.top + 4, nullptr);
                LineTo(hdc, item.rect.left, item.rect.bottom - 4);
                SelectObject(hdc, savedPen);
                DeleteObject(sepPen);
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterEdit)
            {
                // Draw filter field background
                if (filterEdit_)
                {
                    RECT filterBg = item.rect;
                    InflateRect(&filterBg, 0, -2);
                    const bool filterFocused = GetFocus() == filterEdit_;
                    const HBRUSH fieldBrush = CreateSolidBrush(palette.actionFieldBackground);
                    const HPEN fieldPen = CreatePen(PS_SOLID, 1, filterFocused ? palette.accent : palette.actionStripBorder);
                    const HGDIOBJ oldb = SelectObject(hdc, fieldBrush);
                    const HGDIOBJ oldp = SelectObject(hdc, fieldPen);
                    RoundRect(hdc, filterBg.left, filterBg.top, filterBg.right, filterBg.bottom, 14, 14);
                    SelectObject(hdc, oldp);
                    SelectObject(hdc, oldb);
                    DeleteObject(fieldPen);
                    DeleteObject(fieldBrush);

                    if (toolbarIconLibrary_ && iconDC)
                    {
                        const HBITMAP searchBitmap = toolbarIconLibrary_->GetBitmap("search", 14, palette.mutedText);
                        AlphaBlendBitmap(hdc, iconDC, searchBitmap, filterBg.left + 7, filterBg.top + 7, 14, 14);
                    }
                }
                continue;
            }

            // Determine visual state
            const bool isHot = (i == toolbarHotIndex_);
            const bool isPressed = (i == toolbarPressedIndex_);
            const bool isChecked = item.checked;
            const bool isEnabled = item.enabled;

            // Choose icon color
            COLORREF iconColor = palette.mutedText;
            if (isChecked)
            {
                iconColor = palette.accentText;
            }
            else if (!isEnabled)
            {
                iconColor = BlendColor(palette.mutedText, palette.actionStripBackground, 140);
            }

            // Draw background for hot/pressed/checked states
            if (isEnabled && (isHot || isPressed || isChecked))
            {
                RECT bgRect = item.rect;
                InflateRect(&bgRect, -1, -1);

                COLORREF bgColor;
                if (isChecked)
                {
                    bgColor = palette.accentFill;
                    if (isPressed)
                    {
                        bgColor = BlendColor(bgColor, palette.accent, 48);
                    }
                    else if (isHot)
                    {
                        bgColor = BlendColor(bgColor, palette.accent, 24);
                    }
                }
                else if (isPressed)
                {
                    bgColor = BlendColor(palette.actionStripBackground, palette.accent, 48);
                }
                else
                {
                    bgColor = BlendColor(palette.actionStripBackground, palette.text, 20);
                }

                const HBRUSH bgBrush = CreateSolidBrush(bgColor);
                const HPEN bgPen = CreatePen(PS_SOLID, 1, bgColor);
                const HGDIOBJ oldb = SelectObject(hdc, bgBrush);
                const HGDIOBJ oldp = SelectObject(hdc, bgPen);
                RoundRect(hdc, bgRect.left, bgRect.top, bgRect.right, bgRect.bottom, 10, 10);
                SelectObject(hdc, oldp);
                SelectObject(hdc, oldb);
                DeleteObject(bgPen);
                DeleteObject(bgBrush);
            }

            if (!item.iconName.empty() && toolbarIconLibrary_ && iconDC)
            {
                RECT iconRect = item.rect;
                if (item.kind == ToolbarItemKind::IconDropdown)
                {
                    iconRect.right -= kToolbarDropdownChevronSize + 2;
                }

                const int iconWidth = kToolbarIconSize;
                const int iconHeight = kToolbarIconSize;
                const int iconX = iconRect.left + ((iconRect.right - iconRect.left) - iconWidth) / 2;
                const int iconY = iconRect.top + ((iconRect.bottom - iconRect.top) - iconHeight) / 2;
                const HBITMAP iconBitmap = toolbarIconLibrary_->GetBitmap(item.iconName, kToolbarIconSize, iconColor);
                AlphaBlendBitmap(hdc, iconDC, iconBitmap, iconX, iconY, iconWidth, iconHeight);
            }

            // Draw dropdown indicator
            if (item.kind == ToolbarItemKind::IconDropdown && isEnabled && toolbarIconLibrary_ && iconDC)
            {
                RECT chevronRect = item.rect;
                const int chevronX = chevronRect.right - kToolbarDropdownChevronSize - 6;
                const int chevronY = chevronRect.top + ((chevronRect.bottom - chevronRect.top) - kToolbarDropdownChevronSize) / 2;
                const HBITMAP chevronBitmap = toolbarIconLibrary_->GetBitmap("chevron-down", kToolbarDropdownChevronSize, palette.mutedText);
                AlphaBlendBitmap(hdc, iconDC, chevronBitmap, chevronX, chevronY, kToolbarDropdownChevronSize, kToolbarDropdownChevronSize);
            }
        }

        if (iconDC)
        {
            DeleteDC(iconDC);
        }
    }

    int MainWindow::ToolbarHitTest(int x, int y) const
    {
        POINT pt{x, y};
        for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
        {
            const auto& item = toolbarItems_[static_cast<std::size_t>(i)];
            if (item.kind == ToolbarItemKind::Separator || item.kind == ToolbarItemKind::FilterEdit)
            {
                continue;
            }

            if (PtInRect(&item.rect, pt))
            {
                return i;
            }
        }
        return -1;
    }

    void MainWindow::ToolbarHandleClick(int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(toolbarItems_.size()))
        {
            return;
        }

        const auto& item = toolbarItems_[static_cast<std::size_t>(itemIndex)];
        if (!item.enabled)
        {
            return;
        }

        if (item.kind == ToolbarItemKind::IconDropdown)
        {
            ShowDropdownForItem(itemIndex);
            return;
        }

        if (item.commandId != 0)
        {
            HandleCommand(item.commandId);
        }
    }

    void MainWindow::ShowDropdownForItem(int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(toolbarItems_.size()))
        {
            return;
        }

        const auto& item = toolbarItems_[static_cast<std::size_t>(itemIndex)];
        RECT itemScreenRect = item.rect;
        MapWindowPoints(hwnd_, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&itemScreenRect), 2);

        switch (item.commandId)
        {
        case ID_ACTION_SORT_MENU:
        {
            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By Filename");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By Modified Date");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File Size");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By Dimensions");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_TYPE, L"By Type");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date Taken");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By Random");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"Descending");

            const bool sortAscending = browserPaneController_
                ? browserPaneController_->IsSortAscending() : true;
            const UINT checkedCommand = browserPaneController_
                ? CommandIdFromSortMode(browserPaneController_->GetSortMode())
                : ID_VIEW_SORT_FILENAME;
            CheckMenuRadioItem(menu, ID_VIEW_SORT_FILENAME, ID_VIEW_SORT_RANDOM, checkedCommand, MF_BYCOMMAND);
            CheckMenuItem(menu, ID_VIEW_SORT_DIRECTION, MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

            SetForegroundWindow(hwnd_);
            const UINT cmdId = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                itemScreenRect.left, itemScreenRect.bottom, hwnd_, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            if (cmdId != 0) HandleCommand(cmdId);
            break;
        }
        case ID_ACTION_THUMBNAIL_SIZE_MENU:
        {
            if (browserMode_ != BrowserMode::Thumbnails) return;
            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_96, L"96 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_128, L"128 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_160, L"160 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_192, L"192 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_256, L"256 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_320, L"320 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_360, L"360 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_420, L"420 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_480, L"480 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_560, L"560 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_640, L"640 px");

            const browser::ThumbnailSizePreset preset = browserPaneController_
                ? browserPaneController_->GetThumbnailSizePreset() : thumbnailSizePreset_;
            CheckMenuRadioItem(menu, ID_VIEW_THUMBNAIL_SIZE_96, ID_VIEW_THUMBNAIL_SIZE_640,
                CommandIdFromThumbnailSizePreset(preset), MF_BYCOMMAND);

            SetForegroundWindow(hwnd_);
            const UINT cmdId = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                itemScreenRect.left, itemScreenRect.bottom, hwnd_, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            if (cmdId != 0) HandleCommand(cmdId);
            break;
        }
        default:
            break;
        }
    }

    void MainWindow::OnSize()
    {
        LayoutChildren();
    }

    void MainWindow::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const
    {
        minMaxInfo->ptMinTrackSize.x = kMinWindowWidth;
        minMaxInfo->ptMinTrackSize.y = kMinWindowHeight;
    }

    bool MainWindow::IsOverSplitter(int x) const
    {
        return x >= leftPaneWidth_ && x < leftPaneWidth_ + kSplitterWidth;
    }

    void MainWindow::OnLButtonDown(int x, int y)
    {
        // Toolbar hit test
        if (y < kActionStripHeight)
        {
            const int hit = ToolbarHitTest(x, y);
            if (hit >= 0)
            {
                toolbarPressedIndex_ = hit;
                InvalidateToolbarStrip();
                SetCapture(hwnd_);
                return;
            }
        }

        if (IsOverSplitter(x))
        {
            dragMode_ = DragMode::Splitter;
            SetCapture(hwnd_);
        }
    }

    void MainWindow::OnLButtonDoubleClick(int x, int y)
    {
        (void)y;
        if (IsOverSplitter(x))
        {
            leftPaneWidth_ = kDefaultLeftPaneWidth;
            util::LogInfo(L"Reset splitter to default width");
            LayoutChildren();
        }
    }

    void MainWindow::OnLButtonUp()
    {
        if (toolbarPressedIndex_ >= 0)
        {
            const int pressedIdx = toolbarPressedIndex_;
            toolbarPressedIndex_ = -1;
            ReleaseCapture();
            InvalidateToolbarStrip();

            // Confirm click: mouse must still be over the same item
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            if (ToolbarHitTest(pt.x, pt.y) == pressedIdx)
            {
                ToolbarHandleClick(pressedIdx);
            }
            return;
        }

        if (dragMode_ != DragMode::None)
        {
            dragMode_ = DragMode::None;
            ReleaseCapture();
        }
    }

    void MainWindow::OnMouseMove(int x, int y)
    {
        // Track mouse leave for toolbar hover reset
        if (!toolbarMouseTracking_)
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
            toolbarMouseTracking_ = true;
        }

        // Toolbar hover
        if (y < kActionStripHeight && dragMode_ == DragMode::None)
        {
            const int hit = ToolbarHitTest(x, y);
            if (hit != toolbarHotIndex_)
            {
                toolbarHotIndex_ = hit;
                InvalidateToolbarStrip();
            }

            // Relay to tooltip
            if (tooltipControl_)
            {
                MSG msg{};
                msg.hwnd = hwnd_;
                msg.message = WM_MOUSEMOVE;
                msg.lParam = MAKELPARAM(x, y);
                SendMessageW(tooltipControl_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
            }
            return;
        }

        // Clear toolbar hover when mouse leaves strip
        if (toolbarHotIndex_ >= 0)
        {
            toolbarHotIndex_ = -1;
            InvalidateToolbarStrip();
        }

        if (dragMode_ == DragMode::Splitter)
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int maxLeft = std::max(kMinLeftPaneWidth, static_cast<int>(client.right) - kMinRightPaneWidth - kSplitterWidth);
            leftPaneWidth_ = std::clamp(x, kMinLeftPaneWidth, maxLeft);
            LayoutChildren();
        }
        else
        {
            SetCursor(LoadCursorW(nullptr, IsOverSplitter(x) ? IDC_SIZEWE : IDC_ARROW));
        }
    }

    LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_GETMINMAXINFO:
            OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_APP + 1:
            LayoutChildren();
            return 0;
        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ApplyTheme();
            return 0;
        }
        case WM_LBUTTONDOWN:
            OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDBLCLK:
            OnLButtonDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            OnLButtonUp();
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (dragMode_ == DragMode::Splitter || IsOverSplitter(point.x))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            break;
        }
        case services::FolderEnumerationService::kMessageId:
            return OnFolderEnumerationMessage(lParam);
        case services::FolderTreeEnumerationService::kMessageId:
            return OnFolderTreeEnumerationMessage(lParam);
        case services::FolderWatchService::kMessageId:
            return OnFolderWatchMessage(lParam);
        case browser::BrowserPane::kStateChangedMessage:
            return OnBrowserPaneStateMessage(wParam, lParam);
        case browser::BrowserPane::kOpenItemMessage:
            return OnBrowserPaneOpenItemMessage(wParam, lParam);
        case browser::BrowserPane::kContextMenuMessage:
            return OnBrowserPaneContextMenuMessage(wParam, lParam);
        case services::BatchConvertService::kMessageId:
            return OnBatchConvertMessage(lParam);
        case services::FileOperationService::kMessageId:
            return OnFileOperationMessage(lParam);
        case services::ThumbnailScheduler::kMessageId:
            return OnDetailsPanelThumbnailMessage(lParam);
        case viewer::ViewerWindow::kZoomChangedMessage:
            return OnViewerZoomMessage(lParam);
        case viewer::ViewerWindow::kActivityChangedMessage:
            return OnViewerActivityMessage(lParam);
        case viewer::ViewerWindow::kClosedMessage:
            return OnViewerClosedMessage();
        case WM_DRAWITEM:
            break;
        case WM_MOUSELEAVE:
            toolbarMouseTracking_ = false;
            if (toolbarHotIndex_ >= 0)
            {
                toolbarHotIndex_ = -1;
                InvalidateToolbarStrip();
            }
            break;
        case WM_NOTIFY:
        {
            const auto* nmh = reinterpret_cast<NMHDR*>(lParam);
            if (nmh->hwndFrom == tooltipControl_ && nmh->code == TTN_GETDISPINFOW)
            {
                auto* di = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                const auto idx = static_cast<std::size_t>(di->hdr.idFrom);
                if (idx < toolbarItems_.size() && !toolbarItems_[idx].tooltip.empty())
                {
                    di->lpszText = const_cast<wchar_t*>(toolbarItems_[idx].tooltip.c_str());
                }
                return 0;
            }
            return OnFolderTreeNotify(lParam);
        }
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lParam) == filterEdit_)
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.actionFieldBackground);
                return reinterpret_cast<INT_PTR>(actionFieldBrush_ ? actionFieldBrush_ : backgroundBrush_);
            }
            if (reinterpret_cast<HWND>(lParam) == detailsPanelText_)
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.paneBackground);
                return reinterpret_cast<INT_PTR>(detailsPanelBrush_ ? detailsPanelBrush_ : backgroundBrush_);
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == detailsPanelText_)
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.paneBackground);
                return reinterpret_cast<INT_PTR>(detailsPanelBrush_ ? detailsPanelBrush_ : backgroundBrush_);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_ACTION_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE && browserPaneController_)
            {
                const int textLength = GetWindowTextLengthW(filterEdit_);
                std::wstring filterText(static_cast<std::size_t>(textLength) + 1, L'\0');
                GetWindowTextW(filterEdit_, filterText.data(), static_cast<int>(filterText.size()));
                filterText.resize(wcslen(filterText.c_str()));
                browserPaneController_->SetFilterQuery(std::move(filterText));
                return 0;
            }
            if (HandleCommand(LOWORD(wParam)))
            {
                return 0;
            }
            break;
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            HDC eraseDC = reinterpret_cast<HDC>(wParam);
            // Exclude the action strip — PaintToolbar handles it
            RECT below{0, kActionStripHeight, client.right, client.bottom};
            FillRect(
                eraseDC,
                &below,
                backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            RECT client{};
            GetClientRect(hwnd_, &client);

            // Double-buffered toolbar paint
            RECT stripRect{0, 0, client.right, kActionStripHeight};
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, client.right, kActionStripHeight);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
            PaintToolbar(memDC, stripRect);
            BitBlt(hdc, 0, 0, client.right, kActionStripHeight, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            // Splitter
            RECT splitterRect{leftPaneWidth_, kActionStripHeight, leftPaneWidth_ + kSplitterWidth, client.bottom};
            const HBRUSH splitterBrush = CreateSolidBrush(GetThemePalette().splitter);
            FillRect(hdc, &splitterRect, splitterBrush);
            DeleteObject(splitterBrush);

            PaintDetailsPanel(hdc, client);

            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_QUERYENDSESSION:
            // Persist state before the session ends. WM_DESTROY is not guaranteed during
            // a forced shutdown / Windows Update / sign-out.
            SaveWindowState();
            return TRUE;
        case WM_ENDSESSION:
            // Save again on a confirmed end-session in case any state changed between the
            // query and the actual termination. Cheap and idempotent.
            if (wParam)
            {
                SaveWindowState();
            }
            return 0;
        case WM_DESTROY:
            if (folderEnumerationService_)
            {
                folderEnumerationService_->Cancel();
            }
            if (folderTreeEnumerationService_)
            {
                folderTreeEnumerationService_->CancelAll();
            }
            if (folderWatchService_)
            {
                folderWatchService_->Stop();
            }
            if (batchConvertService_)
            {
                batchConvertService_->Cancel();
            }
            SaveWindowState();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
