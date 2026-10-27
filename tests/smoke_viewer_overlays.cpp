#include "smoke_support.h"
#include "smoke_scenarios.h"

#include <future>
#include <map>
#include <numeric>

#include "viewer/ViewerOverlay.h"
#include "viewer/ViewerOverlayDialog.h"
#include "viewer/ViewerWindowInternal.h"
#include "ui/SystemTheme.h"

namespace hyperbrowse::viewer
{
    struct ViewerOverlayTestAccess
    {
        static auto Layout(const ViewerWindow& window) { return window.CurrentOverlayLayout(); }
        static auto Bins(const ViewerWindow& window) { return window.histogram_; }
        static bool Loading(const ViewerWindow& window) { return window.histogramLoading_; }
        static auto Source(const ViewerWindow& window) { return window.histogramSource_; }
        static auto DisplaySource(const ViewerWindow& window) { return window.OverlayImage(); }
        static bool ImageLoading(const ViewerWindow& window) { return window.loading_; }
        static auto HistogramText(const ViewerWindow& window) { return window.OverlayPaneText(OverlayPane::Histogram, 0, 0, 0); }
        static auto MetadataText(const ViewerWindow& window) { return window.OverlayPaneText(OverlayPane::FullMetadata, 0, 0, 0); }
        static bool MetadataReady(const ViewerWindow& window)
        {
            return !window.metadataLoading_ && window.currentMetadataIndex_ == window.OverlayImageIndex()
                && !window.currentMetadataText_.empty();
        }
        static bool HasEllipsisTrimming(const ViewerWindow& window)
        {
            auto& renderer = render::D2DRenderer::Instance();
            for (auto* format : {window.d2dNameFormat_.Get(), window.d2dInfoFormat_.Get(),
                                 window.d2dBottomInfoFormat_.Get(), window.d2dMetadataFormat_.Get()})
            {
                if (!format) return false;
                DWRITE_TRIMMING trimming{};
                Microsoft::WRL::ComPtr<IDWriteInlineObject> sign;
                if (FAILED(format->GetTrimming(&trimming, sign.GetAddressOf())) || !sign
                    || trimming.granularity != DWRITE_TRIMMING_GRANULARITY_CHARACTER) return false;
                if (format == window.d2dMetadataFormat_.Get()) continue;
                const auto layout = renderer.CreateTextLayout(std::wstring(512, L'W'), format, 120, 100);
                DWRITE_LINE_METRICS line{};
                UINT32 count{};
                if (!layout || FAILED(layout->GetLineMetrics(&line, 1, &count)) || count != 1 || !line.isTrimmed)
                    return false;
            }
            return true;
        }
        static auto PartialBounds(ViewerWindow& window)
        {
            const auto bounds = window.InfoOverlayTopPanelRect();
            window.RequestInfoOverlayRepaint();
            return bounds;
        }
        static void Command(ViewerWindow& window, UINT id) { window.DispatchContextMenuCommand(id); }
        static void Menu(ViewerWindow& window)
        {
            RECT rect{};
            GetWindowRect(window.Hwnd(), &rect);
            window.ShowContextMenu({rect.left + 100, rect.top + 100});
        }
        static void Focus(ViewerWindow& window, std::size_t tile) { window.FocusCompareTileInternal(tile); }
        static void Navigate(ViewerWindow& window, int index) { window.NavigateToIndex(index, index > window.currentIndex_); }
        static bool RejectStale(ViewerWindow& window)
        {
            auto stale = std::make_shared<ViewerWindow::HistogramReady>();
            stale->generation = window.histogramGeneration_->load() - 1;
            stale->source = window.OverlayImage();
            stale->index = window.OverlayImageIndex();
            stale->bins.peak = 999999;
            window.histogramMailbox_->store(stale);
            const auto before = window.histogram_.peak;
            window.HandleHistogramReadyMessage(0);
            return before == window.histogram_.peak;
        }
        static bool RejectWrongIdentity(ViewerWindow& window)
        {
            auto stale = std::make_shared<ViewerWindow::HistogramReady>();
            stale->generation = window.histogramGeneration_->load();
            stale->source = window.OverlayImage();
            stale->index = window.OverlayImageIndex();
            stale->identity.filePath = L"C:\\not-the-displayed-image.png";
            stale->bins.peak = 999999;
            window.histogramMailbox_->store(stale);
            const auto before = window.histogram_.peak;
            window.HandleHistogramReadyMessage(0);
            return before == window.histogram_.peak;
        }
        static bool PaintGdi(ViewerWindow& window)
        {
            RECT client{};
            GetClientRect(window.Hwnd(), &client);
            HDC screen = GetDC(window.Hwnd());
            HDC dc = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, (std::max)(1L, client.right), (std::max)(1L, client.bottom));
            ReleaseDC(window.Hwnd(), screen);
            if (!dc || !bitmap)
            {
                if (dc) DeleteDC(dc);
                if (bitmap) DeleteObject(bitmap);
                return false;
            }
            const auto previous = SelectObject(dc, bitmap);
            window.DrawOverlaysGdi(dc);
            const auto layout = window.CurrentOverlayLayout();
            const auto& histogram = layout[static_cast<std::size_t>(OverlayPane::Histogram)];
            bool plotAtTop = true;
            if (histogram.visibility == OverlayVisibility::Shown && window.histogram_.visible)
            {
                const auto bounds = ViewerOverlayTextRect(histogram.rect,
                    ViewerOverlayMetricsForWindow(window.Hwnd(), window.infoOverlayTextSize_), OverlayPane::Histogram);
                plotAtTop = GetPixel(dc, static_cast<int>(std::ceil(bounds.left)), static_cast<int>(std::ceil(bounds.top)))
                    == PanelBorderColor(window.darkTheme_);
            }
            SelectObject(dc, previous);
            DeleteObject(bitmap);
            DeleteDC(dc);
            return plotAtTop;
        }
        static bool BlockWorkers(ViewerWindow& window, std::shared_future<void> signal)
        {
            for (int i = 0; i < 6; ++i)
                if (!window.backgroundExecutor_->Post([signal] { signal.wait(); })) return false;
            return true;
        }
    };
}

namespace hyperbrowse::tests::smoke
{
    namespace
    {
        using namespace hyperbrowse::viewer;

