#include "ui/MainWindowInternal.h"

namespace
{
    constexpr int kDetailsPanelCloseButtonSize = 18;
    constexpr int kDetailsPanelCloseButtonMargin = 8;
    constexpr int kDetailsPanelCloseButtonGap = 8;
    constexpr int kBreadcrumbBarHeightDip = 32;

    constexpr int kToolbarItemSize = 32;
    constexpr int kStatusStripHeight = 28;
    constexpr int kDetailsPanelMargin = 14;
    constexpr int kDetailsPanelTabHeight = 30;
    constexpr int kDetailsPanelTabGap = 10;
    constexpr int kDetailsPanelTabButtonGap = 8;
    constexpr int kDetailsPanelTabButtonHorizontalPadding = 16;
    constexpr int kDetailsPanelTabMinButtonWidth = 96;
    constexpr int kDetailsPanelHistogramHeight = 88;
    constexpr int kDetailsPanelTextTopGap = 14;
}

namespace hyperbrowse::ui
{

    void MainWindow::LayoutChildren()
    {
        if (!hwnd_ || !treePane_ || !browserPane_ || !statusBar_)
        {
            return;
        }

        const int previousLeftPaneWidth = leftPaneWidth_;
        const RECT previousDetailsPanelRect = detailsPanelRect_;
        RECT client{};
        GetClientRect(hwnd_, &client);
        const MenuMetrics menuMetrics = MakeMenuMetrics(
            appTextSize_,
            hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u);
        actionStripHeight_ = menuMetrics.CommandBarHeight();

        const int statusHeight = std::min(kStatusStripHeight, std::max(0, static_cast<int>(client.bottom - client.top) - actionStripHeight_));

        const int clientWidth = client.right - client.left;
        const int detailsSplitterWidth = detailsStripVisible_ ? kSplitterWidth : 0;
        const int maxDetailsPanelWidth = detailsStripVisible_
            ? std::max(0, clientWidth - kMinLeftPaneWidth - kMinRightPaneWidth - kSplitterWidth - detailsSplitterWidth)
            : 0;
        const int minDetailsPanelWidth = detailsStripVisible_ ? std::min(kDetailsPanelMinWidth, maxDetailsPanelWidth) : 0;
        const int desiredDetailsPanelWidth = detailsStripVisible_
            ? std::clamp(detailsPanelWidth_, minDetailsPanelWidth, maxDetailsPanelWidth)
            : 0;
        const int clientHeight = std::max(0, static_cast<int>(client.bottom - client.top) - statusHeight - actionStripHeight_);
        const int contentTop = actionStripHeight_;
        const int breadcrumbHeight = breadcrumbSegments_.empty() ? 0 : menuMetrics.ScaleDip(kBreadcrumbBarHeightDip);
        const int browserContentHeight = std::max(0, clientHeight - breadcrumbHeight);

        const int maxLeft = std::max(kMinLeftPaneWidth,
                                     clientWidth - desiredDetailsPanelWidth - kMinRightPaneWidth - kSplitterWidth - detailsSplitterWidth);
        leftPaneWidth_ = std::clamp(leftPaneWidth_, kMinLeftPaneWidth, maxLeft);

        const int browserWidth = std::max(kMinRightPaneWidth,
                                          clientWidth - leftPaneWidth_ - kSplitterWidth - detailsSplitterWidth - desiredDetailsPanelWidth);
        const int rightContentLeft = leftPaneWidth_ + kSplitterWidth;
        const int rightContentWidth = std::max(0, clientWidth - rightContentLeft);
        const int detailsPanelWidth = detailsStripVisible_
            ? std::max(0, clientWidth - leftPaneWidth_ - kSplitterWidth - browserWidth - detailsSplitterWidth)
            : 0;

        MoveWindow(statusBar_,
               0,
               std::max(0, static_cast<int>(client.bottom) - statusHeight),
               clientWidth,
               statusHeight,
               TRUE);

        MoveWindow(treePane_, 0, contentTop, leftPaneWidth_, clientHeight, TRUE);
        LayoutBreadcrumbBar(rightContentLeft, contentTop, rightContentWidth, breadcrumbHeight);
        MoveWindow(browserPane_, rightContentLeft, contentTop + breadcrumbHeight,
               browserWidth, browserContentHeight, TRUE);

        detailsPanelRect_ = RECT{leftPaneWidth_ + kSplitterWidth + browserWidth + detailsSplitterWidth,
                     contentTop + breadcrumbHeight,
                                 clientWidth,
                     contentTop + clientHeight};
        detailsPanelTabStripRect_ = RECT{};
        detailsPanelTabRects_ = {};
        detailsPanelContentRect_ = RECT{};
        detailsPanelHistogramRect_ = RECT{};
        detailsPanelCloseButtonRect_ = RECT{};
        quickAccessSortButtonRect_ = RECT{};
        quickAccessSortButtonHot_ = false;
        quickAccessSortButtonPressed_ = false;
        UpdateQuickAccessSortTooltip();
        quickAccessDestinationPanelRect_ = RECT{};
        quickAccessDestinationViewportRect_ = RECT{};
        quickAccessDestinationRows_.clear();
        if (quickAccessScrollBar_)
        {
            ShowWindow(quickAccessScrollBar_, SW_HIDE);
        }
        HideQuickAccessShortcutEditControls();
        quickAccessHotRowIndex_ = -1;
        quickAccessHotButtonIndex_ = -1;
        quickAccessPressedRowIndex_ = -1;
        quickAccessPressedButtonIndex_ = -1;
        const bool quickAccessPanelActive = detailsStripVisible_ && activeRightPaneTab_ == RightPaneTab::QuickSend;
        if (!quickAccessPanelActive)
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
        }

        if (detailsPanelText_)
        {
            if (detailsStripVisible_ && detailsPanelWidth > 0)
            {
                const int innerLeft = detailsPanelRect_.left + kDetailsPanelMargin;
                const int innerRight = detailsPanelRect_.right - kDetailsPanelMargin;
                const int innerWidth = std::max(0, innerRight - innerLeft);

                const HFONT tabFont = detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                const int maxLabelWidth = std::max({MeasureTextWidth(tabFont, L"File Details"),
                                                    MeasureTextWidth(tabFont, L"Quick Actions"),
                                                    MeasureTextWidth(tabFont, L"Cache Stats")});
                DetailsPanelLayout::Input layoutInput;
                layoutInput.panelRect = detailsPanelRect_;
                layoutInput.margin = kDetailsPanelMargin;
                layoutInput.tabHeight = std::max(
                    menuMetrics.ScaleDip(kDetailsPanelTabHeight),
                    MeasureSingleLineTextHeight(tabFont, 0) + menuMetrics.ScaleDip(8));
                layoutInput.tabGap = menuMetrics.ScaleDip(kDetailsPanelTabGap);
                layoutInput.tabButtonGap = menuMetrics.ScaleDip(kDetailsPanelTabButtonGap);
                layoutInput.tabButtonHorizontalPadding = menuMetrics.ScaleDip(kDetailsPanelTabButtonHorizontalPadding);
                layoutInput.tabMinButtonWidth = menuMetrics.ScaleDip(kDetailsPanelTabMinButtonWidth);
                layoutInput.closeButtonSize = menuMetrics.ScaleDip(kDetailsPanelCloseButtonSize);
                layoutInput.closeButtonMargin = menuMetrics.ScaleDip(kDetailsPanelCloseButtonMargin);
                layoutInput.closeButtonGap = menuMetrics.ScaleDip(kDetailsPanelCloseButtonGap);
                layoutInput.tabLabelWidth = maxLabelWidth;
                layoutInput.fileDetailsActive = activeRightPaneTab_ == RightPaneTab::FileDetails;

                if (layoutInput.fileDetailsActive)
                {
                    const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
                    layoutInput.titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                                      title,
                                                                      innerWidth,
                                                                      DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                                      menuMetrics.ScaleDip(22));
                    layoutInput.summaryHeight = detailsPanelSummaryText_.empty()
                        ? 0
                        : MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                                 detailsPanelSummaryText_,
                                                 innerWidth,
                                                 DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                 menuMetrics.ScaleDip(18));
                    layoutInput.histogramHeight = menuMetrics.ScaleDip(kDetailsPanelHistogramHeight);
                    layoutInput.textTopGap = menuMetrics.ScaleDip(kDetailsPanelTextTopGap);
                    layoutInput.histogramVisible = detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_;
                }

