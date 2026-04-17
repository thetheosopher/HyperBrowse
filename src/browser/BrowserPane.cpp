#include "browser/BrowserPane.h"

#include <commctrl.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <memory>

#include "app/resource.h"
#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "render/D2DRenderer.h"
#include "services/ImageMetadataService.h"
#include "services/ThumbnailScheduler.h"
#include "util/ResourcePng.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr int kWheelScrollAmount = 72;
    constexpr float kThumbnailPreviewAspectRatio = 0.72f;
    constexpr int kVisiblePriority = 0;
    constexpr int kNearVisiblePriority = 1;
    constexpr int kProactivePrefetchPriority = 2;
    constexpr int kThumbnailNearVisiblePrefetchRows = 2;
    constexpr int kThumbnailMinimumTopOfFolderPrefetchRows = 10;
    constexpr int kThumbnailMaximumTopOfFolderPrefetchRows = 40;
    constexpr int kThumbnailMinimumLookAheadPrefetchRows = 4;
    constexpr int kThumbnailMaximumLookAheadPrefetchRows = 20;
    constexpr int kMetadataNearVisiblePrefetchRows = 2;
    constexpr int kMetadataMinimumTopOfFolderPrefetchRows = 12;
    constexpr int kMetadataMaximumTopOfFolderPrefetchRows = 48;
    constexpr int kMetadataMinimumLookAheadPrefetchRows = 8;
    constexpr int kMetadataMaximumLookAheadPrefetchRows = 32;
    constexpr int kMinimumDetailsMetadataNearVisibleItems = 12;
    constexpr int kMinimumDetailsMetadataTopOfFolderItems = 24;
    constexpr int kMaximumDetailsMetadataTopOfFolderItems = 192;
    constexpr int kMinimumDetailsMetadataLookAheadItems = 12;
    constexpr int kMaximumDetailsMetadataLookAheadItems = 96;
    constexpr int kPlaceholderBrandArtSize = 256;
    constexpr int kPlaceholderIconDisplaySize = 128;
    constexpr int kUnavailableThumbnailIconSize = 48;
    constexpr int kPlaceholderTitlePointSize = 18;
    constexpr int kPlaceholderBodyPointSize = 13;

    const wchar_t* UnavailableThumbnailMessage(hyperbrowse::decode::ThumbnailDecodeFailureKind failureKind)
    {
        switch (failureKind)
        {
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut:
            return L"Thumbnail\r\nTimed Out";
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed:
        default:
            return L"Thumbnail\r\nDecode Failed";
        }
    }

    const wchar_t* UnavailableThumbnailTooltipText(hyperbrowse::decode::ThumbnailDecodeFailureKind failureKind)
    {
        switch (failureKind)
        {
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut:
            return L"Thumbnail: generation timed out.";
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed:
        default:
            return L"Thumbnail: generation failed.";
        }
    }

    hyperbrowse::browser::BrowserPane::ThemeColors MakeThemeColors(bool darkTheme)
    {
        if (darkTheme)
        {
            return hyperbrowse::browser::BrowserPane::ThemeColors{
                RGB(18, 22, 27),
                RGB(31, 36, 42),
                RGB(23, 27, 33),
                RGB(34, 50, 68),
                RGB(236, 240, 245),
                RGB(152, 161, 171),
                RGB(74, 82, 92),
                RGB(112, 169, 227),
                RGB(47, 68, 92),
                RGB(244, 248, 252),
                RGB(138, 189, 239),
                RGB(26, 31, 37),
                RGB(27, 32, 38),
            };
        }

        return hyperbrowse::browser::BrowserPane::ThemeColors{
            RGB(236, 240, 245),
            RGB(252, 253, 255),
            RGB(228, 234, 242),
            RGB(236, 243, 251),
            RGB(27, 34, 43),
            RGB(96, 105, 115),
            RGB(201, 210, 220),
            RGB(54, 114, 186),
            RGB(220, 233, 247),
            RGB(25, 35, 50),
            RGB(90, 144, 214),
            RGB(245, 248, 252),
            RGB(241, 245, 250),
        };
    }

    std::wstring ToLowercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return value;
    }

    std::wstring ToUppercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });
        return value;
    }

    std::wstring TrimWhitespace(std::wstring value)
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

    int CompareCaseInsensitive(const std::wstring& lhs, const std::wstring& rhs)
    {
        return _wcsicmp(lhs.c_str(), rhs.c_str());
    }

    std::wstring BuildCameraLabel(const hyperbrowse::services::ImageMetadata& metadata)
    {
        if (!metadata.cameraMake.empty() && !metadata.cameraModel.empty())
        {
            return metadata.cameraMake + L" " + metadata.cameraModel;
        }

        return !metadata.cameraModel.empty() ? metadata.cameraModel : metadata.cameraMake;
    }

    std::wstring BuildThumbnailDisplayTitle(const hyperbrowse::browser::BrowserItem& item)
    {
        const std::wstring stem = fs::path(item.fileName).stem().wstring();
        return stem.empty() ? item.fileName : stem;
    }

    RECT NormalizeRect(POINT start, POINT end)
    {
        RECT rect{};
        rect.left = std::min(start.x, end.x);
        rect.top = std::min(start.y, end.y);
        rect.right = std::max(start.x, end.x);
        rect.bottom = std::max(start.y, end.y);
        return rect;
    }

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

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> CreateIconThumbnail(HICON iconHandle, int iconSize)
    {
        if (!iconHandle || iconSize <= 0)
        {
            return {};
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = iconSize;
        bitmapInfo.bmiHeader.biHeight = -iconSize;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return {};
        }

        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap)
        {
            ReleaseDC(nullptr, screenDc);
            return {};
        }

        const std::size_t bufferSize = static_cast<std::size_t>(iconSize) * static_cast<std::size_t>(iconSize) * 4U;
        if (pixels)
        {
            ZeroMemory(pixels, bufferSize);
        }

        HDC memoryDc = CreateCompatibleDC(screenDc);
        if (!memoryDc)
        {
            DeleteObject(bitmap);
            ReleaseDC(nullptr, screenDc);
            return {};
        }

        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
        DrawIconEx(memoryDc, 0, 0, iconHandle, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        SelectObject(memoryDc, oldBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);

        return std::make_shared<const hyperbrowse::cache::CachedThumbnail>(
            bitmap,
            iconSize,
            iconSize,
            bufferSize,
            iconSize,
            iconSize);
    }

    HFONT CreateSystemUiFont()
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
        {
            metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
            metrics.lfMessageFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
            return CreateFontIndirectW(&metrics.lfMessageFont);
        }

        return nullptr;
    }

    HFONT CreateSizedUiFont(int pointSize, int weight)
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

    D2D1_RECT_F InsetD2DRect(const D2D1_RECT_F& rect, float insetX, float insetY)
    {
        return D2D1::RectF(rect.left + insetX,
                           rect.top + insetY,
                           rect.right - insetX,
                           rect.bottom - insetY);
    }

}