        void ExpectValidLayout(const OverlayLayout& layout, SIZE viewport, int margin)
        {
            for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
            {
                if (layout[i].visibility != OverlayVisibility::Shown) continue;
                const auto& rect = layout[i].rect;
                Expect(rect.left >= margin && rect.top >= margin && rect.right <= viewport.cx - margin
                    && rect.bottom <= viewport.cy - margin, "Overlay escaped viewport bounds");
                for (std::size_t j = i + 1; j < kOverlayPaneCount; ++j)
                {
                    if (layout[j].visibility != OverlayVisibility::Shown) continue;
                    RECT overlap{};
                    Expect(!IntersectRect(&overlap, &rect, &layout[j].rect), "Overlay panes overlap");
                }
            }
        }

        void ExpectMatchingWidths(const OverlayLayout& layout, const ViewerOverlaySettings& settings,
                                  const OverlaySizes& sizes)
        {
            for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
            {
                if (layout[i].visibility != OverlayVisibility::Shown) continue;
                LONG width{};
                for (std::size_t j = 0; j < kOverlayPaneCount; ++j)
                    if (layout[j].visibility == OverlayVisibility::Shown && settings.panes[i].corner == settings.panes[j].corner)
                        width = (std::max)(width, sizes[j].cx);
                Expect(layout[i].rect.right - layout[i].rect.left == width,
                    "Overlay stack did not use its widest visible pane");
                Expect(layout[i].rect.bottom - layout[i].rect.top == sizes[i].cy,
                    "Matching overlay widths changed pane height");
            }
        }

        void RunMatchingWidths()
        {
            ViewerOverlaySettings settings;
            for (auto& pane : settings.panes)
            {
                pane.corner = OverlayCorner::TopLeft;
                pane.windowedEnabled = pane.fullScreenEnabled = true;
            }
            const OverlaySizes sizes{{{220, 40}, {180, 40}, {500, 250}, {210, 60}}};
            settings.panes[2].windowedEnabled = false;
            auto layout = CalculateOverlayLayout({1200, 900}, settings, false, sizes, 16, 10);
            ExpectMatchingWidths(layout, settings, sizes);
            Expect(layout[0].rect.right - layout[0].rect.left == 220,
                "Disabled pane widened the corner stack");
            layout = CalculateOverlayLayout({1200, 900}, settings, true, sizes, 16, 10);
            ExpectMatchingWidths(layout, settings, sizes);
            Expect(layout[0].rect.right - layout[0].rect.left == 500,
                "Full-screen enabled pane did not widen its stack");
            layout = CalculateOverlayLayout({1200, 200}, settings, true, sizes, 16, 10);
            ExpectMatchingWidths(layout, settings, sizes);
            Expect(layout[2].visibility == OverlayVisibility::HiddenToFit
                && layout[0].rect.right - layout[0].rect.left == 220,
                "Suppressed pane kept widening the remaining stack");
            settings.panes[2].corner = settings.panes[3].corner = OverlayCorner::BottomRight;
            layout = CalculateOverlayLayout({1200, 900}, settings, true, sizes, 16, 10);
            ExpectMatchingWidths(layout, settings, sizes);
            Expect(layout[0].rect.right - layout[0].rect.left == 220
                && layout[2].rect.right - layout[2].rect.left == 500,
                "Independent corners were forced to share a width");
        }