                const DetailsPanelLayout::Result layout = DetailsPanelLayout::Build(layoutInput);
                detailsPanelTabStripRect_ = layout.tabStripRect;
                detailsPanelTabRects_ = layout.tabRects;
                detailsPanelContentRect_ = layout.contentRect;
                detailsPanelHistogramRect_ = layout.histogramRect;
                detailsPanelCloseButtonRect_ = layout.closeButtonRect;

                if (detailsPanelContentRect_.right > detailsPanelContentRect_.left
                    && detailsPanelContentRect_.bottom > detailsPanelContentRect_.top)
                {
                    if (layoutInput.fileDetailsActive)
                    {
                        MoveWindow(detailsPanelText_,
                                   layout.textRect.left,
                                   layout.textRect.top,
                                   layout.textRect.right - layout.textRect.left,
                                   layout.textRect.bottom - layout.textRect.top,
                                   TRUE);
                        ShowWindow(detailsPanelText_, SW_SHOW);

                    }
                    else if (activeRightPaneTab_ == RightPaneTab::QuickSend)
                    {
                        RebuildQuickAccessDestinationRows(detailsPanelContentRect_.left,
                                                         detailsPanelContentRect_.right,
                                                         detailsPanelContentRect_.top);
                        ShowWindow(detailsPanelText_, SW_HIDE);
                    }
                    else
                    {
                        ShowWindow(detailsPanelText_, SW_HIDE);
                        if (quickAccessScrollBar_)
                        {
                            ShowWindow(quickAccessScrollBar_, SW_HIDE);
                        }
                        HideQuickAccessShortcutEditControls();
                    }
                }
                else
                {
                    detailsPanelContentRect_ = RECT{};
                    ShowWindow(detailsPanelText_, SW_HIDE);
                    if (quickAccessScrollBar_)
                    {
                        ShowWindow(quickAccessScrollBar_, SW_HIDE);
                    }
                    HideQuickAccessShortcutEditControls();
                }
            }
            else
            {
                ShowWindow(detailsPanelText_, SW_HIDE);
                detailsPanelHotTabIndex_ = -1;
                detailsPanelPressedTabIndex_ = -1;
                detailsPanelRect_ = RECT{};
                detailsPanelTabStripRect_ = RECT{};
                detailsPanelTabRects_ = {};
                detailsPanelContentRect_ = RECT{};
                quickAccessDestinationPanelRect_ = RECT{};
                quickAccessDestinationRows_.clear();
            }
        }

        if (tooltipControl_ && detailsPanelHistogramTooltipAdded_)
        {
            TTTOOLINFOW toolInfo{};
            toolInfo.cbSize = sizeof(toolInfo);
            toolInfo.hwnd = hwnd_;
            toolInfo.uId = kDetailsPanelHistogramTooltipId;
            toolInfo.rect = detailsPanelHistogramRect_;
            SendMessageW(tooltipControl_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        }

        LayoutToolbar(menuMetrics);

        RECT splitterRect{leftPaneWidth_, actionStripHeight_, leftPaneWidth_ + kSplitterWidth, client.bottom};
        InvalidateRect(hwnd_, &splitterRect, FALSE);
        if (previousLeftPaneWidth != leftPaneWidth_)
        {
            RECT previousSplitterRect{previousLeftPaneWidth,
                                      actionStripHeight_,
                                      previousLeftPaneWidth + kSplitterWidth,
                                      client.bottom};
            InvalidateRect(hwnd_, &previousSplitterRect, FALSE);
        }
        if (detailsStripVisible_)
        {
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                RECT detailsSplitterRect{detailsPanelRect_.left - kSplitterWidth,
                                         actionStripHeight_,
                                         detailsPanelRect_.left,
                                         client.bottom};
                InvalidateRect(hwnd_, &detailsSplitterRect, FALSE);
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }

            if (!IsRectEmpty(&previousDetailsPanelRect))
            {
                RECT previousDetailsSplitterRect{previousDetailsPanelRect.left - kSplitterWidth,
                                                 actionStripHeight_,
                                                 previousDetailsPanelRect.left,
                                                 client.bottom};
                InvalidateRect(hwnd_, &previousDetailsSplitterRect, FALSE);
                InvalidateRect(hwnd_, &previousDetailsPanelRect, FALSE);
            }
        }

        UpdateStatusText();
        LayoutQuickSendConfirmationToast();
        if (accessibility_)
        {
            accessibility_->NotifyStateChanged(CHILDID_SELF);
        }
    }

    void MainWindow::UpdateStatusText()
    {
        if (!statusBar_)
        {
            return;
        }

        const std::uint64_t folderCount = browserModel_ && !browserModel_->FolderPath().empty()
            ? browserModel_->TotalCount()
            : 0;
        const std::uint64_t folderBytes = browserModel_ && !browserModel_->FolderPath().empty()
            ? browserModel_->TotalBytes()
            : 0;
        const bool hasActiveFilter = browserPaneController_ && browserPaneController_->HasActiveFilter();
        const std::uint64_t displayedCount = hasActiveFilter
            ? browserPaneController_->DisplayedItemCount()
            : folderCount;
        statusPrimaryText_ = L"Folder: " + std::to_wstring(displayedCount)
            + (hasActiveFilter ? L" of " + std::to_wstring(folderCount) : L"")
            + (showSubfoldersInBrowser_ ? L" items | " : L" files | ")
            + browser::FormatByteSize(folderBytes);
        if (savedSearchController_)
        {
            if (savedSearchController_->Busy())
                statusPrimaryText_ = L"Updating saved searches...  |  " + statusPrimaryText_;
            else if (!savedSearchController_->LastError().empty())
                statusPrimaryText_ = L"Saved searches: " + savedSearchController_->LastError() + L"  |  " + statusPrimaryText_;
            else if (savedSearchController_->Active())
                statusPrimaryText_ = L"Search: " + savedSearchController_->Active()->name + L"  |  " + statusPrimaryText_;
        }
        const std::uint64_t navigationCount = browserPaneController_
            ? browserPaneController_->DisplayedItemCount()
            : 0;
        if (browserPaneController_ && navigationCount > 0)
        {
            statusPrimaryText_.append(L"  |  Item: ");
            statusPrimaryText_.append(std::to_wstring(browserPaneController_->CurrentItemNumber()));
            statusPrimaryText_.append(L" / ");
            statusPrimaryText_.append(std::to_wstring(navigationCount));
        }

        if (fileOperationActive_ && !activeFileOperationLabel_.empty())
        {
            statusPrimaryText_ = activeFileOperationLabel_ + L"  |  " + statusPrimaryText_;
        }
        else if (!quickSendConfirmationText_.empty())
        {
            statusPrimaryText_ = L"Quick Actions: " + quickSendConfirmationText_ + L"  |  " + statusPrimaryText_;
        }
        else if (batchConvertActive_)
        {
            statusPrimaryText_ = (batchConvertCancelling_ ? L"Cancelling conversion: " : L"Converting: ")
                + std::to_wstring(batchConvertCompleted_)
                + L" / "
                + std::to_wstring(batchConvertTotal_)
                + L"  |  "
                + statusPrimaryText_;
        }
        else if (imageCommandActive_ && !imageCommandActivityLabel_.empty())
        {
            statusPrimaryText_ = imageCommandActivityLabel_ + L" (Esc to cancel)  |  " + statusPrimaryText_;
        }

        const std::uint64_t selectedCount = browserPaneController_ ? browserPaneController_->SelectedCount() : 0;
        const std::uint64_t selectedBytes = browserPaneController_ ? browserPaneController_->SelectedBytes() : 0;
        statusSecondaryText_ = L"Selected: " + std::to_wstring(selectedCount)
            + L" items | " + browser::FormatByteSize(selectedBytes);
        if (rawJpegPairedOperationsEnabled_)
        {
            std::size_t pairedCompanionCount = 0;
            SelectedFileOperationPathsSnapshot(&pairedCompanionCount);
            statusSecondaryText_.append(L"  |  Paired RAW+JPEG On");
            if (pairedCompanionCount > 0)
            {
                statusSecondaryText_.append(L" (+");
                statusSecondaryText_.append(std::to_wstring(pairedCompanionCount));
                statusSecondaryText_.push_back(L')');
            }
        }
        statusSecondaryText_.append(L"  |  Profile: ");
        statusSecondaryText_.append(util::ResourceProfileToDisplayName(resourceProfile_));
        if (thumbnailCacheCapacityOverrideBytes_ != 0 || metadataCacheCapacityOverrideEntries_ != 0)
        {
            statusSecondaryText_.append(L" (Custom cache caps)");
        }
        if (showPressureStateInStatusBar_)
        {
            statusSecondaryText_.append(L"  |  Pressure: ");
            statusSecondaryText_.append(thumbnailMemoryPressureActive_ ? L"Adaptive throttling active" : L"Normal");
        }
        if (viewerWindowActive_ && viewerZoomPercent_ > 0)
        {
            statusSecondaryText_.append(L"  |  Viewer zoom: ");
            statusSecondaryText_.append(std::to_wstring(viewerZoomPercent_));
            statusSecondaryText_.push_back(L'%');
        }

        const bool folderTreeEnumerationActive = folderTreeController_ && folderTreeController_->IsBusy();
        const bool folderEnumerationActive = folderLoadCoordinator_
            && folderLoadCoordinator_->IsEnumerationActive();
        if (folderEnumerationActive || folderTreeEnumerationActive)
        {
            const wchar_t* activityText = folderEnumerationActive && folderTreeEnumerationActive
                ? L"Scanning folders...  |  "
                : (folderEnumerationActive ? L"Scanning folder...  |  " : L"Probing folders...  |  ");
            statusPrimaryText_ = activityText + statusPrimaryText_;
        }

        InvalidateRect(statusBar_, nullptr, TRUE);
    }

    void MainWindow::ShowQuickSendConfirmation(std::wstring message, HWND viewerHwnd)
    {
        if (message.empty())
        {
            return;
        }

        if (viewer::ViewerWindow* viewer = FindViewerByHwnd(viewerHwnd))
        {
            if (viewer->IsOpen())
            {
                viewer->ShowQuickSendConfirmation(std::move(message));
                return;
            }
        }

        if (quickSendConfirmationTimerId_ != 0)
        {
            KillTimer(hwnd_, quickSendConfirmationTimerId_);
        }
        quickSendConfirmationText_ = std::move(message);
        if (!quickSendConfirmationToastWindow_ && hwnd_)
        {
            quickSendConfirmationToastWindow_ = CreateWindowExW(
                WS_EX_NOACTIVATE,
                kQuickSendConfirmationToastWindowClassName,
                nullptr,
                WS_CHILD | WS_CLIPSIBLINGS,
                0,
                0,
                0,
                0,
                hwnd_,
                nullptr,
                instance_,
                this);
            if (!quickSendConfirmationToastWindow_)
            {
                util::LogLastError(L"CreateWindowExW(QuickSendConfirmationToast)");
            }
        }
        if (quickSendConfirmationToastWindow_)
        {
            SetWindowTextW(quickSendConfirmationToastWindow_, quickSendConfirmationText_.c_str());
            NotifyWinEvent(EVENT_SYSTEM_ALERT,
                           quickSendConfirmationToastWindow_,
                           OBJID_WINDOW,
                           CHILDID_SELF);
        }
        LayoutQuickSendConfirmationToast();
        quickSendConfirmationTimerId_ = SetTimer(hwnd_,
                                                 kQuickSendConfirmationTimerId,
                                                 kQuickSendConfirmationDurationMs,
                                                 nullptr);
        if (quickSendConfirmationTimerId_ == 0)
        {
            quickSendConfirmationText_.clear();
            if (quickSendConfirmationToastWindow_)
            {
                ShowWindow(quickSendConfirmationToastWindow_, SW_HIDE);
            }
        }
        UpdateStatusText();
    }

    void MainWindow::LayoutQuickSendConfirmationToast()
    {
        if (!quickSendConfirmationToastWindow_ || !IsWindow(quickSendConfirmationToastWindow_))
        {
            return;
        }

        if (quickSendConfirmationText_.empty())
        {
            ShowWindow(quickSendConfirmationToastWindow_, SW_HIDE);
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientWidth = std::max(0, static_cast<int>(client.right - client.left));
        const int clientHeight = std::max(0, static_cast<int>(client.bottom - client.top));
        const int statusHeight = std::min(kStatusStripHeight,
                                          std::max(0, clientHeight - actionStripHeight_));
        const int contentHeight = std::max(0, clientHeight - statusHeight - actionStripHeight_);
        if (clientWidth <= 0 || contentHeight <= 0)
        {
            ShowWindow(quickSendConfirmationToastWindow_, SW_HIDE);
            return;
        }

        const int dpi = static_cast<int>(std::max<UINT>(96, GetDpiForWindow(hwnd_)));
        const auto scaleForDpi = [dpi](int value)
        {
            return MulDiv(value, dpi, 96);
        };
        const int toastWidth = std::min(scaleForDpi(720),
                                        std::max(scaleForDpi(120), clientWidth - scaleForDpi(32)));
        const int requestedToastHeight = scaleForDpi(
            hyperbrowse::util::ScaleAppTextDimension(64, appTextSize_));
        const int toastHeight = std::min(requestedToastHeight, contentHeight);
        const int toastLeft = std::max(scaleForDpi(8), (clientWidth - toastWidth) / 2);
        const int toastTop = actionStripHeight_
            + std::min(scaleForDpi(16), std::max(0, contentHeight - toastHeight));
        const int cornerRadius = std::min(scaleForDpi(16), std::max(2, toastHeight / 2));
        HRGN region = CreateRoundRectRgn(0,
                                         0,
                                         toastWidth + 1,
                                         toastHeight + 1,
                                         cornerRadius,
                                         cornerRadius);
        if (region)
        {
            if (!SetWindowRgn(quickSendConfirmationToastWindow_, region, TRUE))
            {
                DeleteObject(region);
            }
        }

        SetWindowPos(quickSendConfirmationToastWindow_,
                     HWND_TOP,
                     toastLeft,
                     toastTop,
                     toastWidth,
                     toastHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(quickSendConfirmationToastWindow_, nullptr, FALSE);
    }

    void MainWindow::PaintQuickSendConfirmationToast(HDC hdc) const
    {
        if (!hdc || quickSendConfirmationText_.empty())
        {
            return;
        }

        RECT client{};
        GetClientRect(quickSendConfirmationToastWindow_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0)
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        const int dpi = static_cast<int>(std::max<UINT>(96, GetDpiForWindow(hwnd_)));
        const float textPadding = static_cast<float>(MulDiv(12, dpi, 96));
        auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
        const HFONT textFont = detailsPanelSummaryFont_
            ? detailsPanelSummaryFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (renderer.IsAvailable())
        {
            const auto renderTarget = renderer.CreateDCRenderTarget();
            const auto textFormat = renderer.CreateTextFormatFromFont(textFont);
            if (renderTarget && textFormat && SUCCEEDED(renderTarget->BindDC(hdc, &client)))
            {
                const auto createBrush = [renderTarget](COLORREF color)
                {
                    hyperbrowse::render::ComPtr<ID2D1SolidColorBrush> brush;
                    renderTarget->CreateSolidColorBrush(
                        hyperbrowse::render::ToD2DColor(color),
                        brush.GetAddressOf());
                    return brush;
                };
                const auto fillBrush = createBrush(palette.paneBackground);
                const auto borderBrush = createBrush(palette.actionStripBorder);
                const auto textBrush = createBrush(palette.text);
                if (fillBrush && borderBrush && textBrush)
                {
                    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                    const D2D1_RECT_F toastRect = D2D1::RectF(0.0f,
                                                               0.0f,
                                                               static_cast<float>(width),
                                                               static_cast<float>(height));
                    const D2D1_ROUNDED_RECT roundedToast = D2D1::RoundedRect(toastRect, 8.0f, 8.0f);
                    renderTarget->BeginDraw();
                    renderTarget->FillRoundedRectangle(roundedToast, fillBrush.Get());
                    renderTarget->DrawRoundedRectangle(roundedToast, borderBrush.Get(), 1.0f);
                    const auto textLayout = renderer.CreateTextLayout(
                        quickSendConfirmationText_,
                        textFormat.Get(),
                        std::max(1.0f, static_cast<float>(width) - (textPadding * 2.0f)),
                        static_cast<float>(height));
                    if (textLayout)
                    {
                        const DWRITE_TRIMMING trimming{
                            DWRITE_TRIMMING_GRANULARITY_WORD,
                            0,
                            0};
                        Microsoft::WRL::ComPtr<IDWriteInlineObject> trimmingSign;
                        if (renderer.DWriteFactory())
                        {
                            renderer.DWriteFactory()->CreateEllipsisTrimmingSign(
                                textFormat.Get(),
                                trimmingSign.GetAddressOf());
                        }
                        textLayout->SetTrimming(&trimming, trimmingSign.Get());
                        renderTarget->DrawTextLayout(
                            D2D1::Point2F(textPadding, 0.0f),
                            textLayout.Get(),
                            textBrush.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    if (SUCCEEDED(renderTarget->EndDraw()))
                    {
                        return;
                    }
                }
            }
        }

        const HBRUSH toastBrush = CreateSolidBrush(palette.paneBackground);
        const HPEN toastPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        const HGDIOBJ oldBrush = SelectObject(hdc, toastBrush);
        const HGDIOBJ oldPen = SelectObject(hdc, toastPen);
        RoundRect(hdc, 0, 0, width, height, 12, 12);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(toastPen);
        DeleteObject(toastBrush);

        render::DrawGdiText(hdc,
                            textFont,
                            quickSendConfirmationText_.c_str(),
                            static_cast<int>(quickSendConfirmationText_.size()),
                            RECT{static_cast<LONG>(textPadding), 0,
                                 width - static_cast<LONG>(textPadding), height},
                            DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS,
                            palette.text,
                            palette.paneBackground);
    }

    void MainWindow::DrawStatusStrip(const DRAWITEMSTRUCT& drawItem) const
    {
        const ThemePalette palette = GetThemePalette();
        const StatusBarPainterPalette painterPalette{
            BlendColor(palette.paneBackground,
                       palette.windowBackground,
                       themeMode_ == ThemeMode::Dark ? 34 : 18),
            palette.actionStripBorder,
            palette.text,
            palette.mutedText};
        if (StatusBarPainter::PaintD2D(drawItem.hDC,
                                       drawItem.rcItem,
                                       painterPalette,
                                       detailsPanelSummaryFont_,
                                       statusPrimaryText_,
                                       statusSecondaryText_))
        {
            return;
        }
        StatusBarPainter::PaintGdi(drawItem.hDC,
                                   drawItem.rcItem,
                                   painterPalette,
                                   detailsPanelSummaryFont_,
                                   statusPrimaryText_,
                                   statusSecondaryText_);
    }

    void MainWindow::UpdateWindowTitle()
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
        UpdateBreadcrumbBar();
    }

    void MainWindow::UpdateBreadcrumbBar()
    {
        if (!hwnd_)
        {
            return;
        }

        const std::wstring folderPath = browserModel_ ? browserModel_->FolderPath() : std::wstring{};
        if (folderPath == breadcrumbFolderPath_)
        {
            return;
        }

        const HWND focusedWindow = GetFocus();
        bool breadcrumbHadFocus = focusedWindow == breadcrumbMoreButton_
            || std::find(breadcrumbSegmentButtons_.begin(), breadcrumbSegmentButtons_.end(), focusedWindow)
                != breadcrumbSegmentButtons_.end();
        breadcrumbFolderPath_ = folderPath;
        breadcrumbSegments_ = BuildFolderBreadcrumbSegments(folderPath);

        while (breadcrumbSegmentButtons_.size() > breadcrumbSegments_.size())
        {
            const HWND button = breadcrumbSegmentButtons_.back();
            if (button && IsWindow(button) != FALSE)
            {
                DestroyWindow(button);
            }
            breadcrumbSegmentButtons_.pop_back();
        }

        if (breadcrumbSegments_.empty())
        {
            if (breadcrumbMoreButton_ && IsWindow(breadcrumbMoreButton_) != FALSE)
            {
                DestroyWindow(breadcrumbMoreButton_);
            }
            breadcrumbMoreButton_ = nullptr;
            LayoutChildren();
            if (breadcrumbHadFocus && hwnd_ && IsWindow(hwnd_) != FALSE)
            {
                SetFocus(hwnd_);
            }
            return;
        }

        const auto createBreadcrumbButton = [this](UINT controlId, const wchar_t* accessibleName)
        {
            const HWND button = CreateWindowExW(
                0,
                L"BUTTON",
                accessibleName,
                WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY,
                0,
                0,
                0,
                0,
                hwnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                instance_,
                nullptr);
            if (button)
            {
                SetWindowSubclass(button, &MainWindow::BreadcrumbButtonSubclassProc, 1, 0);
            }
            return button;
        };

        if (breadcrumbSegmentButtons_.empty())
        {
            breadcrumbSegmentButtons_.push_back(nullptr);
        }
        if (!breadcrumbSegmentButtons_.front())
        {
            breadcrumbSegmentButtons_.front() = createBreadcrumbButton(
                kBreadcrumbSegmentControlIdBase,
                L"Navigate to folder root");
        }
        if (!breadcrumbMoreButton_)
        {
            breadcrumbMoreButton_ = createBreadcrumbButton(
                kBreadcrumbMoreControlId,
                L"More parent folders");
        }

        while (breadcrumbSegmentButtons_.size() < breadcrumbSegments_.size())
        {
            const UINT controlId = kBreadcrumbSegmentControlIdBase
                + static_cast<UINT>(breadcrumbSegmentButtons_.size());
            breadcrumbSegmentButtons_.push_back(createBreadcrumbButton(controlId, L"Navigate to folder"));
        }

        const HFONT font = appTextUiFont_
            ? appTextUiFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (std::size_t index = 0; index < breadcrumbSegments_.size(); ++index)
        {
            const HWND button = breadcrumbSegmentButtons_[index];
            if (!button)
            {
                continue;
            }

            std::wstring accessibleName = L"Navigate to ";
            accessibleName.append(breadcrumbSegments_[index].targetFolderPath);
            SetWindowTextW(button, accessibleName.c_str());
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        if (breadcrumbMoreButton_)
        {
            SendMessageW(breadcrumbMoreButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        LayoutChildren();
        if (breadcrumbHadFocus)
        {
            HWND focusTarget = !breadcrumbSegmentButtons_.empty() ? breadcrumbSegmentButtons_.back() : nullptr;
            if (focusTarget && IsWindowVisible(focusTarget) != FALSE)
            {
                SetFocus(focusTarget);
            }
            else if (breadcrumbMoreButton_ && IsWindowVisible(breadcrumbMoreButton_) != FALSE)
            {
                SetFocus(breadcrumbMoreButton_);
            }
        }
    }

    void MainWindow::LayoutBreadcrumbBar(int left, int top, int width, int height)
    {
        const RECT currentBarRect{left, top, left + width, top + height};
        if (EqualRect(&breadcrumbBarRect_, &currentBarRect) == FALSE)
        {
            if (dragMode_ != DragMode::LeftSplitter)
            {
                if (IsRectEmpty(&breadcrumbBarRect_) == FALSE)
                {
                    InvalidateRect(hwnd_, &breadcrumbBarRect_, FALSE);
                }
                if (IsRectEmpty(&currentBarRect) == FALSE)
                {
                    InvalidateRect(hwnd_, &currentBarRect, FALSE);
                }
            }
            breadcrumbBarRect_ = currentBarRect;
        }

        const HWND focusedWindow = GetFocus();
        bool moveFocus = false;
        const auto hideControl = [&](HWND control)
        {
            if (!control || IsWindow(control) == FALSE)
            {
                return;
            }
            if (focusedWindow == control)
            {
                moveFocus = true;
            }
            if (IsWindowVisible(control) != FALSE)
            {
                ShowWindow(control, SW_HIDE);
            }
        };

        if (breadcrumbSegments_.empty() || height <= 0 || width <= 0)
        {
            for (HWND button : breadcrumbSegmentButtons_)
            {
                hideControl(button);
            }
            hideControl(breadcrumbMoreButton_);
            if (moveFocus && hwnd_)
            {
                SetFocus(hwnd_);
            }
            return;
        }

        const UINT dpi = hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u;
        const MenuMetrics metrics = MakeMenuMetrics(appTextSize_, dpi);
        const int horizontalPadding = metrics.ScaleDip(8);
        const int gap = metrics.ScaleDip(4);
        const int minimumButtonWidth = metrics.ScaleDip(42);
        const int maximumButtonWidth = metrics.ScaleDip(190);
        const int chevronWidth = metrics.ScaleDip(14);
        const int availableWidth = std::max(0, width - (horizontalPadding * 2));
        const HFONT font = appTextUiFont_
            ? appTextUiFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        std::vector<int> preferredWidths;
        preferredWidths.reserve(breadcrumbSegments_.size());
        int preferredTotal = 0;
        for (std::size_t index = 0; index < breadcrumbSegments_.size(); ++index)
        {
            const int arrowSpace = index + 1 < breadcrumbSegments_.size() ? chevronWidth : 0;
            const int desiredWidth = MeasureTextWidth(font, breadcrumbSegments_[index].label)
                + metrics.ScaleDip(16)
                + arrowSpace;
            const int preferredWidth = std::clamp(desiredWidth, minimumButtonWidth, maximumButtonWidth);
            preferredWidths.push_back(preferredWidth);
            preferredTotal += preferredWidth;
        }

        const std::size_t segmentCount = breadcrumbSegments_.size();
        const bool showOverflow = segmentCount > 2
            && preferredTotal + gap * static_cast<int>(segmentCount - 1) > availableWidth;
        std::vector<std::size_t> visibleIndices;
        std::vector<int> visibleWidths;
        int moreWidth = 0;
        if (showOverflow)
        {
            visibleIndices = {0, segmentCount - 1};
            moreWidth = std::min(metrics.ScaleDip(76),
                                 std::max(0, availableWidth - (minimumButtonWidth * 2) - (gap * 2)));
            const int segmentWidth = std::max(0, availableWidth - moreWidth - (gap * 2));
            int rootWidth = std::min(preferredWidths.front(), std::max(minimumButtonWidth, segmentWidth / 2));
            rootWidth = std::min(rootWidth, segmentWidth);
            const int leafWidth = std::min(preferredWidths.back(), std::max(0, segmentWidth - rootWidth));
            visibleWidths = {rootWidth, leafWidth};
        }
        else
        {
            for (std::size_t index = 0; index < segmentCount; ++index)
            {
                visibleIndices.push_back(index);
                visibleWidths.push_back(preferredWidths[index]);
            }

            const int buttonSpace = std::max(0, availableWidth - gap * static_cast<int>(segmentCount - 1));
            int visibleTotal = 0;
            for (int buttonWidth : visibleWidths)
            {
                visibleTotal += buttonWidth;
            }
            if (visibleTotal > buttonSpace && segmentCount == 1)
            {
                visibleWidths.front() = buttonSpace;
            }
            else if (visibleTotal > buttonSpace && segmentCount == 2)
            {
                int rootWidth = std::min(preferredWidths.front(),
                                         std::max(minimumButtonWidth, buttonSpace / 3));
                rootWidth = std::min(rootWidth, buttonSpace);
                const int leafWidth = std::min(preferredWidths.back(), std::max(0, buttonSpace - rootWidth));
                visibleWidths = {rootWidth, leafWidth};
            }
        }

        std::vector<bool> visibleButtons(segmentCount, false);
        int x = left + horizontalPadding;
        for (std::size_t position = 0; position < visibleIndices.size(); ++position)
        {
            const std::size_t segmentIndex = visibleIndices[position];
            visibleButtons[segmentIndex] = true;
            const HWND button = breadcrumbSegmentButtons_[segmentIndex];
            if (button && IsWindow(button) != FALSE)
            {
                MoveWindow(button, x, top, visibleWidths[position], height, TRUE);
                if (IsWindowVisible(button) == FALSE)
                {
                    ShowWindow(button, SW_SHOW);
                }
            }
            x += visibleWidths[position] + gap;

            if (showOverflow && position == 0 && breadcrumbMoreButton_)
            {
                MoveWindow(breadcrumbMoreButton_, x, top, moreWidth, height, TRUE);
                if (IsWindowVisible(breadcrumbMoreButton_) == FALSE)
                {
                    ShowWindow(breadcrumbMoreButton_, SW_SHOW);
                }
                x += moreWidth + gap;
            }
        }

        for (std::size_t index = 0; index < breadcrumbSegmentButtons_.size(); ++index)
        {
            if (visibleButtons[index])
            {
                continue;
            }
            hideControl(breadcrumbSegmentButtons_[index]);
        }
        if (!showOverflow)
        {
            hideControl(breadcrumbMoreButton_);
        }

        if (moveFocus)
        {
            const HWND focusTarget = showOverflow
                ? breadcrumbMoreButton_
                : breadcrumbSegmentButtons_[visibleIndices.back()];
            if (focusTarget && IsWindowVisible(focusTarget) != FALSE)
            {
                SetFocus(focusTarget);
            }
        }
    }

    void MainWindow::DrawBreadcrumbButton(const DRAWITEMSTRUCT& drawItem) const
    {
        if (!drawItem.hwndItem || !drawItem.hDC)
        {
            return;
        }

        const bool moreButton = drawItem.CtlID == kBreadcrumbMoreControlId;
        const std::size_t segmentIndex = moreButton
            ? breadcrumbSegments_.size()
            : static_cast<std::size_t>(drawItem.CtlID - kBreadcrumbSegmentControlIdBase);
        if (!moreButton && segmentIndex >= breadcrumbSegments_.size())
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
        const bool hot = (drawItem.itemState & ODS_HOTLIGHT) != 0;
        const bool focused = (drawItem.itemState & ODS_FOCUS) != 0;
        const COLORREF background = selected || hot
            ? BlendColor(palette.windowBackground, palette.accent, themeMode_ == ThemeMode::Dark ? 28 : 16)
            : palette.windowBackground;
        const HFONT font = appTextUiFont_
            ? appTextUiFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        const auto paintButton = [&](HDC target, const RECT& buttonRect)
        {
            HBRUSH backgroundBrush = CreateSolidBrush(background);
            FillRect(target, &buttonRect, backgroundBrush);
            DeleteObject(backgroundBrush);

            const int savedDC = SaveDC(target);
            SetBkMode(target, TRANSPARENT);
            SetTextColor(target, hot ? palette.accent : palette.text);
            const HGDIOBJ previousFont = SelectObject(target, font);

            RECT textRect = buttonRect;
            const int horizontalInset = ScaleAppTextDimension(6, appTextSize_);
            InflateRect(&textRect, -horizontalInset, 0);
            if (moreButton)
            {
                DrawTextW(target, L"...", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            else
            {
                const bool hasNextSegment = segmentIndex + 1 < breadcrumbSegments_.size();
                const int chevronWidth = hasNextSegment ? ScaleAppTextDimension(14, appTextSize_) : 0;
                RECT labelRect = textRect;
                labelRect.right -= chevronWidth;
                const std::wstring& label = breadcrumbSegments_[segmentIndex].label;
                DrawTextW(target,
                          label.c_str(),
                          static_cast<int>(label.size()),
                          &labelRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (hasNextSegment)
                {
                    RECT chevronRect = textRect;
                    chevronRect.left = chevronRect.right - chevronWidth;
                    SetTextColor(target, palette.mutedText);
                    DrawTextW(target, L">", -1, &chevronRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }

            if (previousFont)
            {
                SelectObject(target, previousFont);
            }
            if (focused)
            {
                RECT focusRect = buttonRect;
                InflateRect(&focusRect, -3, -3);
                DrawFocusRect(target, &focusRect);
            }
            if (savedDC != 0)
            {
                RestoreDC(target, savedDC);
            }
        };

        const int buttonWidth = drawItem.rcItem.right - drawItem.rcItem.left;
        const int buttonHeight = drawItem.rcItem.bottom - drawItem.rcItem.top;
        if (buttonWidth <= 0 || buttonHeight <= 0)
        {
            return;
        }

        HDC bufferedDC = CreateCompatibleDC(drawItem.hDC);
        HBITMAP bufferedBitmap = bufferedDC
            ? CreateCompatibleBitmap(drawItem.hDC, buttonWidth, buttonHeight)
            : nullptr;
        if (!bufferedDC || !bufferedBitmap)
        {
            if (bufferedBitmap)
            {
                DeleteObject(bufferedBitmap);
            }
            if (bufferedDC)
            {
                DeleteDC(bufferedDC);
            }
            paintButton(drawItem.hDC, drawItem.rcItem);
            return;
        }

        const HGDIOBJ previousBitmap = SelectObject(bufferedDC, bufferedBitmap);
        if (!previousBitmap || previousBitmap == HGDI_ERROR)
        {
            DeleteObject(bufferedBitmap);
            DeleteDC(bufferedDC);
            paintButton(drawItem.hDC, drawItem.rcItem);
            return;
        }

        const RECT bufferedRect{0, 0, buttonWidth, buttonHeight};
        paintButton(bufferedDC, bufferedRect);
        const BOOL copied = BitBlt(drawItem.hDC,
                                   drawItem.rcItem.left,
                                   drawItem.rcItem.top,
                                   buttonWidth,
                                   buttonHeight,
                                   bufferedDC,
                                   0,
                                   0,
                                   SRCCOPY);
        SelectObject(bufferedDC, previousBitmap);
        DeleteObject(bufferedBitmap);
        DeleteDC(bufferedDC);
        if (!copied)
        {
            paintButton(drawItem.hDC, drawItem.rcItem);
        }
    }

    LRESULT CALLBACK MainWindow::BreadcrumbButtonSubclassProc(HWND hwnd,
                                                                UINT message,
                                                                WPARAM wParam,
                                                                LPARAM lParam,
                                                                UINT_PTR subclassId,
                                                                DWORD_PTR)
    {
        if (message == WM_ERASEBKGND)
        {
            return 1;
        }
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hwnd, &MainWindow::BreadcrumbButtonSubclassProc, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    void MainWindow::ShowBreadcrumbOverflowMenu()
    {
        if (!breadcrumbMoreButton_ || breadcrumbSegments_.size() <= 2)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        for (std::size_t index = 1; index + 1 < breadcrumbSegments_.size(); ++index)
        {
            const FolderBreadcrumbSegment& segment = breadcrumbSegments_[index];
            std::wstring label = segment.label;
            label.append(L"  (");
            label.append(segment.targetFolderPath);
            label.push_back(L')');
            AppendMenuW(menu,
                        MF_STRING,
                        kBreadcrumbSegmentControlIdBase + static_cast<UINT>(index),
                        label.c_str());
        }

        RECT buttonRect{};
        GetWindowRect(breadcrumbMoreButton_, &buttonRect);
        const UINT commandId = TrackPopupMenu(menu,
                                              TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                              buttonRect.left,
                                              buttonRect.bottom,
                                              0,
                                              hwnd_,
                                              nullptr);
        DestroyMenu(menu);
        if (commandId >= kBreadcrumbSegmentControlIdBase)
        {
            const std::size_t segmentIndex = commandId - kBreadcrumbSegmentControlIdBase;
            if (segmentIndex < breadcrumbSegments_.size()
                && (!browserModel_ || !FolderPathsEqual(browserModel_->FolderPath(),
                                                        breadcrumbSegments_[segmentIndex].targetFolderPath)))
            {
                LoadFolderAsync(breadcrumbSegments_[segmentIndex].targetFolderPath);
            }
        }
    }

    void MainWindow::RebuildAppTextFonts()
    {
        const HFONT defaultGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const UINT dpi = static_cast<UINT>(hyperbrowse::util::EffectiveTextDpi(
            hwnd_ ? static_cast<int>(GetDpiForWindow(hwnd_)) : 96));

        DeleteFontIfOwned(appTextUiFont_);
        appTextUiFont_ = CreateSystemUiFont(appTextSize_, dpi);
        if (!appTextUiFont_)
        {
            appTextUiFont_ = defaultGuiFont;
        }

        DeleteFontIfOwned(detailsPanelTitleFont_);
        DeleteFontIfOwned(detailsPanelSummaryFont_);
        DeleteFontIfOwned(detailsPanelBodyFont_);
        detailsPanelTitleFont_ = CreateDialogUiFont(11, FW_SEMIBOLD, appTextSize_, dpi);
        detailsPanelSummaryFont_ = CreateDialogUiFont(9, FW_NORMAL, appTextSize_, dpi);
        detailsPanelBodyFont_ = CreateDialogUiFont(9, FW_NORMAL, appTextSize_, dpi);
        if (!detailsPanelTitleFont_) detailsPanelTitleFont_ = defaultGuiFont;
        if (!detailsPanelSummaryFont_) detailsPanelSummaryFont_ = defaultGuiFont;
        if (!detailsPanelBodyFont_) detailsPanelBodyFont_ = defaultGuiFont;

        const HWND controls[] = {filterEdit_, treePane_, statusBar_};
        for (HWND control : controls)
        {
            if (control)
            {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(appTextUiFont_), TRUE);
            }
        }
        ApplyFolderTreeMetrics();

        if (tooltipControl_)
        {
            SendMessageW(tooltipControl_, WM_SETFONT, reinterpret_cast<WPARAM>(appTextUiFont_), TRUE);
            SendMessageW(tooltipControl_,
                         TTM_SETMAXTIPWIDTH,
                         0,
                         hyperbrowse::util::ScaleAppTextDimension(kToolbarTooltipMaxWidth, appTextSize_));
        }

        if (detailsPanelText_)
        {
            SendMessageW(detailsPanelText_, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
        }

        for (HWND edit : quickAccessShortcutEdits_)
        {
            if (edit)
            {
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
            }
        }

        RefreshDetailsPanelBodyPresentation();
    }

    void MainWindow::ApplyFolderTreeMetrics()
    {
        if (!treePane_)
        {
            return;
        }

        const UINT dpi = static_cast<UINT>(hyperbrowse::util::EffectiveTextDpi(
            hwnd_ ? static_cast<int>(GetDpiForWindow(hwnd_)) : 96));
        const int verticalPadding = MulDiv(
            hyperbrowse::util::ScaleAppTextDimension(2, appTextSize_),
            static_cast<int>(dpi),
            96);
        int rowHeight = MulDiv(
            hyperbrowse::util::AppTextTreeRowHeight(appTextSize_),
            static_cast<int>(dpi),
            96);

        HDC treeDc = GetDC(treePane_);
        if (treeDc)
        {
            const HGDIOBJ previousFont = appTextUiFont_ ? SelectObject(treeDc, appTextUiFont_) : nullptr;
            TEXTMETRICW textMetrics{};
            if (GetTextMetricsW(treeDc, &textMetrics) != FALSE)
            {
                const int textHeight = static_cast<int>(textMetrics.tmHeight - textMetrics.tmInternalLeading);
                rowHeight = (std::max)(1, textHeight) + verticalPadding;
            }
            if (previousFont)
            {
                SelectObject(treeDc, previousFont);
            }
            ReleaseDC(treePane_, treeDc);
        }

        if (treeImageList_)
        {
            int iconWidth = 0;
            int iconHeight = 0;
            if (ImageList_GetIconSize(treeImageList_, &iconWidth, &iconHeight) != FALSE)
            {
                rowHeight = (std::max)(rowHeight,
                                       iconHeight + verticalPadding);
            }
        }

        rowHeight = (std::max)(1, rowHeight);
        rowHeight = (rowHeight + 1) & ~1;
        TreeView_SetItemHeight(treePane_, rowHeight);
        InvalidateRect(treePane_, nullptr, TRUE);
    }

    void MainWindow::ApplyAppTextSize()
    {
        appTextSize_ = util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize_));
        RebuildAppTextFonts();
        d2dToolbarTextFormat_.Reset();
        if (browserPaneController_)
        {
            browserPaneController_->SetAppTextSize(appTextSize_);
        }
        if (diagnosticsWindow_)
        {
            diagnosticsWindow_->SetAppTextSize(appTextSize_);
        }
        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_))
        {
            PostMessageW(shortcutReferenceWindow_,
                         kAppTextSizeChangedMessage,
                         static_cast<WPARAM>(appTextSize_),
                         0);
        }
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetAppTextSize(appTextSize_);
        }
        if (menu_)
        {
            RefreshPersistentMenuOwnerDraw();
        }
        if (hwnd_)
        {
            LayoutChildren();
            InvalidateToolbarStrip();
            InvalidateRect(statusBar_, nullptr, TRUE);
            InvalidateRect(detailsPanelText_, nullptr, TRUE);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    }

    void MainWindow::RebuildThemeBrushes()
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
    }

    void MainWindow::ApplyTheme()
    {
        const ThemePalette palette = GetThemePalette();
        RebuildThemeBrushes();

        if (menuBackgroundBrush_)
        {
            DeleteObject(menuBackgroundBrush_);
            menuBackgroundBrush_ = nullptr;
        }
        menuBackgroundBrush_ = CreateSolidBrush(palette.paneBackground);
        if (menu_ && menuBackgroundBrush_)
        {
            MENUINFO menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
            menuInfo.hbrBack = menuBackgroundBrush_;
            SetMenuInfo(menu_, &menuInfo);
        }

        if (hwnd_)
        {
            ApplyWindowFrameTheme(hwnd_,
                                  themeMode_ == ThemeMode::Dark && !IsHighContrastEnabled(),
                                  palette.actionStripBackground,
                                  palette.text,
                                  palette.actionStripBorder);
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

        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (diagnosticsWindow_)
        {
            diagnosticsWindow_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        RefreshDetailsPanelBodyPresentation();

        if (hwnd_)
        {
            InvalidateToolbarStrip();
            InvalidateRect(statusBar_, nullptr, TRUE);
            InvalidateRect(detailsPanelText_, nullptr, TRUE);
            if (filterEdit_)
            {
                RedrawWindow(filterEdit_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
            }
            RefreshWindowNonClientArea(hwnd_);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    }

    void MainWindow::ApplyViewerMouseWheelSetting()
    {
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetMouseWheelBehavior(viewerMouseWheelBehavior_);
            viewer->SetKeyboardPanningInverted(invertKeyboardPanning_);
        }
    }

    void MainWindow::ApplyViewerEscapeKeyBehavior()
    {
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetEscapeKeyBehavior(viewerEscapeKeyBehavior_);
        }
    }

    void MainWindow::ApplyViewerTransitionSettings()
    {
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetTransitionSettings(
                slideshowTransitionStyle_,
                slideshowTransitionDurationMs_);
            viewer->SetManualTransitionEnabled(useSlideshowTransition_);
        }
    }

    void MainWindow::ApplyColorManagementSetting()
    {
        if (browserPaneController_) browserPaneController_->SetColorManagementEnabled(colorManagementEnabled_);
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetColorManagementEnabled(colorManagementEnabled_);
        }
    }

    void MainWindow::RefreshColorProfiles(bool force)
    {
        if (browserPaneController_) browserPaneController_->RefreshColorProfile(force);
        for (viewer::ViewerWindow* viewer : OpenViewerWindows()) viewer->RefreshColorProfile(force);
    }

    void MainWindow::ApplyThumbnailMemoryPressureState()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetThumbnailMemoryPressureActive(thumbnailMemoryPressureActive_);
        }

        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetMemoryPressureActive(thumbnailMemoryPressureActive_);
        }

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->SetPressureModeEnabled(thumbnailMemoryPressureActive_);
            if (thumbnailMemoryPressureActive_)
            {
                detailsPanelThumbnailScheduler_->TrimCacheToBytes(std::max<std::size_t>(1, detailsPanelThumbnailScheduler_->CacheCapacityBytes() / 2));
            }
        }

        UpdateStatusText();
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
        const ThemeVariant variant = ResolveThemeVariant(themeMode_ == ThemeMode::Dark);
        const ThemeTokens tokens = MakeThemeTokens(variant);

        COLORREF treeLine = RGB(198, 204, 212);
        COLORREF splitter = RGB(210, 215, 223);
        COLORREF actionStripBackground = RGB(249, 250, 252);
        switch (variant)
        {
        case ThemeVariant::HighContrast:
            treeLine = GetSysColor(COLOR_WINDOWTEXT);
            splitter = GetSysColor(COLOR_WINDOWTEXT);
            actionStripBackground = GetSysColor(COLOR_BTNFACE);
            break;
        case ThemeVariant::Dark:
            treeLine = RGB(96, 102, 110);
            splitter = RGB(78, 84, 92);
            actionStripBackground = RGB(29, 33, 38);
            break;
        case ThemeVariant::Light:
        default:
            break;
        }

        return ThemePalette{
            tokens.windowBackground,
            tokens.surfaceBackground,
            tokens.text,
            tokens.mutedText,
            treeLine,
            splitter,
            actionStripBackground,
            tokens.border,
            tokens.fieldBackground,
            tokens.accent,
            tokens.accentFill,
            tokens.accentText,
        };
    }

    void MainWindow::InitToolbarItems()
    {
        commandBarController_.InitializeItems();
    }

    void MainWindow::LayoutToolbar(const MenuMetrics& menuMetrics)
    {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientWidth = client.right - client.left;
        const int itemTop = menuMetrics.ScaleDip(menuMetrics.commandBarPaddingYDip);
        const HFONT menuFont = detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        commandBarController_.Layout(
            clientWidth,
            itemTop,
            menuMetrics,
            menuFont,
            [](HFONT font, std::wstring_view text)
            {
                return MeasureTextWidth(font, text);
            });

        const auto filterItem = std::find_if(toolbarItems_.begin(), toolbarItems_.end(), [](const ToolbarItem& item)
        {
            return item.kind == ToolbarItemKind::FilterEdit;
        });
        if (filterItem != toolbarItems_.end() && filterEdit_)
        {
            const auto filterClearItem = std::find_if(toolbarItems_.begin(), toolbarItems_.end(), [](const ToolbarItem& item)
            {
                return item.kind == ToolbarItemKind::FilterClear;
            });
            const RECT& filterRect = filterItem->rect;
            const int toolbarItemSize = menuMetrics.ScaleDip(kToolbarItemSize);
            const int filterEditHeight = menuMetrics.ScaleDip(kToolbarFilterEditHeight);
            const int filterEditVerticalOffset =
                menuMetrics.ScaleDip(menuMetrics.commandBarFilterEditTextVerticalOffsetDip);
            const int filterTop = itemTop + std::max(0, (toolbarItemSize - filterEditHeight) / 2)
                + filterEditVerticalOffset;
            const int filterControlHeight = std::max(1, filterEditHeight - filterEditVerticalOffset);
            const int filterEditLeft = static_cast<int>(filterRect.left)
                + menuMetrics.ScaleDip(menuMetrics.commandBarFilterEditTextInsetDip);
            const int filterEditRight = filterClearItem != toolbarItems_.end()
                ? static_cast<int>(filterClearItem->rect.left)
                : static_cast<int>(filterRect.right)
                    - menuMetrics.ScaleDip(menuMetrics.commandBarFilterEditHorizontalInsetDip);
            const int filterWidth = std::max(0, filterEditRight - filterEditLeft);
            MoveWindow(filterEdit_,
                       filterEditLeft,
                       filterTop,
                       filterWidth,
                       filterControlHeight,
                       TRUE);
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
        RECT stripRect{0, 0, clientWidth, actionStripHeight_};
        InvalidateRect(hwnd_, &stripRect, FALSE);
    }

    bool MainWindow::EnsureD2DResources()
    {
        if (d2dToolbarTextFormat_)
        {
            return true;
        }

        auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
        if (!renderer.IsAvailable() || !hwnd_)
        {
            return false;
        }

        d2dToolbarTextFormat_ = renderer.CreateTextFormatFromFont(appTextUiFont_, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if (!d2dToolbarTextFormat_)
        {
            ResetD2DResources();
            return false;
        }

        d2dToolbarTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        d2dToolbarTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        d2dToolbarTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return true;
    }

    void MainWindow::ResetD2DResources()
    {
        d2dToolbarTextFormat_.Reset();
        d2dRenderTarget_.Reset();
    }

    void MainWindow::PaintToolbarD2D(ID2D1RenderTarget* renderTarget, const RECT& stripRect)
    {
        const ThemePalette sourcePalette = GetThemePalette();
        const CommandBarPalette palette{
            sourcePalette.actionStripBackground,
            sourcePalette.actionStripBorder,
            sourcePalette.actionFieldBackground,
            sourcePalette.text,
            sourcePalette.mutedText,
            sourcePalette.accent,
            sourcePalette.accentFill,
            sourcePalette.accentText,
        };
        const CommandBarPaintState state{
            commandBarHotIndex_,
            commandBarPressedIndex_,
            toolbarHotIndex_,
            toolbarPressedIndex_,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::ToolbarItem && GetFocus() == hwnd_
                ? keyboardFocusTarget_.index
                : -1,
            commandBarKeyboardActive_,
            filterEdit_ != nullptr,
            GetFocus() == filterEdit_,
        };
        const MenuMetrics menuMetrics = MakeMenuMetrics(
            appTextSize_,
            hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u);
        commandBarPainter_.PaintD2D(renderTarget,
                                    stripRect,
                                    commandBarMenuButtons_,
                                    toolbarItems_,
                                    palette,
                                    menuMetrics,
                                    d2dToolbarTextFormat_.Get(),
                                    toolbarIconLibrary_.get(),
                                    state);
    }

    void MainWindow::PaintToolbar(HDC hdc, const RECT& stripRect)
    {
        const ThemePalette sourcePalette = GetThemePalette();
        const CommandBarPalette palette{
            sourcePalette.actionStripBackground,
            sourcePalette.actionStripBorder,
            sourcePalette.actionFieldBackground,
            sourcePalette.text,
            sourcePalette.mutedText,
            sourcePalette.accent,
            sourcePalette.accentFill,
            sourcePalette.accentText,
        };
        const CommandBarPaintState state{
            commandBarHotIndex_,
            commandBarPressedIndex_,
            toolbarHotIndex_,
            toolbarPressedIndex_,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::ToolbarItem && GetFocus() == hwnd_
                ? keyboardFocusTarget_.index
                : -1,
            commandBarKeyboardActive_,
            filterEdit_ != nullptr,
            GetFocus() == filterEdit_,
        };
        const MenuMetrics menuMetrics = MakeMenuMetrics(
            appTextSize_,
            hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u);
        commandBarPainter_.PaintGdi(hdc,
                                    stripRect,
                                    commandBarMenuButtons_,
                                    toolbarItems_,
                                    palette,
                                    menuMetrics,
                                    detailsPanelSummaryFont_
                                        ? detailsPanelSummaryFont_
                                        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                    toolbarIconLibrary_.get(),
                                    state);
    }

    int MainWindow::ToolbarHitTest(int x, int y) const
    {
        return commandBarController_.ToolbarHitTest(x, y);
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

            std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
            PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

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

            std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
            PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

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
}