namespace hyperbrowse::browser
{
    std::wstring BrowserSortModeToLabel(BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case BrowserSortMode::FileName:
            return L"Filename";
        case BrowserSortMode::ModifiedDate:
            return L"Modified";
        case BrowserSortMode::FileSize:
            return L"File Size";
        case BrowserSortMode::Dimensions:
            return L"Dimensions";
        case BrowserSortMode::FileType:
            return L"Type";
        case BrowserSortMode::Random:
            return L"Random";
        case BrowserSortMode::DateTaken:
            return L"Date Taken";
        default:
            return L"Unknown";
        }
    }

    int ThumbnailSizePresetToPixels(ThumbnailSizePreset preset)
    {
        return static_cast<int>(preset);
    }

    std::wstring ThumbnailSizePresetToLabel(ThumbnailSizePreset preset)
    {
        return std::to_wstring(ThumbnailSizePresetToPixels(preset)) + L" px";
    }

    BrowserPane::BrowserPane(HINSTANCE instance)
        : instance_(instance)
        , colors_(MakeThemeColors(false))
        , thumbnailScheduler_(std::make_unique<services::ThumbnailScheduler>())
        , metadataService_(std::make_unique<services::ImageMetadataService>())
    {
        placeholderArt_ = util::LoadPngResourceBitmap(instance_,
                                                      IDB_HYPERBROWSE_BRAND_PNG,
                                                      kPlaceholderBrandArtSize,
                                                      kPlaceholderBrandArtSize);
        unavailableThumbnailArt_ = CreateIconThumbnail(LoadIconW(nullptr, IDI_WARNING), 64);
        RebuildThemeResources();
        RebuildThumbnailFonts();
    }

    BrowserPane::~BrowserPane()
    {
        ReleaseD2DResources();

        if (detailsListFont_ && ownsDetailsListFont_)
        {
            DeleteObject(detailsListFont_);
        }

        ReleaseThumbnailFonts();

        ReleaseThemeResources();
    }

    bool BrowserPane::Create(HWND parent)
    {
        parent_ = parent;
        if (!RegisterClass())
        {
            return false;
        }

        hwnd_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            kClassName,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | WS_VSCROLL,
            0,
            0,
            100,
            100,
            parent_,
            nullptr,
            instance_,
            this);

        if (hwnd_ && thumbnailScheduler_)
        {
            thumbnailScheduler_->BindTargetWindow(hwnd_);
        }
        if (hwnd_ && metadataService_)
        {
            metadataService_->BindTargetWindow(hwnd_);
        }

        return hwnd_ != nullptr;
    }

    HWND BrowserPane::Hwnd() const noexcept
    {
        return hwnd_;
    }

    void BrowserPane::SetModel(BrowserModel* model)
    {
        if (model_ == model)
        {
            return;
        }

        model_ = model;
        ++thumbnailSessionId_;
        ++metadataSessionId_;
        HideThumbnailTooltip();
        CancelThumbnailWork();
        if (metadataService_)
        {
            metadataService_->CancelOutstanding();
        }
    }

    void BrowserPane::RefreshFromModel()
    {
        HideThumbnailTooltip();
        d2dBitmapCache_.clear();
        RebuildOrder();
        UpdateSelectionBytes();
        UpdateDetailsListView();
        SyncDetailsListSelectionFromModel();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::SetViewMode(BrowserViewMode mode)
    {
        if (viewMode_ == mode)
        {
            return;
        }

        HideThumbnailTooltip();
        viewMode_ = mode;
        LayoutChildren();
        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            ScheduleVisibleThumbnailWork();
        }
        else
        {
            CancelThumbnailWork();
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    BrowserViewMode BrowserPane::GetViewMode() const noexcept
    {
        return viewMode_;
    }

    void BrowserPane::SetSortMode(BrowserSortMode sortMode)
    {
        if (sortMode_ == sortMode)
        {
            return;
        }

        sortMode_ = sortMode;
        RebuildOrder();
        UpdateDetailsListView();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    BrowserSortMode BrowserPane::GetSortMode() const noexcept
    {
        return sortMode_;
    }

    void BrowserPane::SetSortAscending(bool ascending)
    {
        if (sortAscending_ == ascending)
        {
            return;
        }

        sortAscending_ = ascending;
        RebuildOrder();
        UpdateDetailsListView();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    bool BrowserPane::IsSortAscending() const noexcept
    {
        return sortAscending_;
    }

    void BrowserPane::SetFilterQuery(std::wstring query)
    {
        query = TrimWhitespace(std::move(query));
        const std::wstring queryLower = ToLowercase(query);
        if (filterQuery_ == query && filterQueryLower_ == queryLower)
        {
            return;
        }

        filterQuery_ = std::move(query);
        filterQueryLower_ = std::move(queryLower);
        RebuildOrder();
        UpdateSelectionBytes();
        UpdateDetailsListView();
        SyncDetailsListSelectionFromModel();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    const std::wstring& BrowserPane::GetFilterQuery() const noexcept
    {
        return filterQuery_;
    }

    bool BrowserPane::HasActiveFilter() const noexcept
    {
        return !filterQuery_.empty();
    }

    std::uint64_t BrowserPane::DisplayedItemCount() const noexcept
    {
        return static_cast<std::uint64_t>(orderedModelIndices_.size());
    }

    void BrowserPane::SetThumbnailSizePreset(ThumbnailSizePreset preset)
    {
        if (thumbnailSizePreset_ == preset)
        {
            return;
        }

        thumbnailSizePreset_ = preset;
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    ThumbnailSizePreset BrowserPane::GetThumbnailSizePreset() const noexcept
    {
        return thumbnailSizePreset_;
    }

    void BrowserPane::SetCompactThumbnailLayout(bool enabled)
    {
        enabled = true;
        if (compactThumbnailLayout_ == enabled)
        {
            return;
        }

        compactThumbnailLayout_ = enabled;
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    bool BrowserPane::IsCompactThumbnailLayoutEnabled() const noexcept
    {
        return true;
    }

    void BrowserPane::SetThumbnailDetailsVisible(bool visible)
    {
        if (thumbnailDetailsVisible_ == visible)
        {
            return;
        }

        thumbnailDetailsVisible_ = visible;
        RebuildThumbnailFonts();
        RebuildD2DTextFormats();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    bool BrowserPane::AreThumbnailDetailsVisible() const noexcept
    {
        return thumbnailDetailsVisible_;
    }

    void BrowserPane::SetDarkTheme(bool enabled)
    {
        darkTheme_ = enabled;
        colors_ = MakeThemeColors(enabled);

        RebuildThemeResources();
        if (d2dRenderTarget_)
        {
            RebuildD2DBrushes();
        }
        ApplyThemeToDetailsList();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::ClearSelection()
    {
        selectedModelIndices_.clear();
        rubberBandSeedSelection_.clear();
        anchorModelIndex_ = -1;
        focusedModelIndex_ = -1;
        selectedBytes_ = 0;
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    std::uint64_t BrowserPane::SelectedCount() const noexcept
    {
        return static_cast<std::uint64_t>(selectedModelIndices_.size());
    }

    std::uint64_t BrowserPane::SelectedBytes() const noexcept
    {
        return selectedBytes_;
    }

    int BrowserPane::PrimarySelectedModelIndex() const noexcept
    {
        if (focusedModelIndex_ >= 0)
        {
            return focusedModelIndex_;
        }

        if (!selectedModelIndices_.empty())
        {
            return *selectedModelIndices_.begin();
        }

        return -1;
    }

    std::vector<int> BrowserPane::OrderedModelIndicesSnapshot() const
    {
        return orderedModelIndices_;
    }

    std::vector<int> BrowserPane::OrderedSelectedModelIndicesSnapshot() const
    {
        std::vector<int> orderedSelection;
        orderedSelection.reserve(selectedModelIndices_.size());
        for (const int modelIndex : orderedModelIndices_)
        {
            if (selectedModelIndices_.contains(modelIndex))
            {
                orderedSelection.push_back(modelIndex);
            }
        }
        return orderedSelection;
    }

    std::vector<std::wstring> BrowserPane::SelectedFilePathsSnapshot() const
    {
        std::vector<std::wstring> filePaths;
        if (!model_)
        {
            return filePaths;
        }

        const auto& items = model_->Items();
        for (const int modelIndex : OrderedSelectedModelIndicesSnapshot())
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(items.size()))
            {
                filePaths.push_back(items[static_cast<std::size_t>(modelIndex)].filePath);
            }
        }

        return filePaths;
    }

    std::wstring BrowserPane::FocusedFilePathSnapshot() const
    {
        if (!model_ || focusedModelIndex_ < 0)
        {
            return {};
        }

        const auto& items = model_->Items();
        return focusedModelIndex_ < static_cast<int>(items.size())
            ? items[static_cast<std::size_t>(focusedModelIndex_)].filePath
            : std::wstring{};
    }

    void BrowserPane::RestoreSelectionByFilePaths(const std::vector<std::wstring>& filePaths, const std::wstring& focusedFilePath)
    {
        selectedModelIndices_.clear();
        anchorModelIndex_ = -1;
        focusedModelIndex_ = -1;

        if (!model_)
        {
            return;
        }

        for (const std::wstring& filePath : filePaths)
        {
            const int modelIndex = model_->FindItemIndexByPath(filePath);
            if (modelIndex >= 0)
            {
                selectedModelIndices_.insert(modelIndex);
                if (anchorModelIndex_ < 0)
                {
                    anchorModelIndex_ = modelIndex;
                }
            }
        }

        if (!focusedFilePath.empty())
        {
            focusedModelIndex_ = model_->FindItemIndexByPath(focusedFilePath);
        }
        if (focusedModelIndex_ < 0 && !selectedModelIndices_.empty())
        {
            focusedModelIndex_ = *selectedModelIndices_.begin();
        }

        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::InvalidateMediaCacheForPaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        d2dBitmapCache_.clear();

        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->CancelOutstanding();
            thumbnailScheduler_->InvalidateFilePaths(filePaths);
        }
        if (metadataService_)
        {
            metadataService_->InvalidateFilePaths(filePaths);
        }

        ScheduleVisibleThumbnailWork();
    }

    std::shared_ptr<const hyperbrowse::services::ImageMetadata> BrowserPane::FindCachedMetadataForModelIndex(int modelIndex) const
    {
        if (!metadataService_ || !model_ || modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
        {
            return nullptr;
        }

        return metadataService_->FindCachedMetadata(model_->Items()[static_cast<std::size_t>(modelIndex)]);
    }

    void BrowserPane::RequestMetadataForModelIndices(const std::vector<int>& modelIndices) const
    {
        if (!metadataService_ || !model_ || modelIndices.empty())
        {
            return;
        }

        std::vector<services::MetadataWorkItem> workItems;
        workItems.reserve(modelIndices.size());
        for (const int modelIndex : modelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
            {
                continue;
            }

            workItems.push_back(services::MetadataWorkItem{
                modelIndex,
                model_->Items()[static_cast<std::size_t>(modelIndex)],
                kVisiblePriority,
            });
        }

        if (!workItems.empty())
        {
            metadataService_->Schedule(metadataSessionId_, std::move(workItems));
        }
    }

    std::wstring BrowserPane::BuildMetadataReportForModelIndex(int modelIndex) const
    {
        if (!model_ || modelIndex < 0 || modelIndex >= static_cast<int>(model_->Items().size()))
        {
            return {};
        }

        const BrowserItem& item = model_->Items()[static_cast<std::size_t>(modelIndex)];
        std::wstring errorMessage;
        const auto metadata = FindCachedMetadataForModelIndex(modelIndex)
            ? FindCachedMetadataForModelIndex(modelIndex)
            : services::ExtractImageMetadata(item, &errorMessage);
        if (!metadata)
        {
            return errorMessage.empty() ? L"No metadata is available for the selected image." : errorMessage;
        }

        return services::FormatImageMetadataReport(item, *metadata);
    }

    bool BrowserPane::RegisterClass() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kClassName, &windowClass) != FALSE)
        {
            return true;
        }

        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &BrowserPane::WindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        return RegisterClassExW(&windowClass) != 0;
    }

    bool BrowserPane::CreateDetailsListView()
    {
        detailsList_ = CreateWindowExW(
            0,
            WC_LISTVIEWW,
            nullptr,
            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
            0,
            0,
            100,
            100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!detailsList_)
        {
            return false;
        }

        ListView_SetUnicodeFormat(detailsList_, TRUE);
        ListView_SetExtendedListViewStyle(detailsList_, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);

        detailsListFont_ = CreateSystemUiFont();
        ownsDetailsListFont_ = detailsListFont_ != nullptr;
        if (!detailsListFont_)
        {
            detailsListFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            ownsDetailsListFont_ = false;
        }

        SendMessageW(detailsList_, WM_SETFONT, reinterpret_cast<WPARAM>(detailsListFont_), TRUE);
        if (HWND header = ListView_GetHeader(detailsList_))
        {
            SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(detailsListFont_), TRUE);
        }

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        std::wstring name = L"Name";
        column.pszText = name.data();
        column.cx = 280;
        ListView_InsertColumn(detailsList_, 0, &column);

        std::wstring type = L"Type";
        column.pszText = type.data();
        column.cx = 90;
        column.iSubItem = 1;
        ListView_InsertColumn(detailsList_, 1, &column);

        std::wstring size = L"Size";
        column.pszText = size.data();
        column.cx = 110;
        column.iSubItem = 2;
        ListView_InsertColumn(detailsList_, 2, &column);

        std::wstring modified = L"Modified";
        column.pszText = modified.data();
        column.cx = 150;
        column.iSubItem = 3;
        ListView_InsertColumn(detailsList_, 3, &column);

        std::wstring dimensions = L"Dimensions";
        column.pszText = dimensions.data();
        column.cx = 110;
        column.iSubItem = 4;
        ListView_InsertColumn(detailsList_, 4, &column);

        std::wstring dateTaken = L"Date Taken";
        column.pszText = dateTaken.data();
        column.cx = 160;
        column.iSubItem = 5;
        ListView_InsertColumn(detailsList_, 5, &column);

        std::wstring camera = L"Camera";
        column.pszText = camera.data();
        column.cx = 180;
        column.iSubItem = 6;
        ListView_InsertColumn(detailsList_, 6, &column);

        std::wstring title = L"Title";
        column.pszText = title.data();
        column.cx = 220;
        column.iSubItem = 7;
        ListView_InsertColumn(detailsList_, 7, &column);

        ApplyThemeToDetailsList();
        return true;
    }

    void BrowserPane::CreateThumbnailTooltip()
    {
        if (thumbnailTooltip_ || !hwnd_)
        {
            return;
        }

        thumbnailTooltip_ = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            hwnd_,
            nullptr,
            instance_,
            nullptr);
        if (!thumbnailTooltip_)
        {
            return;
        }

        SetWindowPos(thumbnailTooltip_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(thumbnailTooltip_, TTM_SETMAXTIPWIDTH, 0, 420);

        TOOLINFOW toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.uFlags = TTF_SUBCLASS;
        toolInfo.hwnd = hwnd_;
        toolInfo.uId = 1;
        SetRectEmpty(&toolInfo.rect);
        toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(thumbnailTooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
    }

    BrowserPane::ThumbnailLayoutMetrics BrowserPane::CurrentThumbnailLayout() const
    {
        const int previewWidth = ThumbnailSizePresetToPixels(thumbnailSizePreset_);
        const int previewHeight = std::max(72, static_cast<int>(std::lround(static_cast<double>(previewWidth) * kThumbnailPreviewAspectRatio)));
        const bool compact = compactThumbnailLayout_;
        const bool showDetails = thumbnailDetailsVisible_;

        ThumbnailLayoutMetrics layout;
        layout.cellPadding = compact ? std::max(6, previewWidth / 20) : std::max(10, previewWidth / 14);
        layout.previewInset = compact ? std::max(4, previewWidth / 32) : std::max(8, previewWidth / 20);
        layout.previewWidth = previewWidth;
        layout.previewHeight = previewHeight;
        layout.textInset = compact ? std::max(8, previewWidth / 22) : std::max(12, previewWidth / 16);
        layout.titleTopGap = showDetails ? std::max(4, previewWidth / 56) : 0;
        layout.titleHeight = showDetails ? std::clamp(previewWidth / (compact ? 7 : 6), compact ? 20 : 22, compact ? 25 : 28) : 0;
        layout.metaTopGap = 0;
        layout.metaHeight = 0;
        layout.infoBottomInset = showDetails ? (compact ? 6 : 8) : 0;
        layout.infoHeight = showDetails ? std::clamp(previewWidth / (compact ? 10 : 9), compact ? 16 : 18, compact ? 20 : 22) : 0;
        layout.badgeHorizontalPadding = compact ? 6 : 8;
        layout.badgeGap = compact ? 6 : 10;
        layout.badgeCornerRadius = compact ? 6 : 8;
        layout.cellCornerRadius = compact ? 12 : 16;
        layout.previewCornerRadius = compact ? 10 : 12;
        layout.loadingIconSize = std::clamp(previewWidth / 5, 18, 40);
        layout.titlePointSize = std::clamp(previewWidth / (compact ? 15 : 14), compact ? 12 : 13, compact ? 14 : 15);
        layout.metaPointSize = std::clamp(previewWidth / (compact ? 18 : 17), compact ? 11 : 12, compact ? 12 : 13);
        layout.statusPointSize = std::clamp(previewWidth / (compact ? 18 : 17), compact ? 11 : 12, compact ? 12 : 13);
        const int horizontalInset = std::max(layout.previewInset, layout.textInset);
        layout.itemWidth = layout.previewWidth + (horizontalInset * 2);
        layout.itemHeight = layout.previewHeight + (layout.previewInset * 2);
        if (showDetails)
        {
            layout.itemHeight += layout.titleTopGap
                + layout.titleHeight
                + layout.metaTopGap
                + layout.metaHeight
                + layout.infoBottomInset
                + layout.infoHeight;
        }

        return layout;
    }

    void BrowserPane::LayoutChildren()
    {
        if (!hwnd_)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);

        const bool showDetailsList = viewMode_ == BrowserViewMode::Details && !orderedModelIndices_.empty();
        if (detailsList_)
        {
            ShowWindow(detailsList_, showDetailsList ? SW_SHOW : SW_HIDE);
            if (showDetailsList)
            {
                MoveWindow(detailsList_, 0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
                SyncDetailsListSelectionFromModel();
            }
        }

        UpdateVerticalScrollBar();
        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            ScheduleVisibleThumbnailWork();
        }
    }

    void BrowserPane::RebuildOrder()
    {
        orderedModelIndices_.clear();
        if (!model_)
        {
            return;
        }

        const auto& items = model_->Items();
        orderedModelIndices_.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (!filterQueryLower_.empty())
            {
                const BrowserItem& item = items[index];
                if (ToLowercase(item.fileName).find(filterQueryLower_) == std::wstring::npos)
                {
                    continue;
                }
            }

            orderedModelIndices_.push_back(static_cast<int>(index));
        }

        const auto comparator = [this, &items](int lhsIndex, int rhsIndex)
        {
            const BrowserItem& lhs = items[static_cast<std::size_t>(lhsIndex)];
            const BrowserItem& rhs = items[static_cast<std::size_t>(rhsIndex)];

            auto tieBreakByName = [&]() -> bool
            {
                const int fileNameCompare = CompareCaseInsensitive(lhs.fileName, rhs.fileName);
                if (fileNameCompare != 0)
                {
                    return fileNameCompare < 0;
                }

                return lhs.filePath < rhs.filePath;
            };

            auto compareAscending = [&]() -> bool
            {
                switch (sortMode_)
                {
                case BrowserSortMode::FileName:
                    return tieBreakByName();
                case BrowserSortMode::ModifiedDate:
                    if (lhs.modifiedTimestampUtc != rhs.modifiedTimestampUtc)
                    {
                        return lhs.modifiedTimestampUtc < rhs.modifiedTimestampUtc;
                    }
                    return tieBreakByName();
                case BrowserSortMode::FileSize:
                    if (lhs.fileSizeBytes != rhs.fileSizeBytes)
                    {
                        return lhs.fileSizeBytes < rhs.fileSizeBytes;
                    }
                    return tieBreakByName();
                case BrowserSortMode::Dimensions:
                {
                    const int lhsWidth = EffectiveImageWidth(lhs);
                    const int lhsHeight = EffectiveImageHeight(lhs);
                    const int rhsWidth = EffectiveImageWidth(rhs);
                    const int rhsHeight = EffectiveImageHeight(rhs);
                    const auto lhsArea = static_cast<long long>(lhsWidth) * static_cast<long long>(lhsHeight);
                    const auto rhsArea = static_cast<long long>(rhsWidth) * static_cast<long long>(rhsHeight);
                    if (lhsArea != rhsArea)
                    {
                        return lhsArea < rhsArea;
                    }
                    if (lhsWidth != rhsWidth)
                    {
                        return lhsWidth < rhsWidth;
                    }
                    if (lhsHeight != rhsHeight)
                    {
                        return lhsHeight < rhsHeight;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::FileType:
                {
                    const int typeCompare = CompareCaseInsensitive(lhs.fileType, rhs.fileType);
                    if (typeCompare != 0)
                    {
                        return typeCompare < 0;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::DateTaken:
                {
                    const bool lhsHas = lhs.dateTakenTimestampUtc != 0;
                    const bool rhsHas = rhs.dateTakenTimestampUtc != 0;
                    if (lhsHas != rhsHas)
                    {
                        return lhsHas;
                    }
                    if (lhsHas && lhs.dateTakenTimestampUtc != rhs.dateTakenTimestampUtc)
                    {
                        return lhs.dateTakenTimestampUtc < rhs.dateTakenTimestampUtc;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::Random:
                {
                    const auto lhsHash = std::hash<std::wstring>{}(lhs.filePath);
                    const auto rhsHash = std::hash<std::wstring>{}(rhs.filePath);
                    if (lhsHash != rhsHash)
                    {
                        return lhsHash < rhsHash;
                    }
                    return tieBreakByName();
                }
                default:
                    return tieBreakByName();
                }
            };

            const bool ascending = compareAscending();
            if (!sortAscending_ && sortMode_ != BrowserSortMode::Random)
            {
                return !ascending && !(lhs.filePath == rhs.filePath);
            }
            return ascending;
        };

        std::sort(orderedModelIndices_.begin(), orderedModelIndices_.end(), comparator);
        PruneSelectionToVisibleItems();
    }

    void BrowserPane::PruneSelectionToVisibleItems()
    {
        if (selectedModelIndices_.empty())
        {
            return;
        }

        std::unordered_set<int> visibleModelIndices;
        visibleModelIndices.reserve(orderedModelIndices_.size());
        for (const int modelIndex : orderedModelIndices_)
        {
            visibleModelIndices.insert(modelIndex);
        }

        for (auto iterator = selectedModelIndices_.begin(); iterator != selectedModelIndices_.end();)
        {
            if (!visibleModelIndices.contains(*iterator))
            {
                iterator = selectedModelIndices_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }

        if (focusedModelIndex_ >= 0 && !visibleModelIndices.contains(focusedModelIndex_))
        {
            focusedModelIndex_ = selectedModelIndices_.empty() ? -1 : *selectedModelIndices_.begin();
        }

        if (anchorModelIndex_ >= 0 && !visibleModelIndices.contains(anchorModelIndex_))
        {
            anchorModelIndex_ = focusedModelIndex_;
        }
    }

    void BrowserPane::UpdateDetailsListView()
    {
        if (!detailsList_)
        {
            return;
        }

        const int itemCount = static_cast<int>(orderedModelIndices_.size());
        ListView_SetItemCountEx(detailsList_, itemCount, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        LayoutChildren();
        InvalidateRect(detailsList_, nullptr, TRUE);
    }

    void BrowserPane::UpdateVerticalScrollBar()
    {
        if (!hwnd_)
        {
            return;
        }

        if (viewMode_ != BrowserViewMode::Thumbnails || orderedModelIndices_.empty())
        {
            scrollOffsetY_ = 0;
            ShowScrollBar(hwnd_, SB_VERT, FALSE);
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
    const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientWidth);
        const int rows = (static_cast<int>(orderedModelIndices_.size()) + columns - 1) / columns;
    const int contentHeight = layout.cellPadding + rows * (layout.itemHeight + layout.cellPadding);
        const int maxOffset = std::max(0, contentHeight - clientHeight);
        scrollOffsetY_ = std::clamp(scrollOffsetY_, 0, maxOffset);

        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scrollInfo.nMin = 0;
        scrollInfo.nMax = std::max(0, contentHeight - 1);
        scrollInfo.nPage = static_cast<UINT>(std::max(0, clientHeight));
        scrollInfo.nPos = scrollOffsetY_;
        SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
        ShowScrollBar(hwnd_, SB_VERT, maxOffset > 0);
    }

    void BrowserPane::SetScrollOffset(int value)
    {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientWidth);
        const int rows = columns > 0 ? (static_cast<int>(orderedModelIndices_.size()) + columns - 1) / columns : 0;
        const int contentHeight = layout.cellPadding + rows * (layout.itemHeight + layout.cellPadding);
        const int maxOffset = std::max(0, contentHeight - clientHeight);
        const int clampedValue = std::clamp(value, 0, maxOffset);
        if (clampedValue == scrollOffsetY_)
        {
            return;
        }

        HideThumbnailTooltip();
        scrollOffsetY_ = clampedValue;
        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_POS;
        scrollInfo.nPos = scrollOffsetY_;
        SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        ScheduleVisibleThumbnailWork();
    }

    int BrowserPane::ColumnsForClientWidth(int width) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int stride = layout.itemWidth + layout.cellPadding;
        return std::max(1, (std::max(width, stride) - layout.cellPadding) / stride);
    }

    RECT BrowserPane::GetThumbnailCellRect(int viewIndex) const
    {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int row = viewIndex / columns;
        const int column = viewIndex % columns;

        RECT rect{};
        rect.left = layout.cellPadding + column * (layout.itemWidth + layout.cellPadding);
        rect.top = layout.cellPadding + row * (layout.itemHeight + layout.cellPadding) - scrollOffsetY_;
        rect.right = rect.left + layout.itemWidth;
        rect.bottom = rect.top + layout.itemHeight;
        return rect;
    }

    RECT BrowserPane::GetThumbnailPreviewRect(const RECT& cellRect) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int cellWidth = cellRect.right - cellRect.left;
        RECT previewRect{};
        previewRect.left = cellRect.left + std::max(layout.previewInset, (cellWidth - layout.previewWidth) / 2);
        previewRect.top = cellRect.top + layout.previewInset;
        previewRect.right = previewRect.left + layout.previewWidth;
        previewRect.bottom = previewRect.top + layout.previewHeight;
        return previewRect;
    }

    int BrowserPane::HitTestThumbnailItem(POINT point) const
    {
        if (!hwnd_ || orderedModelIndices_.empty())
        {
            return -1;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        if (point.x < layout.cellPadding || point.y < 0)
        {
            return -1;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int horizontalStride = layout.itemWidth + layout.cellPadding;
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int adjustedY = point.y + scrollOffsetY_;
        if (adjustedY < layout.cellPadding)
        {
            return -1;
        }

        const int column = (point.x - layout.cellPadding) / horizontalStride;
        const int row = (adjustedY - layout.cellPadding) / verticalStride;
        if (column < 0 || column >= columns || row < 0)
        {
            return -1;
        }

        const int xWithinStride = (point.x - layout.cellPadding) % horizontalStride;
        const int yWithinStride = (adjustedY - layout.cellPadding) % verticalStride;
        if (xWithinStride > layout.itemWidth || yWithinStride > layout.itemHeight)
        {
            return -1;
        }

        const int viewIndex = row * columns + column;
        return viewIndex >= 0 && viewIndex < static_cast<int>(orderedModelIndices_.size()) ? viewIndex : -1;
    }

    int BrowserPane::ViewIndexFromModelIndex(int modelIndex) const
    {
        const auto iterator = std::find(orderedModelIndices_.begin(), orderedModelIndices_.end(), modelIndex);
        return iterator == orderedModelIndices_.end() ? -1 : static_cast<int>(std::distance(orderedModelIndices_.begin(), iterator));
    }

    int BrowserPane::ModelIndexFromViewIndex(int viewIndex) const
    {
        return viewIndex >= 0 && viewIndex < static_cast<int>(orderedModelIndices_.size())
            ? orderedModelIndices_[static_cast<std::size_t>(viewIndex)]
            : -1;
    }

    const BrowserItem* BrowserPane::ItemFromViewIndex(int viewIndex) const
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (!model_ || modelIndex < 0)
        {
            return nullptr;
        }

        const auto& items = model_->Items();
        return modelIndex < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(modelIndex)] : nullptr;
    }

    void BrowserPane::RebuildThemeResources()
    {
        ReleaseThemeResources();

        backgroundBrush_ = CreateSolidBrush(colors_.windowBackground);
        surfaceBrush_ = CreateSolidBrush(colors_.surfaceBackground);
        previewBrush_ = CreateSolidBrush(colors_.previewBackground);
        selectedCellBrush_ = CreateSolidBrush(colors_.accentFill);
        selectedPreviewBrush_ = CreateSolidBrush(colors_.selectedPreviewBackground);
        placeholderBrush_ = CreateSolidBrush(colors_.placeholderBackground);
        borderPen_ = CreatePen(PS_SOLID, 1, colors_.border);
        selectedBorderPen_ = CreatePen(PS_SOLID, 1, colors_.accent);
        rubberBandPen_ = CreatePen(PS_DOT, 1, colors_.rubberBand);
    }

    void BrowserPane::RebuildThumbnailFonts()
    {
        ReleaseThumbnailFonts();

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();

        thumbnailTitleFont_ = CreateSizedUiFont(layout.titlePointSize, FW_SEMIBOLD);
        thumbnailMetaFont_ = CreateSizedUiFont(layout.metaPointSize, FW_NORMAL);
        thumbnailStatusFont_ = CreateSizedUiFont(layout.statusPointSize, FW_SEMIBOLD);
        placeholderTitleFont_ = CreateSizedUiFont(kPlaceholderTitlePointSize, FW_SEMIBOLD);
        placeholderBodyFont_ = CreateSizedUiFont(kPlaceholderBodyPointSize, FW_NORMAL);

        if (!thumbnailTitleFont_)
        {
            thumbnailTitleFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        if (!thumbnailMetaFont_)
        {
            thumbnailMetaFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        if (!thumbnailStatusFont_)
        {
            thumbnailStatusFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        if (!placeholderTitleFont_)
        {
            placeholderTitleFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        if (!placeholderBodyFont_)
        {
            placeholderBodyFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
    }

    void BrowserPane::ReleaseThemeResources()
    {
        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
            backgroundBrush_ = nullptr;
        }

        if (surfaceBrush_)
        {
            DeleteObject(surfaceBrush_);
            surfaceBrush_ = nullptr;
        }

        if (previewBrush_)
        {
            DeleteObject(previewBrush_);
            previewBrush_ = nullptr;
        }

        if (selectedCellBrush_)
        {
            DeleteObject(selectedCellBrush_);
            selectedCellBrush_ = nullptr;
        }

        if (selectedPreviewBrush_)
        {
            DeleteObject(selectedPreviewBrush_);
            selectedPreviewBrush_ = nullptr;
        }

        if (placeholderBrush_)
        {
            DeleteObject(placeholderBrush_);
            placeholderBrush_ = nullptr;
        }

        if (borderPen_)
        {
            DeleteObject(borderPen_);
            borderPen_ = nullptr;
        }

        if (selectedBorderPen_)
        {
            DeleteObject(selectedBorderPen_);
            selectedBorderPen_ = nullptr;
        }

        if (rubberBandPen_)
        {
            DeleteObject(rubberBandPen_);
            rubberBandPen_ = nullptr;
        }
    }

    void BrowserPane::ReleaseThumbnailFonts()
    {
        if (thumbnailTitleFont_ && thumbnailTitleFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(thumbnailTitleFont_);
        }
        if (thumbnailMetaFont_ && thumbnailMetaFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(thumbnailMetaFont_);
        }
        if (thumbnailStatusFont_ && thumbnailStatusFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(thumbnailStatusFont_);
        }
        if (placeholderTitleFont_ && placeholderTitleFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(placeholderTitleFont_);
        }
        if (placeholderBodyFont_ && placeholderBodyFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(placeholderBodyFont_);
        }

        thumbnailTitleFont_ = nullptr;
        thumbnailMetaFont_ = nullptr;
        thumbnailStatusFont_ = nullptr;
        placeholderTitleFont_ = nullptr;
        placeholderBodyFont_ = nullptr;
    }

    void BrowserPane::NotifyStateChanged() const
    {
        if (parent_)
        {
            PostMessageW(parent_, kStateChangedMessage, reinterpret_cast<WPARAM>(hwnd_), 0);
        }
    }

    void BrowserPane::ApplyThemeToDetailsList() const
    {
        if (!detailsList_)
        {
            return;
        }

        ListView_SetBkColor(detailsList_, colors_.windowBackground);
        ListView_SetTextBkColor(detailsList_, colors_.windowBackground);
        ListView_SetTextColor(detailsList_, colors_.text);
        if (HWND header = ListView_GetHeader(detailsList_))
        {
            InvalidateRect(header, nullptr, TRUE);
        }
        InvalidateRect(detailsList_, nullptr, TRUE);
    }

    LRESULT BrowserPane::HandleDetailsListCustomDraw(LPARAM lParam) const
    {
        const auto* customDraw = reinterpret_cast<const NMLVCUSTOMDRAW*>(lParam);
        switch (customDraw->nmcd.dwDrawStage)
        {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
        {
            auto* mutableCustomDraw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            const int viewIndex = static_cast<int>(mutableCustomDraw->nmcd.dwItemSpec);
            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const bool selected = modelIndex >= 0 && selectedModelIndices_.contains(modelIndex);
            mutableCustomDraw->clrText = selected ? colors_.selectionText : colors_.text;
            mutableCustomDraw->clrTextBk = selected
                ? colors_.accentFill
                : ((viewIndex % 2) == 0 ? colors_.surfaceBackground : colors_.rowAlternateBackground);
            return CDRF_NEWFONT;
        }
        default:
            return CDRF_DODEFAULT;
        }
    }

    void BrowserPane::RebuildSelectionFromDetailsList()
    {
        if (!detailsList_ || syncingDetailsSelection_)
        {
            return;
        }

        std::unordered_set<int> newSelection;
        int itemIndex = -1;
        while ((itemIndex = ListView_GetNextItem(detailsList_, itemIndex, LVNI_SELECTED)) != -1)
        {
            const int modelIndex = ModelIndexFromViewIndex(itemIndex);
            if (modelIndex >= 0)
            {
                newSelection.insert(modelIndex);
            }
        }

        selectedModelIndices_ = std::move(newSelection);
        const int focusedItem = ListView_GetNextItem(detailsList_, -1, LVNI_FOCUSED);
        focusedModelIndex_ = ModelIndexFromViewIndex(focusedItem);
        if (focusedModelIndex_ >= 0)
        {
            anchorModelIndex_ = focusedModelIndex_;
        }

        UpdateSelectionBytes();
        NotifyStateChanged();
    }

    void BrowserPane::SyncDetailsListSelectionFromModel()
    {
        if (!detailsList_ || viewMode_ != BrowserViewMode::Details || orderedModelIndices_.empty())
        {
            return;
        }

        syncingDetailsSelection_ = true;
        ListView_SetItemState(detailsList_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);

        for (int viewIndex = 0; viewIndex < static_cast<int>(orderedModelIndices_.size()); ++viewIndex)
        {
            const int modelIndex = orderedModelIndices_[static_cast<std::size_t>(viewIndex)];
            if (selectedModelIndices_.contains(modelIndex))
            {
                ListView_SetItemState(detailsList_, viewIndex, LVIS_SELECTED, LVIS_SELECTED);
            }

            if (focusedModelIndex_ == modelIndex)
            {
                ListView_SetItemState(detailsList_, viewIndex, LVIS_FOCUSED, LVIS_FOCUSED);
            }
        }

        syncingDetailsSelection_ = false;
    }

    void BrowserPane::UpdateSelectionBytes()
    {
        selectedBytes_ = 0;
        if (!model_)
        {
            return;
        }

        const auto& items = model_->Items();
        for (int modelIndex : selectedModelIndices_)
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(items.size()))
            {
                selectedBytes_ += items[static_cast<std::size_t>(modelIndex)].fileSizeBytes;
            }
        }
    }

    void BrowserPane::SelectAll()
    {
        if (!model_ || orderedModelIndices_.empty())
        {
            return;
        }

        selectedModelIndices_.clear();
        for (const int modelIndex : orderedModelIndices_)
        {
            selectedModelIndices_.insert(modelIndex);
        }

        if (focusedModelIndex_ < 0 || !selectedModelIndices_.contains(focusedModelIndex_))
        {
            focusedModelIndex_ = orderedModelIndices_.front();
        }
        if (anchorModelIndex_ < 0 || !selectedModelIndices_.contains(anchorModelIndex_))
        {
            anchorModelIndex_ = focusedModelIndex_;
        }

        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::SelectSingleViewIndex(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        selectedModelIndices_.clear();
        selectedModelIndices_.insert(modelIndex);
        anchorModelIndex_ = modelIndex;
        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::ToggleViewIndexSelection(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        if (selectedModelIndices_.contains(modelIndex))
        {
            selectedModelIndices_.erase(modelIndex);
        }
        else
        {
            selectedModelIndices_.insert(modelIndex);
        }

        anchorModelIndex_ = modelIndex;
        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::ExtendSelectionToViewIndex(int viewIndex)
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        if (anchorModelIndex_ < 0)
        {
            SelectSingleViewIndex(viewIndex);
            return;
        }

        const int anchorViewIndex = ViewIndexFromModelIndex(anchorModelIndex_);
        if (anchorViewIndex < 0)
        {
            SelectSingleViewIndex(viewIndex);
            return;
        }

        selectedModelIndices_.clear();
        const int rangeStart = std::min(anchorViewIndex, viewIndex);
        const int rangeEnd = std::max(anchorViewIndex, viewIndex);
        for (int index = rangeStart; index <= rangeEnd; ++index)
        {
            const int rangeModelIndex = ModelIndexFromViewIndex(index);
            if (rangeModelIndex >= 0)
            {
                selectedModelIndices_.insert(rangeModelIndex);
            }
        }

        focusedModelIndex_ = modelIndex;
        UpdateSelectionBytes();
        SyncDetailsListSelectionFromModel();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::BeginRubberBandSelection(POINT point, bool additive)
    {
        rubberBandActive_ = true;
        rubberBandStart_ = point;
        rubberBandRect_ = {point.x, point.y, point.x, point.y};
        rubberBandSeedSelection_ = additive ? selectedModelIndices_ : std::unordered_set<int>{};
        SetCapture(hwnd_);
        SetFocus(hwnd_);
        UpdateRubberBandSelection(point);
    }

    void BrowserPane::UpdateRubberBandSelection(POINT point)
    {
        if (!rubberBandActive_)
        {
            return;
        }

        rubberBandRect_ = NormalizeRect(rubberBandStart_, point);
        std::unordered_set<int> newSelection = rubberBandSeedSelection_;
        for (int viewIndex = 0; viewIndex < static_cast<int>(orderedModelIndices_.size()); ++viewIndex)
        {
            RECT cellRect = GetThumbnailCellRect(viewIndex);
            RECT intersection{};
            if (IntersectRect(&intersection, &cellRect, &rubberBandRect_))
            {
                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex >= 0)
                {
                    newSelection.insert(modelIndex);
                }
            }
        }

        selectedModelIndices_ = std::move(newSelection);
        UpdateSelectionBytes();
        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::EndRubberBandSelection()
    {
        if (!rubberBandActive_)
        {
            return;
        }

        rubberBandActive_ = false;
        rubberBandSeedSelection_.clear();
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::RequestOpenPrimarySelection() const
    {
        const int primaryModelIndex = PrimarySelectedModelIndex();
        if (primaryModelIndex < 0)
        {
            return;
        }

        const int viewIndex = ViewIndexFromModelIndex(primaryModelIndex);
        if (viewIndex >= 0)
        {
            RequestOpenItemForViewIndex(viewIndex);
        }
    }

    void BrowserPane::RequestOpenItemForViewIndex(int viewIndex) const
    {
        if (!parent_)
        {
            return;
        }

        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return;
        }

        PostMessageW(parent_, kOpenItemMessage, reinterpret_cast<WPARAM>(hwnd_), static_cast<LPARAM>(modelIndex));
    }

    POINT BrowserPane::ContextMenuAnchorScreenPoint() const
    {
        POINT screenPoint{};
        const int primaryModelIndex = PrimarySelectedModelIndex();
        const int viewIndex = ViewIndexFromModelIndex(primaryModelIndex);

        if (viewMode_ == BrowserViewMode::Details && detailsList_ && IsWindowVisible(detailsList_))
        {
            RECT itemRect{};
            if (viewIndex >= 0 && ListView_GetItemRect(detailsList_, viewIndex, &itemRect, LVIR_BOUNDS) != FALSE)
            {
                screenPoint.x = itemRect.left + ((itemRect.right - itemRect.left) / 2);
                screenPoint.y = itemRect.top + ((itemRect.bottom - itemRect.top) / 2);
                ClientToScreen(detailsList_, &screenPoint);
                return screenPoint;
            }
        }

        RECT anchorRect{};
        if (viewMode_ == BrowserViewMode::Thumbnails && viewIndex >= 0)
        {
            anchorRect = GetThumbnailCellRect(viewIndex);
        }
        else if (hwnd_)
        {
            GetClientRect(hwnd_, &anchorRect);
        }

        screenPoint.x = anchorRect.left + ((anchorRect.right - anchorRect.left) / 2);
        screenPoint.y = anchorRect.top + ((anchorRect.bottom - anchorRect.top) / 2);
        if (hwnd_)
        {
            ClientToScreen(hwnd_, &screenPoint);
        }
        return screenPoint;
    }

    void BrowserPane::ShowContextMenu(POINT screenPoint) const
    {
        if (!parent_)
        {
            return;
        }

        SendMessageW(parent_, kContextMenuMessage, reinterpret_cast<WPARAM>(hwnd_), MAKELPARAM(screenPoint.x, screenPoint.y));
    }

    void BrowserPane::ScheduleMetadataForItem(int modelIndex, const BrowserItem& item) const
    {
        if (!metadataService_ || modelIndex < 0)
        {
            return;
        }

        metadataService_->Schedule(metadataSessionId_, services::MetadataWorkItem{modelIndex, item, 0});
    }

    void BrowserPane::ScheduleVisibleMetadataWork() const
    {
        if (!metadataService_ || !model_ || orderedModelIndices_.empty())
        {
            return;
        }

        std::vector<services::MetadataWorkItem> workItems;
        workItems.reserve(128);

        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            if (!hwnd_)
            {
                return;
            }

            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
            const int clientHeight = clientRect.bottom - clientRect.top;
            const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
            const int verticalStride = layout.itemHeight + layout.cellPadding;
            const int firstVisibleRow = std::max(0, scrollOffsetY_ / verticalStride);
            const int lastVisibleRow = std::max(firstVisibleRow, (scrollOffsetY_ + clientHeight) / verticalStride);
            const int visibleRowCount = std::max(1, (lastVisibleRow - firstVisibleRow) + 1);
            const int proactivePrefetchRows = scrollOffsetY_ == 0
                ? std::clamp(visibleRowCount * 6,
                             kMetadataMinimumTopOfFolderPrefetchRows,
                             kMetadataMaximumTopOfFolderPrefetchRows)
                : std::clamp(visibleRowCount * 3,
                             kMetadataMinimumLookAheadPrefetchRows,
                             kMetadataMaximumLookAheadPrefetchRows);
            const int requestStartRow = std::max(0, firstVisibleRow - kMetadataNearVisiblePrefetchRows);
            const int requestEndRow = lastVisibleRow + kMetadataNearVisiblePrefetchRows + proactivePrefetchRows;
            const int firstIndex = requestStartRow * columns;
            const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (requestEndRow + 1) * columns);

            workItems.reserve(static_cast<std::size_t>(std::max(0, lastIndex - firstIndex)));
            for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
            {
                const BrowserItem* item = ItemFromViewIndex(viewIndex);
                if (!item)
                {
                    continue;
                }

                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex < 0)
                {
                    continue;
                }

                const int row = viewIndex / columns;
                int priority = kProactivePrefetchPriority;
                if (row >= firstVisibleRow && row <= lastVisibleRow)
                {
                    priority = kVisiblePriority;
                }
                else if (row >= (firstVisibleRow - kMetadataNearVisiblePrefetchRows)
                    && row <= (lastVisibleRow + kMetadataNearVisiblePrefetchRows))
                {
                    priority = kNearVisiblePriority;
                }

                workItems.push_back(services::MetadataWorkItem{modelIndex, *item, priority});
            }
        }
        else if (viewMode_ == BrowserViewMode::Details && detailsList_)
        {
            const int topIndex = std::max(0, ListView_GetTopIndex(detailsList_));
            const int visibleCount = std::max(1, ListView_GetCountPerPage(detailsList_));
            const int nearVisibleCount = std::max(visibleCount, kMinimumDetailsMetadataNearVisibleItems);
            const int proactivePrefetchCount = topIndex == 0
                ? std::clamp(visibleCount * 6,
                             kMinimumDetailsMetadataTopOfFolderItems,
                             kMaximumDetailsMetadataTopOfFolderItems)
                : std::clamp(visibleCount * 3,
                             kMinimumDetailsMetadataLookAheadItems,
                             kMaximumDetailsMetadataLookAheadItems);
            const int firstIndex = std::max(0, topIndex - nearVisibleCount);
            const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()),
                                           topIndex + visibleCount + nearVisibleCount + proactivePrefetchCount);
            const int visibleEndIndex = std::min(lastIndex - 1, topIndex + visibleCount - 1);
            const int nearEndIndex = std::min(lastIndex - 1, topIndex + visibleCount + nearVisibleCount - 1);

            workItems.reserve(static_cast<std::size_t>(std::max(0, lastIndex - firstIndex)));
            for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
            {
                const BrowserItem* item = ItemFromViewIndex(viewIndex);
                if (!item)
                {
                    continue;
                }

                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex < 0)
                {
                    continue;
                }

                int priority = kProactivePrefetchPriority;
                if (viewIndex >= topIndex && viewIndex <= visibleEndIndex)
                {
                    priority = kVisiblePriority;
                }
                else if (viewIndex >= firstIndex && viewIndex <= nearEndIndex)
                {
                    priority = kNearVisiblePriority;
                }

                workItems.push_back(services::MetadataWorkItem{modelIndex, *item, priority});
            }
        }

        if (!workItems.empty())
        {
            metadataService_->Schedule(metadataSessionId_, std::move(workItems));
        }
    }

    void BrowserPane::ScheduleVisibleThumbnailWork()
    {
        ScheduleVisibleMetadataWork();

        if (!thumbnailScheduler_)
        {
            return;
        }

        if (!hwnd_ || viewMode_ != BrowserViewMode::Thumbnails || !model_ || orderedModelIndices_.empty())
        {
            CancelThumbnailWork();
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstVisibleRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastVisibleRow = std::max(firstVisibleRow, (scrollOffsetY_ + clientHeight) / verticalStride);
        const int visibleRowCount = std::max(1, (lastVisibleRow - firstVisibleRow) + 1);
        const int proactivePrefetchRows = scrollOffsetY_ == 0
            ? std::clamp(visibleRowCount * 5,
                         kThumbnailMinimumTopOfFolderPrefetchRows,
                         kThumbnailMaximumTopOfFolderPrefetchRows)
            : std::clamp(visibleRowCount * 2,
                         kThumbnailMinimumLookAheadPrefetchRows,
                         kThumbnailMaximumLookAheadPrefetchRows);
        const int requestStartRow = std::max(0, firstVisibleRow - kThumbnailNearVisiblePrefetchRows);
        const int requestEndRow = lastVisibleRow + kThumbnailNearVisiblePrefetchRows + proactivePrefetchRows;
        const int firstIndex = requestStartRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (requestEndRow + 1) * columns);
        const int targetWidth = layout.previewWidth;
        const int targetHeight = layout.previewHeight;

        std::vector<services::ThumbnailWorkItem> workItems;
        workItems.reserve(static_cast<std::size_t>(std::max(0, lastIndex - firstIndex)));
        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item || !decode::CanDecodeThumbnail(*item))
            {
                continue;
            }

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            if (modelIndex < 0)
            {
                continue;
            }

            const int row = viewIndex / columns;
            int priority = kProactivePrefetchPriority;
            if (row >= firstVisibleRow && row <= lastVisibleRow)
            {
                priority = kVisiblePriority;
            }
            else if (row >= (firstVisibleRow - kThumbnailNearVisiblePrefetchRows)
                && row <= (lastVisibleRow + kThumbnailNearVisiblePrefetchRows))
            {
                priority = kNearVisiblePriority;
            }

            const bool preferCpu = priority <= kNearVisiblePriority;

            workItems.push_back(services::ThumbnailWorkItem{
                modelIndex,
                MakeThumbnailCacheKey(*item, targetWidth, targetHeight),
                priority,
                preferCpu,
            });
        }

        ++thumbnailRequestEpoch_;
        thumbnailScheduler_->Schedule(thumbnailSessionId_, thumbnailRequestEpoch_, std::move(workItems));
    }

    void BrowserPane::CancelThumbnailWork()
    {
        if (thumbnailScheduler_)
        {
            thumbnailScheduler_->CancelOutstanding();
        }
    }

    void BrowserPane::InvalidateThumbnailCellForModelIndex(int modelIndex) const
    {
        if (viewMode_ != BrowserViewMode::Thumbnails || !hwnd_)
        {
            return;
        }

        const int viewIndex = ViewIndexFromModelIndex(modelIndex);
        if (viewIndex < 0)
        {
            return;
        }

        RECT invalidRect = GetThumbnailCellRect(viewIndex);
        InflateRect(&invalidRect, 1, 1);
        InvalidateRect(hwnd_, &invalidRect, FALSE);
    }

    void BrowserPane::EnsureD2DRenderTarget()
    {
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
            RebuildD2DBrushes();
            RebuildD2DTextFormats();

            if (placeholderArt_)
            {
                d2dPlaceholderArtBitmap_ = renderer.CreateBitmapFromCachedThumbnail(
                    d2dRenderTarget_.Get(), *placeholderArt_);
            }
        }
    }

    void BrowserPane::ReleaseD2DResources()
    {
        d2dBitmapCache_.clear();
        d2dPlaceholderArtBitmap_.Reset();
        d2dTitleFormat_.Reset();
        d2dMetaFormat_.Reset();
        d2dStatusFormat_.Reset();
        d2dPlaceholderTitleFormat_.Reset();
        d2dPlaceholderBodyFormat_.Reset();
        d2dBackgroundBrush_.Reset();
        d2dSurfaceBrush_.Reset();
        d2dPreviewBrush_.Reset();
        d2dSelectedCellBrush_.Reset();
        d2dSelectedPreviewBrush_.Reset();
        d2dPlaceholderBrush_.Reset();
        d2dBorderBrush_.Reset();
        d2dSelectedBorderBrush_.Reset();
        d2dRubberBandBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dSelectionTextBrush_.Reset();
        d2dRenderTarget_.Reset();
    }

    void BrowserPane::RebuildD2DBrushes()
    {
        if (!d2dRenderTarget_)
        {
            return;
        }

        d2dBackgroundBrush_.Reset();
        d2dSurfaceBrush_.Reset();
        d2dPreviewBrush_.Reset();
        d2dSelectedCellBrush_.Reset();
        d2dSelectedPreviewBrush_.Reset();
        d2dPlaceholderBrush_.Reset();
        d2dBorderBrush_.Reset();
        d2dSelectedBorderBrush_.Reset();
        d2dRubberBandBrush_.Reset();
        d2dTextBrush_.Reset();
        d2dMutedTextBrush_.Reset();
        d2dSelectionTextBrush_.Reset();

        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.windowBackground), d2dBackgroundBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.surfaceBackground), d2dSurfaceBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.previewBackground), d2dPreviewBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.accentFill), d2dSelectedCellBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.selectedPreviewBackground), d2dSelectedPreviewBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.placeholderBackground), d2dPlaceholderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.border), d2dBorderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.accent), d2dSelectedBorderBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.rubberBand), d2dRubberBandBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.text), d2dTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.mutedText), d2dMutedTextBrush_.GetAddressOf());
        d2dRenderTarget_->CreateSolidColorBrush(render::ToD2DColor(colors_.selectionText), d2dSelectionTextBrush_.GetAddressOf());
    }

    void BrowserPane::RebuildD2DTextFormats()
    {
        auto& renderer = render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        d2dTitleFormat_ = renderer.CreateTextFormat(L"Segoe UI", static_cast<float>(layout.titlePointSize), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        d2dMetaFormat_ = renderer.CreateTextFormat(L"Segoe UI", static_cast<float>(layout.metaPointSize), DWRITE_FONT_WEIGHT_NORMAL);
        d2dStatusFormat_ = renderer.CreateTextFormat(L"Segoe UI", static_cast<float>(layout.statusPointSize), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        d2dPlaceholderTitleFormat_ = renderer.CreateTextFormat(L"Segoe UI", static_cast<float>(kPlaceholderTitlePointSize), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        d2dPlaceholderBodyFormat_ = renderer.CreateTextFormat(L"Segoe UI", static_cast<float>(kPlaceholderBodyPointSize), DWRITE_FONT_WEIGHT_NORMAL);

        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};

        if (d2dTitleFormat_)
        {
            d2dTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dTitleFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dMetaFormat_)
        {
            d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            d2dMetaFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dMetaFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dStatusFormat_)
        {
            d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dStatusFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dStatusFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dPlaceholderTitleFormat_)
        {
            d2dPlaceholderTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dPlaceholderTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            d2dPlaceholderTitleFormat_->SetTrimming(&trimming, nullptr);
        }

        if (d2dPlaceholderBodyFormat_)
        {
            d2dPlaceholderBodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dPlaceholderBodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dPlaceholderBodyFormat_->SetTrimming(&trimming, nullptr);
        }
    }

    ID2D1Bitmap* BrowserPane::GetOrCreateD2DBitmap(ID2D1RenderTarget* rt, const cache::CachedThumbnail& thumbnail) const
    {
        const HBITMAP key = thumbnail.Bitmap();
        auto it = d2dBitmapCache_.find(key);
        if (it != d2dBitmapCache_.end())
        {
            return it->second.Get();
        }

        auto bitmap = render::D2DRenderer::Instance().CreateBitmapFromCachedThumbnail(rt, thumbnail);
        if (!bitmap)
        {
            return nullptr;
        }

        auto [insertIt, _] = d2dBitmapCache_.emplace(key, std::move(bitmap));
        return insertIt->second.Get();
    }

    void BrowserPane::D2DDrawPlaceholderState(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const
    {
        const bool hasFolder = model_ && !model_->FolderPath().empty();
        const bool hasError = hasFolder && model_->HasError();
        const bool loadingFolder = hasFolder && model_->IsEnumerating() && orderedModelIndices_.empty();
        const bool noMatches = hasFolder && !filterQuery_.empty() && orderedModelIndices_.empty();
        const bool emptyFolder = hasFolder && !hasError && !loadingFolder && !noMatches && orderedModelIndices_.empty();
        const bool showIcon = !hasError && (!hasFolder || loadingFolder || emptyFolder);

        std::wstring title = L"HyperBrowse";
        if (hasFolder)
        {
            if (hasError) title = L"Folder Load Failed";
            else if (loadingFolder) title = L"Loading Images";
            else if (noMatches) title = L"No Matches";
            else if (emptyFolder) title = L"Empty Folder";
        }

        const float clientWidth = size.width;
        const float clientHeight = size.height;
        constexpr float kPanelPaddingLeft = 24.0f;
        constexpr float kPanelPaddingRight = 28.0f;
        constexpr float kPanelPaddingVertical = 16.0f;
        constexpr float kIconTextGap = 28.0f;
        constexpr float kDesiredTextBlockWidth = 300.0f;
        constexpr float kMinimumTextBlockWidth = 220.0f;
        constexpr float kTitleHeight = 42.0f;
        constexpr float kBodyHeight = 52.0f;
        constexpr float kTitleBodyGap = 10.0f;

        const float maxPanelWidth = std::max(280.0f, clientWidth - 40.0f);
        const float maxPanelHeight = std::max(120.0f, clientHeight - 32.0f);

        float renderedIconSize = 0.0f;
        float panelWidth = std::max(280.0f, std::min(520.0f, clientWidth - 56.0f));
        float panelHeight = showIcon
            ? std::min(196.0f, std::max(152.0f, clientHeight - 56.0f))
            : std::min(160.0f, std::max(120.0f, clientHeight - 56.0f));

        if (showIcon && d2dPlaceholderArtBitmap_)
        {
            const float artWidth = static_cast<float>(d2dPlaceholderArtBitmap_->GetSize().width);
            const float maxIconWidth = std::max(96.0f, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - kMinimumTextBlockWidth);
            const float maxIconHeight = std::max(96.0f, maxPanelHeight - (kPanelPaddingVertical * 2.0f));
            renderedIconSize = std::min({static_cast<float>(kPlaceholderIconDisplaySize), artWidth, maxIconWidth, maxIconHeight});

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

        if (d2dPlaceholderBrush_) rt->FillRoundedRectangle(roundedPanel, d2dPlaceholderBrush_.Get());
        if (d2dBorderBrush_) rt->DrawRoundedRectangle(roundedPanel, d2dBorderBrush_.Get(), 1.0f);

        D2D1_RECT_F titleRect{};
        D2D1_RECT_F bodyRect{};
        if (showIcon && d2dPlaceholderArtBitmap_)
        {
            const float iconX = panelLeft + kPanelPaddingLeft;
            const float iconY = panelTop + (panelHeight - renderedIconSize) / 2.0f;
            rt->DrawBitmap(d2dPlaceholderArtBitmap_.Get(),
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

        auto* titleFormat = d2dPlaceholderTitleFormat_.Get();
        if (titleFormat && d2dTextBrush_)
        {
            titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawText(title.c_str(), static_cast<UINT32>(title.size()), titleFormat, titleRect, d2dTextBrush_.Get());
        }

        const std::wstring text = BuildPlaceholderText();
        if (!text.empty() && d2dPlaceholderBodyFormat_ && d2dMutedTextBrush_)
        {
            auto* fmt = d2dPlaceholderBodyFormat_.Get();
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt, bodyRect, d2dMutedTextBrush_.Get());
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    void BrowserPane::D2DDrawThumbnailCells(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const
    {
        if (d2dBackgroundBrush_)
        {
            rt->Clear(render::ToD2DColor(colors_.windowBackground));
        }

        if (orderedModelIndices_.empty())
        {
            D2DDrawPlaceholderState(rt, size);
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientWidth = static_cast<int>(size.width);
        const int clientHeight = static_cast<int>(size.height);
        const int columns = ColumnsForClientWidth(clientWidth);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastRow = std::max(firstRow, (scrollOffsetY_ + clientHeight) / verticalStride + 1);
        const int firstIndex = firstRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (lastRow + 1) * columns);

        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item) continue;

            const RECT cellRectGdi = GetThumbnailCellRect(viewIndex);
            if (cellRectGdi.bottom < 0 || cellRectGdi.top > clientHeight) continue;

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const bool selected = selectedModelIndices_.contains(modelIndex);

            const D2D1_RECT_F cellRect = render::ToD2DRect(cellRectGdi);
            const D2D1_RECT_F cellStrokeRect = InsetD2DRect(cellRect, 0.5f, 0.5f);
            const float cornerRadius = static_cast<float>(layout.cellCornerRadius);
            const D2D1_ROUNDED_RECT roundedCell = D2D1::RoundedRect(cellRect, cornerRadius, cornerRadius);
            const D2D1_ROUNDED_RECT roundedCellStroke = D2D1::RoundedRect(
                cellStrokeRect,
                std::max(0.0f, cornerRadius - 0.5f),
                std::max(0.0f, cornerRadius - 0.5f));

            ID2D1SolidColorBrush* cellBrush = selected
                ? (d2dSelectedCellBrush_ ? d2dSelectedCellBrush_.Get() : d2dSurfaceBrush_.Get())
                : d2dSurfaceBrush_.Get();
            ID2D1SolidColorBrush* borderBrush = selected
                ? d2dSelectedBorderBrush_.Get()
                : d2dBorderBrush_.Get();

            if (cellBrush) rt->FillRoundedRectangle(roundedCell, cellBrush);
            if (borderBrush) rt->DrawRoundedRectangle(roundedCellStroke, borderBrush, 1.0f);

            const RECT previewRectGdi = GetThumbnailPreviewRect(cellRectGdi);
            const D2D1_RECT_F previewRect = render::ToD2DRect(previewRectGdi);
            const float previewCorner = static_cast<float>(layout.previewCornerRadius);
            const D2D1_ROUNDED_RECT roundedPreview = D2D1::RoundedRect(previewRect, previewCorner, previewCorner);

            ID2D1SolidColorBrush* previewBrush = selected
                ? (d2dSelectedPreviewBrush_ ? d2dSelectedPreviewBrush_.Get() : d2dPreviewBrush_.Get())
                : (d2dPreviewBrush_ ? d2dPreviewBrush_.Get() : d2dBackgroundBrush_.Get());
            if (previewBrush) rt->FillRoundedRectangle(roundedPreview, previewBrush);

            D2DDrawPreviewThumbnail(rt, previewRect, *item, selected);

            if (thumbnailDetailsVisible_)
            {
                ID2D1SolidColorBrush* textBrush = selected ? d2dSelectionTextBrush_.Get() : d2dTextBrush_.Get();
                ID2D1SolidColorBrush* mutedBrush = selected ? d2dSelectionTextBrush_.Get() : d2dMutedTextBrush_.Get();

                const float titleTop = previewRect.bottom + static_cast<float>(layout.titleTopGap);
                D2D1_RECT_F nameRect = D2D1::RectF(
                    cellRect.left + layout.textInset, titleTop,
                    cellRect.right - layout.textInset, titleTop + layout.titleHeight);

                if (d2dTitleFormat_ && textBrush)
                {
                    d2dTitleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    d2dTitleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    const std::wstring displayTitle = BuildThumbnailDisplayTitle(*item);
                    rt->DrawText(displayTitle.c_str(), static_cast<UINT32>(displayTitle.size()),
                                 d2dTitleFormat_.Get(), nameRect, textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }

                const float infoTop = cellRect.bottom - layout.infoBottomInset - layout.infoHeight;
                D2D1_RECT_F infoRect = D2D1::RectF(
                    cellRect.left + layout.textInset, infoTop,
                    cellRect.right - layout.textInset, cellRect.bottom - layout.infoBottomInset);

                const std::wstring typeLabel = ToUppercase(item->fileType);
                const std::wstring dimensionLabel = FormatDimensionsForItem(*item);

                if (d2dMetaFormat_)
                {
                    d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    if (mutedBrush && !dimensionLabel.empty())
                    {
                        rt->DrawText(dimensionLabel.c_str(), static_cast<UINT32>(dimensionLabel.size()),
                                     d2dMetaFormat_.Get(), infoRect, mutedBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }

                    if (textBrush && !typeLabel.empty())
                    {
                        d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                        rt->DrawText(typeLabel.c_str(), static_cast<UINT32>(typeLabel.size()),
                                     d2dMetaFormat_.Get(), infoRect, textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        d2dMetaFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    }
                }
            }
        }

        if (rubberBandActive_ && d2dRubberBandBrush_)
        {
            D2D1_RECT_F bandRect = render::ToD2DRect(rubberBandRect_);
            rt->DrawRectangle(bandRect, d2dRubberBandBrush_.Get(), 1.0f);
        }
    }

    void BrowserPane::D2DDrawPreviewThumbnail(ID2D1RenderTarget* rt, const D2D1_RECT_F& previewRect, const BrowserItem& item, bool selected) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int targetWidth = layout.previewWidth;
        const int targetHeight = layout.previewHeight;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);
        const auto thumbnail = thumbnailScheduler_ ? thumbnailScheduler_->FindCachedThumbnail(cacheKey) : nullptr;

        ID2D1SolidColorBrush* statusBrush = selected ? d2dSelectionTextBrush_.Get() : d2dMutedTextBrush_.Get();

        if (!thumbnailScheduler_ || !decode::CanDecodeThumbnail(item))
        {
            D2D1_RECT_F iconTextRect = D2D1::RectF(
                previewRect.left + layout.textInset,
                previewRect.top + std::max(10.0f, static_cast<float>(layout.previewInset) - 2),
                previewRect.right - layout.textInset,
                previewRect.top + std::max(34.0f, static_cast<float>(layout.previewInset + layout.metaHeight)));
            const std::wstring placeholder = decode::IsRawFileType(item.fileType) ? item.fileType : std::wstring(L"IMAGE");
            if (d2dStatusFormat_ && statusBrush)
            {
                d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                rt->DrawText(placeholder.c_str(), static_cast<UINT32>(placeholder.size()),
                             d2dStatusFormat_.Get(), iconTextRect, statusBrush);
            }
            return;
        }

        if (!thumbnail)
        {
            const decode::ThumbnailDecodeFailureKind knownFailureKind = thumbnailScheduler_->KnownFailureKind(cacheKey);
            if (knownFailureKind != decode::ThumbnailDecodeFailureKind::None)
            {
                D2DDrawUnavailableThumbnailState(rt, previewRect, knownFailureKind, selected);
                return;
            }

            const float previewHeight = previewRect.bottom - previewRect.top;
            const float iconSize = static_cast<float>(layout.loadingIconSize);
            const float iconY = previewRect.top + std::max(static_cast<float>(layout.previewInset),
                ((previewHeight - iconSize) / 2.0f) - (static_cast<float>(layout.metaHeight) / 2.0f));

            D2D1_RECT_F statusRect = D2D1::RectF(
                previewRect.left + layout.textInset,
                iconY + iconSize + std::max(6.0f, static_cast<float>(layout.metaTopGap)),
                previewRect.right - layout.textInset,
                previewRect.bottom - layout.previewInset);
            if (d2dStatusFormat_ && statusBrush)
            {
                d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawText(L"Loading thumbnail", 17, d2dStatusFormat_.Get(), statusRect, statusBrush);
            }
            return;
        }

        ID2D1Bitmap* d2dBitmap = GetOrCreateD2DBitmap(rt, *thumbnail);
        if (!d2dBitmap)
        {
            return;
        }

        const D2D1_RECT_F imageBounds = InsetD2DRect(previewRect, 1.0f, 1.0f);
        const float thumbWidth = static_cast<float>(thumbnail->Width());
        const float thumbHeight = static_cast<float>(thumbnail->Height());
        const float drawX = imageBounds.left + ((imageBounds.right - imageBounds.left - thumbWidth) / 2.0f);
        const float drawY = imageBounds.top + ((imageBounds.bottom - imageBounds.top - thumbHeight) / 2.0f);

        rt->PushAxisAlignedClip(imageBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        hyperbrowse::render::DrawBitmapHighQuality(rt,
                   d2dBitmap,
                   D2D1::RectF(drawX, drawY, drawX + thumbWidth, drawY + thumbHeight),
                   1.0f);
        rt->PopAxisAlignedClip();
    }

    void BrowserPane::D2DDrawUnavailableThumbnailState(ID2D1RenderTarget* rt,
                                                       const D2D1_RECT_F& previewRect,
                                                       decode::ThumbnailDecodeFailureKind failureKind,
                                                       bool selected) const
    {
        if (!rt)
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        ID2D1SolidColorBrush* statusBrush = selected ? d2dSelectionTextBrush_.Get() : d2dMutedTextBrush_.Get();
        const float previewWidth = previewRect.right - previewRect.left;
        const float previewHeight = previewRect.bottom - previewRect.top;

        float iconSize = 0.0f;
        if (unavailableThumbnailArt_)
        {
            iconSize = std::min({
                static_cast<float>(kUnavailableThumbnailIconSize),
                std::max(0.0f, previewWidth - (layout.textInset * 2.0f)),
                previewHeight * 0.34f,
            });
        }

        float textTop = previewRect.top + (previewHeight * 0.34f);
        if (iconSize > 0.0f && unavailableThumbnailArt_)
        {
            if (ID2D1Bitmap* iconBitmap = GetOrCreateD2DBitmap(rt, *unavailableThumbnailArt_))
            {
                const float iconX = previewRect.left + ((previewWidth - iconSize) / 2.0f);
                const float iconY = previewRect.top + std::max(10.0f, previewHeight * 0.14f);
                rt->DrawBitmap(iconBitmap,
                               D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize),
                               1.0f,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                textTop = iconY + iconSize + std::max(6.0f, static_cast<float>(layout.metaTopGap));
            }
        }

        const wchar_t* message = UnavailableThumbnailMessage(failureKind);
        if (d2dStatusFormat_ && statusBrush)
        {
            D2D1_RECT_F statusRect = D2D1::RectF(
                previewRect.left + layout.textInset,
                textTop,
                previewRect.right - layout.textInset,
                previewRect.bottom - layout.previewInset);
            d2dStatusFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            d2dStatusFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            d2dStatusFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            rt->DrawText(message,
                         static_cast<UINT32>(wcslen(message)),
                         d2dStatusFormat_.Get(),
                         statusRect,
                         statusBrush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
            d2dStatusFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    void BrowserPane::DrawPlaceholderState(HDC hdc, const RECT& clientRect) const
    {
        FillRect(hdc, &clientRect, backgroundBrush_ ? backgroundBrush_ : surfaceBrush_);

        const bool hasFolder = model_ && !model_->FolderPath().empty();
        const bool hasError = hasFolder && model_->HasError();
        const bool loadingFolder = hasFolder && model_->IsEnumerating() && orderedModelIndices_.empty();
        const bool noMatches = hasFolder && !filterQuery_.empty() && orderedModelIndices_.empty();
        const bool emptyFolder = hasFolder && !hasError && !loadingFolder && !noMatches && orderedModelIndices_.empty();
        const bool showIcon = !hasError && (!hasFolder || loadingFolder || emptyFolder);

        std::wstring title = L"HyperBrowse";
        if (hasFolder)
        {
            if (hasError)
            {
                title = L"Folder Load Failed";
            }
            else if (loadingFolder)
            {
                title = L"Loading Images";
            }
            else if (noMatches)
            {
                title = L"No Matches";
            }
            else if (emptyFolder)
            {
                title = L"Empty Folder";
            }
        }

        RECT panelRect = clientRect;
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        constexpr int kPanelMarginX = 20;
        constexpr int kPanelMarginY = 16;
        constexpr int kPanelPaddingLeft = 24;
        constexpr int kPanelPaddingRight = 28;
        constexpr int kPanelPaddingVertical = 16;
        constexpr int kIconTextGap = 28;
        constexpr int kDesiredTextBlockWidth = 300;
        constexpr int kMinimumTextBlockWidth = 220;
        constexpr int kTitleHeight = 42;
        constexpr int kBodyHeight = 52;
        constexpr int kTitleBodyGap = 10;

        const int maxPanelWidth = std::max(280, clientWidth - (kPanelMarginX * 2));
        const int maxPanelHeight = std::max(120, clientHeight - (kPanelMarginY * 2));

        int renderedIconSize = 0;
        int panelWidth = std::max(280, std::min(520, clientWidth - 56));
        int panelHeight = showIcon
            ? std::min(196, std::max(152, clientHeight - 56))
            : std::min(160, std::max(120, clientHeight - 56));
        if (showIcon && placeholderArt_)
        {
            const int maxIconWidth = std::max(96, maxPanelWidth - kPanelPaddingLeft - kPanelPaddingRight - kIconTextGap - kMinimumTextBlockWidth);
            const int maxIconHeight = std::max(96, maxPanelHeight - (kPanelPaddingVertical * 2));
            renderedIconSize = std::min({kPlaceholderIconDisplaySize, placeholderArt_->Width(), maxIconWidth, maxIconHeight});

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

        HGDIOBJ oldBrush = SelectObject(hdc, placeholderBrush_ ? placeholderBrush_ : surfaceBrush_);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen_ ? borderPen_ : GetStockObject(BLACK_PEN));
        RoundRect(hdc, panelRect.left, panelRect.top, panelRect.right, panelRect.bottom, 22, 22);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);

        const std::wstring text = BuildPlaceholderText();
        RECT titleRect{};
        RECT bodyRect{};
        if (showIcon && placeholderArt_)
        {
            const int iconX = panelRect.left + kPanelPaddingLeft;
            const int iconY = panelRect.top + ((panelHeight - renderedIconSize) / 2);
            util::DrawBitmapWithAlpha(hdc, *placeholderArt_, iconX, iconY, renderedIconSize, renderedIconSize);

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
            if (text.empty())
            {
                titleRect.top = panelRect.top + ((panelRect.bottom - panelRect.top - 34) / 2);
                titleRect.bottom = titleRect.top + 34;
            }
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors_.text);
        HGDIOBJ oldTitleFont = placeholderTitleFont_
            ? SelectObject(hdc, placeholderTitleFont_)
            : static_cast<HGDIOBJ>(nullptr);
        DrawTextW(hdc, title.c_str(), -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (oldTitleFont)
        {
            SelectObject(hdc, oldTitleFont);
        }

        if (!text.empty())
        {
            SetTextColor(hdc, colors_.mutedText);
            HGDIOBJ oldBodyFont = placeholderBodyFont_
                ? SelectObject(hdc, placeholderBodyFont_)
                : static_cast<HGDIOBJ>(nullptr);
            DrawTextW(hdc, text.c_str(), -1, &bodyRect, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            if (oldBodyFont)
            {
                SelectObject(hdc, oldBodyFont);
            }
        }
    }

    void BrowserPane::DrawThumbnailCells(HDC hdc, const RECT& clientRect) const
    {
        FillRect(hdc, &clientRect, backgroundBrush_ ? backgroundBrush_ : surfaceBrush_);
        if (orderedModelIndices_.empty())
        {
            DrawPlaceholderState(hdc, clientRect);
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int firstRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastRow = std::max(firstRow, (scrollOffsetY_ + static_cast<int>(clientRect.bottom - clientRect.top)) / verticalStride + 1);
        const int firstIndex = firstRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (lastRow + 1) * columns);

        SetBkMode(hdc, TRANSPARENT);
        for (int viewIndex = firstIndex; viewIndex < lastIndex; ++viewIndex)
        {
            const BrowserItem* item = ItemFromViewIndex(viewIndex);
            if (!item)
            {
                continue;
            }

            const RECT cellRect = GetThumbnailCellRect(viewIndex);
            if (cellRect.bottom < clientRect.top || cellRect.top > clientRect.bottom)
            {
                continue;
            }

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const bool selected = selectedModelIndices_.contains(modelIndex);

            const HBRUSH cellBrush = selected
                ? (selectedCellBrush_ ? selectedCellBrush_ : surfaceBrush_)
                : surfaceBrush_;
            HGDIOBJ pen = selected
                ? static_cast<HGDIOBJ>(selectedBorderPen_ ? selectedBorderPen_ : GetStockObject(BLACK_PEN))
                : static_cast<HGDIOBJ>(borderPen_ ? borderPen_ : GetStockObject(BLACK_PEN));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, cellBrush);
            RoundRect(hdc,
                      cellRect.left,
                      cellRect.top,
                      cellRect.right,
                      cellRect.bottom,
                      layout.cellCornerRadius,
                      layout.cellCornerRadius);

            RECT previewRect = GetThumbnailPreviewRect(cellRect);
            const HBRUSH previewBrush = selected
                ? (selectedPreviewBrush_ ? selectedPreviewBrush_ : previewBrush_)
                : (previewBrush_ ? previewBrush_ : backgroundBrush_);
            SelectObject(hdc, previewBrush);
            RoundRect(hdc,
                      previewRect.left,
                      previewRect.top,
                      previewRect.right,
                      previewRect.bottom,
                      layout.previewCornerRadius,
                      layout.previewCornerRadius);

            SetTextColor(hdc, selected ? colors_.selectionText : colors_.text);
            DrawPreviewThumbnail(hdc, previewRect, *item, selected);

            if (thumbnailDetailsVisible_)
            {
                SetTextColor(hdc, selected ? colors_.selectionText : colors_.text);
                const int titleTop = previewRect.bottom + layout.titleTopGap;
                RECT nameRect{cellRect.left + layout.textInset,
                              titleTop,
                              cellRect.right - layout.textInset,
                              titleTop + layout.titleHeight};
                HGDIOBJ oldFont = thumbnailTitleFont_
                    ? SelectObject(hdc, thumbnailTitleFont_)
                    : static_cast<HGDIOBJ>(nullptr);
                const std::wstring displayTitle = BuildThumbnailDisplayTitle(*item);
                DrawTextW(hdc,
                          displayTitle.c_str(),
                          -1,
                          &nameRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (oldFont)
                {
                    SelectObject(hdc, oldFont);
                }

                RECT infoRect{cellRect.left + layout.textInset,
                              cellRect.bottom - layout.infoBottomInset - layout.infoHeight,
                              cellRect.right - layout.textInset,
                              cellRect.bottom - layout.infoBottomInset};
                const std::wstring typeLabel = ToUppercase(item->fileType);
                const std::wstring dimensionLabel = FormatDimensionsForItem(*item);
                HGDIOBJ footerFont = thumbnailMetaFont_
                    ? SelectObject(hdc, thumbnailMetaFont_)
                    : static_cast<HGDIOBJ>(nullptr);

                SIZE typeTextExtent{};
                if (!typeLabel.empty())
                {
                    GetTextExtentPoint32W(hdc,
                                          typeLabel.c_str(),
                                          static_cast<int>(typeLabel.size()),
                                          &typeTextExtent);
                }

                SIZE dimensionTextExtent{};
                if (!dimensionLabel.empty())
                {
                    GetTextExtentPoint32W(hdc,
                                          dimensionLabel.c_str(),
                                          static_cast<int>(dimensionLabel.size()),
                                          &dimensionTextExtent);
                }

                const int footerWidth = infoRect.right - infoRect.left;
                const int gapWidth = typeLabel.empty() ? 0 : std::max(4, layout.badgeGap - 2);
                const int typeTextWidth = static_cast<int>(typeTextExtent.cx);
                const int preferredTypeWidth = typeLabel.empty() ? 0 : (typeTextWidth + 2);
                const int requiredDimensionsWidth = static_cast<int>(dimensionTextExtent.cx) + 2;
                const int maxTypeWidth = std::max(0, footerWidth - requiredDimensionsWidth - gapWidth);
                const int typeWidth = typeLabel.empty()
                    ? 0
                    : std::max(0, std::min(preferredTypeWidth, maxTypeWidth));
                RECT dimensionsRect{infoRect.left,
                                    infoRect.top,
                                    std::max(infoRect.left, infoRect.right - typeWidth - (typeWidth > 0 ? gapWidth : 0)),
                                    infoRect.bottom};
                RECT typeRect{std::max(infoRect.left, infoRect.right - typeWidth),
                              infoRect.top,
                              infoRect.right,
                              infoRect.bottom};

                SetTextColor(hdc, selected ? colors_.selectionText : colors_.mutedText);
                DrawTextW(hdc,
                          dimensionLabel.c_str(),
                          -1,
                          &dimensionsRect,
                          DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                if (typeWidth > 0)
                {
                    SetTextColor(hdc, selected ? colors_.selectionText : colors_.text);
                    DrawTextW(hdc,
                              typeLabel.c_str(),
                              -1,
                              &typeRect,
                              DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                if (footerFont)
                {
                    SelectObject(hdc, footerFont);
                }
            }

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
        }

        if (rubberBandActive_)
        {
            HGDIOBJ pen = static_cast<HGDIOBJ>(rubberBandPen_ ? rubberBandPen_ : GetStockObject(BLACK_PEN));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, rubberBandRect_.left, rubberBandRect_.top, rubberBandRect_.right, rubberBandRect_.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
        }
    }

    void BrowserPane::DrawPreviewThumbnail(HDC hdc, const RECT& previewRect, const BrowserItem& item, bool selected) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int targetWidth = layout.previewWidth;
        const int targetHeight = layout.previewHeight;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);
        const auto thumbnail = thumbnailScheduler_ ? thumbnailScheduler_->FindCachedThumbnail(cacheKey) : nullptr;

        if (!thumbnailScheduler_ || !decode::CanDecodeThumbnail(item))
        {
            RECT iconTextRect{previewRect.left + layout.textInset,
                              previewRect.top + std::max(10, layout.previewInset - 2),
                              previewRect.right - layout.textInset,
                              previewRect.top + std::max(34, layout.previewInset + layout.metaHeight)};
            const std::wstring placeholder = decode::IsRawFileType(item.fileType) ? item.fileType : std::wstring(L"IMAGE");
            SetTextColor(hdc, selected ? colors_.selectionText : colors_.mutedText);
            HGDIOBJ oldFont = thumbnailStatusFont_
                ? SelectObject(hdc, thumbnailStatusFont_)
                : static_cast<HGDIOBJ>(nullptr);
            DrawTextW(hdc, placeholder.c_str(), -1, &iconTextRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

            if (oldFont)
            {
                SelectObject(hdc, oldFont);
            }
            return;
        }

        if (!thumbnail)
        {
            const decode::ThumbnailDecodeFailureKind knownFailureKind = thumbnailScheduler_->KnownFailureKind(cacheKey);
            if (knownFailureKind != decode::ThumbnailDecodeFailureKind::None)
            {
                DrawUnavailableThumbnailState(hdc, previewRect, knownFailureKind, selected);
                return;
            }

            const int previewWidth = previewRect.right - previewRect.left;
            const int previewHeight = previewRect.bottom - previewRect.top;
            const int iconSize = layout.loadingIconSize;
            const int iconX = previewRect.left + ((previewWidth - iconSize) / 2);
            const int iconY = previewRect.top + std::max(layout.previewInset,
                                                         ((previewHeight - iconSize) / 2) - (layout.metaHeight / 2));
            if (HCURSOR waitCursor = LoadCursorW(nullptr, IDC_WAIT))
            {
                DrawIconEx(hdc, iconX, iconY, waitCursor, iconSize, iconSize, 0, nullptr, DI_NORMAL);
            }

            const COLORREF statusColor = selected ? colors_.selectionText : colors_.mutedText;
            SetTextColor(hdc, statusColor);
            HGDIOBJ oldFont = thumbnailStatusFont_
                ? SelectObject(hdc, thumbnailStatusFont_)
                : static_cast<HGDIOBJ>(nullptr);
            RECT statusRect{previewRect.left + layout.textInset,
                            iconY + iconSize + std::max(6, layout.metaTopGap),
                            previewRect.right - layout.textInset,
                            previewRect.bottom - layout.previewInset};
            DrawTextW(hdc, L"Loading thumbnail", -1, &statusRect, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (oldFont)
            {
                SelectObject(hdc, oldFont);
            }
            return;
        }

        const int drawX = previewRect.left + (((previewRect.right - previewRect.left) - thumbnail->Width()) / 2);
        const int drawY = previewRect.top + (((previewRect.bottom - previewRect.top) - thumbnail->Height()) / 2);
        HDC bitmapDc = CreateCompatibleDC(hdc);
        HGDIOBJ oldBitmap = SelectObject(bitmapDc, thumbnail->Bitmap());

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(hdc,
                   drawX,
                   drawY,
                   thumbnail->Width(),
                   thumbnail->Height(),
                   bitmapDc,
                   0,
                   0,
                   thumbnail->Width(),
                   thumbnail->Height(),
                   blend);

        SelectObject(bitmapDc, oldBitmap);
        DeleteDC(bitmapDc);
    }

    void BrowserPane::DrawUnavailableThumbnailState(HDC hdc,
                                                    const RECT& previewRect,
                                                    decode::ThumbnailDecodeFailureKind failureKind,
                                                    bool selected) const
    {
        if (!hdc)
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int previewWidth = previewRect.right - previewRect.left;
        const int previewHeight = previewRect.bottom - previewRect.top;
        int textTop = previewRect.top + (previewHeight / 3);

        if (unavailableThumbnailArt_)
        {
            const int iconSize = std::min({
                kUnavailableThumbnailIconSize,
                std::max(0, previewWidth - (layout.textInset * 2)),
                static_cast<int>(previewHeight * 0.34f),
            });
            if (iconSize > 0)
            {
                const int iconX = previewRect.left + ((previewWidth - iconSize) / 2);
                const int iconY = previewRect.top + std::max(10, static_cast<int>(previewHeight * 0.14f));
                util::DrawBitmapWithAlpha(hdc, *unavailableThumbnailArt_, iconX, iconY, iconSize, iconSize);
                textTop = iconY + iconSize + std::max(6, layout.metaTopGap);
            }
        }

        const wchar_t* message = UnavailableThumbnailMessage(failureKind);
        SetTextColor(hdc, selected ? colors_.selectionText : colors_.mutedText);
        HGDIOBJ oldFont = thumbnailStatusFont_
            ? SelectObject(hdc, thumbnailStatusFont_)
            : static_cast<HGDIOBJ>(nullptr);
        RECT statusRect{previewRect.left + layout.textInset,
                        textTop,
                        previewRect.right - layout.textInset,
                        previewRect.bottom - layout.previewInset};
        DrawTextW(hdc,
              message,
              -1,
                  &statusRect,
                  DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (oldFont)
        {
            SelectObject(hdc, oldFont);
        }
    }

    std::wstring BrowserPane::BuildListText(int viewIndex, int subItem) const
    {
        const BrowserItem* item = ItemFromViewIndex(viewIndex);
        if (!item)
        {
            return L"";
        }

        switch (subItem)
        {
        case 0:
            return item->fileName;
        case 1:
            return item->fileType;
        case 2:
            return FormatByteSize(item->fileSizeBytes);
        case 3:
            return item->modifiedDate;
        case 4:
        {
            if (item->imageWidth > 0 && item->imageHeight > 0)
            {
                return FormatDimensionsForItem(*item);
            }

            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const auto metadata = modelIndex >= 0 ? FindCachedMetadataForModelIndex(modelIndex) : nullptr;
            if (metadata && metadata->imageWidth > 0 && metadata->imageHeight > 0)
            {
                return FormatDimensions(metadata->imageWidth, metadata->imageHeight);
            }

            if (modelIndex >= 0)
            {
                ScheduleMetadataForItem(modelIndex, *item);
            }
            return L"...";
        }
        case 5:
        case 6:
        case 7:
        {
            const int modelIndex = ModelIndexFromViewIndex(viewIndex);
            const auto metadata = modelIndex >= 0 ? FindCachedMetadataForModelIndex(modelIndex) : nullptr;
            if (!metadata)
            {
                if (modelIndex >= 0)
                {
                    ScheduleMetadataForItem(modelIndex, *item);
                }
                return L"...";
            }

            if (subItem == 5)
            {
                return metadata->dateTaken;
            }
            if (subItem == 6)
            {
                return BuildCameraLabel(*metadata);
            }
            return metadata->title;
        }
        default:
            return L"";
        }
    }

    std::wstring BrowserPane::BuildThumbnailTooltipText(int viewIndex) const
    {
        const BrowserItem* item = ItemFromViewIndex(viewIndex);
        if (!item)
        {
            return {};
        }

        std::wstring tooltip = item->fileName;
        tooltip.append(L"\r\nType: ");
        tooltip.append(ToUppercase(item->fileType));
        tooltip.append(L"\r\nDimensions: ");
        tooltip.append(FormatDimensionsForItem(*item));
        tooltip.append(L"\r\nSize: ");
        tooltip.append(FormatByteSize(item->fileSizeBytes));
        tooltip.append(L"\r\nModified: ");
        tooltip.append(item->modifiedDate);

        if (thumbnailScheduler_ && decode::CanDecodeThumbnail(*item))
        {
            const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
            const auto cacheKey = MakeThumbnailCacheKey(*item, layout.previewWidth, layout.previewHeight);
            const decode::ThumbnailDecodeFailureKind failureKind = thumbnailScheduler_->KnownFailureKind(cacheKey);
            if (failureKind != decode::ThumbnailDecodeFailureKind::None)
            {
                tooltip.append(L"\r\n");
                tooltip.append(UnavailableThumbnailTooltipText(failureKind));
            }
        }

        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        const auto metadata = modelIndex >= 0 ? FindCachedMetadataForModelIndex(modelIndex) : nullptr;
        if (metadata)
        {
            const std::wstring cameraLabel = BuildCameraLabel(*metadata);
            if (!cameraLabel.empty())
            {
                tooltip.append(L"\r\nCamera: ");
                tooltip.append(cameraLabel);
            }
            if (!metadata->dateTaken.empty())
            {
                tooltip.append(L"\r\nTaken: ");
                tooltip.append(metadata->dateTaken);
            }
        }
        else if (modelIndex >= 0)
        {
            ScheduleMetadataForItem(modelIndex, *item);
        }

        return tooltip;
    }

    void BrowserPane::UpdateThumbnailTooltip(POINT point, WPARAM wParam, LPARAM lParam)
    {
        if (!thumbnailTooltip_)
        {
            return;
        }

        if (!trackingMouseLeave_)
        {
            TRACKMOUSEEVENT trackMouseEvent{};
            trackMouseEvent.cbSize = sizeof(trackMouseEvent);
            trackMouseEvent.dwFlags = TME_LEAVE;
            trackMouseEvent.hwndTrack = hwnd_;
            if (TrackMouseEvent(&trackMouseEvent) != FALSE)
            {
                trackingMouseLeave_ = true;
            }
        }

        const int viewIndex = HitTestThumbnailItem(point);
        if (viewIndex != hoveredTooltipViewIndex_)
        {
            hoveredTooltipViewIndex_ = viewIndex;

            TOOLINFOW toolInfo{};
            toolInfo.cbSize = sizeof(toolInfo);
            toolInfo.hwnd = hwnd_;
            toolInfo.uId = 1;
            toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
            if (viewIndex >= 0)
            {
                toolInfo.rect = GetThumbnailCellRect(viewIndex);
            }
            else
            {
                SetRectEmpty(&toolInfo.rect);
            }

            SendMessageW(thumbnailTooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            SendMessageW(thumbnailTooltip_, TTM_POP, 0, 0);
        }

        MSG relayMessage{};
        relayMessage.hwnd = hwnd_;
        relayMessage.message = WM_MOUSEMOVE;
        relayMessage.wParam = wParam;
        relayMessage.lParam = lParam;
        SendMessageW(thumbnailTooltip_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }

    void BrowserPane::HideThumbnailTooltip()
    {
        if (!thumbnailTooltip_)
        {
            hoveredTooltipViewIndex_ = -1;
            trackingMouseLeave_ = false;
            return;
        }

        hoveredTooltipViewIndex_ = -1;
        trackingMouseLeave_ = false;

        TOOLINFOW toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.hwnd = hwnd_;
        toolInfo.uId = 1;
        toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
        SetRectEmpty(&toolInfo.rect);
        SendMessageW(thumbnailTooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        SendMessageW(thumbnailTooltip_, TTM_POP, 0, 0);
    }

    std::wstring BrowserPane::BuildPlaceholderText() const
    {
        if (!model_ || model_->FolderPath().empty())
        {
            return L"Select a folder to begin browsing.";
        }

        if (model_->HasError())
        {
            return L"Unable to load this folder.\r\n\r\n" + model_->ErrorMessage();
        }

        if (model_->IsEnumerating() && orderedModelIndices_.empty())
        {
            return L"Scanning folder...";
        }

        if (!filterQuery_.empty() && orderedModelIndices_.empty())
        {
            return L"No images match \"" + filterQuery_ + L"\".";
        }

        return orderedModelIndices_.empty()
            ? L"No supported images found."
            : L"";
    }

    LRESULT BrowserPane::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            if (!CreateDetailsListView())
            {
                return -1;
            }
            CreateThumbnailTooltip();
            return 0;
        case WM_SIZE:
            LayoutChildren();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETFOCUS:
            if (viewMode_ == BrowserViewMode::Details && detailsList_ && IsWindowVisible(detailsList_))
            {
                SetFocus(detailsList_);
            }
            return 0;
        case WM_KEYDOWN:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                const bool controlPressed = GetKeyState(VK_CONTROL) < 0;
                if (wParam == VK_RETURN)
                {
                    RequestOpenPrimarySelection();
                    return 0;
                }
                if ((wParam == 'A' || wParam == 'a') && controlPressed)
                {
                    SelectAll();
                    return 0;
                }
                if (wParam == VK_ESCAPE && !selectedModelIndices_.empty())
                {
                    ClearSelection();
                    return 0;
                }
                if (wParam == VK_APPS || (wParam == VK_F10 && GetKeyState(VK_SHIFT) < 0))
                {
                    ShowContextMenu(ContextMenuAnchorScreenPoint());
                    return 0;
                }
            }
            break;
        case WM_VSCROLL:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                HideThumbnailTooltip();
                SCROLLINFO scrollInfo{};
                scrollInfo.cbSize = sizeof(scrollInfo);
                scrollInfo.fMask = SIF_ALL;
                GetScrollInfo(hwnd_, SB_VERT, &scrollInfo);

                int nextOffset = scrollOffsetY_;
                switch (LOWORD(wParam))
                {
                case SB_LINEUP:
                    nextOffset -= kWheelScrollAmount;
                    break;
                case SB_LINEDOWN:
                    nextOffset += kWheelScrollAmount;
                    break;
                case SB_PAGEUP:
                    nextOffset -= static_cast<int>(scrollInfo.nPage);
                    break;
                case SB_PAGEDOWN:
                    nextOffset += static_cast<int>(scrollInfo.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    nextOffset = scrollInfo.nTrackPos;
                    break;
                case SB_TOP:
                    nextOffset = 0;
                    break;
                case SB_BOTTOM:
                    nextOffset = scrollInfo.nMax;
                    break;
                default:
                    break;
                }

                SetScrollOffset(nextOffset);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                HideThumbnailTooltip();
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const double scrollAmount = static_cast<double>((delta / WHEEL_DELTA) * kWheelScrollAmount);

                if (!smoothScrollTimerId_)
                {
                    smoothScrollCurrent_ = static_cast<double>(scrollOffsetY_);
                }

                smoothScrollTarget_ = smoothScrollCurrent_ - scrollAmount * 2.5;

                if (!smoothScrollTimerId_)
                {
                    smoothScrollTimerId_ = SetTimer(hwnd_, kSmoothScrollTimerId, kSmoothScrollIntervalMs, nullptr);
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kSmoothScrollTimerId && smoothScrollTimerId_)
            {
                const double diff = smoothScrollTarget_ - smoothScrollCurrent_;
                if (std::abs(diff) < kSmoothScrollSnapThreshold)
                {
                    smoothScrollCurrent_ = smoothScrollTarget_;
                    KillTimer(hwnd_, kSmoothScrollTimerId);
                    smoothScrollTimerId_ = 0;
                }
                else
                {
                    smoothScrollCurrent_ += diff * 0.18;
                }

                const int targetOffset = static_cast<int>(std::lround(smoothScrollCurrent_));
                SetScrollOffset(targetOffset);

                // If the scroll position was clamped at a boundary, stop animation
                if (scrollOffsetY_ != targetOffset)
                {
                    smoothScrollCurrent_ = static_cast<double>(scrollOffsetY_);
                    smoothScrollTarget_ = smoothScrollCurrent_;
                    KillTimer(hwnd_, kSmoothScrollTimerId);
                    smoothScrollTimerId_ = 0;
                }
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                HideThumbnailTooltip();
                SetFocus(hwnd_);
                const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const int viewIndex = HitTestThumbnailItem(point);
                const bool shiftPressed = (wParam & MK_SHIFT) != 0;
                const bool controlPressed = (wParam & MK_CONTROL) != 0;

                if (viewIndex >= 0)
                {
                    if (shiftPressed)
                    {
                        ExtendSelectionToViewIndex(viewIndex);
                    }
                    else if (controlPressed)
                    {
                        ToggleViewIndexSelection(viewIndex);
                    }
                    else
                    {
                        SelectSingleViewIndex(viewIndex);
                    }
                }
                else
                {
                    BeginRubberBandSelection(point, controlPressed);
                }
                return 0;
            }
            break;
        case WM_LBUTTONDBLCLK:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                const int viewIndex = HitTestThumbnailItem(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (viewIndex >= 0)
                {
                    SelectSingleViewIndex(viewIndex);
                    RequestOpenItemForViewIndex(viewIndex);
                }
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
                if (rubberBandActive_)
                {
                    HideThumbnailTooltip();
                    UpdateRubberBandSelection(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                }
                else
                {
                    UpdateThumbnailTooltip(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, wParam, lParam);
                }
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            HideThumbnailTooltip();
            return 0;
        case WM_LBUTTONUP:
            if (rubberBandActive_)
            {
                EndRubberBandSelection();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            HideThumbnailTooltip();
            EndRubberBandSelection();
            return 0;
        case WM_CONTEXTMENU:
            if (viewMode_ == BrowserViewMode::Thumbnails && reinterpret_cast<HWND>(wParam) == hwnd_)
            {
                HideThumbnailTooltip();
                POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (screenPoint.x == -1 && screenPoint.y == -1)
                {
                    ShowContextMenu(ContextMenuAnchorScreenPoint());
                    return 0;
                }

                POINT clientPoint = screenPoint;
                ScreenToClient(hwnd_, &clientPoint);
                const int viewIndex = HitTestThumbnailItem(clientPoint);
                const int modelIndex = ModelIndexFromViewIndex(viewIndex);
                if (modelIndex >= 0 && !selectedModelIndices_.contains(modelIndex))
                {
                    SelectSingleViewIndex(viewIndex);
                }

                ShowContextMenu(screenPoint);
                return 0;
            }
            break;
        case services::ThumbnailScheduler::kMessageId:
        {
            std::unique_ptr<services::ThumbnailReadyUpdate> update(
                reinterpret_cast<services::ThumbnailReadyUpdate*>(lParam));
            if (!update || update->sessionId != thumbnailSessionId_)
            {
                return 0;
            }

            if (!update->success)
            {
                InvalidateThumbnailCellForModelIndex(update->modelIndex);
                return 0;
            }

            bool dimensionsChanged = false;
            if (model_)
            {
                dimensionsChanged = model_->UpdateDecodedDimensions(update->modelIndex, update->imageWidth, update->imageHeight);
            }

            if (dimensionsChanged && sortMode_ == BrowserSortMode::Dimensions)
            {
                RebuildOrder();
                UpdateDetailsListView();
                UpdateVerticalScrollBar();
                ScheduleVisibleThumbnailWork();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }

            if (dimensionsChanged && viewMode_ == BrowserViewMode::Details)
            {
                UpdateDetailsListView();
                InvalidateRect(detailsList_, nullptr, FALSE);
            }

            InvalidateThumbnailCellForModelIndex(update->modelIndex);
            return 0;
        }
        case services::ImageMetadataService::kMessageId:
        {
            std::unique_ptr<services::MetadataReadyUpdate> update(
                reinterpret_cast<services::MetadataReadyUpdate*>(lParam));
            if (!update || !update->success || update->sessionId != metadataSessionId_)
            {
                return 0;
            }

            if (model_ && metadataService_)
            {
                auto metadata = metadataService_->FindCachedMetadata(update->item);
                bool dimensionsChanged = false;
                if (metadata && metadata->imageWidth > 0 && metadata->imageHeight > 0)
                {
                    dimensionsChanged = model_->UpdateDecodedDimensions(update->modelIndex,
                                                                        metadata->imageWidth,
                                                                        metadata->imageHeight);
                }
                if (metadata && metadata->dateTakenTimestampUtc != 0)
                {
                    model_->UpdateDateTakenTimestamp(update->modelIndex, metadata->dateTakenTimestampUtc);
                }

                if (dimensionsChanged && sortMode_ == BrowserSortMode::Dimensions)
                {
                    RebuildOrder();
                    UpdateDetailsListView();
                    UpdateVerticalScrollBar();
                    ScheduleVisibleThumbnailWork();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return 0;
                }
            }

            if (detailsList_ && viewMode_ == BrowserViewMode::Details)
            {
                InvalidateRect(detailsList_, nullptr, FALSE);
            }
            if (update->modelIndex == ModelIndexFromViewIndex(hoveredTooltipViewIndex_))
            {
                SendMessageW(thumbnailTooltip_, TTM_UPDATE, 0, 0);
            }
            if (update->modelIndex == focusedModelIndex_ || selectedModelIndices_.contains(update->modelIndex))
            {
                NotifyStateChanged();
            }
            return 0;
        }
        case WM_NOTIFY:
            if (thumbnailTooltip_ && reinterpret_cast<const NMHDR*>(lParam)->hwndFrom == thumbnailTooltip_)
            {
                const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
                if (header->code == TTN_GETDISPINFOW)
                {
                    auto* displayInfo = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                    tooltipTextBuffer_ = BuildThumbnailTooltipText(hoveredTooltipViewIndex_);
                    displayInfo->lpszText = tooltipTextBuffer_.empty()
                        ? const_cast<wchar_t*>(L"")
                        : tooltipTextBuffer_.data();
                    return 0;
                }
            }
            if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == detailsList_)
            {
                const NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
                switch (header->code)
                {
                case LVN_GETDISPINFOW:
                {
                    auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                    if ((info->item.mask & LVIF_TEXT) != 0 && info->item.pszText && info->item.cchTextMax > 0)
                    {
                        listViewTextBuffer_ = BuildListText(info->item.iItem, info->item.iSubItem);
                        wcsncpy_s(info->item.pszText, info->item.cchTextMax, listViewTextBuffer_.c_str(), _TRUNCATE);
                    }
                    return 0;
                }
                case LVN_ITEMCHANGED:
                    RebuildSelectionFromDetailsList();
                    return 0;
                case NM_CUSTOMDRAW:
                    return HandleDetailsListCustomDraw(lParam);
                case LVN_KEYDOWN:
                {
                    const auto* keyDown = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
                    const bool controlPressed = GetKeyState(VK_CONTROL) < 0;
                    if (keyDown->wVKey == VK_RETURN)
                    {
                        const int focusedItem = ListView_GetNextItem(detailsList_, -1, LVNI_FOCUSED);
                        if (focusedItem >= 0)
                        {
                            RequestOpenItemForViewIndex(focusedItem);
                        }
                        return 0;
                    }
                    if ((keyDown->wVKey == 'A' || keyDown->wVKey == 'a') && controlPressed)
                    {
                        SelectAll();
                        return 0;
                    }
                    if (keyDown->wVKey == VK_ESCAPE && !selectedModelIndices_.empty())
                    {
                        ClearSelection();
                        return 0;
                    }
                    if (keyDown->wVKey == VK_APPS || (keyDown->wVKey == VK_F10 && GetKeyState(VK_SHIFT) < 0))
                    {
                        ShowContextMenu(ContextMenuAnchorScreenPoint());
                        return 0;
                    }
                    break;
                }
                case LVN_COLUMNCLICK:
                {
                    const auto* columnClick = reinterpret_cast<const NMLISTVIEW*>(lParam);
                    switch (columnClick->iSubItem)
                    {
                    case 0:
                        SetSortMode(BrowserSortMode::FileName);
                        break;
                    case 1:
                        SetSortMode(BrowserSortMode::FileType);
                        break;
                    case 2:
                        SetSortMode(BrowserSortMode::FileSize);
                        break;
                    case 3:
                        SetSortMode(BrowserSortMode::ModifiedDate);
                        break;
                    case 4:
                        SetSortMode(BrowserSortMode::Dimensions);
                        break;
                    default:
                        break;
                    }
                    return 0;
                }
                case NM_DBLCLK:
                {
                    const auto* activate = reinterpret_cast<const NMLISTVIEW*>(lParam);
                    if (activate->iItem >= 0)
                    {
                        RequestOpenItemForViewIndex(activate->iItem);
                    }
                    return 0;
                }
                case NM_RCLICK:
                {
                    POINT screenPoint{};
                    GetCursorPos(&screenPoint);

                    POINT clientPoint = screenPoint;
                    ScreenToClient(detailsList_, &clientPoint);

                    LVHITTESTINFO hitTestInfo{};
                    hitTestInfo.pt = clientPoint;
                    ListView_SubItemHitTest(detailsList_, &hitTestInfo);
                    const int modelIndex = ModelIndexFromViewIndex(hitTestInfo.iItem);
                    if (modelIndex >= 0 && !selectedModelIndices_.contains(modelIndex))
                    {
                        SelectSingleViewIndex(hitTestInfo.iItem);
                    }

                    ShowContextMenu(screenPoint);
                    return 0;
                }
                default:
                    break;
                }
            }
            break;
        case WM_PAINT:
        {
            EnsureD2DRenderTarget();

            if (d2dRenderTarget_ && viewMode_ != BrowserViewMode::Details)
            {
                PAINTSTRUCT paintStruct{};
                BeginPaint(hwnd_, &paintStruct);

                render::D2DRenderer::Instance().ResizeRenderTarget(d2dRenderTarget_.Get(), hwnd_);

                d2dRenderTarget_->BeginDraw();
                const D2D1_SIZE_F size = d2dRenderTarget_->GetSize();

                if (orderedModelIndices_.empty() || viewMode_ != BrowserViewMode::Thumbnails)
                {
                    d2dRenderTarget_->Clear(render::ToD2DColor(colors_.windowBackground));
                    D2DDrawPlaceholderState(d2dRenderTarget_.Get(), size);
                }
                else
                {
                    D2DDrawThumbnailCells(d2dRenderTarget_.Get(), size);
                }

                const HRESULT hr = d2dRenderTarget_->EndDraw();
                if (hr == D2DERR_RECREATE_TARGET)
                {
                    ReleaseD2DResources();
                }

                EndPaint(hwnd_, &paintStruct);
                return 0;
            }

            PAINTSTRUCT paintStruct{};
            HDC hdc = BeginPaint(hwnd_, &paintStruct);

            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const int width = clientRect.right - clientRect.left;
            const int height = clientRect.bottom - clientRect.top;

            HDC memoryDc = CreateCompatibleDC(hdc);
            HBITMAP bitmap = CreateCompatibleBitmap(hdc, std::max(1, width), std::max(1, height));
            HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

            if (viewMode_ == BrowserViewMode::Details && !orderedModelIndices_.empty())
            {
                FillRect(memoryDc, &clientRect, surfaceBrush_);
            }
            else
            {
                DrawThumbnailCells(memoryDc, clientRect);
            }

            BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);

            SelectObject(memoryDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memoryDc);
            EndPaint(hwnd_, &paintStruct);
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK BrowserPane::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        BrowserPane* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<BrowserPane*>(createStruct->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<BrowserPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}