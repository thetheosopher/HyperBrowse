#include "browser/BrowserPaneInternal.h"

namespace
{
    constexpr int kPlaceholderTitlePointSize = 11;
    constexpr int kPlaceholderBodyPointSize = 8;

    HFONT CreateSystemUiFont(hyperbrowse::util::AppTextSize size, int dpi)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
        {
            metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
            metrics.lfMessageFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
            HDC screenDc = GetDC(nullptr);
            const int systemDpi = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
            if (screenDc)
            {
                ReleaseDC(nullptr, screenDc);
            }
            metrics.lfMessageFont.lfHeight = MulDiv(
                metrics.lfMessageFont.lfHeight,
                hyperbrowse::util::EffectiveTextDpi(dpi),
                (std::max)(1, systemDpi));
            metrics.lfMessageFont.lfHeight = static_cast<LONG>(std::lround(
                static_cast<double>(metrics.lfMessageFont.lfHeight)
                * hyperbrowse::util::AppTextSizeScale(size)));
            return CreateFontIndirectW(&metrics.lfMessageFont);
        }

        return nullptr;
    }

    HFONT CreateSizedUiFont(int pointSize, int weight, int dpi)
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

        logFont.lfHeight = -MulDiv(pointSize, hyperbrowse::util::EffectiveTextDpi(dpi), 72);
        logFont.lfWeight = weight;
        logFont.lfCharSet = DEFAULT_CHARSET;
        logFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        return CreateFontIndirectW(&logFont);
    }
}

namespace hyperbrowse::browser
{

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

    void BrowserPane::RebuildDetailsListFont()
    {
        if (!detailsList_)
        {
            return;
        }

        if (detailsListFont_ && ownsDetailsListFont_)
        {
            DeleteObject(detailsListFont_);
        }

        detailsListFont_ = CreateSystemUiFont(appTextSize_, BrowserTextDpi(hwnd_));
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
    }

