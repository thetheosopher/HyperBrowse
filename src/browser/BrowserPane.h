#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hyperbrowse::browser
{
    struct BrowserItem;
    class BrowserModel;

    enum class BrowserViewMode : int
    {
        Thumbnails = 0,
        Details = 1,
    };

    enum class BrowserSortMode : int
    {
        FileName = 0,
        ModifiedDate = 1,
        FileSize = 2,
        Dimensions = 3,
        FileType = 4,
        Random = 5,
        DateTaken = 6,
    };

    enum class ThumbnailSizePreset : int
    {
        Pixels96 = 96,
        Pixels128 = 128,
        Pixels160 = 160,
        Pixels192 = 192,
        Pixels256 = 256,
        Pixels320 = 320,
    };
}

namespace hyperbrowse::services
{
    class ThumbnailScheduler;
    struct ImageMetadata;
    class ImageMetadataService;
}

namespace hyperbrowse::cache
{
    class CachedThumbnail;
}

namespace hyperbrowse::browser
{
    class BrowserPane
    {
    public:
        static constexpr UINT kStateChangedMessage = WM_APP + 42;
        static constexpr UINT kOpenItemMessage = WM_APP + 44;
        static constexpr UINT kContextMenuMessage = WM_APP + 45;

        struct ThemeColors
        {
            COLORREF windowBackground;
            COLORREF surfaceBackground;
            COLORREF previewBackground;
            COLORREF selectedPreviewBackground;
            COLORREF text;
            COLORREF mutedText;
            COLORREF border;
            COLORREF accent;
            COLORREF accentFill;
            COLORREF selectionText;
            COLORREF rubberBand;
            COLORREF placeholderBackground;
            COLORREF rowAlternateBackground;
        };

        explicit BrowserPane(HINSTANCE instance);
        ~BrowserPane();

        bool Create(HWND parent);
        HWND Hwnd() const noexcept;

        void SetModel(BrowserModel* model);
        void RefreshFromModel();
        void SetViewMode(BrowserViewMode mode);
        BrowserViewMode GetViewMode() const noexcept;
        void SetSortMode(BrowserSortMode sortMode);
        BrowserSortMode GetSortMode() const noexcept;
        void SetSortAscending(bool ascending);
        bool IsSortAscending() const noexcept;
        void SetFilterQuery(std::wstring query);
        const std::wstring& GetFilterQuery() const noexcept;
        bool HasActiveFilter() const noexcept;
        std::uint64_t DisplayedItemCount() const noexcept;
        void SetThumbnailSizePreset(ThumbnailSizePreset preset);
        ThumbnailSizePreset GetThumbnailSizePreset() const noexcept;
        void SetCompactThumbnailLayout(bool enabled);
        bool IsCompactThumbnailLayoutEnabled() const noexcept;
        void SetThumbnailDetailsVisible(bool visible);
        bool AreThumbnailDetailsVisible() const noexcept;
        void SetDarkTheme(bool enabled);

        void ClearSelection();
        std::uint64_t SelectedCount() const noexcept;
        std::uint64_t SelectedBytes() const noexcept;
        int PrimarySelectedModelIndex() const noexcept;
        std::vector<int> OrderedModelIndicesSnapshot() const;
        std::vector<int> OrderedSelectedModelIndicesSnapshot() const;
        std::vector<std::wstring> SelectedFilePathsSnapshot() const;
        std::wstring FocusedFilePathSnapshot() const;
        void RestoreSelectionByFilePaths(const std::vector<std::wstring>& filePaths, const std::wstring& focusedFilePath);
        void InvalidateMediaCacheForPaths(const std::vector<std::wstring>& filePaths);
        std::shared_ptr<const hyperbrowse::services::ImageMetadata> FindCachedMetadataForModelIndex(int modelIndex) const;
        std::wstring BuildMetadataReportForModelIndex(int modelIndex) const;

    private:
        static constexpr const wchar_t* kClassName = L"HyperBrowseBrowserPane";

