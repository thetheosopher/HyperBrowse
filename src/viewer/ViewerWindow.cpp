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
#include "services/ImageMetadataService.h"
#include "util/ResourcePng.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/Timing.h"

namespace
{
    constexpr CLSID kGaussianBlurEffectClsid =
        {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
    constexpr CLSID kDirectionalBlurEffectClsid =
        {0x174319a6, 0x58e9, 0x49b2, {0xbb, 0x63, 0xca, 0xf2, 0xc8, 0x11, 0xa3, 0xdb}};
    constexpr CLSID kColorMatrixEffectClsid =
        {0x921f03d6, 0x641c, 0x47df, {0x85, 0x2d, 0xb4, 0xbb, 0x61, 0x53, 0xae, 0x11}};

    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";
    constexpr wchar_t kRegistryValueViewerInfoOverlayTextSize[] = L"ViewerInfoOverlayTextSize";
    constexpr wchar_t kRegistryValueViewerFullMetadataVisible[] = L"ViewerFullMetadataVisible";
    constexpr int kPlaceholderBrandArtSize = 256;
    constexpr bool kEnableFullImagePrefetch = false;
    constexpr std::size_t kViewerConservativeBackgroundQueueCapacity = 16;
    constexpr std::size_t kViewerBalancedBackgroundQueueCapacity = 32;
    constexpr std::size_t kViewerPerformanceBackgroundQueueCapacity = 64;
    constexpr std::size_t kViewerAggressiveBackgroundQueueCapacity = 256;
    constexpr double kKeyboardPanStep = 64.0;
    constexpr double kMaximumZoomScale = 64.0;
    constexpr int kContextMenuItemHeight = 28;
    constexpr int kContextMenuSeparatorHeight = 10;
    constexpr int kContextMenuCheckColumnWidth = 24;
    constexpr int kContextMenuTextPadding = 12;
    constexpr int kContextMenuShortcutGap = 24;
    constexpr int kContextMenuMeasurementAllowance = 8;

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
        static hyperbrowse::cache::ThumbnailCache cache(
            hyperbrowse::util::ResolveViewerFullImageCacheCapacityBytes(
                hyperbrowse::util::ResourceProfile::Balanced));
        return cache;
    }

    std::size_t ViewerBackgroundQueueCapacity(hyperbrowse::util::ResourceProfile profile) noexcept
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

    std::size_t EffectiveViewerFullImageCacheCapacity(hyperbrowse::util::ResourceProfile profile,
                                                       bool memoryPressureActive) noexcept
    {
        const std::size_t resolvedCapacity = hyperbrowse::util::ResolveViewerFullImageCacheCapacityBytes(profile);
        return memoryPressureActive
            ? std::max<std::size_t>(1, resolvedCapacity / 2)
            : resolvedCapacity;
    }

    float SmoothStep(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - (2.0f * value));
    }

    D2D1_MATRIX_5X4_F LerpColorMatrix(const D2D1_MATRIX_5X4_F& start,
                                      const D2D1_MATRIX_5X4_F& end,
                                      float amount)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        const auto lerp = [amount](float a, float b)
        {
            return a + ((b - a) * amount);
        };

        return D2D1::Matrix5x4F(
            lerp(start._11, end._11), lerp(start._12, end._12), lerp(start._13, end._13), lerp(start._14, end._14),
            lerp(start._21, end._21), lerp(start._22, end._22), lerp(start._23, end._23), lerp(start._24, end._24),
            lerp(start._31, end._31), lerp(start._32, end._32), lerp(start._33, end._33), lerp(start._34, end._34),
            lerp(start._41, end._41), lerp(start._42, end._42), lerp(start._43, end._43), lerp(start._44, end._44),
            lerp(start._51, end._51), lerp(start._52, end._52), lerp(start._53, end._53), lerp(start._54, end._54));
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

    struct MetadataReadyResult
    {
        std::uint64_t requestId{};
        int index{-1};
        std::shared_ptr<const hyperbrowse::services::ImageMetadata> metadata;
        std::wstring text;
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

    HFONT CreateViewerMenuFont(hyperbrowse::util::AppTextSize size)
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

    HFONT MenuFontOrDefault(HFONT menuFont)
    {
        return menuFont ? menuFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    int MeasureMenuTextWidth(HFONT font, const std::wstring& text)
    {
        if (!font || text.empty())
        {
            return 0;
        }

        HDC dc = GetDC(nullptr);
        if (!dc)
        {
            return 0;
        }

        const HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, oldFont);
        ReleaseDC(nullptr, dc);
        return size.cx;
    }

    int MeasureMenuTextHeight(HFONT font)
    {
        if (!font)
        {
            return 0;
        }

        HDC dc = GetDC(nullptr);
        if (!dc)
        {
            return 0;
        }

        const HGDIOBJ oldFont = SelectObject(dc, font);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, oldFont);
        ReleaseDC(nullptr, dc);
        return metrics.tmHeight;
    }

    void SplitMenuDisplayText(const std::wstring& text, std::wstring* label, std::wstring* shortcut)
    {
        if (!label || !shortcut)
        {
            return;
        }

        const std::size_t separator = text.find(L'\t');
        if (separator == std::wstring::npos)
        {
            *label = text;
            shortcut->clear();
            return;
        }

        *label = text.substr(0, separator);
        *shortcut = text.substr(separator + 1);
    }

    COLORREF BlendMenuColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
    {
        const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
        return RGB(
            (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
            (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
            (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
    }

    float MetadataPanelFillAlpha(bool darkTheme)
    {
        return darkTheme ? 0.68f : 0.74f;
    }

    float MetadataPanelBorderAlpha(bool darkTheme)
    {
        return darkTheme ? 0.88f : 0.82f;
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

    InfoOverlayTextSize NormalizeInfoOverlayTextSize(DWORD value)
    {
        switch (static_cast<InfoOverlayTextSize>(value))
        {
        case InfoOverlayTextSize::Small:
        case InfoOverlayTextSize::Medium:
        case InfoOverlayTextSize::Large:
            return static_cast<InfoOverlayTextSize>(value);
        default:
            return InfoOverlayTextSize::Small;
        }
    }

    InfoOverlayTextSize LoadViewerInfoOverlayTextSizeSetting()
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return InfoOverlayTextSize::Small;
        }

        DWORD value = static_cast<DWORD>(InfoOverlayTextSize::Small);
        const bool foundValue = TryReadDwordValue(key, kRegistryValueViewerInfoOverlayTextSize, &value);
        RegCloseKey(key);
        return foundValue ? NormalizeInfoOverlayTextSize(value) : InfoOverlayTextSize::Small;
    }

    void SaveViewerInfoOverlayTextSizeSetting(InfoOverlayTextSize size)
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

        WriteDwordValue(key, kRegistryValueViewerInfoOverlayTextSize, static_cast<DWORD>(size));
        RegCloseKey(key);
    }

    bool LoadViewerFullMetadataVisibleSetting()
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD value = 0;
        const bool foundValue = TryReadDwordValue(key, kRegistryValueViewerFullMetadataVisible, &value);
        RegCloseKey(key);
        return foundValue && value != 0;
    }

    void SaveViewerFullMetadataVisibleSetting(bool visible)
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

        WriteDwordValue(key, kRegistryValueViewerFullMetadataVisible, visible ? 1UL : 0UL);
        RegCloseKey(key);
    }

    std::wstring BuildMetadataOverlayText(const hyperbrowse::browser::BrowserItem& item,
                                          const hyperbrowse::services::ImageMetadata* metadata,
                                          const std::wstring& errorMessage)
    {
        std::wstring text = hyperbrowse::services::FormatImageInfoContent(item);
        text.append(L"\r\n\r\n");

        if (metadata)
        {
            const std::wstring expanded = hyperbrowse::services::FormatImageInfoExpanded(*metadata);
            if (!expanded.empty())
            {
                text.append(expanded);
                return text;
            }
        }

        if (!errorMessage.empty())
        {
            text.append(errorMessage);
            return text;
        }

        text.append(L"No embedded EXIF, IPTC, or XMP metadata is available for this image.");
        return text;
    }

    float MeasureWrappedTextHeight(IDWriteFactory* dwriteFactory,
                                   IDWriteTextFormat* textFormat,
                                   const std::wstring& text,
                                   float maxWidth,
                                   float maxHeight)
    {
        if (!dwriteFactory || !textFormat || text.empty() || maxWidth <= 0.0f || maxHeight <= 0.0f)
        {
            return 0.0f;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
        if (FAILED(dwriteFactory->CreateTextLayout(text.c_str(),
                                                   static_cast<UINT32>(text.size()),
                                                   textFormat,
                                                   maxWidth,
                                                   maxHeight,
                                                   textLayout.GetAddressOf()))
            || !textLayout)
        {
            return 0.0f;
        }

        DWRITE_TEXT_METRICS metrics{};
        return SUCCEEDED(textLayout->GetMetrics(&metrics)) ? metrics.height : 0.0f;
    }

    const ViewerOverlayMetrics& ViewerOverlayMetricsForTextSize(InfoOverlayTextSize size)
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
            30.0f,
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

    using NavigationCursorPolygon = std::array<POINT, 7>;

    NavigationCursorPolygon MakeBaseNavigationCursorPolygon(int width, int height)
    {
        const int paddingX = std::max(3, width / 8);
        const int paddingY = std::max(4, height / 8);
        const int centerY = height / 2;
        const int shaftHalfHeight = std::max(3, height / 10);
        const int headBaseX = std::max(paddingX + 4, width * 7 / 16);
        const int tailX = width - paddingX - 1;

        return {{
            POINT{paddingX, centerY},
            POINT{headBaseX, paddingY},
            POINT{headBaseX, centerY - shaftHalfHeight},
            POINT{tailX, centerY - shaftHalfHeight},
            POINT{tailX, centerY + shaftHalfHeight},
            POINT{headBaseX, centerY + shaftHalfHeight},
            POINT{headBaseX, height - paddingY - 1},
        }};
    }

    NavigationCursorPolygon MirrorNavigationCursorPolygon(const NavigationCursorPolygon& polygon, int width)
    {
        NavigationCursorPolygon mirrored = polygon;
        for (POINT& point : mirrored)
        {
            point.x = (width - 1) - point.x;
        }

        return mirrored;
    }

    bool PointInPolygon(const NavigationCursorPolygon& polygon, double x, double y)
    {
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            const double currentX = static_cast<double>(polygon[i].x);
            const double currentY = static_cast<double>(polygon[i].y);
            const double previousX = static_cast<double>(polygon[j].x);
            const double previousY = static_cast<double>(polygon[j].y);
            const bool intersects = ((currentY > y) != (previousY > y))
                && (x < (((previousX - currentX) * (y - currentY)) / (previousY - currentY) + currentX));
            if (intersects)
            {
                inside = !inside;
            }
        }

        return inside;
    }

    HCURSOR CreateViewerNavigationCursor(bool previous)
    {
        const int width = std::max(22, (GetSystemMetrics(SM_CXCURSOR) * 7) / 10);
        const int height = std::max(22, (GetSystemMetrics(SM_CYCURSOR) * 7) / 10);

        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = width;
        header.bV5Height = -height;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;

        void* bitmapBits = nullptr;
        HDC screenDc = GetDC(nullptr);
        HBITMAP colorBitmap = CreateDIBSection(screenDc,
                                               reinterpret_cast<BITMAPINFO*>(&header),
                                               DIB_RGB_COLORS,
                                               &bitmapBits,
                                               nullptr,
                                               0);
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }
        if (!colorBitmap || !bitmapBits)
        {
            if (colorBitmap)
            {
                DeleteObject(colorBitmap);
            }
            return nullptr;
        }

        HBITMAP maskBitmap = CreateBitmap(width, height, 1, 1, nullptr);
        if (!maskBitmap)
        {
            DeleteObject(colorBitmap);
            return nullptr;
        }

        NavigationCursorPolygon polygon = MakeBaseNavigationCursorPolygon(width, height);
        if (!previous)
        {
            polygon = MirrorNavigationCursorPolygon(polygon, width);
        }

        std::vector<std::uint8_t> fillMask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (PointInPolygon(polygon, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5))
                {
                    fillMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 1;
                }
            }
        }

        auto* pixels = static_cast<std::uint32_t*>(bitmapBits);
        constexpr std::uint32_t fillColor = 0xFFF7F9FCu;
        constexpr std::uint32_t outlineColor = 0xFF11161Bu;
        std::fill(pixels, pixels + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height)), 0u);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (fillMask[index] != 0)
                {
                    pixels[index] = fillColor;
                }
            }
        }

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (fillMask[index] != 0)
                {
                    continue;
                }

                bool touchesFilledPixel = false;
                for (int offsetY = -1; offsetY <= 1 && !touchesFilledPixel; ++offsetY)
                {
                    for (int offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        const int neighborX = x + offsetX;
                        const int neighborY = y + offsetY;
                        if (neighborX < 0 || neighborY < 0 || neighborX >= width || neighborY >= height)
                        {
                            continue;
                        }

                        const std::size_t neighborIndex = static_cast<std::size_t>(neighborY) * static_cast<std::size_t>(width)
                            + static_cast<std::size_t>(neighborX);
                        if (fillMask[neighborIndex] != 0)
                        {
                            touchesFilledPixel = true;
                            break;
                        }
                    }
                }

                if (touchesFilledPixel)
                {
                    pixels[index] = outlineColor;
                }
            }
        }

        ICONINFO cursorInfo{};
        cursorInfo.fIcon = FALSE;
        cursorInfo.xHotspot = static_cast<DWORD>(polygon[0].x);
        cursorInfo.yHotspot = static_cast<DWORD>(polygon[0].y);
        cursorInfo.hbmMask = maskBitmap;
        cursorInfo.hbmColor = colorBitmap;

        HCURSOR cursor = CreateIconIndirect(&cursorInfo);
        DeleteObject(maskBitmap);
        DeleteObject(colorBitmap);
        return cursor;
    }

    bool ExceedsPanDragThreshold(POINT origin, POINT current)
    {
        const int dragThresholdX = std::max(4, GetSystemMetrics(SM_CXDRAG));
        const int dragThresholdY = std::max(4, GetSystemMetrics(SM_CYDRAG));
        return std::abs(current.x - origin.x) >= dragThresholdX
            || std::abs(current.y - origin.y) >= dragThresholdY;
    }

    bool CopyTextToClipboardLocal(HWND ownerWindow, const std::wstring& text)
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

}