        void RunLayoutAndPersistence()
        {
            ViewerOverlaySettings settings;
            Expect(!settings.panes[3].windowedEnabled && !settings.panes[3].fullScreenEnabled,
                "Histogram must default off in both modes");
            OverlaySizes sizes{{{220, 72}, {180, 48}, {250, 230}, {210, 180}}};
            for (auto& pane : settings.panes) pane.windowedEnabled = pane.fullScreenEnabled = true;
            for (int assignment = 0; assignment < 256; ++assignment)
            {
                int bits = assignment;
                for (auto& pane : settings.panes)
                {
                    pane.corner = static_cast<OverlayCorner>(bits % 4);
                    bits /= 4;
                }
                for (const SIZE viewport : {SIZE{1200, 900}, SIZE{450, 340}, SIZE{180, 90}, SIZE{1, 1}})
                {
                    const auto layout = CalculateOverlayLayout(viewport, settings, false, sizes, 16, 10);
                    ExpectValidLayout(layout, viewport, 16);
                    ExpectMatchingWidths(layout, settings, sizes);
                    const auto repeat = CalculateOverlayLayout(viewport, settings, false, sizes, 16, 10);
                    for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                        Expect(layout[i].visibility == repeat[i].visibility && EqualRect(&layout[i].rect, &repeat[i].rect),
                            "Overlay layout is nondeterministic");
                }
            }
            for (auto& pane : settings.panes) { pane.corner = OverlayCorner::BottomRight; pane.importance = OverlayImportance::Normal; }
            settings = NormalizeOverlaySettings(settings);
            auto layout = CalculateOverlayLayout({1000, 1000}, settings, false, sizes, 16, 10);
            Expect(layout[0].rect.bottom == 984 && layout[1].rect.bottom < layout[0].rect.top
                && layout[0].rect.right == layout[1].rect.right, "Bottom stack anchoring/order failed");
            MoveOverlayPane(settings, OverlayPane::Histogram, true);
            Expect(settings.panes[3].order == 2 && settings.panes[2].order == 3, "Move toward corner did not swap stack order");
            MoveOverlayPane(settings, OverlayPane::Histogram, false);
            Expect(settings.panes[3].order == 3, "Move away from corner did not swap stack order");
            sizes = {{{100, 100}, {100, 100}, {100, 100}, {100, 100}}};
            layout = CalculateOverlayLayout({160, 250}, settings, false, sizes, 16, 10);
            Expect(layout[0].visibility == OverlayVisibility::Shown && layout[1].visibility == OverlayVisibility::Shown
                && layout[2].visibility == OverlayVisibility::HiddenToFit && layout[3].visibility == OverlayVisibility::HiddenToFit,
                "Equal-importance retention order changed");
            settings.panes[2].importance = OverlayImportance::High;
            layout = CalculateOverlayLayout({160, 140}, settings, false, sizes, 16, 10);
            Expect(layout[2].visibility == OverlayVisibility::Shown && layout[0].visibility == OverlayVisibility::HiddenToFit,
                "Importance incorrectly follows stack order");
            layout = CalculateOverlayLayout({1000, 1000}, settings, false, sizes, 16, 10);
            for (const auto& pane : layout) Expect(pane.visibility == OverlayVisibility::Shown, "Suppressed pane did not restore");
            settings.masterVisible = false;
            settings.panes[1].windowedEnabled = false;
            layout = CalculateOverlayLayout({1000, 1000}, settings, false, sizes, 16, 10);
            Expect(layout[0].visibility == OverlayVisibility::MasterHidden && layout[1].visibility == OverlayVisibility::Disabled,
                "Disabled and master-hidden states were conflated");
            layout = CalculateOverlayLayout({1000, 1000}, settings, true, sizes, 16, 10);
            Expect(layout[1].visibility == OverlayVisibility::MasterHidden, "Per-mode visibility was lost");

            std::map<std::wstring, DWORD> values{{L"ViewerInfoOverlaysVisible", 0},
                {L"ViewerInfoOverlayTextSize", 2}, {L"ViewerFullMetadataVisible", 1},
                {L"ViewerWindowedFullMetadataVisible", 0}};
            const auto read = [&](std::wstring_view key, DWORD* value)
            {
                const auto found = values.find(std::wstring(key));
                if (found == values.end()) return false;
                *value = found->second;
                return true;
            };
            auto loaded = ViewerOverlayPersistence::Load(read);
            Expect(!loaded.masterVisible && loaded.textSize == InfoOverlayTextSize::Large
                && !loaded.panes[2].windowedEnabled && loaded.panes[2].fullScreenEnabled,
                "Legacy per-mode migration overwrote user choices");
            values[L"ViewerOverlayFullMetadataWindowedEnabled"] = 1;
            values[L"ViewerOverlayHistogramCorner"] = 99;
            values[L"ViewerOverlayHistogramImportance"] = 99;
            values[L"ViewerOverlayHistogramOrder"] = 99;
            values[L"ViewerInfoOverlayTextSize"] = 99;
            loaded = ViewerOverlayPersistence::Load(read);
            Expect(loaded.panes[2].windowedEnabled && loaded.panes[3].corner == OverlayCorner::BottomLeft
                && loaded.panes[3].importance == OverlayImportance::Normal && loaded.panes[3].order == 0
                && loaded.textSize == InfoOverlayTextSize::Small, "Malformed settings failed normalization");
            ViewerOverlayPersistence::Save(settings, [&](std::wstring_view key, DWORD value) { values[std::wstring(key)] = value; });
            Expect(ViewerOverlayPersistence::Load(read) == NormalizeOverlaySettings(settings), "Overlay persistence round trip failed");
            for (const UINT dpi : {96U, 144U, 192U})
                for (const auto text : {InfoOverlayTextSize::Small, InfoOverlayTextSize::Medium, InfoOverlayTextSize::Large})
                {
                    const SIZE viewport{MulDiv(1280, dpi, 96), MulDiv(800, dpi, 96)};
                    settings.masterVisible = true;
                    ExpectValidLayout(CalculateOverlayLayout(viewport, settings, false,
                        ViewerWindow::MeasureOverlaySizes(viewport, dpi, text), MulDiv(16, dpi, 96), MulDiv(10, dpi, 96)),
                        viewport, MulDiv(16, dpi, 96));
                }
        }