        struct ThumbnailLayoutMetrics
        {
            int cellPadding{};
            int itemWidth{};
            int itemHeight{};
            int previewInset{};
            int previewWidth{};
            int previewHeight{};
            int textInset{};
            int titleTopGap{};
            int titleHeight{};
            int metaTopGap{};
            int metaHeight{};
            int infoBottomInset{};
            int infoHeight{};
            int badgeHorizontalPadding{};
            int badgeGap{};
            int badgeCornerRadius{};
            int cellCornerRadius{};
            int previewCornerRadius{};
            int loadingIconSize{};
            int titlePointSize{};
            int metaPointSize{};
            int statusPointSize{};
        };

        bool RegisterClass() const;
        bool CreateDetailsListView();
        void CreateThumbnailTooltip();
        ThumbnailLayoutMetrics CurrentThumbnailLayout() const;
        void LayoutChildren();
        void RebuildOrder();
        void PruneSelectionToVisibleItems();
        void UpdateDetailsListView();
        void UpdateVerticalScrollBar();
        void SetScrollOffset(int value);
        int ColumnsForClientWidth(int width) const;
        RECT GetThumbnailCellRect(int viewIndex) const;
        RECT GetThumbnailPreviewRect(const RECT& cellRect) const;
        int HitTestThumbnailItem(POINT point) const;
        int ViewIndexFromModelIndex(int modelIndex) const;
        int ModelIndexFromViewIndex(int viewIndex) const;
        const BrowserItem* ItemFromViewIndex(int viewIndex) const;
        void NotifyStateChanged() const;
        void ApplyThemeToDetailsList() const;
        void RebuildThemeResources();
        void ReleaseThemeResources();
        void RebuildThumbnailFonts();
        void ReleaseThumbnailFonts();
        LRESULT HandleDetailsListCustomDraw(LPARAM lParam) const;
        void RebuildSelectionFromDetailsList();
        void SyncDetailsListSelectionFromModel();
        void UpdateSelectionBytes();
        void SelectAll();
        void SelectSingleViewIndex(int viewIndex);
        void ToggleViewIndexSelection(int viewIndex);
        void ExtendSelectionToViewIndex(int viewIndex);
        void BeginRubberBandSelection(POINT point, bool additive);
        void UpdateRubberBandSelection(POINT point);
        void EndRubberBandSelection();
        void RequestOpenPrimarySelection() const;
        void RequestOpenItemForViewIndex(int viewIndex) const;
        POINT ContextMenuAnchorScreenPoint() const;
        void ShowContextMenu(POINT screenPoint) const;
        void ScheduleVisibleThumbnailWork();
        void ScheduleVisibleMetadataWork() const;
        void ScheduleMetadataForItem(int modelIndex, const BrowserItem& item) const;
        void CancelThumbnailWork();
        void InvalidateThumbnailCellForModelIndex(int modelIndex) const;
        void UpdateThumbnailTooltip(POINT point, WPARAM wParam, LPARAM lParam);
        void HideThumbnailTooltip();
        void EnsureD2DRenderTarget();
        void ReleaseD2DResources();
        void RebuildD2DBrushes();
        void RebuildD2DTextFormats();
        void D2DDrawPlaceholderState(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const;
        void D2DDrawThumbnailCells(ID2D1RenderTarget* rt, const D2D1_SIZE_F& size) const;
        void D2DDrawPreviewThumbnail(ID2D1RenderTarget* rt, const D2D1_RECT_F& previewRect, const BrowserItem& item, bool selected) const;
        ID2D1Bitmap* GetOrCreateD2DBitmap(ID2D1RenderTarget* rt, const cache::CachedThumbnail& thumbnail) const;
        void DrawPlaceholderState(HDC hdc, const RECT& clientRect) const;
        void DrawThumbnailCells(HDC hdc, const RECT& clientRect) const;
        void DrawPreviewThumbnail(HDC hdc, const RECT& previewRect, const BrowserItem& item, bool selected) const;
        std::wstring BuildListText(int viewIndex, int subItem) const;
        std::wstring BuildThumbnailTooltipText(int viewIndex) const;
        std::wstring BuildPlaceholderText() const;
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HINSTANCE instance_{};
        HWND parent_{};
        HWND hwnd_{};
        HWND detailsList_{};
        HWND thumbnailTooltip_{};
        HFONT detailsListFont_{};
        HFONT thumbnailTitleFont_{};
        HFONT thumbnailMetaFont_{};
        HFONT thumbnailStatusFont_{};
        HFONT placeholderTitleFont_{};
        HFONT placeholderBodyFont_{};
        bool ownsDetailsListFont_{};
        HBRUSH backgroundBrush_{};
        HBRUSH surfaceBrush_{};
        HBRUSH previewBrush_{};
        HBRUSH selectedCellBrush_{};
        HBRUSH selectedPreviewBrush_{};
        HBRUSH placeholderBrush_{};
        HPEN borderPen_{};
        HPEN selectedBorderPen_{};
        HPEN rubberBandPen_{};
        std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> placeholderArt_;
        ThemeColors colors_{};
        std::unique_ptr<hyperbrowse::services::ThumbnailScheduler> thumbnailScheduler_;
        std::unique_ptr<hyperbrowse::services::ImageMetadataService> metadataService_;
        bool darkTheme_{};
        bool syncingDetailsSelection_{};
        BrowserViewMode viewMode_{BrowserViewMode::Thumbnails};
        BrowserSortMode sortMode_{BrowserSortMode::FileName};
        bool sortAscending_{true};
        std::wstring filterQuery_;
        std::wstring filterQueryLower_;
        ThumbnailSizePreset thumbnailSizePreset_{ThumbnailSizePreset::Pixels192};
        bool compactThumbnailLayout_{true};
        bool thumbnailDetailsVisible_{true};
        BrowserModel* model_{};
        std::vector<int> orderedModelIndices_;
        std::unordered_set<int> selectedModelIndices_;
        std::unordered_set<int> rubberBandSeedSelection_;
        std::uint64_t selectedBytes_{};
        int scrollOffsetY_{};
        int anchorModelIndex_{-1};
        int focusedModelIndex_{-1};
        bool rubberBandActive_{};
        bool trackingMouseLeave_{};
        POINT rubberBandStart_{};
        RECT rubberBandRect_{};
        int hoveredTooltipViewIndex_{-1};
        std::uint64_t thumbnailSessionId_{1};
        std::uint64_t metadataSessionId_{1};
        std::uint64_t thumbnailRequestEpoch_{};
        mutable std::wstring listViewTextBuffer_;
        mutable std::wstring tooltipTextBuffer_;

        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> d2dRenderTarget_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dBackgroundBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dSurfaceBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dPreviewBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dSelectedCellBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dSelectedPreviewBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dPlaceholderBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dBorderBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dSelectedBorderBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dRubberBandBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dTextBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dMutedTextBrush_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> d2dSelectionTextBrush_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dTitleFormat_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dMetaFormat_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dStatusFormat_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dPlaceholderTitleFormat_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dPlaceholderBodyFormat_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dPlaceholderArtBitmap_;
        mutable std::unordered_map<HBITMAP, Microsoft::WRL::ComPtr<ID2D1Bitmap>> d2dBitmapCache_;

        double smoothScrollTarget_{};
        double smoothScrollCurrent_{};
        double smoothScrollVelocity_{};
        UINT_PTR smoothScrollTimerId_{};
        static constexpr UINT_PTR kSmoothScrollTimerId = 9001;
        static constexpr UINT kSmoothScrollIntervalMs = 16;
        static constexpr double kSmoothScrollDeceleration = 0.82;
        static constexpr double kSmoothScrollSnapThreshold = 2.0;
    };

    std::wstring BrowserSortModeToLabel(BrowserSortMode sortMode);
    int ThumbnailSizePresetToPixels(ThumbnailSizePreset preset);
    std::wstring ThumbnailSizePresetToLabel(ThumbnailSizePreset preset);
}