namespace hyperbrowse::viewer
{
    bool ViewerWindow::DefaultInfoOverlaysVisible()
    {
        return LoadViewerInfoOverlaysVisibleSetting();
    }

    InfoOverlayTextSize ViewerWindow::DefaultOverlayTextSize()
    {
        return LoadViewerInfoOverlayTextSizeSetting();
    }

    bool ViewerWindow::DefaultFullMetadataVisible()
    {
        return LoadViewerFullMetadataVisibleSetting();
    }

    void ViewerWindow::SetDefaultInfoOverlaysVisible(bool visible)
    {
        SaveViewerInfoOverlaysVisibleSetting(visible);
    }

    void ViewerWindow::SetDefaultOverlayTextSize(InfoOverlayTextSize size)
    {
        SaveViewerInfoOverlayTextSizeSetting(size);
    }

    void ViewerWindow::SetDefaultFullMetadataVisible(bool visible)
    {
        SaveViewerFullMetadataVisibleSetting(visible);
    }

    ViewerWindow::ViewerWindow(HINSTANCE instance)
        : instance_(instance)
        , asyncState_(std::make_shared<AsyncState>())
        , infoOverlaysVisible_(LoadViewerInfoOverlaysVisibleSetting())
        , infoOverlayTextSize_(LoadViewerInfoOverlayTextSizeSetting())
        , fullMetadataVisible_(LoadViewerFullMetadataVisibleSetting())
    {
        menuFont_ = CreateViewerMenuFont(appTextSize_);
        backgroundBrush_ = CreateSolidBrush(BackgroundColor(false));
        statusArt_ = util::LoadPngResourceBitmap(instance_,
                                                 IDB_HYPERBROWSE_BRAND_PNG,
                                                 kPlaceholderBrandArtSize,
                                                 kPlaceholderBrandArtSize);
        // Six workers: one for foreground image decode, up to four for adjacent prefetches,
        // one spare. This allows prefetch to degrade gracefully without starving the main
        // image load when many adjacent images need decoding during rapid delete sequences.
        backgroundExecutor_ = std::make_unique<util::BackgroundExecutor>(
            6,
            ViewerBackgroundQueueCapacity(resourceProfile_));
    }