        void RunTextBounds()
        {
            auto& renderer = render::D2DRenderer::Instance();
            Expect(renderer.IsAvailable(), "DirectWrite is unavailable for overlay text bounds coverage");
            for (const UINT dpi : {96U, 120U, 144U, 192U, 240U})
                for (const auto size : {InfoOverlayTextSize::Small, InfoOverlayTextSize::Medium, InfoOverlayTextSize::Large})
                {
                    const auto metrics = ViewerOverlayMetricsForDpi(dpi, size);
                    const auto name = renderer.CreateTextFormat(L"Segoe UI", metrics.nameFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                    const auto info = renderer.CreateTextFormat(L"Segoe UI", metrics.infoFontSize);
                    const auto status = renderer.CreateTextFormat(L"Segoe UI", metrics.bottomInfoFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                    const auto sizes = ViewerWindow::MeasureOverlaySizes({MulDiv(1600, dpi, 96), MulDiv(1000, dpi, 96)}, dpi, size);
                    const auto expectFits = [&](std::wstring_view text, IDWriteTextFormat* format,
                                                float width, float height)
                    {
                        const auto layout = renderer.CreateTextLayout(text, format, width, height);
                        DWRITE_TEXT_METRICS textMetrics{};
                        Expect(layout && SUCCEEDED(layout->GetMetrics(&textMetrics)),
                            "Unable to measure overlay text");
                        Expect(textMetrics.height <= height + 0.01f,
                            "Overlay content bounds clip a complete line of text: dpi=" + std::to_string(dpi)
                                + ", size=" + std::to_string(static_cast<int>(size))
                                + ", font=" + std::to_string(format->GetFontSize())
                                + ", required=" + std::to_string(textMetrics.height)
                                + ", available=" + std::to_string(height));
                    };
                    for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                    {
                        const auto pane = static_cast<OverlayPane>(i);
                        const RECT panel{0, 0, sizes[i].cx, sizes[i].cy};
                        const auto bounds = ViewerOverlayTextRect(panel, metrics, pane);
                        const float width = bounds.right - bounds.left;
                        const float height = bounds.bottom - bounds.top;
                        if (pane == OverlayPane::Histogram)
                            expectFits(L"Loading histogram...", info.Get(), width, height);
                        else if (pane == OverlayPane::ImageStatus)
                            expectFits(L"780 x 1200 | 64% | Fit", status.Get(), width, height);
                        else
                        {
                            expectFits(L"Image gjpqy.png", name.Get(), width, metrics.topNameHeight);
                            if (pane == OverlayPane::FileInformation)
                                expectFits(L"1 / 61 | JPG | 536.6 KB", info.Get(), width, height - metrics.topNameHeight);
                        }
                        LOGFONTW font{};
                        font.lfHeight = -static_cast<LONG>(std::lround(metrics.infoFontSize));
                        wcscpy_s(font.lfFaceName, L"Segoe UI");
                        HFONT gdiFont = CreateFontIndirectW(&font);
                        HDC dc = CreateCompatibleDC(nullptr);
                        Expect(gdiFont && dc, "Unable to create GDI overlay text metrics resources");
                        const auto oldFont = SelectObject(dc, gdiFont);
                        TEXTMETRICW textMetrics{};
                        const bool measured = GetTextMetricsW(dc, &textMetrics) != FALSE;
                        SelectObject(dc, oldFont);
                        DeleteDC(dc);
                        DeleteObject(gdiFont);
                        const float rowHeight = pane == OverlayPane::ImageStatus || pane == OverlayPane::Histogram ? height
                            : (std::min)(metrics.topNameHeight, height - metrics.topNameHeight);
                        Expect(measured && textMetrics.tmHeight <= std::floor(rowHeight),
                            "GDI overlay content bounds clip a complete line of text: dpi=" + std::to_string(dpi)
                                + ", size=" + std::to_string(static_cast<int>(size))
                                + ", pane=" + std::to_string(i));
                    }
                }
        }

        void RunHistogramComputation()
        {
            const unsigned char pixels[]{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 8, 8, 8, 255};
            ui::DetailsPanelHistogram::Result bins;
            Expect(ui::DetailsPanelHistogram::ComputePixels(pixels, 1, 1, 4, &bins)
                && bins.red[255] == 1 && bins.green[0] == 1 && bins.blue[0] == 1 && bins.peak == 1,
                "Shared RGB histogram computed incorrect solid-color bins");
            Expect(ui::DetailsPanelHistogram::ComputePixels(pixels, 2, 2, 8, &bins)
                && bins.red[255] == 1 && bins.green[255] == 1 && bins.blue[255] == 1
                && bins.red[0] == 2 && bins.green[0] == 2 && bins.blue[0] == 2 && bins.red[8] == 1 && bins.peak == 2,
                "Shared RGB histogram computed incorrect mixed-pixel bins");
            Expect(!ui::DetailsPanelHistogram::ComputePixels(nullptr, 2, 2, 8, &bins) && !bins.visible,
                "Histogram failure left stale bins visible");
            Expect(!ui::DetailsPanelHistogram::ComputePixels(pixels, 2, 2, 4, &bins),
                "Histogram accepted a truncated stride");
            int rows{};
            Expect(!ui::DetailsPanelHistogram::ComputePixels(pixels, 2, 2, 8, &bins, [&] { return ++rows == 2; })
                && !bins.visible && bins.red[255] == 0, "Cancelled histogram exposed partial bins");
        }

        struct DialogProbe
        {
            bool apply{};
            bool found{};
            bool nativeControls{};
            bool summary{};
            bool accessible{};
            bool editable{};
            bool dpiAdapted{};
            ULONGLONG deadline{};
            bool geometry{};
            bool previewPaint{};
            std::string geometryError;
            bool ok{};
            bool editAfterApply{};
            bool cleanClose{};
            bool dirtyState{};
        };

        bool CheckOverlayDialogGeometry(HWND dialog, std::string& error)
        {
            const HWND content = GetDlgItem(dialog, 120);
            if (GetWindowTextLengthW(content) != 0)
            {
                error = "Scroll host paints a caption behind its controls";
                return false;
            }
            std::vector<std::pair<int, RECT>> controls;
            for (const int id : {100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
                                 200, 201, 202, 203, 204, 205})
            {
                const HWND control = GetDlgItem(content, id);
                RECT rect{};
                GetWindowRect(control, &rect);
                MapWindowPoints(nullptr, content, reinterpret_cast<POINT*>(&rect), 2);
                for (const auto& [other, bounds] : controls)
                {
                    RECT overlap{};
                    if (IntersectRect(&overlap, &rect, &bounds))
                    {
                        error = "Overlapping overlay dialog controls " + std::to_string(other) + " and " + std::to_string(id);
                        return false;
                    }
                }
                controls.emplace_back(id, rect);
                if (id == 100 || id == 110) continue;
                wchar_t text[1200]{};
                GetWindowTextW(control, text, 1200);
                const HDC dc = GetDC(control);
                const auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
                RECT measured{0, 0, rect.right - rect.left, 0};
                if (id == 101 || id == 102 || id == 108)
                {
                    TEXTMETRICW metrics{};
                    GetTextMetricsW(dc, &metrics);
                    measured.right -= metrics.tmHeight + 8;
                }
                DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_WORDBREAK);
                SelectObject(dc, previous);
                ReleaseDC(control, dc);
                if (measured.bottom > rect.bottom - rect.top)
                {
                    error = "Overlay dialog control clips text: " + std::to_string(id)
                        + ", required height=" + std::to_string(measured.bottom)
                        + ", available=" + std::to_string(rect.bottom - rect.top)
                        + ", width=" + std::to_string(rect.right - rect.left);
                    return false;
                }
            }
            RECT body{}, client{};
            GetWindowRect(content, &body);
            MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&body), 2);
            GetClientRect(dialog, &client);
            std::vector<RECT> footerButtons;
            for (const int id : {112, IDOK, IDCANCEL, 113})
            {
                RECT rect{};
                GetWindowRect(GetDlgItem(dialog, id), &rect);
                MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
                if (rect.top < body.bottom || rect.bottom > client.bottom || rect.left < 0 || rect.right > client.right)
                {
                    error = "Overlay dialog footer button is obscured";
                    return false;
                }
                for (const auto& other : footerButtons)
                {
                    RECT overlap{};
                    if (IntersectRect(&overlap, &rect, &other))
                    {
                        error = "Overlay dialog footer buttons overlap";
                        return false;
                    }
                }
                footerButtons.push_back(rect);
            }
            return true;
        }