    void BrowserPane::RebuildThumbnailFonts()
    {
        ReleaseThumbnailFonts();

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();

        thumbnailTitleFont_ = CreateSizedUiFont(layout.titlePointSize, FW_SEMIBOLD, layout.textDpi);
        folderTitleFont_ = CreateSizedUiFont(layout.titlePointSize, FW_SEMIBOLD, layout.textDpi);
        thumbnailMetaFont_ = CreateSizedUiFont(layout.metaPointSize, FW_NORMAL, layout.textDpi);
        thumbnailStatusFont_ = CreateSizedUiFont(layout.statusPointSize, FW_SEMIBOLD, layout.textDpi);
        placeholderTitleFont_ = CreateSizedUiFont(
            hyperbrowse::util::ScaleAppTextDimension(kPlaceholderTitlePointSize, appTextSize_),
            FW_SEMIBOLD,
            layout.textDpi);
        placeholderBodyFont_ = CreateSizedUiFont(
            hyperbrowse::util::ScaleAppTextDimension(kPlaceholderBodyPointSize, appTextSize_),
            FW_NORMAL,
            layout.textDpi);

        if (!thumbnailTitleFont_)
        {
            thumbnailTitleFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        if (!folderTitleFont_)
        {
            folderTitleFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
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
        if (folderTitleFont_ && folderTitleFont_ != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(folderTitleFont_);
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
        folderTitleFont_ = nullptr;
        thumbnailMetaFont_ = nullptr;
        thumbnailStatusFont_ = nullptr;
        placeholderTitleFont_ = nullptr;
        placeholderBodyFont_ = nullptr;
    }

    void BrowserPane::NotifyStateChanged()
    {
        if (parent_ && !stateChangedMessagePending_)
        {
            stateChangedMessagePending_ = true;
            if (!PostMessageW(parent_, kStateChangedMessage, reinterpret_cast<WPARAM>(hwnd_), 0))
            {
                stateChangedMessagePending_ = false;
            }
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
            return performanceHudText_.empty()
                ? CDRF_NOTIFYITEMDRAW
                : CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
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
        case CDDS_POSTPAINT:
            DrawDetailsListPerformanceHud(customDraw->nmcd.hdc);
            return CDRF_DODEFAULT;
        default:
            return CDRF_DODEFAULT;
        }
    }

    void BrowserPane::DrawDetailsListPerformanceHud(HDC hdc) const
    {
        if (!hdc || !detailsList_ || performanceHudText_.empty())
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(detailsList_, &clientRect);
        const int clientWidth = static_cast<int>(clientRect.right);
        const int clientHeight = static_cast<int>(clientRect.bottom);
        const int padding = hyperbrowse::util::ScaleAppTextDimension(10, appTextSize_);
        const int panelWidth = std::min(hyperbrowse::util::ScaleAppTextDimension(280, appTextSize_),
                        std::max(0, clientWidth - (padding * 2)));
        const int panelHeight = (padding * 2)
            + (hyperbrowse::util::ScaleAppTextDimension(17, appTextSize_) * 5);
        RECT panelRect{
            std::max(padding, clientWidth - padding - panelWidth),
            padding,
            clientWidth - padding,
            std::min(clientHeight - padding, padding + panelHeight),
        };
        if (panelRect.right <= panelRect.left || panelRect.bottom <= panelRect.top)
        {
            return;
        }

        HDC sourceDc = CreateCompatibleDC(hdc);
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = 1;
        bitmapInfo.bmiHeader.biHeight = -1;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* bitmapBits = nullptr;
        HBITMAP overlayBitmap = CreateDIBSection(hdc,
                                                 &bitmapInfo,
                                                 DIB_RGB_COLORS,
                                                 &bitmapBits,
                                                 nullptr,
                                                 0);
        if (sourceDc && overlayBitmap && bitmapBits)
        {
            const DWORD pixel = (static_cast<DWORD>(GetRValue(colors_.windowBackground)) << 16)
                | (static_cast<DWORD>(GetGValue(colors_.windowBackground)) << 8)
                | static_cast<DWORD>(GetBValue(colors_.windowBackground));
            *static_cast<DWORD*>(bitmapBits) = pixel;
            const HGDIOBJ previousBitmap = SelectObject(sourceDc, overlayBitmap);
            const BLENDFUNCTION blend{AC_SRC_OVER, 0, 224, 0};
            AlphaBlend(hdc,
                       panelRect.left,
                       panelRect.top,
                       panelRect.right - panelRect.left,
                       panelRect.bottom - panelRect.top,
                       sourceDc,
                       0,
                       0,
                       1,
                       1,
                       blend);
            SelectObject(sourceDc, previousBitmap);
        }
        else
        {
            HBRUSH panelBrush = CreateSolidBrush(colors_.windowBackground);
            FillRect(hdc, &panelRect, panelBrush);
            DeleteObject(panelBrush);
        }

        if (overlayBitmap)
        {
            DeleteObject(overlayBitmap);
        }
        if (sourceDc)
        {
            DeleteDC(sourceDc);
        }

        HBRUSH borderBrush = CreateSolidBrush(colors_.border);
        FrameRect(hdc, &panelRect, borderBrush);
        DeleteObject(borderBrush);

        const int savedDc = SaveDC(hdc);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, colors_.text);
        const HFONT font = detailsListFont_
            ? detailsListFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ previousFont = SelectObject(hdc, font);
        InflateRect(&panelRect, -padding, -padding);
        DrawTextW(hdc,
                  performanceHudText_.c_str(),
                  static_cast<int>(performanceHudText_.size()),
                  &panelRect,
                  DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_END_ELLIPSIS);
        if (previousFont)
        {
            SelectObject(hdc, previousFont);
        }
        RestoreDC(hdc, savedDc);
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

        const auto& items = model_->Items();
        selectedModelIndices_.clear();
        selectedModelIndices_.reserve(orderedModelIndices_.size());
        selectedBytes_ = 0;
        for (const int modelIndex : orderedModelIndices_)
        {
            selectedModelIndices_.insert(modelIndex);
            if (modelIndex >= 0 && modelIndex < static_cast<int>(items.size()))
            {
                selectedBytes_ += items[static_cast<std::size_t>(modelIndex)].fileSizeBytes;
            }
        }

        if (focusedModelIndex_ < 0 || !selectedModelIndices_.contains(focusedModelIndex_))
        {
            focusedModelIndex_ = orderedModelIndices_.front();
        }
        if (anchorModelIndex_ < 0 || !selectedModelIndices_.contains(anchorModelIndex_))
        {
            anchorModelIndex_ = focusedModelIndex_;
        }

        if (detailsList_ && viewMode_ == BrowserViewMode::Details)
        {
            syncingDetailsSelection_ = true;
            SendMessageW(detailsList_, WM_SETREDRAW, FALSE, 0);
            ListView_SetItemState(detailsList_, -1, LVIS_SELECTED, LVIS_SELECTED);
            ListView_SetItemState(detailsList_, -1, 0, LVIS_FOCUSED);

            const int focusedViewIndex = ViewIndexFromModelIndex(focusedModelIndex_);
            if (focusedViewIndex >= 0)
            {
                ListView_SetItemState(detailsList_, focusedViewIndex, LVIS_FOCUSED, LVIS_FOCUSED);
            }

            SendMessageW(detailsList_, WM_SETREDRAW, TRUE, 0);
            syncingDetailsSelection_ = false;
            InvalidateRect(detailsList_, nullptr, FALSE);
        }
        else
        {
            SyncDetailsListSelectionFromModel();
        }

        NotifyStateChanged();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}