    ViewerWindow::~ViewerWindow()
    {
        asyncState_->shutdown.store(true, std::memory_order_release);
        asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
        asyncState_->targetWindow.store(nullptr, std::memory_order_release);
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

        if (menuFont_ && menuFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(menuFont_);
        }

        if (previousNavigationCursor_)
        {
            DestroyCursor(previousNavigationCursor_);
        }

        if (nextNavigationCursor_)
        {
            DestroyCursor(nextNavigationCursor_);
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
        ClearWraparoundMessage();
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
            windowFitMode_ = WindowFitMode::Regular;
            hasRegularPlacementBeforeFit_ = false;
            regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
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

            DragAcceptFiles(hwnd_, TRUE);
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
        NotifyCurrentItemChanged();
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

    InfoOverlayTextSize ViewerWindow::OverlayTextSize() const noexcept
    {
        return infoOverlayTextSize_;
    }

    bool ViewerWindow::IsFullMetadataVisible() const noexcept
    {
        return fullMetadataVisible_;
    }

    void ViewerWindow::StartSlideshow(UINT intervalMs)
    {
        if (!hwnd_ || items_.size() < 2)
        {
            return;
        }

        slideshowIntervalMs_ = std::max<UINT>(250, intervalMs);
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
            ClampPanOffsets();
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
        ClampPanOffsets();
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

    void ViewerWindow::SetKeyboardPanningInverted(bool inverted) noexcept
    {
        keyboardPanningInverted_ = inverted;
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
        if (fullMetadataVisible_ == visible)
        {
            return;
        }

        fullMetadataVisible_ = visible;
        SaveViewerFullMetadataVisibleSetting(fullMetadataVisible_);
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
        NotifyCurrentItemChanged();
        ClearWraparoundMessage();
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

    bool ViewerWindow::GetDeleteCurrentPaths(std::wstring* sourcePath, std::wstring* preferredFocusPath) const
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

        const int preferredIndex = (currentIndex_ + 1 < static_cast<int>(items_.size()))
            ? currentIndex_ + 1
            : currentIndex_ - 1;
        if (preferredFocusPath && preferredIndex >= 0 && preferredIndex < static_cast<int>(items_.size()))
        {
            *preferredFocusPath = items_[static_cast<std::size_t>(preferredIndex)].filePath;
        }
        return true;
    }

    bool ViewerWindow::AdvanceAfterDeleteCurrent()
    {
        util::ScopedTimer functionTimer(L"ViewerWindow::AdvanceAfterDeleteCurrent");
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return false;
        }

        // Capture whether adjacent prefetch slots already hold the images that
        // will become the new current item after the erase, so we can reuse them
        // and avoid a redundant full decode.
        const int nextIndexOld = currentIndex_ + 1;
        const int prevIndexOld = currentIndex_ - 1;
        const bool canReuseNext = nextIndexOld < static_cast<int>(items_.size())
            && nextSlot_.index == nextIndexOld
            && nextSlot_.image;
        const bool canReusePrev = prevIndexOld >= 0
            && previousSlot_.index == prevIndexOld
            && previousSlot_.image;

        items_.erase(items_.begin() + currentIndex_);
        if (items_.empty())
        {
            const HWND viewerHwnd = hwnd_;
            if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
            {
                PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
            }
            return true;
        }

        const bool wentBackward = currentIndex_ >= static_cast<int>(items_.size());
        if (wentBackward)
        {
            currentIndex_ = static_cast<int>(items_.size()) - 1;
        }
        NotifyCurrentItemChanged();
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

        // The paint-time bitmap cache is keyed on currentIndex_ (see
        // ensureCurrentBitmap / BeginTransitionFromPending). Deleting an item
        // shifts every subsequent item down by one, so currentIndex_ can end up
        // numerically unchanged even though the image *at* that index is now a
        // completely different file. That produces a false-positive cache hit
        // that skips re-uploading the D2D bitmap, leaving the stale (deleted)
        // image on screen until some other navigation forces a refresh. Force
        // invalidation here so the next paint always re-uploads.
        d2dCurrentImageBitmap_.Reset();
        d2dCurrentImageIndex_ = -1;

        // Fast path: reuse the prefetched image that is now the current item.
        const bool reusePrefetch = (!wentBackward && canReuseNext)
                                || ( wentBackward && canReusePrev);
        util::LogInfo(L"ViewerWindow::AdvanceAfterDeleteCurrent path="
            + std::wstring(reusePrefetch ? L"FAST(prefetch-reuse)" : L"SLOW(needs-decode)")
            + L", newCurrentIndex=" + std::to_wstring(currentIndex_)
            + L", itemCount=" + std::to_wstring(items_.size()));
        if (reusePrefetch)
        {
            // Extract the image before touching the slots — sourceSlot is an alias
            // into nextSlot_/previousSlot_ and would be invalidated by clearing them.
            auto reusedImage = (!wentBackward) ? nextSlot_.image : previousSlot_.image;

            // Cancel any in-flight decode so stale results are not applied.
            asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);

            previousSlot_ = {};
            nextSlot_ = {};
            SetCurrentImageSlot(currentIndex_, std::move(reusedImage), true);
            prefetchHitCount_.fetch_add(1, std::memory_order_acq_rel);
            util::IncrementCounter(L"viewer.prefetch.hit");
            util::RecordTiming(L"viewer.navigation", 0.0);
            PrepareForImageChange();
            loading_ = false;
            errorMessage_.clear();
            currentImage_ = currentSlot_.image;
            BeginTransitionFromPending();
            const std::uint64_t navigationGeneration =
                asyncState_->navigationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            ScheduleAdjacentPrefetch(navigationGeneration);
            if (hwnd_)
            {
                RequestRepaint();
            }
            return true;
        }

        // Slow path: no prefetched image is available yet. Mirror Navigate()'s
        // cache-miss behavior by leaving currentImage_/currentSlot_ untouched so the
        // last-displayed image keeps showing (instead of blanking to a "loading"
        // placeholder) while the new current image decodes in the background.
        // Only the now-stale adjacent prefetch slots need to be dropped.
        previousSlot_ = {};
        nextSlot_ = {};
        d2dCompareImageBitmap_.Reset();
        d2dCompareImageIndex_ = -1;
        ResetPrefetchStatistics();
        ReapCompletedBackgroundTasks();
        UpdateWindowTitle();
        LoadCurrentImageAsync(LoadReason::Navigation);
        return true;
    }

    void ViewerWindow::RecoverDisplaySurface()
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        ReleaseD2DResources();
        EnsureD2DRenderTarget();
        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
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
            RebuildD2DTextFormats();

            if (statusArt_)
            {
                d2dStatusArtBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *statusArt_);
            }
        }
    }

    void ViewerWindow::RebuildD2DTextFormats()
    {
        d2dNameFormat_.Reset();
        d2dInfoFormat_.Reset();
        d2dBottomInfoFormat_.Reset();
        d2dMetadataFormat_.Reset();

        if (!d2dRenderTarget_)
        {
            return;
        }

        auto& renderer = render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return;
        }

        const ViewerOverlayMetrics& overlayMetrics = ViewerOverlayMetricsForTextSize(infoOverlayTextSize_);
        d2dNameFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.nameFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        d2dInfoFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.infoFontSize, DWRITE_FONT_WEIGHT_NORMAL);
    d2dBottomInfoFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.bottomInfoFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    d2dMetadataFormat_ = renderer.CreateTextFormat(L"Segoe UI", overlayMetrics.metadataFontSize, DWRITE_FONT_WEIGHT_NORMAL);

        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        if (d2dNameFormat_)
        {
            d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dNameFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dNameFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dInfoFormat_)
        {
            d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dInfoFormat_->SetTrimming(&trimming, nullptr);
            d2dInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (d2dBottomInfoFormat_)
        {
            d2dBottomInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dBottomInfoFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dBottomInfoFormat_->SetTrimming(&trimming, nullptr);
            d2dBottomInfoFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (d2dMetadataFormat_)
        {
            d2dMetadataFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dMetadataFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dMetadataFormat_->SetTrimming(&trimming, nullptr);
            d2dMetadataFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
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
        d2dBottomInfoFormat_.Reset();
        d2dMetadataFormat_.Reset();
        d2dBackgroundBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dPanelFillBrush_.Reset();
        d2dPanelBorderBrush_.Reset();
        d2dMetadataPanelFillBrush_.Reset();
        d2dMetadataPanelBorderBrush_.Reset();
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
        d2dMetadataPanelFillBrush_.Reset();
        d2dMetadataPanelBorderBrush_.Reset();

        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(BackgroundColor(darkTheme_)), d2dBackgroundBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(TextColor(darkTheme_)), d2dTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(MutedTextColor(darkTheme_)), d2dMutedTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelFillColor(darkTheme_)), d2dPanelFillBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelBorderColor(darkTheme_)), d2dPanelBorderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelFillColor(darkTheme_), MetadataPanelFillAlpha(darkTheme_)),
                                                d2dMetadataPanelFillBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(PanelBorderColor(darkTheme_), MetadataPanelBorderAlpha(darkTheme_)),
                                                d2dMetadataPanelBorderBrush_.GetAddressOf());
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
        ClearCurrentMetadata();
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
            return 6;
        case util::ResourceProfile::Aggressive:
            return 10;
        case util::ResourceProfile::Balanced:
        default:
            return 3;
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
        ClearCurrentMetadata();
        RefreshWindowFitForCurrentImage();
        if (fullMetadataVisible_)
        {
            LoadMetadataAsyncForIndex(index);
        }
    }

    void ViewerWindow::ClearCurrentMetadata(bool invalidateRequest)
    {
        if (invalidateRequest)
        {
            metadataRequestId_.fetch_add(1, std::memory_order_acq_rel);
        }

        metadataLoading_ = false;
        currentMetadataIndex_ = -1;
        currentMetadata_.reset();
        currentMetadataText_.clear();
    }

    void ViewerWindow::LoadMetadataAsyncForIndex(int index)
    {
        if (!fullMetadataVisible_)
        {
            return;
        }

        if (index < 0 || index >= static_cast<int>(items_.size()))
        {
            ClearCurrentMetadata();
            return;
        }

        currentMetadataIndex_ = index;
        currentMetadata_.reset();
        currentMetadataText_.clear();
        metadataLoading_ = true;

        const browser::BrowserItem item = items_[static_cast<std::size_t>(index)];
        const std::uint64_t requestId = metadataRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        asyncState_->targetWindow.store(hwnd_, std::memory_order_release);

        if (!backgroundExecutor_
            || !backgroundExecutor_->Post([asyncState = asyncState_, metadataRequestId = &metadataRequestId_, item, index, requestId]()
            {
                std::wstring errorMessage;
                std::shared_ptr<const hyperbrowse::services::ImageMetadata> metadata;
                try
                {
                    metadata = hyperbrowse::services::ExtractImageMetadata(item, &errorMessage);
                }
                catch (const std::exception&)
                {
                    errorMessage = L"Metadata extraction failed unexpectedly.";
                }
                catch (...)
                {
                    errorMessage = L"Metadata extraction failed unexpectedly.";
                }

                if (asyncState->shutdown.load(std::memory_order_acquire)
                    || metadataRequestId->load(std::memory_order_acquire) != requestId)
                {
                    return;
                }

                auto update = std::make_unique<MetadataReadyResult>();
                update->requestId = requestId;
                update->index = index;
                update->metadata = std::move(metadata);
                update->text = BuildMetadataOverlayText(item, update->metadata.get(), errorMessage);

                const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
                if (!targetWindow || !PostMessageW(targetWindow, ViewerWindow::kMetadataReadyMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            }))
        {
            metadataLoading_ = false;
            currentMetadataText_ = BuildMetadataOverlayText(item, nullptr, L"Metadata loading is unavailable.");
        }

        if (hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            RequestRepaint();
        }
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
        asyncState_->targetWindow.store(hwnd_, std::memory_order_release);
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
            if (!backgroundExecutor_->Post([asyncState, item, selectedIndex, requestId, navigationGeneration]()
            {
                std::wstring errorMessage;
                std::shared_ptr<const cache::CachedThumbnail> image;
                try
                {
                    image = decode::DecodeFullImage(item, &errorMessage);
                }
                catch (const std::exception&)
                {
                    errorMessage = L"Image decoding failed unexpectedly.";
                }
                catch (...)
                {
                    errorMessage = L"Image decoding failed unexpectedly.";
                }

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

                const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
                if (!targetWindow || !PostMessageW(targetWindow, kDecodedImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            }))
            {
                loading_ = false;
                pendingLoadActive_ = false;
                errorMessage_ = L"Image loading is unavailable.";
                if (hwnd_)
                {
                    RequestRepaint();
                }
            }
        }
    }

    void ViewerWindow::Navigate(int delta)
    {
        slideshowAdvancePending_ = false;

        if (items_.empty())
        {
            return;
        }

        const int itemCount = static_cast<int>(items_.size());
        int nextIndex = currentIndex_ + delta;
        bool wrapped = false;
        if (nextIndex < 0)
        {
            nextIndex = itemCount - 1;
            wrapped = true;
        }
        else if (nextIndex >= itemCount)
        {
            nextIndex = 0;
            wrapped = true;
        }

        if (nextIndex == currentIndex_)
        {
            return;
        }

        if (wrapped)
        {
            ShowWraparoundMessage(delta > 0);
        }

        NavigateToIndex(nextIndex, delta > 0);
    }

    void ViewerWindow::NavigateToIndex(int targetIndex, bool forward, bool slideshowNavigation)
    {
        slideshowAdvancePending_ = false;

        if (targetIndex < 0 || targetIndex >= static_cast<int>(items_.size()) || targetIndex == currentIndex_)
        {
            return;
        }

        const int nextIndex = targetIndex;
        QueueTransitionFromCurrent(forward, slideshowNavigation);

        if (forward)
        {
            if (nextSlot_.index == nextIndex && nextSlot_.image)
            {
                previousSlot_ = currentSlot_;
                SetCurrentImageSlot(nextSlot_.index, nextSlot_.image, true);
                nextSlot_ = {};
                currentIndex_ = nextIndex;
                NotifyCurrentItemChanged();
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
        else
        {
            if (previousSlot_.index == nextIndex && previousSlot_.image)
            {
                nextSlot_ = currentSlot_;
                SetCurrentImageSlot(previousSlot_.index, previousSlot_.image, true);
                previousSlot_ = {};
                currentIndex_ = nextIndex;
                NotifyCurrentItemChanged();
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
        NotifyCurrentItemChanged();
        LoadCurrentImageAsync(LoadReason::Navigation);
    }

    int ViewerWindow::NavigationDeltaForPoint(POINT point) const noexcept
    {
        if (!hwnd_ || items_.size() < 2)
        {
            return 0;
        }

        RECT clientRect{};
        if (GetClientRect(hwnd_, &clientRect) == FALSE)
        {
            return 0;
        }

        const int clientWidth = clientRect.right - clientRect.left;
        if (clientWidth < 8)
        {
            return 0;
        }

        const int navigationZoneWidth = std::max(1, clientWidth / 8);
        const int leftBoundary = clientRect.left + navigationZoneWidth;
        const int rightBoundary = clientRect.right - navigationZoneWidth;
        if (point.x < leftBoundary)
        {
            return -1;
        }

        if (point.x >= rightBoundary)
        {
            return +1;
        }

        return 0;
    }

    bool ViewerWindow::SetNavigationCursorForPoint(POINT point)
    {
        const int navigationDelta = NavigationDeltaForPoint(point);
        if (navigationDelta < 0)
        {
            if (!previousNavigationCursor_)
            {
                previousNavigationCursor_ = CreateViewerNavigationCursor(true);
            }

            if (previousNavigationCursor_)
            {
                SetCursor(previousNavigationCursor_);
                return true;
            }
        }
        else if (navigationDelta > 0)
        {
            if (!nextNavigationCursor_)
            {
                nextNavigationCursor_ = CreateViewerNavigationCursor(false);
            }

            if (nextNavigationCursor_)
            {
                SetCursor(nextNavigationCursor_);
                return true;
            }
        }

        return false;
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

            const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
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
        if (!backgroundExecutor_->Post([asyncState, item, index, navigationGeneration]()
        {
            std::wstring errorMessage;
            std::shared_ptr<const cache::CachedThumbnail> image;
            try
            {
                image = decode::DecodeFullImage(item, &errorMessage);
            }
            catch (const std::exception&)
            {
                errorMessage = L"Image decoding failed unexpectedly.";
            }
            catch (...)
            {
                errorMessage = L"Image decoding failed unexpectedly.";
            }

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

            const HWND targetWindow = asyncState->targetWindow.load(std::memory_order_acquire);
            if (!targetWindow || !PostMessageW(targetWindow, ViewerWindow::kPrefetchImageMessage, 0, reinterpret_cast<LPARAM>(update.get())))
            {
                return;
            }

            update.release();
        }) && slideshowNextPrefetch)
        {
            slideshowNextPrefetchIndex_ = -1;
            slideshowNextPrefetchGeneration_ = 0;
        }
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

    void ViewerWindow::ZoomBy(double factor, const POINT* anchorPoint)
    {
        if (!currentImage_ || factor <= 0.0 || !hwnd_)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const double fitScale = FitScaleForClient(clientRect);
        const bool smoothZoomActive = smoothZoomTimerId_ != 0;
        const double baseScale = smoothZoomActive
            ? smoothZoomTarget_
            : (zoomMode_ == ZoomMode::Fit ? fitScale : customZoomScale_);
        double targetPanX = panOffsetX_;
        double targetPanY = panOffsetY_;

        double targetScale = std::clamp(baseScale * factor, std::min(baseScale, fitScale), kMaximumZoomScale);

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

        const double currentScale = smoothZoomActive
            ? smoothZoomCurrent_
            : (zoomMode_ == ZoomMode::Fit ? fitScale : customZoomScale_);

        if (anchorPoint)
        {
            const double centerX = static_cast<double>(clientRect.right - clientRect.left) / 2.0;
            const double centerY = static_cast<double>(clientRect.bottom - clientRect.top) / 2.0;
            const double imagePointX = (static_cast<double>(anchorPoint->x) - centerX - panOffsetX_) / std::max(0.01, currentScale);
            const double imagePointY = (static_cast<double>(anchorPoint->y) - centerY - panOffsetY_) / std::max(0.01, currentScale);
            targetPanX = static_cast<double>(anchorPoint->x) - centerX - (imagePointX * targetScale);
            targetPanY = static_cast<double>(anchorPoint->y) - centerY - (imagePointY * targetScale);
        }

        zoomMode_ = ZoomMode::Custom;
        smoothZoomTarget_ = targetScale;
        smoothZoomCurrent_ = currentScale;
        smoothZoomTargetPanX_ = targetPanX;
        smoothZoomTargetPanY_ = targetPanY;
        customZoomScale_ = smoothZoomCurrent_;

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

    void ViewerWindow::SetWindowFitMode(WindowFitMode mode)
    {
        if (!hwnd_)
        {
            return;
        }

        if (fullScreen_)
        {
            SetFullScreen(false);
        }

        if (mode == windowFitMode_)
        {
            if (mode != WindowFitMode::Regular)
            {
                RestoreRegularWindowPlacement();
            }
            return;
        }

        if (mode == WindowFitMode::Regular)
        {
            RestoreRegularWindowPlacement();
            return;
        }

        if (!currentImage_)
        {
            return;
        }

        if (IsIconic(hwnd_) != FALSE)
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }

        if (windowFitMode_ == WindowFitMode::Regular)
        {
            regularPlacementBeforeFit_.length = sizeof(WINDOWPLACEMENT);
            if (GetWindowPlacement(hwnd_, &regularPlacementBeforeFit_) == FALSE)
            {
                return;
            }
            hasRegularPlacementBeforeFit_ = true;
        }

        if (IsZoomed(hwnd_) != FALSE)
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }

        if (ResizeWindowForFitMode(mode))
        {
            windowFitMode_ = mode;
        }
    }

    bool ViewerWindow::ResizeWindowForFitMode(WindowFitMode mode)
    {
        if (!hwnd_ || fullScreen_ || mode == WindowFitMode::Regular || !currentImage_)
        {
            return false;
        }

        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (!monitor || GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
        {
            return false;
        }

        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE))
            & ~(WS_MAXIMIZE | WS_MINIMIZE);
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        RECT frameRect{0, 0, 0, 0};
        if (AdjustWindowRectEx(&frameRect, style, FALSE, exStyle) == FALSE)
        {
            return false;
        }

        const LONG frameWidth = frameRect.right - frameRect.left;
        const LONG frameHeight = frameRect.bottom - frameRect.top;
        const LONG workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const LONG workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const double imageWidth = static_cast<double>(swapDimensions
            ? currentImage_->SourceHeight()
            : currentImage_->SourceWidth());
        const double imageHeight = static_cast<double>(swapDimensions
            ? currentImage_->SourceWidth()
            : currentImage_->SourceHeight());
        const LONG availableClientWidth = std::max<LONG>(1, workWidth - frameWidth);
        const LONG availableClientHeight = std::max<LONG>(1, workHeight - frameHeight);
        LONG clientWidth = availableClientWidth;
        LONG clientHeight = availableClientHeight;
        if (mode == WindowFitMode::Height)
        {
            clientWidth = std::max<LONG>(1, static_cast<LONG>(std::lround(
                static_cast<double>(clientHeight) * imageWidth / std::max(1.0, imageHeight))));
        }
        else
        {
            const LONG aspectHeight = std::max<LONG>(1, static_cast<LONG>(std::lround(
                static_cast<double>(clientWidth) * imageHeight / std::max(1.0, imageWidth))));
            clientHeight = std::min(availableClientHeight, aspectHeight);
        }

        RECT desiredWindowRect{0, 0, clientWidth, clientHeight};
        if (AdjustWindowRectEx(&desiredWindowRect, style, FALSE, exStyle) == FALSE)
        {
            return false;
        }

        const LONG windowWidth = desiredWindowRect.right - desiredWindowRect.left;
        const LONG windowHeight = desiredWindowRect.bottom - desiredWindowRect.top;
        const LONG windowLeft = mode == WindowFitMode::Height
            ? monitorInfo.rcWork.left + (workWidth - windowWidth) / 2
            : monitorInfo.rcWork.left;
        const LONG windowTop = mode == WindowFitMode::Height
            ? monitorInfo.rcWork.top
            : monitorInfo.rcWork.top + std::max<LONG>(0, (workHeight - windowHeight) / 2);

        if (SetWindowPos(hwnd_, HWND_TOP,
                         windowLeft,
                         windowTop,
                         windowWidth,
                         windowHeight,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW) == FALSE)
        {
            return false;
        }
        FitToWindow();
        return true;
    }

    void ViewerWindow::RefreshWindowFitForCurrentImage()
    {
        if (windowFitMode_ != WindowFitMode::Regular)
        {
            ResizeWindowForFitMode(windowFitMode_);
        }
    }

    void ViewerWindow::RestoreRegularWindowPlacement()
    {
        if (!hwnd_ || !hasRegularPlacementBeforeFit_)
        {
            return;
        }

        SetWindowPlacement(hwnd_, &regularPlacementBeforeFit_);
        windowFitMode_ = WindowFitMode::Regular;
        hasRegularPlacementBeforeFit_ = false;
        regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
        RequestRepaint();
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
                if (windowFitMode_ != WindowFitMode::Regular)
                {
                    RestoreRegularWindowPlacement();
                }
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
        windowFitMode_ = WindowFitMode::Regular;
        hasRegularPlacementBeforeFit_ = false;
        regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
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

    void ViewerWindow::ShowContextMenu(POINT screenPoint)
    {
        if (!hwnd_ || currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return;
        }

        const std::wstring currentPath = items_[static_cast<std::size_t>(currentIndex_)].filePath;
        if (currentPath.empty())
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        AppendMenuW(menu, MF_STRING, kContextMenuCopyPath, L"Copy Pat&h\tCtrl+Shift+C");
        AppendMenuW(menu, MF_STRING, kContextMenuCopyImage, L"Copy &Image\tCtrl+Shift+I");
        AppendMenuW(menu, MF_STRING, kContextMenuRevealInExplorer, L"Reveal in &Explorer");
        AppendMenuW(menu, MF_STRING, kContextMenuImageInformation, L"Image &Information\tCtrl+I");
        AppendMenuW(menu, MF_STRING, kContextMenuProperties, L"P&roperties\tAlt+Enter");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kContextMenuSetWallpaper, L"Set as Desktop &Wallpaper");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kContextMenuDelete, L"&Delete\tDel");
        AppendMenuW(menu, MF_STRING, kContextMenuDeletePermanently, L"Delete &Permanently\tShift+Del");

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareContextMenuForOwnerDraw(menu, menuDrawItems);

        HBRUSH menuBackgroundBrush = CreateSolidBrush(PanelFillColor(darkTheme_));
        if (menuBackgroundBrush)
        {
            MENUINFO menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIM_BACKGROUND;
            menuInfo.hbrBack = menuBackgroundBrush;
            SetMenuInfo(menu, &menuInfo);
        }

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
        if (menuBackgroundBrush)
        {
            DeleteObject(menuBackgroundBrush);
        }

        if (commandId == 0)
        {
            return;
        }

        if (commandId == kContextMenuDelete || commandId == kContextMenuDeletePermanently)
        {
            // Route deletes through the owner's delete pipeline (confirmation,
            // recycle-bin handling, focus restoration) exactly like VK_DELETE.
            if (owner_ && IsWindow(owner_))
            {
                const WPARAM flags = commandId == kContextMenuDeletePermanently ? kDeleteRequestPermanent : 0;
                SendMessageW(owner_, kDeleteRequestedMessage, flags, 0);
            }
            return;
        }

        DispatchContextMenuCommand(commandId);
    }

    void ViewerWindow::MeasureContextMenuItem(MEASUREITEMSTRUCT* measureItem) const
    {
        if (!measureItem)
        {
            return;
        }

        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(measureItem->itemData);
        const auto scaleMenuDimension = [this](int dimension)
        {
            return util::ScaleAppTextDimension(dimension, appTextSize_);
        };
        if (!drawData)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = static_cast<UINT>(scaleMenuDimension(kContextMenuItemHeight));
            return;
        }

        if (drawData->separator)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = static_cast<UINT>(scaleMenuDimension(kContextMenuSeparatorHeight));
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);
        const HFONT menuFont = MenuFontOrDefault(menuFont_);
        const int textPadding = scaleMenuDimension(kContextMenuTextPadding);
        const int checkColumnWidth = scaleMenuDimension(kContextMenuCheckColumnWidth);
        const int shortcutGap = scaleMenuDimension(kContextMenuShortcutGap);
        const int measurementAllowance = scaleMenuDimension(kContextMenuMeasurementAllowance);
        const int labelWidth = MeasureMenuTextWidth(menuFont, label);
        const int shortcutWidth = shortcut.empty() ? 0 : MeasureMenuTextWidth(menuFont, shortcut);
        int itemWidth = checkColumnWidth + (textPadding * 2) + labelWidth + measurementAllowance;
        if (shortcutWidth > 0)
        {
            itemWidth += shortcutGap + shortcutWidth;
        }

        const int itemHeight = scaleMenuDimension(kContextMenuItemHeight);
        measureItem->itemWidth = static_cast<UINT>(itemWidth);
        measureItem->itemHeight = static_cast<UINT>(std::max(
            itemHeight,
            MeasureMenuTextHeight(menuFont) + measurementAllowance));
    }

    void ViewerWindow::DrawContextMenuItem(const DRAWITEMSTRUCT& drawItem) const
    {
        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(drawItem.itemData);
        if (!drawData)
        {
            return;
        }

        const auto scaleMenuDimension = [this](int dimension)
        {
            return util::ScaleAppTextDimension(dimension, appTextSize_);
        };
        const COLORREF menuBackground = PanelFillColor(darkTheme_);
        const COLORREF selectedBackground = darkTheme_
            ? RGB(54, 68, 84)
            : RGB(222, 235, 250);
        const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
        const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
        const COLORREF backgroundColor = selected ? selectedBackground : menuBackground;
        const HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
        FillRect(drawItem.hDC, &drawItem.rcItem, backgroundBrush);
        DeleteObject(backgroundBrush);

        if (drawData->separator)
        {
            const HPEN separatorPen = CreatePen(PS_SOLID, 1, PanelBorderColor(darkTheme_));
            const HGDIOBJ oldPen = SelectObject(drawItem.hDC, separatorPen);
            const int y = drawItem.rcItem.top + ((drawItem.rcItem.bottom - drawItem.rcItem.top) / 2);
            MoveToEx(drawItem.hDC,
                     drawItem.rcItem.left + scaleMenuDimension(kContextMenuCheckColumnWidth),
                     y,
                     nullptr);
            LineTo(drawItem.hDC, drawItem.rcItem.right - scaleMenuDimension(kContextMenuTextPadding), y);
            SelectObject(drawItem.hDC, oldPen);
            DeleteObject(separatorPen);
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);
        const HFONT menuFont = MenuFontOrDefault(menuFont_);
        const int textPadding = scaleMenuDimension(kContextMenuTextPadding);
        const int checkColumnWidth = scaleMenuDimension(kContextMenuCheckColumnWidth);
        const int shortcutGap = scaleMenuDimension(kContextMenuShortcutGap);
        const HGDIOBJ oldFont = SelectObject(drawItem.hDC, menuFont);
        SetBkMode(drawItem.hDC, TRANSPARENT);

        RECT labelRect{drawItem.rcItem.left + checkColumnWidth + textPadding,
                       drawItem.rcItem.top,
                       drawItem.rcItem.right - textPadding,
                       drawItem.rcItem.bottom};
        if (!shortcut.empty())
        {
            labelRect.right -= MeasureMenuTextWidth(menuFont, shortcut) + shortcutGap;
        }

        SetTextColor(drawItem.hDC, disabled
            ? BlendMenuColor(MutedTextColor(darkTheme_), backgroundColor, 128)
            : TextColor(darkTheme_));
        DrawTextW(drawItem.hDC,
                  label.c_str(),
                  -1,
                  &labelRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (!shortcut.empty())
        {
            RECT shortcutRect{labelRect.right + shortcutGap,
                              drawItem.rcItem.top,
                              drawItem.rcItem.right - textPadding,
                              drawItem.rcItem.bottom};
            SetTextColor(drawItem.hDC, disabled
                ? BlendMenuColor(MutedTextColor(darkTheme_), backgroundColor, 128)
                : MutedTextColor(darkTheme_));
            DrawTextW(drawItem.hDC,
                      shortcut.c_str(),
                      -1,
                      &shortcutRect,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        SelectObject(drawItem.hDC, oldFont);
    }

    void ViewerWindow::PrepareContextMenuForOwnerDraw(
        HMENU menu,
        std::vector<std::unique_ptr<MenuDrawItemData>>& storage) const
    {
        if (!menu)
        {
            return;
        }

        const int itemCount = GetMenuItemCount(menu);
        for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
        {
            MENUITEMINFOW menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIIM_FTYPE;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &menuInfo))
            {
                continue;
            }

            auto drawData = std::make_unique<MenuDrawItemData>();
            drawData->separator = (menuInfo.fType & MFT_SEPARATOR) != 0;
            if (!drawData->separator)
            {
                const int textLength = GetMenuStringW(menu, static_cast<UINT>(itemIndex), nullptr, 0, MF_BYPOSITION);
                if (textLength > 0)
                {
                    std::wstring buffer(static_cast<std::size_t>(textLength) + 1, L'\0');
                    GetMenuStringW(menu,
                                   static_cast<UINT>(itemIndex),
                                   buffer.data(),
                                   textLength + 1,
                                   MF_BYPOSITION);
                    buffer.resize(static_cast<std::size_t>(textLength));
                    drawData->text = std::move(buffer);
                }
            }

            MENUITEMINFOW updateInfo{};
            updateInfo.cbSize = sizeof(updateInfo);
            updateInfo.fMask = MIIM_FTYPE | MIIM_DATA;
            updateInfo.fType = drawData->separator ? (MFT_SEPARATOR | MFT_OWNERDRAW) : MFT_OWNERDRAW;
            updateInfo.dwItemData = reinterpret_cast<ULONG_PTR>(drawData.get());
            SetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &updateInfo);
            storage.push_back(std::move(drawData));
        }
    }

    void ViewerWindow::DispatchContextMenuCommand(UINT commandId)
    {
        if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(items_.size()))
        {
            return;
        }

        const std::wstring currentPath = items_[static_cast<std::size_t>(currentIndex_)].filePath;
        if (currentPath.empty())
        {
            return;
        }

        switch (commandId)
        {
        case kContextMenuCopyPath:
            if (!CopyTextToClipboardLocal(hwnd_, currentPath))
            {
                MessageBoxW(hwnd_, L"Failed to copy the file path to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
            }
            return;
        case kContextMenuRevealInExplorer:
        {
            PIDLIST_ABSOLUTE folderPidl = nullptr;
            const std::wstring parentPath = std::filesystem::path(currentPath).parent_path().wstring();
            if (SUCCEEDED(SHParseDisplayName(parentPath.c_str(), nullptr, &folderPidl, 0, nullptr)) && folderPidl)
            {
                PIDLIST_ABSOLUTE itemPidl = nullptr;
                LPCITEMIDLIST relativePidl = nullptr;
                if (SUCCEEDED(SHParseDisplayName(currentPath.c_str(), nullptr, &itemPidl, 0, nullptr)) && itemPidl)
                {
                    relativePidl = ILFindLastID(itemPidl);
                    if (relativePidl)
                    {
                        SHOpenFolderAndSelectItems(folderPidl, 1, &relativePidl, 0);
                    }
                    ILFree(itemPidl);
                }
                ILFree(folderPidl);
            }
            return;
        }
        case kContextMenuCopyImage:
        case kContextMenuImageInformation:
        case kContextMenuProperties:
        case kContextMenuSetWallpaper:
            if (owner_ && IsWindow(owner_))
            {
                // Defer to the main window so metadata dialogs and the wallpaper
                // operation reuse the same code paths as the browser.
                PostMessageW(owner_, kContextMenuCommandMessage, static_cast<WPARAM>(commandId), 0);
            }
            return;
        default:
            return;
        }
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
            QueueTransitionFromCurrent(true, true);
            previousSlot_ = currentSlot_;
            SetCurrentImageSlot(nextSlot_.index, nextSlot_.image, true);
            nextSlot_ = {};
            currentIndex_ = nextIndex;
            NotifyCurrentItemChanged();
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
            QueueTransitionFromCurrent(true, true);
            currentIndex_ = 0;
            NotifyCurrentItemChanged();
            LoadCurrentImageAsync(LoadReason::Navigation);
            return;
        }

        NavigateToIndex(nextIndex, true, true);
    }

    void ViewerWindow::ShowWraparoundMessage(bool forward)
    {
        ShowNavigationMessage(forward ? L"Wrapped to first image" : L"Wrapped to last image");
    }

    void ViewerWindow::NavigateToBoundary(bool first)
    {
        if (items_.empty())
        {
            return;
        }

        const int targetIndex = first ? 0 : static_cast<int>(items_.size()) - 1;
        ShowNavigationMessage(first ? L"First image" : L"Last image");
        if (targetIndex == currentIndex_)
        {
            return;
        }

        NavigateToIndex(targetIndex, targetIndex > currentIndex_);
    }

    void ViewerWindow::ShowNavigationMessage(std::wstring message)
    {
        wraparoundMessage_ = std::move(message);
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        if (wraparoundTimerId_ != 0)
        {
            KillTimer(hwnd_, wraparoundTimerId_);
        }

        wraparoundTimerId_ = SetTimer(hwnd_, kWraparoundTimerId, kWraparoundMessageDurationMs, nullptr);
        if (wraparoundTimerId_ == 0)
        {
            wraparoundMessage_.clear();
            return;
        }

        RequestRepaint();
    }

    void ViewerWindow::ClearWraparoundMessage()
    {
        if (hwnd_ && wraparoundTimerId_ != 0)
        {
            KillTimer(hwnd_, wraparoundTimerId_);
        }

        wraparoundTimerId_ = 0;
        wraparoundMessage_.clear();
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
        smoothZoomTargetPanX_ = 0.0;
        smoothZoomTargetPanY_ = 0.0;
    }

    void ViewerWindow::CalculatePanLimits(double& maxPanX, double& maxPanY) const
    {
        maxPanX = 0.0;
        maxPanY = 0.0;
        if (!currentImage_ || !hwnd_ || zoomMode_ == ZoomMode::Fit)
        {
            return;
        }

        RECT clientRect{};
        if (GetClientRect(hwnd_, &clientRect) == FALSE)
        {
            return;
        }

        if (compareMode_ && ActiveCompareIndex() >= 0)
        {
            const LONG totalWidth = clientRect.right - clientRect.left;
            const LONG gapWidth = std::clamp<LONG>(totalWidth / 40, 12, 24);
            const LONG paneWidth = std::max<LONG>(1, (totalWidth - gapWidth) / 2);
            clientRect.right = clientRect.left + paneWidth;
        }

        const double scale = EffectiveScaleForClient(clientRect);
        const bool swapDimensions = (rotationQuarterTurns_ % 2) != 0;
        const int rotatedWidth = swapDimensions ? currentImage_->SourceHeight() : currentImage_->SourceWidth();
        const int rotatedHeight = swapDimensions ? currentImage_->SourceWidth() : currentImage_->SourceHeight();
        const double destinationWidth = static_cast<double>(std::max(
            1, static_cast<int>(std::lround(static_cast<double>(rotatedWidth) * scale))));
        const double destinationHeight = static_cast<double>(std::max(
            1, static_cast<int>(std::lround(static_cast<double>(rotatedHeight) * scale))));
        const double clientWidth = static_cast<double>(std::max<LONG>(1, clientRect.right - clientRect.left));
        const double clientHeight = static_cast<double>(std::max<LONG>(1, clientRect.bottom - clientRect.top));
        maxPanX = std::max(0.0, (destinationWidth - clientWidth) / 2.0);
        maxPanY = std::max(0.0, (destinationHeight - clientHeight) / 2.0);
    }

    void ViewerWindow::ClampPanOffsets()
    {
        double maxPanX = 0.0;
        double maxPanY = 0.0;
        CalculatePanLimits(maxPanX, maxPanY);

        panOffsetX_ = std::clamp(panOffsetX_, -maxPanX, maxPanX);
        panOffsetY_ = std::clamp(panOffsetY_, -maxPanY, maxPanY);
    }

    bool ViewerWindow::CanPanHorizontally() const
    {
        double maxPanX = 0.0;
        double maxPanY = 0.0;
        CalculatePanLimits(maxPanX, maxPanY);
        return maxPanX > 0.0;
    }

    bool ViewerWindow::CanPanVertically() const
    {
        double maxPanX = 0.0;
        double maxPanY = 0.0;
        CalculatePanLimits(maxPanX, maxPanY);
        return maxPanY > 0.0;
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

    void ViewerWindow::QueueTransitionFromCurrent(bool forward, bool slideshowNavigation)
    {
        StopTransition(false);
        activeTransitionStyle_ = (slideshowNavigation || manualTransitionEnabled_)
            ? ResolveActiveTransitionStyle()
            : TransitionStyle::Cut;

        pendingTransitionFromImage_.reset();
        pendingTransitionFromBitmap_.Reset();
        pendingTransitionFromIndex_ = -1;
        pendingTransitionForward_ = forward;

        if (activeTransitionStyle_ == TransitionStyle::Cut || transitionDurationMs_ == 0 || !currentImage_)
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
            || activeTransitionStyle_ == TransitionStyle::Cut
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

        const float clientLeft = static_cast<float>(clientRect.left);
        const float clientTop = static_cast<float>(clientRect.top);
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
        const float cx = clientLeft + ((clientWidth - destW) / 2.0f) + offsetX;
        const float cy = clientTop + ((clientHeight - destH) / 2.0f) + offsetY;

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

    bool ViewerWindow::DrawImageEffect(ID2D1RenderTarget* renderTarget,
                                       ID2D1Image* imageSource,
                                       const cache::CachedThumbnail& image,
                                       const RECT& clientRect,
                                       float opacity,
                                       float scaleMultiplier,
                                       float offsetX,
                                       float offsetY) const
    {
        if (!renderTarget || !imageSource || opacity <= 0.0f)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<ID2D1DeviceContext> deviceContext;
        if (FAILED(renderTarget->QueryInterface(__uuidof(ID2D1DeviceContext),
                                                reinterpret_cast<void**>(deviceContext.GetAddressOf()))))
        {
            return false;
        }

        const float clientLeft = static_cast<float>(clientRect.left);
        const float clientTop = static_cast<float>(clientRect.top);
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
        const float cx = clientLeft + ((clientWidth - destW) / 2.0f) + offsetX;
        const float cy = clientTop + ((clientHeight - destH) / 2.0f) + offsetY;

        D2D1_MATRIX_3X2_F previousTransform{};
        deviceContext->GetTransform(&previousTransform);

        Microsoft::WRL::ComPtr<ID2D1Layer> opacityLayer;
        const bool useOpacityLayer = opacity < 0.999f;
        if (useOpacityLayer)
        {
            if (FAILED(deviceContext->CreateLayer(nullptr, opacityLayer.GetAddressOf())) || !opacityLayer)
            {
                return false;
            }

            deviceContext->PushLayer(
                D2D1::LayerParameters1(
                    D2D1::InfiniteRect(),
                    nullptr,
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(),
                    opacity,
                    nullptr,
                    D2D1_LAYER_OPTIONS1_NONE),
                opacityLayer.Get());
        }

        if (rotationQuarterTurns_ == 0)
        {
            const float scaleX = destW / static_cast<float>(std::max(1, sourceWidth));
            const float scaleY = destH / static_cast<float>(std::max(1, sourceHeight));
            const D2D1_POINT_2F drawOrigin = D2D1::Point2F(0.0f, 0.0f);
            deviceContext->SetTransform(previousTransform
                * D2D1::Matrix3x2F::Scale(scaleX, scaleY, D2D1::Point2F(0.0f, 0.0f))
                * D2D1::Matrix3x2F::Translation(cx, cy));
            deviceContext->DrawImage(imageSource,
                                     &drawOrigin,
                                     nullptr,
                                     D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                     D2D1_COMPOSITE_MODE_SOURCE_OVER);
            if (useOpacityLayer)
            {
                deviceContext->PopLayer();
            }
            deviceContext->SetTransform(previousTransform);
            return true;
        }

        const float centerX = cx + (destW / 2.0f);
        const float centerY = cy + (destH / 2.0f);
        const float unrotW = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceWidth) * scale))));
        const float unrotH = static_cast<float>(std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceHeight) * scale))));
        const float drawX = centerX - (unrotW / 2.0f);
        const float drawY = centerY - (unrotH / 2.0f);
        const D2D1_POINT_2F drawOrigin = D2D1::Point2F(0.0f, 0.0f);

        deviceContext->SetTransform(previousTransform
            * D2D1::Matrix3x2F::Scale(static_cast<float>(scale), static_cast<float>(scale), D2D1::Point2F(0.0f, 0.0f))
            * D2D1::Matrix3x2F::Translation(drawX, drawY)
            * D2D1::Matrix3x2F::Rotation(static_cast<float>(rotationQuarterTurns_) * 90.0f,
                                         D2D1::Point2F(centerX, centerY)));
        deviceContext->DrawImage(imageSource,
                                 &drawOrigin,
                                 nullptr,
                                 D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                                 D2D1_COMPOSITE_MODE_SOURCE_OVER);
        if (useOpacityLayer)
        {
            deviceContext->PopLayer();
        }
        deviceContext->SetTransform(previousTransform);
        return true;
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

    void ViewerWindow::NotifyCurrentItemChanged() const
    {
        if (owner_)
        {
            PostMessageW(owner_, kCurrentItemChangedMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
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

    LRESULT ViewerWindow::HandleMetadataReadyMessage(LPARAM lParam)
    {
        std::unique_ptr<MetadataReadyResult> update(reinterpret_cast<MetadataReadyResult*>(lParam));
        if (!update)
        {
            return 0;
        }

        if (update->requestId != metadataRequestId_.load(std::memory_order_acquire)
            || update->index != currentMetadataIndex_)
        {
            return 0;
        }

        currentMetadata_ = std::move(update->metadata);
        currentMetadataText_ = std::move(update->text);
        metadataLoading_ = false;
        if (hwnd_ && IsWindow(hwnd_) != FALSE)
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
        case WM_GETMINMAXINFO:
        {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            if (minMaxInfo)
            {
                minMaxInfo->ptMaxTrackSize.x = std::numeric_limits<LONG>::max();
                minMaxInfo->ptMaxTrackSize.y = std::numeric_limits<LONG>::max();
            }
            return 0;
        }
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED)
            {
                ReleaseD2DResources();
                return 0;
            }
            ClampPanOffsets();
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
        case WM_DISPLAYCHANGE:
            RecoverDisplaySurface();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            NotifyActivityChanged(LOWORD(wParam) != WA_INACTIVE);
            RequestRepaint();
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            RequestRepaint();
            return 0;
        case WM_MEASUREITEM:
        {
            auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measureItem && measureItem->CtlType == ODT_MENU)
            {
                MeasureContextMenuItem(measureItem);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (drawItem && drawItem->CtlType == ODT_MENU)
            {
                DrawContextMenuItem(*drawItem);
                return TRUE;
            }
            break;
        }
        case WM_KEYDOWN:
            if ((wParam == VK_F7 || wParam == VK_F8)
                && (GetKeyState(VK_CONTROL) & 0x8000) == 0
                && (GetKeyState(VK_SHIFT) & 0x8000) == 0
                && (GetKeyState(VK_MENU) & 0x8000) == 0)
            {
                if ((lParam & (1LL << 30)) != 0)
                {
                    return 0;
                }

                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    const QuickSendOperation operation = wParam == VK_F7
                        ? QuickSendOperation::Move
                        : QuickSendOperation::Copy;
                    PostMessageW(owner_,
                                 kQuickSendRequestedMessage,
                                 static_cast<WPARAM>(operation),
                                 reinterpret_cast<LPARAM>(hwnd_));
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('F')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    PostMessageW(owner_, kStartFolderSlideshowMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('I')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                if (owner_ && IsWindow(owner_) != FALSE)
                {
                    PostMessageW(owner_, kContextMenuCommandMessage, kContextMenuCopyImage, 0);
                }
                return 0;
            }

            if (wParam == static_cast<WPARAM>('W')
                && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return 0;
            }

            switch (wParam)
            {
            case VK_DELETE:
                if (owner_ && IsWindow(owner_))
                {
                    const WPARAM deleteRequestFlags = (GetKeyState(VK_SHIFT) & 0x8000) != 0
                        ? kDeleteRequestPermanent
                        : 0;
                    util::LogInfo(L"ViewerWindow VK_DELETE dispatch starting, currentIndex=" + std::to_wstring(currentIndex_));
                    {
                        util::ScopedTimer timer(L"ViewerWindow VK_DELETE SendMessageW round-trip");
                        SendMessageW(owner_, kDeleteRequestedMessage, deleteRequestFlags, 0);
                    }
                    util::LogInfo(L"ViewerWindow VK_DELETE dispatch returned, currentIndex=" + std::to_wstring(currentIndex_));
                }
                return 0;
            case VK_RIGHT:
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                {
                    SetCompareMode(true, CompareDirection::Next);
                }
                else if (CanPanHorizontally())
                {
                    double maxPanX = 0.0;
                    double maxPanY = 0.0;
                    CalculatePanLimits(maxPanX, maxPanY);
                    const double panStep = std::min(kKeyboardPanStep, maxPanX);
                    panOffsetX_ += keyboardPanningInverted_ ? panStep : -panStep;
                    ClampPanOffsets();
                    RequestRepaint();
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
                else if (CanPanHorizontally())
                {
                    double maxPanX = 0.0;
                    double maxPanY = 0.0;
                    CalculatePanLimits(maxPanX, maxPanY);
                    const double panStep = std::min(kKeyboardPanStep, maxPanX);
                    panOffsetX_ -= keyboardPanningInverted_ ? panStep : -panStep;
                    ClampPanOffsets();
                    RequestRepaint();
                }
                else
                {
                    Navigate(-1);
                }
                return 0;
            case VK_UP:
            case VK_DOWN:
                if (CanPanVertically())
                {
                    double maxPanX = 0.0;
                    double maxPanY = 0.0;
                    CalculatePanLimits(maxPanX, maxPanY);
                    const double panStep = std::min(kKeyboardPanStep, maxPanY);
                    const double direction = wParam == VK_UP ? 1.0 : -1.0;
                    panOffsetY_ += (keyboardPanningInverted_ ? -direction : direction) * panStep;
                    ClampPanOffsets();
                    RequestRepaint();
                }
                else
                {
                    Navigate(wParam == VK_UP ? -1 : +1);
                }
                return 0;
            case VK_PRIOR:
                Navigate(-1);
                return 0;
            case VK_NEXT:
                Navigate(+1);
                return 0;
            case VK_HOME:
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    NavigateToBoundary(true);
                }
                return 0;
            case VK_END:
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    NavigateToBoundary(false);
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
            case VK_RETURN:
                if (zoomMode_ == ZoomMode::Fit)
                {
                    SetActualSize();
                }
                else
                {
                    FitToWindow();
                }
                return 0;
            case '0':
                FitToWindow();
                return 0;
            case '1':
                SetActualSize();
                return 0;
            case 'H':
                SetWindowFitMode(WindowFitMode::Height);
                return 0;
            case 'W':
                SetWindowFitMode(WindowFitMode::Width);
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
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                return 0;
            default:
                break;
            }
            break;
        case WM_CONTEXTMENU:
        {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (screenPoint.x == -1 && screenPoint.y == -1)
            {
                RECT clientRect{};
                GetClientRect(hwnd_, &clientRect);
                screenPoint.x = (clientRect.left + clientRect.right) / 2;
                screenPoint.y = (clientRect.top + clientRect.bottom) / 2;
                ClientToScreen(hwnd_, &screenPoint);
            }
            ShowContextMenu(screenPoint);
            return 0;
        }
        case WM_DROPFILES:
        {
            HDROP dropHandle = reinterpret_cast<HDROP>(wParam);
            if (dropHandle && owner_ && IsWindow(owner_))
            {
                const UINT pathCount = DragQueryFileW(dropHandle, 0xFFFFFFFFu, nullptr, 0);
                for (UINT index = 0; index < pathCount; ++index)
                {
                    const UINT length = DragQueryFileW(dropHandle, index, nullptr, 0);
                    if (length == 0)
                    {
                        continue;
                    }
                    std::wstring path(length + 1, L'\0');
                    DragQueryFileW(dropHandle, index, path.data(), length + 1);
                    path.resize(length);
                    if (!path.empty())
                    {
                        // Hand off to the main window, which owns folder navigation
                        // and the viewer item list.
                        auto* payload = new std::wstring(std::move(path));
                        if (!PostMessageW(owner_, kDroppedFileMessage, 0, reinterpret_cast<LPARAM>(payload)))
                        {
                            delete payload;
                        }
                        break; // only open the first dropped image
                    }
                }
            }
            if (dropHandle)
            {
                DragFinish(dropHandle);
            }
            return 0;
        }
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
                POINT zoomPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const bool hasZoomPoint = ScreenToClient(hwnd_, &zoomPoint) != FALSE;
                const double zoomFactor = wheelDelta > 0 ? 1.1 : 0.9;
                for (int step = 0; step < stepCount; ++step)
                {
                    ZoomBy(zoomFactor, hasZoomPoint ? &zoomPoint : nullptr);
                }
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && !panning_)
            {
                POINT point{};
                if (GetCursorPos(&point) != FALSE
                    && ScreenToClient(hwnd_, &point) != FALSE
                    && SetNavigationCursorForPoint(point))
                {
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN:
        {
            const POINT clickPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int navigationDelta = NavigationDeltaForPoint(clickPoint);
            if (navigationDelta != 0)
            {
                pendingNavigationDelta_ = navigationDelta;
                pendingNavigationPoint_ = clickPoint;
                SetCapture(hwnd_);
                return 0;
            }

            if (currentImage_)
            {
                panning_ = true;
                lastPanPoint_ = clickPoint;
                SetCapture(hwnd_);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            const POINT currentPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pendingNavigationDelta_ != 0)
            {
                if (currentImage_ && ExceedsPanDragThreshold(pendingNavigationPoint_, currentPoint))
                {
                    pendingNavigationDelta_ = 0;
                    panning_ = true;
                    lastPanPoint_ = pendingNavigationPoint_;
                    panOffsetX_ += static_cast<double>(currentPoint.x - lastPanPoint_.x);
                    panOffsetY_ += static_cast<double>(currentPoint.y - lastPanPoint_.y);
                    ClampPanOffsets();
                    lastPanPoint_ = currentPoint;
                    RequestRepaint();
                    return 0;
                }

                if (!SetNavigationCursorForPoint(currentPoint))
                {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                }
                return 0;
            }

            if (panning_)
            {
                panOffsetX_ += static_cast<double>(currentPoint.x - lastPanPoint_.x);
                panOffsetY_ += static_cast<double>(currentPoint.y - lastPanPoint_.y);
                ClampPanOffsets();
                lastPanPoint_ = currentPoint;
                RequestRepaint();
                return 0;
            }

            if (!SetNavigationCursorForPoint(currentPoint))
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            const POINT releasePoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int navigationDelta = pendingNavigationDelta_;
            pendingNavigationDelta_ = 0;

            if (panning_)
            {
                panning_ = false;
            }

            if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }

            if (navigationDelta != 0)
            {
                Navigate(navigationDelta);
            }

            if (!SetNavigationCursorForPoint(releasePoint))
            {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            panning_ = false;
            pendingNavigationDelta_ = 0;
            return 0;
        case WM_LBUTTONDBLCLK:
            ToggleFullScreen();
            return 0;
        case WM_TIMER:
            if (wParam == kWraparoundTimerId && wraparoundTimerId_)
            {
                KillTimer(hwnd_, wraparoundTimerId_);
                wraparoundTimerId_ = 0;
                wraparoundMessage_.clear();
                RequestRepaint();
                return 0;
            }
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
                    panOffsetX_ = smoothZoomTargetPanX_;
                    panOffsetY_ = smoothZoomTargetPanY_;
                    KillTimer(hwnd_, kSmoothZoomTimerId);
                    smoothZoomTimerId_ = 0;
                }
                else
                {
                    smoothZoomCurrent_ += diff * 0.22;
                    customZoomScale_ = smoothZoomCurrent_;
                    panOffsetX_ += (smoothZoomTargetPanX_ - panOffsetX_) * 0.22;
                    panOffsetY_ += (smoothZoomTargetPanY_ - panOffsetY_) * 0.22;
                }
                ClampPanOffsets();
                RequestRepaint();
                return 0;
            }
            break;
        case kDecodedImageMessage:
            return HandleDecodedImageMessage(lParam);
        case kPrefetchImageMessage:
            return HandlePrefetchImageMessage(lParam);
        case kMetadataReadyMessage:
            return HandleMetadataReadyMessage(lParam);
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

                    const float maxPanelWidth = std::max(320.0f, clientWidth - 48.0f);
                    const float maxPanelHeight = std::max(140.0f, clientHeight - 36.0f);
                    const float loadingTextBlockHeight = ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTitleHeight
                        + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingGap
                        + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingBodyHeight;
                    float renderedIconSize = 0.0f;
                    float panelWidth = std::max(320.0f, std::min(560.0f, clientWidth - 64.0f));
                    float panelHeight = showIcon
                        ? std::min(198.0f, std::max(152.0f, clientHeight - 64.0f))
                        : std::min(maxPanelHeight,
                            std::max(126.0f, (ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset * 2.0f) + loadingTextBlockHeight));

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
                        panelHeight = std::min(maxPanelHeight,
                            (kPanelPaddingVertical * 2.0f) + std::max(renderedIconSize, loadingTextBlockHeight));
                    }
                    else
                    {
                        panelHeight = std::min(maxPanelHeight,
                            std::max(panelHeight, (ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset * 2.0f) + loadingTextBlockHeight));
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
                        const float textBlockHeight = ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTitleHeight
                            + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingGap
                            + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingBodyHeight;
                        const float contentTop = panelTop + std::max(kPanelPaddingVertical, (panelHeight - textBlockHeight) / 2.0f);
                        titleRect = D2D1::RectF(contentLeft,
                                                contentTop,
                                                contentRight,
                                                contentTop + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTitleHeight);
                        bodyRect = D2D1::RectF(contentLeft,
                                              titleRect.bottom + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingGap,
                                              contentRight,
                                              titleRect.bottom + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingGap + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingBodyHeight);
                    }
                    else
                    {
                        titleRect = D2D1::RectF(panelLeft + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset,
                                                panelTop + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset,
                                                panelLeft + panelWidth - ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset,
                                                panelTop + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTitleHeight);
                        bodyRect = D2D1::RectF(panelLeft + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset,
                                               titleRect.bottom + ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingGap,
                                               panelLeft + panelWidth - ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset,
                                               panelTop + panelHeight - ViewerOverlayMetricsForTextSize(infoOverlayTextSize_).loadingTextInset);
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
                                Microsoft::WRL::ComPtr<ID2D1DeviceContext> transitionDeviceContext;
                                if (d2dRenderTarget_)
                                {
                                    d2dRenderTarget_->QueryInterface(__uuidof(ID2D1DeviceContext),
                                                                     reinterpret_cast<void**>(transitionDeviceContext.GetAddressOf()));
                                }
                                const auto drawClippedImage = [&](ID2D1Bitmap* bitmap,
                                                                  const cache::CachedThumbnail& image,
                                                                  const D2D1_RECT_F& clipRect,
                                                                  float opacity,
                                                                  float scaleMultiplier,
                                                                  float offsetX,
                                                                  float offsetY)
                                {
                                    if (!d2dRenderTarget_ || clipRect.right <= clipRect.left || clipRect.bottom <= clipRect.top)
                                    {
                                        return;
                                    }

                                    d2dRenderTarget_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_ALIASED);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), bitmap, image,
                                                    gdiClientRect, opacity, scaleMultiplier, offsetX, offsetY);
                                    d2dRenderTarget_->PopAxisAlignedClip();
                                };

                                switch (activeTransitionStyle_)
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
                                case TransitionStyle::FadeToBlack:
                                    if (eased < 0.5f)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - (eased * 2.0f), 1.0f, 0.0f, 0.0f);
                                    }
                                    else
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, (eased - 0.5f) * 2.0f, 1.0f, 0.0f, 0.0f);
                                    }
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::DiagonalSlide:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    -direction * clientWidth * eased,
                                                    parityDirection * clientHeight * 0.18f * eased);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, 1.0f, 1.0f,
                                                    direction * clientWidth * (1.0f - eased),
                                                    -parityDirection * clientHeight * 0.18f * (1.0f - eased));
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::Push:
                                {
                                    const float pushEased = std::min(eased * 1.15f, 1.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f, 1.0f - (0.03f * eased),
                                                    -direction * clientWidth * pushEased, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, 1.0f, 0.97f + (0.03f * eased),
                                                    direction * clientWidth * (1.0f - pushEased), 0.0f);
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::CenterWipe:
                                {
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.35f * eased), 1.0f, 0.0f, 0.0f);
                                    const float clipHalfWidth = (clientWidth * eased) * 0.5f;
                                    const float clipHalfHeight = (clientHeight * eased) * 0.5f;
                                    const float clipCenterX = static_cast<float>(gdiClientRect.left) + (clientWidth * 0.5f);
                                    const float clipCenterY = static_cast<float>(gdiClientRect.top) + (clientHeight * 0.5f);
                                    drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                     D2D1::RectF(clipCenterX - clipHalfWidth,
                                                                 clipCenterY - clipHalfHeight,
                                                                 clipCenterX + clipHalfWidth,
                                                                 clipCenterY + clipHalfHeight),
                                                     1.0f,
                                                     1.03f - (0.03f * eased),
                                                     0.0f,
                                                     0.0f);
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::VenetianBlinds:
                                {
                                    constexpr int kBlindCount = 10;
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.25f * eased), 1.0f, 0.0f, 0.0f);
                                    const float blindWidth = clientWidth / static_cast<float>(kBlindCount);
                                    for (int blindIndex = 0; blindIndex < kBlindCount; ++blindIndex)
                                    {
                                        const float blindLeft = static_cast<float>(gdiClientRect.left)
                                            + (blindWidth * static_cast<float>(blindIndex));
                                        const float blindRight = blindIndex == (kBlindCount - 1)
                                            ? static_cast<float>(gdiClientRect.right)
                                            : blindLeft + blindWidth;
                                        const float visibleWidth = (blindRight - blindLeft) * eased;
                                        const bool revealFromLeft = ((blindIndex % 2) == 0) == (direction > 0.0f);
                                        const float clipLeft = revealFromLeft ? blindLeft : (blindRight - visibleWidth);
                                        const float clipRight = revealFromLeft ? (blindLeft + visibleWidth) : blindRight;
                                        drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                         D2D1::RectF(clipLeft,
                                                                     static_cast<float>(gdiClientRect.top),
                                                                     clipRight,
                                                                     static_cast<float>(gdiClientRect.bottom)),
                                                         1.0f,
                                                         1.0f,
                                                         0.0f,
                                                         0.0f);
                                    }
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::SplitWipe:
                                {
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.20f * eased), 1.0f, 0.0f, 0.0f);
                                    const float centerX = static_cast<float>(gdiClientRect.left) + (clientWidth * 0.5f);
                                    const float halfRevealWidth = (clientWidth * 0.5f) * eased;
                                    drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                     D2D1::RectF(centerX - halfRevealWidth,
                                                                 static_cast<float>(gdiClientRect.top),
                                                                 centerX,
                                                                 static_cast<float>(gdiClientRect.bottom)),
                                                     1.0f,
                                                     1.0f,
                                                     -clientWidth * 0.05f * (1.0f - eased),
                                                     0.0f);
                                    drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                     D2D1::RectF(centerX,
                                                                 static_cast<float>(gdiClientRect.top),
                                                                 centerX + halfRevealWidth,
                                                                 static_cast<float>(gdiClientRect.bottom)),
                                                     1.0f,
                                                     1.0f,
                                                     clientWidth * 0.05f * (1.0f - eased),
                                                     0.0f);
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::HorizontalBlinds:
                                {
                                    constexpr int kBlindCount = 8;
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.25f * eased), 1.0f, 0.0f, 0.0f);
                                    const float blindHeight = clientHeight / static_cast<float>(kBlindCount);
                                    for (int blindIndex = 0; blindIndex < kBlindCount; ++blindIndex)
                                    {
                                        const float blindTop = static_cast<float>(gdiClientRect.top)
                                            + (blindHeight * static_cast<float>(blindIndex));
                                        const float blindBottom = blindIndex == (kBlindCount - 1)
                                            ? static_cast<float>(gdiClientRect.bottom)
                                            : blindTop + blindHeight;
                                        const float visibleHeight = (blindBottom - blindTop) * eased;
                                        const bool revealFromTop = ((blindIndex % 2) == 0) == (direction > 0.0f);
                                        const float clipTop = revealFromTop ? blindTop : (blindBottom - visibleHeight);
                                        const float clipBottom = revealFromTop ? (blindTop + visibleHeight) : blindBottom;
                                        drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                         D2D1::RectF(static_cast<float>(gdiClientRect.left),
                                                                     clipTop,
                                                                     static_cast<float>(gdiClientRect.right),
                                                                     clipBottom),
                                                         1.0f,
                                                         1.0f,
                                                         0.0f,
                                                         0.0f);
                                    }
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::CheckerboardWipe:
                                {
                                    constexpr int kGridColumns = 6;
                                    constexpr int kGridRows = 4;
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - (0.15f * eased), 1.0f, 0.0f, 0.0f);
                                    const float cellWidth = clientWidth / static_cast<float>(kGridColumns);
                                    const float cellHeight = clientHeight / static_cast<float>(kGridRows);
                                    for (int row = 0; row < kGridRows; ++row)
                                    {
                                        for (int column = 0; column < kGridColumns; ++column)
                                        {
                                            const float cellLeft = static_cast<float>(gdiClientRect.left)
                                                + (cellWidth * static_cast<float>(column));
                                            const float cellRight = column == (kGridColumns - 1)
                                                ? static_cast<float>(gdiClientRect.right)
                                                : cellLeft + cellWidth;
                                            const float cellTop = static_cast<float>(gdiClientRect.top)
                                                + (cellHeight * static_cast<float>(row));
                                            const float cellBottom = row == (kGridRows - 1)
                                                ? static_cast<float>(gdiClientRect.bottom)
                                                : cellTop + cellHeight;
                                            const float phaseOffset = ((row + column) % 2 == 0) ? 0.0f : 0.18f;
                                            const float phased = std::clamp((eased - phaseOffset) / (1.0f - phaseOffset), 0.0f, 1.0f);
                                            if (phased <= 0.0f)
                                            {
                                                continue;
                                            }

                                            const float insetX = ((cellRight - cellLeft) * (1.0f - phased)) * 0.5f;
                                            const float insetY = ((cellBottom - cellTop) * (1.0f - phased)) * 0.5f;
                                            drawClippedImage(d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                             D2D1::RectF(cellLeft + insetX,
                                                                         cellTop + insetY,
                                                                         cellRight - insetX,
                                                                         cellBottom - insetY),
                                                             1.0f,
                                                             1.0f,
                                                             0.0f,
                                                             0.0f);
                                        }
                                    }
                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::ZoomFade:
                                    DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                    gdiClientRect, 1.0f - eased, 1.0f + (0.10f * eased),
                                                    0.0f, 0.0f);
                                    DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                    gdiClientRect, eased, 1.08f - (0.08f * eased),
                                                    0.0f, 0.0f);
                                    drewTransition = true;
                                    break;
                                case TransitionStyle::BlurCrossfade:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildBlurImage = [&](ID2D1Bitmap* bitmap,
                                                                        float blurDeviation,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> blurEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kGaussianBlurEffectClsid,
                                                                                            blurEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            blurEffect->SetInput(0, bitmap);
                                            blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurDeviation);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            blurEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float outgoingBlur = 12.0f * eased;
                                        const float incomingBlur = 12.0f * (1.0f - eased);
                                        if (buildBlurImage(transitionFromBitmap_.Get(), outgoingBlur, &fromImageEffect)
                                            && buildBlurImage(d2dCurrentImageBitmap_.Get(), incomingBlur, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f + (0.04f * eased), 0.0f, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.02f - (0.02f * eased), 0.0f, 0.0f))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.0f, 0.0f, 0.0f);
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::MotionBlur:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildDirectionalBlurImage = [&](ID2D1Bitmap* bitmap,
                                                                                   float blurDeviation,
                                                                                   float blurAngle,
                                                                                   Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> blurEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kDirectionalBlurEffectClsid,
                                                                                            blurEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            blurEffect->SetInput(0, bitmap);
                                            blurEffect->SetValue(D2D1_DIRECTIONALBLUR_PROP_STANDARD_DEVIATION, blurDeviation);
                                            blurEffect->SetValue(D2D1_DIRECTIONALBLUR_PROP_ANGLE, blurAngle);
                                            blurEffect->SetValue(D2D1_DIRECTIONALBLUR_PROP_OPTIMIZATION,
                                                                 D2D1_DIRECTIONALBLUR_OPTIMIZATION_BALANCED);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            blurEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float outgoingBlur = 18.0f * eased;
                                        const float incomingBlur = 18.0f * (1.0f - eased);
                                        const float motionAngle = direction > 0.0f ? 0.0f : 180.0f;
                                        if (buildDirectionalBlurImage(transitionFromBitmap_.Get(), outgoingBlur, motionAngle, &fromImageEffect)
                                            && buildDirectionalBlurImage(d2dCurrentImageBitmap_.Get(), incomingBlur, motionAngle, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f,
                                                               -direction * clientWidth * 0.10f * eased, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.0f,
                                                               direction * clientWidth * 0.10f * (1.0f - eased), 0.0f))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f,
                                                        -direction * clientWidth * 0.10f * eased, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.0f,
                                                        direction * clientWidth * 0.10f * (1.0f - eased), 0.0f);
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::ColorWash:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F warmMatrix = D2D1::Matrix5x4F(
                                            1.08f, 0.04f, 0.01f, 0.0f,
                                            0.14f, 1.00f, 0.06f, 0.0f,
                                            0.04f, 0.14f, 0.72f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.03f, 0.00f, -0.02f, 0.0f);
                                        const D2D1_MATRIX_5X4_F coolMatrix = D2D1::Matrix5x4F(
                                            0.82f, 0.06f, 0.02f, 0.0f,
                                            0.06f, 0.94f, 0.12f, 0.0f,
                                            0.12f, 0.18f, 1.10f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            -0.02f, 0.01f, 0.03f, 0.0f);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, warmMatrix, eased);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(coolMatrix, identityMatrix, eased);
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f + (0.03f * eased),
                                                               -direction * clientWidth * 0.02f * eased,
                                                               parityDirection * clientHeight * 0.02f * eased)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.03f - (0.03f * eased),
                                                               direction * clientWidth * 0.02f * (1.0f - eased),
                                                               -parityDirection * clientHeight * 0.02f * (1.0f - eased)))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f + (0.03f * eased),
                                                        -direction * clientWidth * 0.02f * eased,
                                                        parityDirection * clientHeight * 0.02f * eased);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.03f - (0.03f * eased),
                                                        direction * clientWidth * 0.02f * (1.0f - eased),
                                                        -parityDirection * clientHeight * 0.02f * (1.0f - eased));
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::SepiaDrift:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F sepiaMatrix = D2D1::Matrix5x4F(
                                            0.420f, 0.390f, 0.300f, 0.0f,
                                            0.770f, 0.720f, 0.560f, 0.0f,
                                            0.200f, 0.190f, 0.150f, 0.0f,
                                            0.000f, 0.000f, 0.000f, 1.0f,
                                            0.040f, 0.010f, -0.030f, 0.0f);
                                        const D2D1_MATRIX_5X4_F dustMatrix = D2D1::Matrix5x4F(
                                            0.82f, 0.05f, 0.01f, 0.0f,
                                            0.09f, 0.74f, 0.03f, 0.0f,
                                            0.03f, 0.10f, 0.63f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.030f, 0.020f, -0.020f, 0.0f);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float outgoingAmount = std::min(1.0f, eased * 1.15f);
                                        const float incomingAmount = 1.0f - eased;
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, sepiaMatrix, outgoingAmount);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(dustMatrix, identityMatrix, eased);
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.02f + (0.02f * eased),
                                                               -direction * clientWidth * 0.03f * eased,
                                                               -clientHeight * 0.01f * eased)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.05f - (0.05f * eased),
                                                               direction * clientWidth * 0.03f * incomingAmount,
                                                               clientHeight * 0.01f * incomingAmount))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.02f + (0.02f * eased),
                                                        -direction * clientWidth * 0.03f * eased,
                                                        -clientHeight * 0.01f * eased);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.05f - (0.05f * eased),
                                                        direction * clientWidth * 0.03f * (1.0f - eased),
                                                        clientHeight * 0.01f * (1.0f - eased));
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::Flashbulb:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F flashMatrix = D2D1::Matrix5x4F(
                                            1.35f, 0.18f, 0.18f, 0.0f,
                                            0.18f, 1.35f, 0.18f, 0.0f,
                                            0.18f, 0.18f, 1.35f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.22f, 0.22f, 0.22f, 0.0f);
                                        const D2D1_MATRIX_5X4_F bleachedMatrix = D2D1::Matrix5x4F(
                                            1.12f, 0.08f, 0.08f, 0.0f,
                                            0.08f, 1.12f, 0.08f, 0.0f,
                                            0.08f, 0.08f, 1.12f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.08f, 0.08f, 0.08f, 0.0f);

                                        const float outgoingFlash = std::clamp(eased / 0.45f, 0.0f, 1.0f);
                                        const float incomingRecover = std::clamp((eased - 0.22f) / 0.78f, 0.0f, 1.0f);
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, flashMatrix, outgoingFlash);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(bleachedMatrix, identityMatrix, incomingRecover);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f + (0.04f * outgoingFlash), 0.0f, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.02f - (0.02f * incomingRecover), 0.0f, 0.0f))
                                        {
                                            renderedWithEffects = true;

                                            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> flashBrush;
                                            if (SUCCEEDED(d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                                                                                  flashBrush.GetAddressOf()))
                                                && flashBrush)
                                            {
                                                const float flashPeak = 1.0f - std::min(1.0f, std::abs((eased - 0.35f) / 0.35f));
                                                const float overlayOpacity = 0.50f * flashPeak * flashPeak;
                                                if (overlayOpacity > 0.0f)
                                                {
                                                    flashBrush->SetOpacity(overlayOpacity);
                                                    d2dRenderTarget_->FillRectangle(D2D1::RectF(static_cast<float>(gdiClientRect.left),
                                                                                                static_cast<float>(gdiClientRect.top),
                                                                                                static_cast<float>(gdiClientRect.right),
                                                                                                static_cast<float>(gdiClientRect.bottom)),
                                                                                    flashBrush.Get());
                                                }
                                            }
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        const float flashPeak = 1.0f - std::min(1.0f, std::abs((eased - 0.35f) / 0.35f));
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f + (0.04f * flashPeak), 0.0f, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.02f - (0.02f * eased), 0.0f, 0.0f);

                                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> flashBrush;
                                        if (d2dRenderTarget_
                                            && SUCCEEDED(d2dRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f),
                                                                                                  flashBrush.GetAddressOf()))
                                            && flashBrush)
                                        {
                                            const float overlayOpacity = 0.42f * flashPeak * flashPeak;
                                            if (overlayOpacity > 0.0f)
                                            {
                                                flashBrush->SetOpacity(overlayOpacity);
                                                d2dRenderTarget_->FillRectangle(D2D1::RectF(static_cast<float>(gdiClientRect.left),
                                                                                            static_cast<float>(gdiClientRect.top),
                                                                                            static_cast<float>(gdiClientRect.right),
                                                                                            static_cast<float>(gdiClientRect.bottom)),
                                                                                flashBrush.Get());
                                            }
                                        }
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::Prism:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();
                                        const D2D1_MATRIX_5X4_F prismOutMatrix = D2D1::Matrix5x4F(
                                            1.10f, 0.08f, 0.05f, 0.0f,
                                            0.05f, 1.10f, 0.08f, 0.0f,
                                            0.08f, 0.05f, 1.10f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.02f, 0.02f, 0.02f, 0.0f);
                                        const D2D1_MATRIX_5X4_F prismInMatrix = D2D1::Matrix5x4F(
                                            1.00f, 0.06f, 0.14f, 0.0f,
                                            0.14f, 1.00f, 0.06f, 0.0f,
                                            0.06f, 0.14f, 1.00f, 0.0f,
                                            0.00f, 0.00f, 0.00f, 1.0f,
                                            0.00f, 0.01f, 0.02f, 0.0f);

                                        const float outgoingAmount = eased;
                                        const float incomingAmount = 1.0f - eased;
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, prismOutMatrix, outgoingAmount);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(prismInMatrix, identityMatrix, eased);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        const float prismOffset = clientWidth * 0.015f;
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.01f,
                                                               -direction * prismOffset * outgoingAmount,
                                                               -parityDirection * clientHeight * 0.01f * outgoingAmount)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.01f,
                                                               direction * prismOffset * incomingAmount,
                                                               parityDirection * clientHeight * 0.01f * incomingAmount))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.01f,
                                                        -direction * clientWidth * 0.015f * eased,
                                                        -parityDirection * clientHeight * 0.01f * eased);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.01f,
                                                        direction * clientWidth * 0.015f * (1.0f - eased),
                                                        parityDirection * clientHeight * 0.01f * (1.0f - eased));
                                    }

                                    drewTransition = true;
                                    break;
                                }
                                case TransitionStyle::MonochromeReveal:
                                {
                                    bool renderedWithEffects = false;
                                    if (transitionDeviceContext)
                                    {
                                        const auto buildColorMatrixImage = [&](ID2D1Bitmap* bitmap,
                                                                               const D2D1_MATRIX_5X4_F& colorMatrix,
                                                                               Microsoft::WRL::ComPtr<ID2D1Image>* outputImage)
                                        {
                                            if (!outputImage || !bitmap)
                                            {
                                                return false;
                                            }

                                            Microsoft::WRL::ComPtr<ID2D1Effect> colorMatrixEffect;
                                            if (FAILED(transitionDeviceContext->CreateEffect(kColorMatrixEffectClsid,
                                                                                            colorMatrixEffect.GetAddressOf())))
                                            {
                                                return false;
                                            }

                                            colorMatrixEffect->SetInput(0, bitmap);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colorMatrix);
                                            colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

                                            Microsoft::WRL::ComPtr<ID2D1Image> imageOutput;
                                            colorMatrixEffect->GetOutput(imageOutput.GetAddressOf());
                                            if (!imageOutput)
                                            {
                                                return false;
                                            }

                                            *outputImage = std::move(imageOutput);
                                            return true;
                                        };

                                        const D2D1_MATRIX_5X4_F grayscaleMatrix = D2D1::Matrix5x4F(
                                            0.299f, 0.299f, 0.299f, 0.0f,
                                            0.587f, 0.587f, 0.587f, 0.0f,
                                            0.114f, 0.114f, 0.114f, 0.0f,
                                            0.000f, 0.000f, 0.000f, 1.0f,
                                            0.000f, 0.000f, 0.000f, 0.0f);
                                        const D2D1_MATRIX_5X4_F contrastGrayMatrix = D2D1::Matrix5x4F(
                                            0.380f, 0.380f, 0.380f, 0.0f,
                                            0.560f, 0.560f, 0.560f, 0.0f,
                                            0.120f, 0.120f, 0.120f, 0.0f,
                                            0.000f, 0.000f, 0.000f, 1.0f,
                                            0.020f, 0.020f, 0.020f, 0.0f);
                                        const D2D1_MATRIX_5X4_F identityMatrix = D2D1::Matrix5x4F();

                                        const float outgoingAmount = std::clamp(eased * 1.1f, 0.0f, 1.0f);
                                        const float incomingAmount = std::clamp((1.0f - eased) * 1.05f, 0.0f, 1.0f);
                                        const D2D1_MATRIX_5X4_F outgoingMatrix = LerpColorMatrix(identityMatrix, contrastGrayMatrix, outgoingAmount);
                                        const D2D1_MATRIX_5X4_F incomingMatrix = LerpColorMatrix(grayscaleMatrix, identityMatrix, eased);

                                        Microsoft::WRL::ComPtr<ID2D1Image> fromImageEffect;
                                        Microsoft::WRL::ComPtr<ID2D1Image> toImageEffect;
                                        if (buildColorMatrixImage(transitionFromBitmap_.Get(), outgoingMatrix, &fromImageEffect)
                                            && buildColorMatrixImage(d2dCurrentImageBitmap_.Get(), incomingMatrix, &toImageEffect)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), fromImageEffect.Get(), *transitionFromImage_,
                                                               gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f)
                                            && DrawImageEffect(d2dRenderTarget_.Get(), toImageEffect.Get(), *currentImage_,
                                                               gdiClientRect, eased, 1.01f + (0.01f * incomingAmount),
                                                               0.0f, 0.0f))
                                        {
                                            renderedWithEffects = true;
                                        }
                                    }

                                    if (!renderedWithEffects)
                                    {
                                        DrawImageBitmap(d2dRenderTarget_.Get(), transitionFromBitmap_.Get(), *transitionFromImage_,
                                                        gdiClientRect, 1.0f - eased, 1.0f, 0.0f, 0.0f);
                                        DrawImageBitmap(d2dRenderTarget_.Get(), d2dCurrentImageBitmap_.Get(), *currentImage_,
                                                        gdiClientRect, eased, 1.01f + (0.01f * (1.0f - eased)), 0.0f, 0.0f);
                                    }

                                    drewTransition = true;
                                    break;
                                }
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
                        const ViewerOverlayMetrics& overlayMetrics = ViewerOverlayMetricsForTextSize(infoOverlayTextSize_);
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

                        const float availablePanelWidth = fullMetadataVisible_
                            ? std::max(120.0f, (clientWidth * (2.0f / 3.0f)) - 32.0f)
                            : std::max(120.0f, clientWidth - 32.0f);
                        const float topPanelWidth = std::min((compareLayout ? 760.0f : 560.0f) * overlayMetrics.overlayWidthScale, availablePanelWidth);
                        const float bottomPanelWidth = std::min((compareLayout ? 640.0f : 380.0f) * overlayMetrics.overlayWidthScale, availablePanelWidth);
                        const float topPanelHeight = (overlayMetrics.topPanelPaddingY * 2.0f)
                            + overlayMetrics.topNameHeight
                            + overlayMetrics.topInfoHeight;
                        const float bottomPanelHeight = (overlayMetrics.bottomPanelPaddingY * 2.0f)
                            + overlayMetrics.bottomInfoHeight;
                        const float bottomPanelLeft = fullMetadataVisible_
                            ? 16.0f
                            : clientWidth - 16.0f - bottomPanelWidth;
                        D2D1_RECT_F topPanel = D2D1::RectF(16, 16, 16 + topPanelWidth, 16 + topPanelHeight);
                        D2D1_RECT_F bottomPanel = D2D1::RectF(bottomPanelLeft,
                                                              clientHeight - 16 - bottomPanelHeight,
                                                              bottomPanelLeft + bottomPanelWidth,
                                                              clientHeight - 16);

                        const D2D1_ROUNDED_RECT roundedTop = D2D1::RoundedRect(topPanel, 8.0f, 8.0f);
                        const D2D1_ROUNDED_RECT roundedBottom = D2D1::RoundedRect(bottomPanel, 8.0f, 8.0f);

                        if (d2dPanelFillBrush_) d2dRenderTarget_->FillRoundedRectangle(roundedTop, d2dPanelFillBrush_.Get());
                        if (d2dPanelBorderBrush_) d2dRenderTarget_->DrawRoundedRectangle(roundedTop, d2dPanelBorderBrush_.Get(), 1.0f);
                        if (d2dPanelFillBrush_) d2dRenderTarget_->FillRoundedRectangle(roundedBottom, d2dPanelFillBrush_.Get());
                        if (d2dPanelBorderBrush_) d2dRenderTarget_->DrawRoundedRectangle(roundedBottom, d2dPanelBorderBrush_.Get(), 1.0f);

                        D2D1_RECT_F nameRect = D2D1::RectF(topPanel.left + overlayMetrics.topPanelPaddingX,
                                                           topPanel.top + overlayMetrics.topPanelPaddingY,
                                                           topPanel.right - overlayMetrics.topPanelPaddingX,
                                                           topPanel.top + overlayMetrics.topPanelPaddingY + overlayMetrics.topNameHeight);
                        D2D1_RECT_F topInfoRect = D2D1::RectF(topPanel.left + overlayMetrics.topPanelPaddingX,
                                                              nameRect.bottom,
                                                              topPanel.right - overlayMetrics.topPanelPaddingX,
                                                              nameRect.bottom + overlayMetrics.topInfoHeight);
                        D2D1_RECT_F bottomInfoRect = D2D1::RectF(bottomPanel.left + overlayMetrics.bottomPanelPaddingX,
                                                                 bottomPanel.top + overlayMetrics.bottomPanelPaddingY,
                                                                 bottomPanel.right - overlayMetrics.bottomPanelPaddingX,
                                                                 bottomPanel.top + overlayMetrics.bottomPanelPaddingY + overlayMetrics.bottomInfoHeight);

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
                        }
                        if (d2dBottomInfoFormat_ && d2dTextBrush_)
                        {
                            d2dBottomInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            d2dRenderTarget_->DrawText(bottomLine.c_str(), static_cast<UINT32>(bottomLine.size()),
                                                       d2dBottomInfoFormat_.Get(), bottomInfoRect, d2dTextBrush_.Get());
                        }
                    }

                    if (fullMetadataVisible_)
                    {
                        const ViewerOverlayMetrics& overlayMetrics = ViewerOverlayMetricsForTextSize(infoOverlayTextSize_);
                        const float metadataLeft = std::max(16.0f, (clientWidth * (2.0f / 3.0f)) + 8.0f);
                        const std::wstring metadataBody = metadataLoading_
                            ? std::wstring(L"Loading metadata...")
                            : (currentMetadataText_.empty() ? std::wstring(L"No metadata is available for the current image.") : currentMetadataText_);
                        const float metadataPadding = overlayMetrics.metadataPanelPadding;
                        const float metadataPanelWidth = std::max(160.0f, clientWidth - metadataLeft - 16.0f);
                        const float metadataTextWidth = std::max(64.0f, metadataPanelWidth - (metadataPadding * 2.0f));
                        const float metadataMaxPanelHeight = std::max(96.0f, clientHeight - 32.0f);
                        const float metadataMaxBodyHeight = std::max(
                            overlayMetrics.metadataFontSize + overlayMetrics.metadataGap,
                            metadataMaxPanelHeight - ((metadataPadding * 2.0f) + overlayMetrics.topNameHeight + overlayMetrics.metadataGap));
                        const float measuredBodyHeight = (d2dMetadataFormat_ && render::D2DRenderer::Instance().DWriteFactory())
                            ? MeasureWrappedTextHeight(render::D2DRenderer::Instance().DWriteFactory(),
                                                       d2dMetadataFormat_.Get(),
                                                       metadataBody,
                                                       metadataTextWidth,
                                                       metadataMaxBodyHeight)
                            : 0.0f;
                        const float metadataBodyHeight = measuredBodyHeight > 0.0f
                            ? std::min(metadataMaxBodyHeight, measuredBodyHeight)
                            : metadataMaxBodyHeight;
                        const float metadataPanelHeight = std::min(
                            metadataMaxPanelHeight,
                            (metadataPadding * 2.0f) + overlayMetrics.topNameHeight + overlayMetrics.metadataGap + metadataBodyHeight);
                        const D2D1_RECT_F metadataPanel = D2D1::RectF(metadataLeft,
                                                                      16.0f,
                                                                      metadataLeft + metadataPanelWidth,
                                                                      16.0f + metadataPanelHeight);
                        const D2D1_ROUNDED_RECT roundedMetadata = D2D1::RoundedRect(metadataPanel, 10.0f, 10.0f);
                        const D2D1_RECT_F metadataTitleRect = D2D1::RectF(metadataPanel.left + metadataPadding,
                                                                          metadataPanel.top + metadataPadding,
                                                                          metadataPanel.right - metadataPadding,
                                                                          metadataPanel.top + metadataPadding + overlayMetrics.topNameHeight);
                        const D2D1_RECT_F metadataBodyRect = D2D1::RectF(metadataPanel.left + metadataPadding,
                                                                         metadataTitleRect.bottom + overlayMetrics.metadataGap,
                                                                         metadataPanel.right - metadataPadding,
                                                                         metadataTitleRect.bottom + overlayMetrics.metadataGap + metadataBodyHeight);

                        if (d2dMetadataPanelFillBrush_)
                        {
                            d2dRenderTarget_->FillRoundedRectangle(roundedMetadata, d2dMetadataPanelFillBrush_.Get());
                        }
                        if (d2dMetadataPanelBorderBrush_)
                        {
                            d2dRenderTarget_->DrawRoundedRectangle(roundedMetadata, d2dMetadataPanelBorderBrush_.Get(), 1.0f);
                        }
                        if (d2dNameFormat_ && d2dTextBrush_)
                        {
                            d2dNameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            d2dRenderTarget_->DrawText(L"Image Metadata", 14,
                                                       d2dNameFormat_.Get(), metadataTitleRect, d2dTextBrush_.Get());
                        }
                        if (d2dMetadataFormat_ && (metadataLoading_ ? d2dMutedTextBrush_ : d2dTextBrush_))
                        {
                            d2dMetadataFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            d2dRenderTarget_->DrawText(metadataBody.c_str(), static_cast<UINT32>(metadataBody.size()),
                                                       d2dMetadataFormat_.Get(), metadataBodyRect,
                                                       metadataLoading_ ? d2dMutedTextBrush_.Get() : d2dTextBrush_.Get());
                        }
                    }

                    if (!wraparoundMessage_.empty() && d2dInfoFormat_ && d2dTextBrush_)
                    {
                        const float toastWidth = std::min(320.0f, std::max(120.0f, clientWidth - 32.0f));
                        const float toastHeight = 42.0f;
                        const float toastLeft = std::max(8.0f, (clientWidth - toastWidth) / 2.0f);
                        const D2D1_RECT_F toastRect = D2D1::RectF(toastLeft,
                                                                  16.0f,
                                                                  toastLeft + toastWidth,
                                                                  16.0f + toastHeight);
                        const D2D1_ROUNDED_RECT roundedToast = D2D1::RoundedRect(toastRect, 8.0f, 8.0f);
                        if (d2dPanelFillBrush_)
                        {
                            d2dRenderTarget_->FillRoundedRectangle(roundedToast, d2dPanelFillBrush_.Get());
                        }
                        if (d2dPanelBorderBrush_)
                        {
                            d2dRenderTarget_->DrawRoundedRectangle(roundedToast, d2dPanelBorderBrush_.Get(), 1.0f);
                        }

                        d2dInfoFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        d2dRenderTarget_->DrawText(wraparoundMessage_.c_str(),
                                                   static_cast<UINT32>(wraparoundMessage_.size()),
                                                   d2dInfoFormat_.Get(),
                                                   D2D1::RectF(toastRect.left + 12.0f,
                                                               toastRect.top,
                                                               toastRect.right - 12.0f,
                                                               toastRect.bottom),
                                                   d2dTextBrush_.Get());
                    }
                }

                const HRESULT hr = d2dRenderTarget_->EndDraw();
                const bool recreateTarget = hr == D2DERR_RECREATE_TARGET;
                if (recreateTarget)
                {
                    ReleaseD2DResources();
                }

                EndPaint(hwnd_, &paintStruct);
                if (recreateTarget)
                {
                    RequestRepaint();
                }
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

            if (!wraparoundMessage_.empty())
            {
                const int toastWidth = std::min(320, std::max(120, clientWidth - 32));
                const int toastHeight = 42;
                const int toastLeft = std::max(8, (clientWidth - toastWidth) / 2);
                const int toastTop = 16;
                HBRUSH toastBrush = CreateSolidBrush(PanelFillColor(darkTheme_));
                HPEN toastPen = CreatePen(PS_SOLID, 1, PanelBorderColor(darkTheme_));
                HGDIOBJ oldBrush = SelectObject(hdc, toastBrush);
                HGDIOBJ oldPen = SelectObject(hdc, toastPen);
                RoundRect(hdc, toastLeft, toastTop, toastLeft + toastWidth, toastTop + toastHeight, 12, 12);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(toastPen);
                DeleteObject(toastBrush);

                SetTextColor(hdc, TextColor(darkTheme_));
                RECT toastRect{toastLeft + 12, toastTop, toastLeft + toastWidth - 12, toastTop + toastHeight};
                DrawTextW(hdc, wraparoundMessage_.c_str(), static_cast<int>(wraparoundMessage_.size()), &toastRect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            EndPaint(hwnd_, &paintStruct);
            return 0;
        }
        case WM_CLOSE:
            util::LogInfo(L"ViewerWindow WM_CLOSE hwnd=" + FormatWindowHandle(hwnd));
            asyncState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
            asyncState_->targetWindow.store(nullptr, std::memory_order_release);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            util::LogInfo(L"ViewerWindow WM_DESTROY hwnd=" + FormatWindowHandle(hwnd));
            ClearWraparoundMessage();
            StopSlideshow();
            StopTransition();
            memoryPressureActive_ = false;
            if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }
            ReleaseD2DResources();
            asyncState_->targetWindow.store(nullptr, std::memory_order_release);
            fullScreen_ = false;
            windowFitMode_ = WindowFitMode::Regular;
            hasRegularPlacementBeforeFit_ = false;
            regularPlacementBeforeFit_ = WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)};
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
            self->asyncState_->targetWindow.store(hwnd, std::memory_order_release);
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