        bool CheckOverlayPreviewPainting(HWND dialog)
        {
            constexpr LONG width = 520, height = 300;
            const HDC dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels{};
            const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (!dc || !bitmap)
            {
                if (bitmap) DeleteObject(bitmap);
                if (dc) DeleteDC(dc);
                return false;
            }
            const auto previous = SelectObject(dc, bitmap);
            auto* buffer = static_cast<std::uint32_t*>(pixels);
            constexpr std::uint32_t sentinel = 0x00c020e0;
            std::fill_n(buffer, width * height, sentinel);
            DRAWITEMSTRUCT draw{};
            draw.CtlType = ODT_STATIC;
            draw.CtlID = 110;
            draw.hwndItem = GetDlgItem(GetDlgItem(dialog, 120), 110);
            draw.hDC = dc;
            draw.rcItem = {70, 50, 470, 230};
            SendMessageW(dialog, WM_DRAWITEM, 110, reinterpret_cast<LPARAM>(&draw));
            GdiFlush();
            bool insideChanged{}, outsideChanged{};
            for (LONG y = 0; y < height; ++y)
                for (LONG x = 0; x < width; ++x)
                {
                    if (buffer[y * width + x] == sentinel) continue;
                    if (x >= draw.rcItem.left && x < draw.rcItem.right && y >= draw.rcItem.top && y < draw.rcItem.bottom)
                        insideChanged = true;
                    else outsideChanged = true;
                }
            SelectObject(dc, previous);
            DeleteObject(bitmap);
            DeleteDC(dc);
            return insideChanged && !outsideChanged;
        }

