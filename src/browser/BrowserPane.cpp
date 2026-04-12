#include "browser/BrowserPane.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <memory>

#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "services/ThumbnailScheduler.h"

namespace
{
    constexpr int kWheelScrollAmount = 72;
    constexpr int kVisiblePriority = 0;
    constexpr int kNearVisiblePriority = 1;

    hyperbrowse::browser::BrowserPane::ThemeColors MakeThemeColors(bool darkTheme)
    {
        if (darkTheme)
        {
            return hyperbrowse::browser::BrowserPane::ThemeColors{
                RGB(24, 28, 32),
                RGB(34, 38, 43),
                RGB(234, 238, 242),
                RGB(157, 165, 173),
                RGB(86, 92, 101),
                RGB(95, 160, 220),
                RGB(40, 76, 114),
                RGB(244, 248, 252),
                RGB(124, 182, 236),
            };
        }

        return hyperbrowse::browser::BrowserPane::ThemeColors{
            RGB(243, 245, 248),
            RGB(255, 255, 255),
            RGB(32, 36, 40),
            RGB(103, 109, 116),
            RGB(210, 215, 223),
            RGB(36, 99, 178),
            RGB(218, 233, 250),
            RGB(28, 37, 52),
            RGB(73, 132, 208),
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

    int CompareCaseInsensitive(const std::wstring& lhs, const std::wstring& rhs)
    {
        return _wcsicmp(lhs.c_str(), rhs.c_str());
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

    HFONT CreateSystemUiFont()
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
        {
            metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
            return CreateFontIndirectW(&metrics.lfMessageFont);
        }

        return nullptr;
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
        default:
            return L"Unknown";
        }
    }

    BrowserPane::BrowserPane(HINSTANCE instance)
        : instance_(instance)
        , colors_(MakeThemeColors(false))
        , thumbnailScheduler_(std::make_unique<services::ThumbnailScheduler>())
    {
        backgroundBrush_ = CreateSolidBrush(colors_.windowBackground);
        surfaceBrush_ = CreateSolidBrush(colors_.surfaceBackground);
    }

    BrowserPane::~BrowserPane()
    {
        if (detailsListFont_ && ownsDetailsListFont_)
        {
            DeleteObject(detailsListFont_);
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (surfaceBrush_)
        {
            DeleteObject(surfaceBrush_);
        }
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

        return hwnd_ != nullptr;
    }

    HWND BrowserPane::Hwnd() const noexcept
    {
        return hwnd_;
    }

    void BrowserPane::SetModel(BrowserModel* model)
    {
        model_ = model;
        ++thumbnailSessionId_;
        CancelThumbnailWork();
    }

    void BrowserPane::RefreshFromModel()
    {
        ++thumbnailSessionId_;
        RebuildOrder();
        UpdateSelectionBytes();
        UpdateDetailsListView();
        UpdateVerticalScrollBar();
        ScheduleVisibleThumbnailWork();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void BrowserPane::SetViewMode(BrowserViewMode mode)
    {
        if (viewMode_ == mode)
        {
            return;
        }

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

    void BrowserPane::SetDarkTheme(bool enabled)
    {
        darkTheme_ = enabled;
        colors_ = MakeThemeColors(enabled);

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (surfaceBrush_)
        {
            DeleteObject(surfaceBrush_);
        }

        backgroundBrush_ = CreateSolidBrush(colors_.windowBackground);
        surfaceBrush_ = CreateSolidBrush(colors_.surfaceBackground);
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

        ApplyThemeToDetailsList();
        return true;
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

        std::sort(orderedModelIndices_.begin(), orderedModelIndices_.end(), comparator);
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
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientWidth);
        const int rows = (static_cast<int>(orderedModelIndices_.size()) + columns - 1) / columns;
        const int contentHeight = kItemPadding + rows * (kItemHeight + kItemPadding);
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
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientWidth);
        const int rows = columns > 0 ? (static_cast<int>(orderedModelIndices_.size()) + columns - 1) / columns : 0;
        const int contentHeight = kItemPadding + rows * (kItemHeight + kItemPadding);
        const int maxOffset = std::max(0, contentHeight - clientHeight);
        const int clampedValue = std::clamp(value, 0, maxOffset);
        if (clampedValue == scrollOffsetY_)
        {
            return;
        }

        scrollOffsetY_ = clampedValue;
        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_POS;
        scrollInfo.nPos = scrollOffsetY_;
        SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
        InvalidateRect(hwnd_, nullptr, TRUE);
        ScheduleVisibleThumbnailWork();
    }

    int BrowserPane::ColumnsForClientWidth(int width) const
    {
        const int stride = kItemWidth + kItemPadding;
        return std::max(1, (std::max(width, stride) - kItemPadding) / stride);
    }

    RECT BrowserPane::GetThumbnailCellRect(int viewIndex) const
    {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int row = viewIndex / columns;
        const int column = viewIndex % columns;

        RECT rect{};
        rect.left = kItemPadding + column * (kItemWidth + kItemPadding);
        rect.top = kItemPadding + row * (kItemHeight + kItemPadding) - scrollOffsetY_;
        rect.right = rect.left + kItemWidth;
        rect.bottom = rect.top + kItemHeight;
        return rect;
    }

    RECT BrowserPane::GetThumbnailPreviewRect(const RECT& cellRect) const
    {
        RECT previewRect{};
        previewRect.left = cellRect.left + kPreviewInset;
        previewRect.top = cellRect.top + kPreviewInset;
        previewRect.right = cellRect.right - kPreviewInset;
        previewRect.bottom = previewRect.top + kPreviewHeight;
        return previewRect;
    }

    int BrowserPane::HitTestThumbnailItem(POINT point) const
    {
        if (!hwnd_ || orderedModelIndices_.empty())
        {
            return -1;
        }

        if (point.x < kItemPadding || point.y < 0)
        {
            return -1;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int horizontalStride = kItemWidth + kItemPadding;
        const int verticalStride = kItemHeight + kItemPadding;
        const int adjustedY = point.y + scrollOffsetY_;
        if (adjustedY < kItemPadding)
        {
            return -1;
        }

        const int column = (point.x - kItemPadding) / horizontalStride;
        const int row = (adjustedY - kItemPadding) / verticalStride;
        if (column < 0 || column >= columns || row < 0)
        {
            return -1;
        }

        const int xWithinStride = (point.x - kItemPadding) % horizontalStride;
        const int yWithinStride = (adjustedY - kItemPadding) % verticalStride;
        if (xWithinStride > kItemWidth || yWithinStride > kItemHeight)
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
        if (!model_)
        {
            return nullptr;
        }

        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (modelIndex < 0)
        {
            return nullptr;
        }

        const auto& items = model_->Items();
        return modelIndex < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(modelIndex)] : nullptr;
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

        ListView_SetBkColor(detailsList_, colors_.surfaceBackground);
        ListView_SetTextBkColor(detailsList_, colors_.surfaceBackground);
        ListView_SetTextColor(detailsList_, colors_.text);
        InvalidateRect(detailsList_, nullptr, TRUE);
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

    void BrowserPane::ScheduleVisibleThumbnailWork()
    {
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
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = kItemHeight + kItemPadding;
        const int firstVisibleRow = std::max(0, scrollOffsetY_ / verticalStride);
        const int lastVisibleRow = std::max(firstVisibleRow, (scrollOffsetY_ + clientHeight) / verticalStride);
        const int requestStartRow = std::max(0, firstVisibleRow - 1);
        const int requestEndRow = lastVisibleRow + 1;
        const int firstIndex = requestStartRow * columns;
        const int lastIndex = std::min(static_cast<int>(orderedModelIndices_.size()), (requestEndRow + 1) * columns);
        const int targetWidth = kItemWidth - (2 * kPreviewInset);
        const int targetHeight = kPreviewHeight;

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
            workItems.push_back(services::ThumbnailWorkItem{
                modelIndex,
                MakeThumbnailCacheKey(*item, targetWidth, targetHeight),
                row >= firstVisibleRow && row <= lastVisibleRow ? kVisiblePriority : kNearVisiblePriority,
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

    void BrowserPane::DrawPlaceholderState(HDC hdc, const RECT& clientRect) const
    {
        FillRect(hdc, &clientRect, surfaceBrush_);

        RECT textRect = clientRect;
        InflateRect(&textRect, -24, -24);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors_.text);
        const std::wstring text = BuildPlaceholderText();
        DrawTextW(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }

    void BrowserPane::DrawThumbnailCells(HDC hdc, const RECT& clientRect) const
    {
        FillRect(hdc, &clientRect, surfaceBrush_);
        if (orderedModelIndices_.empty())
        {
            DrawPlaceholderState(hdc, clientRect);
            return;
        }

        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int verticalStride = kItemHeight + kItemPadding;
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

            const bool selected = selectedModelIndices_.contains(ModelIndexFromViewIndex(viewIndex));

            const HBRUSH cellBrush = CreateSolidBrush(selected ? colors_.accentFill : colors_.surfaceBackground);
            FillRect(hdc, &cellRect, cellBrush);
            DeleteObject(cellBrush);

            HPEN pen = CreatePen(PS_SOLID, 1, selected ? colors_.accent : colors_.border);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, cellRect.left, cellRect.top, cellRect.right, cellRect.bottom);

            RECT previewRect = GetThumbnailPreviewRect(cellRect);
            const HBRUSH previewBrush = CreateSolidBrush(selected ? colors_.accent : colors_.windowBackground);
            FillRect(hdc, &previewRect, previewBrush);
            DeleteObject(previewBrush);
            Rectangle(hdc, previewRect.left, previewRect.top, previewRect.right, previewRect.bottom);

            SetTextColor(hdc, selected ? colors_.selectionText : colors_.text);
            DrawPreviewThumbnail(hdc, previewRect, *item);

            RECT nameRect{cellRect.left + 12, previewRect.bottom + 10, cellRect.right - 12, previewRect.bottom + 46};
            DrawTextW(hdc, item->fileName.c_str(), -1, &nameRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);

            SetTextColor(hdc, selected ? colors_.selectionText : colors_.mutedText);
            RECT infoRect{cellRect.left + 12, cellRect.bottom - 42, cellRect.right - 12, cellRect.bottom - 12};
            std::wstring info = item->fileType + L"  |  " + FormatByteSize(item->fileSizeBytes);
            DrawTextW(hdc, info.c_str(), -1, &infoRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        if (rubberBandActive_)
        {
            HPEN pen = CreatePen(PS_DOT, 1, colors_.rubberBand);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, rubberBandRect_.left, rubberBandRect_.top, rubberBandRect_.right, rubberBandRect_.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
    }

    void BrowserPane::DrawPreviewThumbnail(HDC hdc, const RECT& previewRect, const BrowserItem& item) const
    {
        const int targetWidth = kItemWidth - (2 * kPreviewInset);
        const int targetHeight = kPreviewHeight;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);

        if (!thumbnailScheduler_ || !decode::CanDecodeThumbnail(item))
        {
            RECT iconTextRect = previewRect;
            const std::wstring placeholder = decode::IsRawFileType(item.fileType) ? item.fileType : std::wstring(L"IMG");
            DrawTextW(hdc, placeholder.c_str(), -1, &iconTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT dimTextRect = previewRect;
            dimTextRect.top += ((previewRect.bottom - previewRect.top) / 2) + 6;
            DrawTextW(hdc, FormatDimensionsForItem(item).c_str(), -1, &dimTextRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return;
        }

        const auto thumbnail = thumbnailScheduler_->FindCachedThumbnail(cacheKey);
        if (!thumbnail)
        {
            RECT iconTextRect = previewRect;
            const std::wstring placeholder = decode::IsRawFileType(item.fileType) ? item.fileType : std::wstring(L"IMG");
            DrawTextW(hdc, placeholder.c_str(), -1, &iconTextRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT dimTextRect = previewRect;
            dimTextRect.top += ((previewRect.bottom - previewRect.top) / 2) + 6;
            DrawTextW(hdc, FormatDimensionsForItem(item).c_str(), -1, &dimTextRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
            return FormatDimensionsForItem(*item);
        default:
            return L"";
        }
    }

    std::wstring BrowserPane::BuildPlaceholderText() const
    {
        if (!model_ || model_->FolderPath().empty())
        {
            return L"Open a folder to start browsing.\r\n\r\nThumbnail mode is virtualized and paints only visible cells. Details mode uses an owner-data list view for thousands of items.";
        }

        if (model_->HasError())
        {
            return L"Enumeration failed:\r\n\r\n" + model_->ErrorMessage();
        }

        if (model_->IsEnumerating() && orderedModelIndices_.empty())
        {
            return L"Scanning the selected folder asynchronously...\r\n\r\nSupported formats: jpg, jpeg, png, gif, tif, tiff, nef, nrw";
        }

        return orderedModelIndices_.empty()
            ? L"No supported images were found in the selected folder scope."
            : L"";
    }

    LRESULT BrowserPane::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            return CreateDetailsListView() ? 0 : -1;
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
        case WM_VSCROLL:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
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
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                SetScrollOffset(scrollOffsetY_ - ((delta / WHEEL_DELTA) * kWheelScrollAmount));
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (viewMode_ == BrowserViewMode::Thumbnails)
            {
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
            if (viewMode_ == BrowserViewMode::Thumbnails && rubberBandActive_)
            {
                UpdateRubberBandSelection(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (rubberBandActive_)
            {
                EndRubberBandSelection();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            EndRubberBandSelection();
            return 0;
        case services::ThumbnailScheduler::kMessageId:
        {
            std::unique_ptr<services::ThumbnailReadyUpdate> update(
                reinterpret_cast<services::ThumbnailReadyUpdate*>(lParam));
            if (!update || !update->success || update->sessionId != thumbnailSessionId_)
            {
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
        case WM_NOTIFY:
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
                default:
                    break;
                }
            }
            break;
        case WM_PAINT:
        {
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