        LRESULT CALLBACK ProbeDialog(HWND owner, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR id, DWORD_PTR reference)
        {
            auto& probe = *reinterpret_cast<DialogProbe*>(reference);
            if (message == WM_TIMER && wParam == id)
            {
                HWND dialog{};
                EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM data)
                {
                    wchar_t title[80]{};
                    GetWindowTextW(candidate, title, 80);
                    if (std::wstring_view(title) == L"Configure overlays")
                    {
                        *reinterpret_cast<HWND*>(data) = candidate;
                        return FALSE;
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&dialog));
                if (dialog)
                {
                    probe.found = true;
                    const HWND content = GetDlgItem(dialog, 120);
                    probe.nativeControls = GetDlgItem(content, 100) && GetDlgItem(content, 101)
                        && GetDlgItem(content, 103) && GetDlgItem(content, 104);
                    probe.geometry = CheckOverlayDialogGeometry(dialog, probe.geometryError);
                    probe.previewPaint = CheckOverlayPreviewPainting(dialog);
                    probe.dirtyState = GetDlgItem(dialog, IDOK) && !IsWindowEnabled(GetDlgItem(dialog, 113))
                        && LOWORD(SendMessageW(dialog, DM_GETDEFID, 0, 0)) == IDOK;
                    Microsoft::WRL::ComPtr<IAccessible> accessible;
                    if (SUCCEEDED(AccessibleObjectFromWindow(GetDlgItem(content, 101), static_cast<DWORD>(OBJID_CLIENT),
                        IID_IAccessible, reinterpret_cast<void**>(accessible.GetAddressOf()))))
                    {
                        VARIANT self{};
                        self.vt = VT_I4;
                        self.lVal = CHILDID_SELF;
                        VARIANT role{};
                        BSTR name{};
                        accessible->get_accRole(self, &role);
                        accessible->get_accName(self, &name);
                        probe.accessible = role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_CHECKBUTTON && name && SysStringLen(name) > 0;
                        SysFreeString(name);
                        VariantClear(&role);
                    }
                    const auto command = [&](int id, int notification)
                    {
                        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, notification),
                            reinterpret_cast<LPARAM>(GetDlgItem(content, id)));
                    };
                    SendMessageW(GetDlgItem(content, 100), LB_SETCURSEL, 3, 0);
                    command(100, LBN_SELCHANGE);
                    SendMessageW(GetDlgItem(content, 101), BM_CLICK, 0, 0);
                    SendMessageW(GetDlgItem(content, 103), CB_SETCURSEL, 1, 0);
                    command(103, CBN_SELCHANGE);
                    SendMessageW(GetDlgItem(content, 104), CB_SETCURSEL, 2, 0);
                    command(104, CBN_SELCHANGE);
                    SendMessageW(GetDlgItem(content, 105), BM_CLICK, 0, 0);
                    wchar_t edited[1200]{};
                    GetWindowTextW(GetDlgItem(content, 111), edited, 1200);
                    probe.editable = SendMessageW(GetDlgItem(content, 101), BM_GETCHECK, 0, 0) == BST_CHECKED
                        && std::wstring_view(edited).find(L"Histogram: top right, order 1") != std::wstring_view::npos;
                    LOGFONTW before{}, after{};
                    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(content, 101), WM_GETFONT, 0, 0)),
                        sizeof(before), &before);
                    RECT frame{};
                    GetWindowRect(dialog, &frame);
                    SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&frame));
                    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(content, 101), WM_GETFONT, 0, 0)),
                        sizeof(after), &after);
                    SCROLLINFO scroll{sizeof(scroll), SIF_ALL};
                    GetScrollInfo(content, SB_VERT, &scroll);
                    probe.dpiAdapted = std::abs(after.lfHeight) > std::abs(before.lfHeight)
                        && scroll.nMax > static_cast<int>(scroll.nPage);
                    probe.geometry = CheckOverlayDialogGeometry(dialog, probe.geometryError) && probe.geometry;
                    probe.previewPaint = CheckOverlayPreviewPainting(dialog) && probe.previewPaint;
                    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(112, BN_CLICKED), 0);
                    wchar_t summary[1200]{};
                    GetWindowTextW(GetDlgItem(content, 111), summary, 1200);
                    probe.summary = std::wstring_view(summary).find(L"hidden to fit") != std::wstring_view::npos;
                    probe.dirtyState = probe.dirtyState && IsWindowEnabled(GetDlgItem(dialog, 113));
                    if (probe.cleanClose)
                    {
                        SendMessageW(GetDlgItem(content, 107), CB_SETCURSEL, 2, 0);
                        command(107, CBN_SELCHANGE);
                        probe.dirtyState = probe.dirtyState && !IsWindowEnabled(GetDlgItem(dialog, 113));
                    }
                    if (probe.apply)
                    {
                        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(113, BN_CLICKED), 0);
                        probe.dirtyState = probe.dirtyState && IsWindow(dialog) && !IsWindowEnabled(GetDlgItem(dialog, 113));
                        if (probe.editAfterApply)
                        {
                            SendMessageW(GetDlgItem(content, 108), BM_CLICK, 0, 0);
                            probe.dirtyState = probe.dirtyState && IsWindowEnabled(GetDlgItem(dialog, 113));
                        }
                    }
                    SendMessageW(dialog, WM_COMMAND, probe.ok ? IDOK : IDCANCEL, 0);
                }
                else if (GetTickCount64() > probe.deadline) PostMessageW(owner, WM_CANCELMODE, 0, 0);
            }
            return DefSubclassProc(owner, message, wParam, lParam);
        }

        void RunDialog(HINSTANCE instance, HWND owner)
        {
            ViewerOverlaySettings initial;
            initial.textSize = InfoOverlayTextSize::Large;
            for (const auto appSize : {util::AppTextSize::Small, util::AppTextSize::Medium, util::AppTextSize::Large})
            for (int action = 0; action < 6; ++action)
            {
                const bool apply = action == 1 || action == 3;
                DialogProbe probe{apply, false, false, false, false, false, false, GetTickCount64() + 5000};
                probe.ok = action == 2 || action == 4;
                probe.editAfterApply = action == 3;
                probe.cleanClose = action >= 4;
                constexpr UINT_PTR timer = 0x48424f56;
                Expect(SetWindowSubclass(owner, ProbeDialog, timer, reinterpret_cast<DWORD_PTR>(&probe)) != FALSE,
                    "Could not install overlay dialog probe");
                SetTimer(owner, timer, 30, nullptr);
                int calls{};
                ViewerOverlaySettings applied = initial;
                const bool result = ShowViewerOverlayDialog(owner, instance, true, appSize,
                    initial, {240, 100}, 96, [&](const auto& settings) { ++calls; applied = settings; return true; });
                KillTimer(owner, timer);
                RemoveWindowSubclass(owner, ProbeDialog, timer);
                Expect(probe.found && probe.nativeControls && probe.summary && probe.accessible && probe.editable && probe.dpiAdapted,
                    "Overlay dialog failed native controls or hidden-to-fit preview coverage");
                Expect(probe.geometry, probe.geometryError);
                Expect(probe.previewPaint, "Overlay preview painting escaped its owner-draw bounds");
                Expect(probe.dirtyState, "Overlay Apply state or default OK action did not track pending changes");
                const bool saved = apply || (probe.ok && !probe.cleanClose);
                Expect(result == saved && calls == (saved ? 1 : 0), "Overlay OK/Apply/Cancel saved or discarded the wrong draft");
                Expect(saved ? applied == ViewerOverlaySettings{} : applied == initial,
                    "Restore defaults did not remain a draft until Apply");
            }
        }

        struct MenuProbe
        {
            HWND viewer{};
            bool found{};
            bool valid{};
        };
        LRESULT CALLBACK ProbeMenu(HWND owner, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR reference)
        {
            auto& probe = *reinterpret_cast<MenuProbe*>(reference);
            if (message == WM_TIMER && wParam == id)
            {
                HMENU menu{};
                EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM data)
                {
                    wchar_t name[32]{};
                    GetClassNameW(candidate, name, 32);
                    if (std::wstring_view(name) != L"#32768") return TRUE;
                    const auto handle = reinterpret_cast<HMENU>(SendMessageW(candidate, MN_GETHMENU, 0, 0));
                    if (!handle || GetMenuState(handle, ViewerWindow::kContextMenuTogglePaneBase, MF_BYCOMMAND) == static_cast<UINT>(-1))
                        return TRUE;
                    *reinterpret_cast<HMENU*>(data) = handle;
                    return FALSE;
                }, reinterpret_cast<LPARAM>(&menu));
                if (menu)
                {
                    probe.found = true;
                    probe.valid = GetMenuState(menu, ViewerWindow::kContextMenuConfigureOverlays, MF_BYCOMMAND) != static_cast<UINT>(-1);
                    for (UINT i = 0; i < kOverlayPaneCount; ++i)
                    {
                        const UINT state = GetMenuState(menu, ViewerWindow::kContextMenuTogglePaneBase + i, MF_BYCOMMAND);
                        const bool expected = i != static_cast<UINT>(OverlayPane::FullMetadata);
                        probe.valid = probe.valid && state != static_cast<UINT>(-1) && !(state & (MF_DISABLED | MF_GRAYED))
                            && ((state & MF_CHECKED) != 0) == expected;
                    }
                    PostMessageW(probe.viewer, WM_CANCELMODE, 0, 0);
                }
            }
            return DefSubclassProc(owner, message, wParam, lParam);
        }

        void RunContextMenu(HWND owner, ViewerWindow& window)
        {
            MenuProbe probe{window.Hwnd()};
            constexpr UINT_PTR timer = 0x48424f4d;
            Expect(SetWindowSubclass(owner, ProbeMenu, timer, reinterpret_cast<DWORD_PTR>(&probe)) != FALSE,
                "Could not install overlay context menu probe");
            SetTimer(owner, timer, 30, nullptr);
            ViewerOverlayTestAccess::Menu(window);
            KillTimer(owner, timer);
            RemoveWindowSubclass(owner, ProbeMenu, timer);
            Expect(probe.found && probe.valid, "Context pane checkmarks or enabled states depend on master visibility");
        }

        struct SettingsProbe
        {
            DialogProbe dialog;
            int phase{};
        };
        LRESULT CALLBACK ProbeSettings(HWND owner, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR reference)
        {
            auto& probe = *reinterpret_cast<SettingsProbe*>(reference);
            if (message == WM_TIMER && wParam == id)
            {
                if (probe.phase == 0)
                {
                    if (const HWND settings = FindWindowW(L"HyperBrowseExperimentalSettingsDialog", nullptr))
                    {
                        probe.phase = 1;
                        SendMessageW(settings, WM_SYSKEYDOWN, L'V', 0);
                        SendMessageW(settings, WM_SYSKEYDOWN, L'C', 0);
                        SendMessageW(settings, WM_KEYDOWN, VK_SPACE, 0);
                        PostMessageW(settings, WM_COMMAND, MAKEWPARAM(5602, BN_CLICKED), 0);
                        probe.phase = 3;
                    }
                }
                else if (probe.phase == 1)
                {
                    ProbeDialog(owner, message, wParam, lParam, id, reinterpret_cast<DWORD_PTR>(&probe.dialog));
                    if (probe.dialog.found) probe.phase = 2;
                }
            }
            return DefSubclassProc(owner, message, wParam, lParam);
        }

        void RunPropagation(HINSTANCE instance, const fs::path& imagePath)
        {
            ui::MainWindow mainWindow(instance);
            Expect(mainWindow.Create(), "Unable to create settings propagation host");
            mainWindow.OpenViewerAtPath(imagePath.wstring());
            Expect(PumpMessagesUntil([] { return FindOpenViewerWindowHandles().size() >= 1; }, 10000),
                "Settings propagation primary viewer did not open");
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND,
                MAKEWPARAM(ui::command_ids::ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, 0), 0);
            Expect(PumpMessagesUntil([] { return FindOpenViewerWindowHandles().size() >= 2; }, 10000),
                "Settings propagation additional viewer did not open");
            const auto handles = FindOpenViewerWindowHandles();
            ViewerOverlaySettings initial;
            initial.masterVisible = false;
            initial.textSize = InfoOverlayTextSize::Large;
            initial.panes[3].windowedEnabled = initial.panes[3].fullScreenEnabled = true;
            for (const HWND handle : handles)
            {
                auto* viewer = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
                Expect(viewer != nullptr, "Settings propagation viewer has no owning instance");
                viewer->SetOverlaySettings(initial);
            }
            Expect(ViewerWindow::SetDefaultOverlaySettings(initial), "Could not persist initial overlay settings");
            auto* primary = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(handles.front(), GWLP_USERDATA));
            if (primary->IsFullScreen()) ViewerWindow::SetDefaultWindowedFullMetadataVisible(true);
            else ViewerWindow::SetDefaultFullScreenFullMetadataVisible(true);
            primary->SetFullMetadataVisible(true);
            Expect(ViewerWindow::DefaultWindowedFullMetadataVisible() && ViewerWindow::DefaultFullScreenFullMetadataVisible(),
                "Legacy metadata setter overwrote the other mode's newer persisted preference");
            primary->SetInfoOverlaysVisible(true);
            auto* other = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(handles.back(), GWLP_USERDATA));
            const bool wasFullScreen = other->IsFullScreen();
            SendMessageW(other->Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
            Expect(other->IsFullScreen() != wasFullScreen && !other->AreInfoOverlaysVisible()
                && primary->AreInfoOverlaysVisible(), "Viewer mode change adopted another viewer's master visibility");
            DialogProbe probe{true, false, false, false, false, false, false, GetTickCount64() + 5000};
            constexpr UINT_PTR timer = 0x48424f50;
            Expect(SetWindowSubclass(mainWindow.Hwnd(), ProbeDialog, timer, reinterpret_cast<DWORD_PTR>(&probe)) != FALSE,
                "Could not probe integrated configuration dialog");
            SetTimer(mainWindow.Hwnd(), timer, 30, nullptr);
            ViewerOverlayTestAccess::Command(*reinterpret_cast<ViewerWindow*>(
                GetWindowLongPtrW(handles.front(), GWLP_USERDATA)), ViewerWindow::kContextMenuConfigureOverlays);
            Expect(PumpMessagesUntil([&] { return probe.found; }, 5000),
                "Viewer context command did not open the integrated configuration dialog");
            KillTimer(mainWindow.Hwnd(), timer);
            RemoveWindowSubclass(mainWindow.Hwnd(), ProbeDialog, timer);
            Expect(probe.found && ViewerWindow::DefaultOverlaySettings() == ViewerOverlaySettings{},
                "Integrated configuration Apply did not persist its draft");
            SettingsProbe settingsProbe;
            settingsProbe.dialog.deadline = GetTickCount64() + 5000;
            Expect(SetWindowSubclass(mainWindow.Hwnd(), ProbeSettings, timer, reinterpret_cast<DWORD_PTR>(&settingsProbe)) != FALSE,
                "Could not probe Settings Viewer overlay entry");
            SetTimer(mainWindow.Hwnd(), timer, 30, nullptr);
            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ui::command_ids::ID_VIEW_SETTINGS, 0), 0);
            KillTimer(mainWindow.Hwnd(), timer);
            RemoveWindowSubclass(mainWindow.Hwnd(), ProbeSettings, timer);
            Expect(settingsProbe.dialog.found && ViewerWindow::DefaultOverlaySettings() == ViewerOverlaySettings{},
                "Settings Viewer Configure overlays entry failed or Cancel leaked draft changes");
            for (const HWND handle : handles)
            {
                const auto* viewer = reinterpret_cast<const ViewerWindow*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
                Expect(viewer && viewer->OverlaySettings() == ViewerOverlaySettings{},
                    "Configuration Apply did not refresh every open viewer");
                SendMessageW(handle, WM_CLOSE, 0, 0);
            }
            DestroyWindow(mainWindow.Hwnd());
            PumpMessagesFor(50);
        }
    }

    void RunViewerOverlayScenario(HINSTANCE instance, HWND ownerWindow)
    {
        RunLayoutAndPersistence();
        RunMatchingWidths();
        RunTextBounds();
        RunHistogramComputation();
        RunDialog(instance, ownerWindow);

        TempFolder root(L"HyperBrowseViewerOverlays");
        const auto first = root.Root() / L"first.png";
        const auto second = root.Root() / L"second.png";
        WriteTestImage(first, TestImageFormat::Png, 64, 32);
        WriteTestImage(second, TestImageFormat::Png, 40, 20);
        std::vector<browser::BrowserItem> items{
            {L"first.png", first.wstring(), L"PNG", L"", 1, 1, 64, 32},
            {L"second.png", second.wstring(), L"PNG", L"", 2, 2, 40, 20},
            {L"missing.png", (root.Root() / L"missing.png").wstring(), L"PNG", L"", 3, 3, 64, 32}};
        using Access = viewer::ViewerOverlayTestAccess;
        ViewerWindow window(instance);
        Expect(window.Open(ownerWindow, items, 0, false), "Unable to open histogram test viewer");
        Expect(PumpMessagesUntil([&] { return window.CurrentZoomPercent() > 0; }, 5000), "Histogram viewer did not decode");
        ViewerOverlaySettings settings;
        settings.panes[3].windowedEnabled = settings.panes[3].fullScreenEnabled = true;
        auto metadataSettings = settings;
        metadataSettings.panes[2].windowedEnabled = metadataSettings.panes[2].fullScreenEnabled = true;
        window.SetOverlaySettings(metadataSettings);
        Expect(Access::HasEllipsisTrimming(window),
            "Overlay text must visibly ellipsize long single-line labels");
        Expect(PumpMessagesUntil([&] { return !Access::Loading(window) && Access::Bins(window).visible; }, 5000),
            "Worker histogram did not complete");
        auto bins = Access::Bins(window);
        Expect(Access::HistogramText(window).empty(), "Ready histogram retained an unnecessary caption");
        Expect(PumpMessagesUntil([&] { return Access::MetadataReady(window); }, 5000),
            "Viewer metadata did not finish loading");
        Expect(Access::MetadataText(window).starts_with(
            L"Image Metadata\nName: first.png\nPath: " + first.parent_path().wstring() + L"\r\nType:"),
            "Metadata must label the name separately and show only the containing directory in Path");
        Expect(services::FormatImageInfoContent(items[0]).starts_with(L"Path: " + first.wstring() + L"\r\nType:"),
            "Image Information must retain the complete file path");
        Expect(Access::MetadataText(window).find(L"Full details:") == std::wstring::npos,
            "Metadata pane retained the removed image information shortcut hint");
        Expect(std::accumulate(bins.red.begin(), bins.red.end(), std::uint64_t{}) == 64 * 32
            && Access::Source(window) == Access::DisplaySource(window), "Viewer histogram did not scan full decoded source");
        window.ToggleFitActualSize();
        SendMessageW(window.Hwnd(), WM_KEYDOWN, L'R', 0);
        Expect(Access::Bins(window).red == bins.red, "Viewport transform changed histogram counts");
        Expect(Access::RejectStale(window) && Access::RejectWrongIdentity(window),
            "Viewer accepted stale histogram generation or content identity");
        const auto validImage = Access::DisplaySource(window);
        Access::Navigate(window, 2);
        Expect(PumpMessagesUntil([&] { return window.CurrentIndex() == 2 && !Access::ImageLoading(window); }, 5000)
            && Access::DisplaySource(window) == validImage, "Failed replacement discarded the last valid displayed image");
        Access::Navigate(window, 0);
        Expect(PumpMessagesUntil([&] { return window.CurrentIndex() == 0 && !Access::ImageLoading(window); }, 5000),
            "Histogram viewer did not recover after a failed replacement");
        settings.masterVisible = false;
        window.SetOverlaySettings(settings);
        Expect(Access::Layout(window)[3].visibility == OverlayVisibility::MasterHidden, "Master switch changed pane configuration");
        RunContextMenu(ownerWindow, window);
        const bool statusEnabled = window.OverlaySettings().panes[1].fullScreenEnabled;
        Access::Command(window, ViewerWindow::kContextMenuTogglePaneBase + 1);
        Expect(!window.AreInfoOverlaysVisible() && window.OverlaySettings().panes[1].fullScreenEnabled != statusEnabled,
            "Individual pane toggle was blocked by master visibility");
        settings.masterVisible = true;
        settings.panes[0].corner = OverlayCorner::BottomLeft;
        window.SetOverlaySettings(settings);
        const auto moved = Access::Layout(window)[0];
        const auto repaintBounds = Access::PartialBounds(window);
        Expect(moved.visibility != OverlayVisibility::Shown || EqualRect(&moved.rect, &repaintBounds),
            "Moved file pane retained stale partial repaint bounds");
        Expect(PumpMessagesUntil([&] { return !Access::Loading(window); }, 5000), "Histogram did not resume after master switch");
        Expect(window.BeginCompareSession({0, 1}), "Overlay comparison did not start");
        Expect(PumpMessagesUntil([&] { Access::Focus(window, 1); return Access::DisplaySource(window) != nullptr; }, 5000),
            "Focused comparison image did not decode");
        Expect(PumpMessagesUntil([&] { return !Access::Loading(window) && Access::Bins(window).visible; }, 5000),
            "Focused comparison histogram did not complete");
        bins = Access::Bins(window);
        metadataSettings = window.OverlaySettings();
        metadataSettings.panes[2].windowedEnabled = metadataSettings.panes[2].fullScreenEnabled = true;
        window.SetOverlaySettings(metadataSettings);
        Expect(PumpMessagesUntil([&]
        {
            return Access::MetadataReady(window) && !Access::Loading(window) && Access::Bins(window).visible;
        }, 5000), "Focused comparison metadata did not finish loading");
        Expect(Access::MetadataText(window).starts_with(
            L"Image Metadata\nName: second.png\nPath: " + second.parent_path().wstring() + L"\r\nType:"),
            "Comparison metadata Name and folder-only Path did not follow the focused image");
        Expect(Access::MetadataText(window).find(L"Full details:") == std::wstring::npos,
            "Comparison metadata retained the removed image information shortcut hint");
        Expect(std::accumulate(bins.red.begin(), bins.red.end(), std::uint64_t{}) == 40 * 20,
            "Comparison histogram did not follow focused tile");
        Expect(Access::RejectStale(window), "Compare focus accepted stale histogram result");
        window.SetDarkTheme(true);
        ui::HighContrastOverrideForTesting() = true;
        Expect(Access::PaintGdi(window), "GDI overlay painter failed with high contrast enabled");
        InvalidateRect(window.Hwnd(), nullptr, FALSE);
        UpdateWindow(window.Hwnd());
        ui::HighContrastOverrideForTesting().reset();

        std::promise<void> release;
        Expect(Access::BlockWorkers(window, release.get_future().share()), "Could not queue shutdown ownership coverage");
        window.SetOverlaySettings(settings);
        Expect(Access::Loading(window), "Histogram did not expose explicit loading state");
        Expect(Access::HistogramText(window) == L"Loading histogram...",
            "Caption removal also removed the histogram loading state");
        SendMessageW(window.Hwnd(), WM_CLOSE, 0, 0);
        release.set_value();
        PumpMessagesFor(50);
        Expect(!window.IsOpen(), "Viewer did not close with histogram work pending");
        RunPropagation(instance, first);
    }
}
