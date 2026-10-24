#include "ui/MainWindowInternal.h"

namespace
{
    using hyperbrowse::ui::dialog_detail::FileAssociationsDialogLayoutMetrics;
    using hyperbrowse::ui::dialog_detail::FileAssociationsDialogState;
    using hyperbrowse::ui::dialog_detail::SlideshowSettingsDialogLayoutMetrics;
    using hyperbrowse::ui::dialog_detail::SlideshowSettingsDialogState;
    constexpr wchar_t kSlideshowSettingsDialogClassName[] = L"HyperBrowseSlideshowSettingsDialog";
    constexpr int kSlideshowSettingsDialogWidth = 560;
    constexpr int kSlideshowSettingsDialogHeight = 368;
    constexpr int kSlideshowSettingsInstructionControlId = 340;
    constexpr int kSlideshowSettingsTransitionLabelControlId = 341;
    constexpr int kSlideshowSettingsTransitionComboControlId = 342;
    constexpr int kSlideshowSettingsDurationLabelControlId = 343;
    constexpr int kSlideshowSettingsDurationEditControlId = 344;
    constexpr int kSlideshowSettingsDurationUnitControlId = 345;
    constexpr int kSlideshowSettingsDurationSpinControlId = 351;
    constexpr int kSlideshowSettingsTransitionDurationLabelControlId = 346;
    constexpr int kSlideshowSettingsTransitionDurationEditControlId = 347;
    constexpr int kSlideshowSettingsTransitionDurationUnitControlId = 348;
    constexpr int kSlideshowSettingsTransitionDurationSpinControlId = 352;
    constexpr int kSlideshowSettingsFootnoteControlId = 349;
    constexpr int kSlideshowSettingsDividerControlId = 350;

    constexpr wchar_t kFileAssociationsDialogClassName[] = L"HyperBrowseFileAssociationsDialog";
    constexpr int kFileAssociationsDialogWidth = 760;
    constexpr int kFileAssociationsDialogHeight = 500;
    constexpr int kFileAssociationsDialogMargin = 20;
    constexpr int kFileAssociationsDialogInstructionControlId = 361;
    constexpr int kFileAssociationsDialogSelectAllControlId = 363;
    constexpr int kFileAssociationsDialogClearAllControlId = 364;
    constexpr int kFileAssociationsDialogFootnoteControlId = 365;
    constexpr int kFileAssociationsDialogDefaultAppsControlId = 367;
    constexpr int kFileAssociationsDialogFormatBaseControlId = 380;
    constexpr int kFileAssociationsDialogButtonWidth = 88;
    constexpr int kFileAssociationsDialogButtonHeight = 28;
    constexpr int kFileAssociationsDialogDefaultAppsButtonWidth = 124;
    constexpr int kFileAssociationsDialogDefaultAppsButtonHeight = 32;
    constexpr int kFileAssociationsDialogFormatRowHeight = 28;
    constexpr int kFileAssociationsDialogFormatCheckboxWidth = 72;
    bool IsWindows11OrGreater();
    bool LaunchDefaultAppsSettings(HWND ownerWindow);

    void ScaleDialogChildWindows(HWND parentWindow, UINT fromDpi, UINT toDpi)
    {
        if (!parentWindow || fromDpi == 0 || toDpi == 0 || fromDpi == toDpi)
        {
            return;
        }

        struct ScaleContext
        {
            HWND parent{};
            UINT fromDpi{};
            UINT toDpi{};
        } context{parentWindow, fromDpi, toDpi};

        EnumChildWindows(parentWindow,
                         [](HWND child, LPARAM parameter) -> BOOL
                         {
                             auto* context = reinterpret_cast<ScaleContext*>(parameter);
                             RECT rect{};
                             if (!GetWindowRect(child, &rect))
                             {
                                 return TRUE;
                             }
                             POINT points[] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
                             MapWindowPoints(nullptr, context->parent, points, 2);
                             SetWindowPos(child,
                                          nullptr,
                                          MulDiv(points[0].x, static_cast<int>(context->toDpi), static_cast<int>(context->fromDpi)),
                                          MulDiv(points[0].y, static_cast<int>(context->toDpi), static_cast<int>(context->fromDpi)),
                                          MulDiv(points[1].x - points[0].x, static_cast<int>(context->toDpi), static_cast<int>(context->fromDpi)),
                                          MulDiv(points[1].y - points[0].y, static_cast<int>(context->toDpi), static_cast<int>(context->fromDpi)),
                                          SWP_NOZORDER | SWP_NOACTIVATE);
                             return TRUE;
                         },
                         reinterpret_cast<LPARAM>(&context));
    }

    void SetDialogUIntEditAndSpin(HWND editWindow, HWND spinWindow, UINT value)
    {
        if (spinWindow)
        {
            SendMessageW(spinWindow, UDM_SETPOS32, 0, static_cast<LPARAM>(value));
        }

        if (editWindow)
        {
            SetWindowTextIfDifferent(editWindow, std::to_wstring(value));
        }
    }

    UINT ComputeNextSpinValue(HWND editWindow, int fallbackValue, int delta, UINT minimum, UINT maximum)
    {
        UINT currentValue = static_cast<UINT>(std::clamp(fallbackValue, static_cast<int>(minimum), static_cast<int>(maximum)));
        TryReadDialogUInt(editWindow, minimum, maximum, &currentValue);

        const int nextValue = std::clamp(static_cast<int>(currentValue) + delta,
                                         static_cast<int>(minimum),
                                         static_cast<int>(maximum));
        return static_cast<UINT>(nextValue);
    }

    int MeasureFileAssociationsCheckboxWidth(HFONT font,
                                              hyperbrowse::util::AppTextSize appTextSize,
                                              UINT dpi)
    {
        int labelWidth = 0;
        for (const auto& fileType : hyperbrowse::decode::SupportedFileTypes())
        {
            std::wstring label = L".";
            label.append(fileType.extension);
            labelWidth = std::max(labelWidth, MeasureDialogButtonWidth(font, label, 0));
        }

        const int checkboxPadding = ScaleDialogAppTextDimension(30, appTextSize, dpi);
        return std::max(
            ScaleDialogAppTextDimension(kFileAssociationsDialogFormatCheckboxWidth, appTextSize, dpi),
            labelWidth + checkboxPadding);
    }

    FileAssociationsDialogLayoutMetrics BuildFileAssociationsDialogLayoutMetrics(
        int clientWidth,
        const FileAssociationsDialogState& state,
        std::size_t formatCount)
    {
        FileAssociationsDialogLayoutMetrics metrics;
        metrics.margin = ScaleDialogAppTextDimension(kFileAssociationsDialogMargin, state.appTextSize, state.dpi);
        metrics.contentWidth = std::max(0, clientWidth - (metrics.margin * 2));
        const int lineHeight = MeasureSingleLineTextHeight(state.bodyFont, 20);
        const int scaledGap = ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi);
        const int formatRowCount = std::max(1, static_cast<int>((formatCount + 1) / 2));

        metrics.instructionTop = ScaleDialogAppTextDimension(16, state.appTextSize, state.dpi);
        metrics.instructionHeight = MeasureTextBlockHeight(state.bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           lineHeight + scaledGap / 2);
        metrics.actionGap = scaledGap;
        metrics.actionTop = metrics.instructionTop + metrics.instructionHeight + metrics.actionGap;
        metrics.selectAllWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Select all",
            ScaleDialogAppTextDimension(92, state.appTextSize, state.dpi));
        metrics.clearAllWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Clear all",
            ScaleDialogAppTextDimension(92, state.appTextSize, state.dpi));
        metrics.defaultAppsButtonWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Default Apps...",
            ScaleDialogAppTextDimension(kFileAssociationsDialogDefaultAppsButtonWidth, state.appTextSize, state.dpi));
        metrics.buttonHeight = std::max(
            ScaleDialogAppTextDimension(kFileAssociationsDialogButtonHeight, state.appTextSize, state.dpi),
            lineHeight + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi));
        metrics.defaultAppsButtonHeight = std::max(
            ScaleDialogAppTextDimension(kFileAssociationsDialogDefaultAppsButtonHeight, state.appTextSize, state.dpi),
            lineHeight + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi));
        metrics.formatGroupTop = metrics.actionTop + metrics.buttonHeight + metrics.actionGap;
        metrics.formatRowHeight = std::max(
            ScaleDialogAppTextDimension(kFileAssociationsDialogFormatRowHeight, state.appTextSize, state.dpi),
            lineHeight + ScaleDialogAppTextDimension(6, state.appTextSize, state.dpi));
        metrics.formatGroupContentTop = ScaleDialogAppTextDimension(26, state.appTextSize, state.dpi);
        metrics.formatCheckboxWidth = MeasureFileAssociationsCheckboxWidth(state.bodyFont, state.appTextSize, state.dpi);
        int descriptionWidth = 0;
        for (const auto& fileType : hyperbrowse::decode::SupportedFileTypes())
        {
            descriptionWidth = std::max(
                descriptionWidth,
                MeasureDialogButtonWidth(state.bodyFont, fileType.description, 0));
        }
        const int columnGap = ScaleDialogAppTextDimension(28, state.appTextSize, state.dpi);
        const int descriptionRightInset = ScaleDialogAppTextDimension(24, state.appTextSize, state.dpi);
        const int minimumColumnWidth = metrics.formatCheckboxWidth + descriptionWidth + descriptionRightInset;
        metrics.minimumClientWidth = metrics.margin * 2 + columnGap + minimumColumnWidth * 2;
        const int formatGroupBottomInset = ScaleDialogAppTextDimension(10, state.appTextSize, state.dpi);
        metrics.formatGroupHeight = formatGroupBottomInset
            + metrics.formatGroupContentTop
            + formatRowCount * metrics.formatRowHeight;
        metrics.footnoteTop = metrics.formatGroupTop + metrics.formatGroupHeight + metrics.actionGap;
        metrics.footnoteHeight = MeasureTextBlockHeight(state.bodyFont,
                                                        state.footnote,
                                                        metrics.contentWidth,
                                                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                        lineHeight);
        metrics.buttonRowHeight = std::max(metrics.buttonHeight, metrics.defaultAppsButtonHeight);
        metrics.buttonTop = metrics.footnoteTop + metrics.footnoteHeight + metrics.actionGap * 2;
        metrics.minimumClientHeight = metrics.buttonTop
            + metrics.buttonRowHeight
            + metrics.margin;
        return metrics;
    }

    int MeasureFileAssociationsDialogClientWidth(const FileAssociationsDialogState& state,
                                                 std::size_t formatCount)
    {
        const int defaultWidth = ScaleDialogAppTextDimension(
            kFileAssociationsDialogWidth,
            state.appTextSize,
            state.dpi);
        const FileAssociationsDialogLayoutMetrics metrics = BuildFileAssociationsDialogLayoutMetrics(
            defaultWidth,
            state,
            formatCount);
        return std::max(defaultWidth, metrics.minimumClientWidth);
    }

    int MeasureFileAssociationsDialogClientHeight(const FileAssociationsDialogState& state,
                                                  int clientWidth,
                                                  std::size_t formatCount)
    {
        const FileAssociationsDialogLayoutMetrics metrics = BuildFileAssociationsDialogLayoutMetrics(
            clientWidth,
            state,
            formatCount);
        return std::max(
            ScaleDialogAppTextDimension(kFileAssociationsDialogHeight, state.appTextSize, state.dpi),
            metrics.minimumClientHeight);
    }

    void LayoutFileAssociationsDialogControls(HWND hwnd, const FileAssociationsDialogState& state)
    {
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const FileAssociationsDialogLayoutMetrics metrics = BuildFileAssociationsDialogLayoutMetrics(
            clientWidth,
            state,
            state.formatCheckWindows.size());
        const int contentWidth = metrics.contentWidth;
        const int buttonRowTop = std::max(metrics.buttonTop, clientHeight - metrics.margin - metrics.buttonRowHeight);
        const int buttonTop = buttonRowTop + (metrics.buttonRowHeight - metrics.buttonHeight) / 2;
        const int cancelLeft = clientWidth - metrics.margin - ScaleDialogAppTextDimension(kFileAssociationsDialogButtonWidth, state.appTextSize, state.dpi);
        const int okLeft = cancelLeft - metrics.actionGap - ScaleDialogAppTextDimension(kFileAssociationsDialogButtonWidth, state.appTextSize, state.dpi);
        const int defaultAppsTop = buttonRowTop + (metrics.buttonRowHeight - metrics.defaultAppsButtonHeight) / 2;

        const HWND instructionWindow = GetDlgItem(hwnd, kFileAssociationsDialogInstructionControlId);
        if (instructionWindow)
        {
            MoveWindow(instructionWindow, metrics.margin, metrics.instructionTop, contentWidth, metrics.instructionHeight, TRUE);
        }

        const HWND selectAllButton = GetDlgItem(hwnd, kFileAssociationsDialogSelectAllControlId);
        if (selectAllButton)
        {
            MoveWindow(selectAllButton,
                       metrics.margin,
                       metrics.actionTop,
                       metrics.selectAllWidth,
                       metrics.buttonHeight,
                       TRUE);
        }

        const HWND clearAllButton = GetDlgItem(hwnd, kFileAssociationsDialogClearAllControlId);
        if (clearAllButton)
        {
            MoveWindow(clearAllButton,
                       metrics.margin + metrics.selectAllWidth + metrics.actionGap,
                       metrics.actionTop,
                       metrics.clearAllWidth,
                       metrics.buttonHeight,
                       TRUE);
        }

        const int columnGap = ScaleDialogAppTextDimension(28, state.appTextSize, state.dpi);
        const int columnWidth = std::max(0, (contentWidth - columnGap) / 2);
        const int formatLeft = metrics.margin + ScaleDialogAppTextDimension(16, state.appTextSize, state.dpi);
        const int descriptionRightInset = ScaleDialogAppTextDimension(24, state.appTextSize, state.dpi);
        const int formatColumnCount = std::max(1, static_cast<int>((state.formatCheckWindows.size() + 1) / 2));
        for (std::size_t index = 0; index < state.formatCheckWindows.size(); ++index)
        {
            const int column = index < static_cast<std::size_t>(formatColumnCount) ? 0 : 1;
            const int row = static_cast<int>(index % static_cast<std::size_t>(formatColumnCount));
            const int left = formatLeft + column * (columnWidth + columnGap);
            const HWND formatWindow = state.formatCheckWindows[index];
            if (formatWindow)
            {
                MoveWindow(formatWindow,
                           left,
                           metrics.formatGroupTop + metrics.formatGroupContentTop + row * metrics.formatRowHeight,
                           metrics.formatCheckboxWidth,
                           metrics.formatRowHeight,
                           TRUE);
            }
            if (index < state.formatDescriptionWindows.size())
            {
                const HWND descriptionWindow = state.formatDescriptionWindows[index];
                if (descriptionWindow)
                {
                    MoveWindow(descriptionWindow,
                               left + metrics.formatCheckboxWidth,
                               metrics.formatGroupTop + metrics.formatGroupContentTop + row * metrics.formatRowHeight,
                               std::max(0, columnWidth - metrics.formatCheckboxWidth - descriptionRightInset),
                               metrics.formatRowHeight,
                               TRUE);
                }
            }
        }

        const HWND footnoteWindow = GetDlgItem(hwnd, kFileAssociationsDialogFootnoteControlId);
        if (footnoteWindow)
        {
            MoveWindow(footnoteWindow,
                       metrics.margin,
                       metrics.footnoteTop,
                       contentWidth,
                       metrics.footnoteHeight,
                       TRUE);
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton,
                       okLeft,
                       buttonTop,
                       ScaleDialogAppTextDimension(kFileAssociationsDialogButtonWidth, state.appTextSize, state.dpi),
                       metrics.buttonHeight,
                       TRUE);
        }

        const HWND defaultAppsButton = GetDlgItem(hwnd, kFileAssociationsDialogDefaultAppsControlId);
        if (defaultAppsButton)
        {
            MoveWindow(defaultAppsButton,
                       metrics.margin,
                       defaultAppsTop,
                       metrics.defaultAppsButtonWidth,
                       metrics.defaultAppsButtonHeight,
                       TRUE);
        }

        const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
        if (cancelButton)
        {
            MoveWindow(cancelButton,
                       cancelLeft,
                       buttonTop,
                       ScaleDialogAppTextDimension(kFileAssociationsDialogButtonWidth, state.appTextSize, state.dpi),
                       metrics.buttonHeight,
                       TRUE);
        }
    }

    void DrawFileAssociationsFormatGroup(HDC dc,
                                         const FileAssociationsDialogState& state,
                                         int clientWidth)
    {
        if (!dc)
        {
            return;
        }

        const FileAssociationsDialogLayoutMetrics metrics = BuildFileAssociationsDialogLayoutMetrics(
            clientWidth,
            state,
            state.formatCheckWindows.size());
        const int captionGap = ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi);
        const int captionHeight = MeasureSingleLineTextHeight(state.bodyFont, 20);
        RECT frameRect{
            metrics.margin,
            metrics.formatGroupTop + captionHeight / 2,
            metrics.margin + metrics.contentWidth,
            metrics.formatGroupTop + metrics.formatGroupHeight};
        RECT captionRect{
            frameRect.left + captionGap,
            metrics.formatGroupTop,
            frameRect.left + captionGap + MeasureDialogButtonWidth(state.bodyFont,
                                                                      L"Supported formats",
                                                                      0),
            metrics.formatGroupTop + captionHeight};

        const HGDIOBJ oldFont = SelectObject(dc,
                                             state.bodyFont
                                                 ? state.bodyFont
                                                 : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, state.theme.windowBackground);
        SetTextColor(dc, state.theme.text);
        const HPEN framePen = CreatePen(PS_SOLID, 1, state.theme.border);
        const HGDIOBJ oldPen = SelectObject(dc, framePen);
        const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, frameRect.left, frameRect.top, frameRect.right, frameRect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(framePen);
        DrawTextW(dc,
                  L"Supported formats",
                  -1,
                  &captionRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }

    void SetFileAssociationChecks(FileAssociationsDialogState& state, bool checked)
    {
        for (std::size_t index = 0; index < state.formatCheckWindows.size(); ++index)
        {
            if (index < state.checkedDefaults.size())
            {
                state.checkedDefaults[index] = checked;
            }
            const HWND formatWindow = state.formatCheckWindows[index];
            if (formatWindow)
            {
                InvalidateRect(formatWindow, nullptr, FALSE);
            }
        }
    }

    void ToggleFileAssociationCheck(FileAssociationsDialogState& state, std::size_t index)
    {
        if (index >= state.checkedDefaults.size())
        {
            return;
        }

        state.checkedDefaults[index] = !state.checkedDefaults[index];
        if (index < state.formatCheckWindows.size() && state.formatCheckWindows[index])
        {
            InvalidateRect(state.formatCheckWindows[index], nullptr, FALSE);
            UpdateWindow(state.formatCheckWindows[index]);
        }
    }

    void DrawFileAssociationsCheckbox(const DRAWITEMSTRUCT& drawItem, const FileAssociationsDialogState& state)
    {
        if (!drawItem.hDC || !drawItem.hwndItem)
        {
            return;
        }

        RECT itemRect = drawItem.rcItem;
        const HBRUSH backgroundBrush = CreateSolidBrush(state.theme.windowBackground);
        FillRect(drawItem.hDC, &itemRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        const int controlId = static_cast<int>(GetDlgCtrlID(drawItem.hwndItem));
        const int formatIndex = controlId - kFileAssociationsDialogFormatBaseControlId;
        const bool checked = formatIndex >= 0
            && formatIndex < static_cast<int>(state.checkedDefaults.size())
            && state.checkedDefaults[static_cast<std::size_t>(formatIndex)];
        const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
        const int itemHeight = static_cast<int>(itemRect.bottom - itemRect.top);
        const int boxSize = std::min(18, std::max(12, itemHeight - 6));
        const int boxTop = itemRect.top + ((itemRect.bottom - itemRect.top) - boxSize) / 2;
        const RECT boxRect{itemRect.left + 2, boxTop, itemRect.left + 2 + boxSize, boxTop + boxSize};
        const COLORREF boxFill = disabled
            ? state.theme.surfaceBackground
            : (checked ? state.theme.accent : state.theme.fieldBackground);
        const COLORREF boxBorder = disabled ? state.theme.border : state.theme.accent;
        const HBRUSH boxBrush = CreateSolidBrush(boxFill);
        const HPEN boxPen = CreatePen(PS_SOLID, 1, boxBorder);
        const HGDIOBJ oldBrush = SelectObject(drawItem.hDC, boxBrush);
        const HGDIOBJ oldPen = SelectObject(drawItem.hDC, boxPen);
        RoundRect(drawItem.hDC, boxRect.left, boxRect.top, boxRect.right, boxRect.bottom, 4, 4);
        SelectObject(drawItem.hDC, oldPen);
        SelectObject(drawItem.hDC, oldBrush);
        DeleteObject(boxPen);
        DeleteObject(boxBrush);

        if (checked)
        {
            const HPEN markPen = CreatePen(PS_SOLID, 2, state.theme.accentText);
            const HGDIOBJ oldMarkPen = SelectObject(drawItem.hDC, markPen);
            MoveToEx(drawItem.hDC, boxRect.left + 4, boxRect.top + (boxSize / 2), nullptr);
            LineTo(drawItem.hDC, boxRect.left + (boxSize / 2) - 1, boxRect.bottom - 4);
            LineTo(drawItem.hDC, boxRect.right - 3, boxRect.top + 4);
            SelectObject(drawItem.hDC, oldMarkPen);
            DeleteObject(markPen);
        }

        wchar_t label[64]{};
        GetWindowTextW(drawItem.hwndItem, label, static_cast<int>(std::size(label)));
        RECT textRect{boxRect.right + 6, itemRect.top, itemRect.right, itemRect.bottom};
        SetBkMode(drawItem.hDC, TRANSPARENT);
        SetTextColor(drawItem.hDC, disabled ? state.theme.mutedText : state.theme.text);
        DrawTextW(drawItem.hDC,
                  label,
                  -1,
                  &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if ((drawItem.itemState & ODS_FOCUS) != 0)
        {
            RECT focusRect = textRect;
            InflateRect(&focusRect, -1, -2);
            DrawFocusRect(drawItem.hDC, &focusRect);
        }
    }

    bool CollectFileAssociationDialogResult(HWND hwnd, FileAssociationsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        state->selectedDefaults.clear();
        state->selectedDefaults.reserve(state->formatCheckWindows.size());
        for (std::size_t index = 0; index < state->formatCheckWindows.size(); ++index)
        {
            state->selectedDefaults.push_back(index < state->checkedDefaults.size() && state->checkedDefaults[index]);
        }

        std::wstring errorMessage;
        bool defaultsRejected = false;
        if (!hyperbrowse::services::ApplyFileAssociationDefaults(state->selectedDefaults,
                                                                 &errorMessage,
                                                                 &defaultsRejected))
        {
            const std::wstring message = (errorMessage.empty()
                ? L"Windows could not apply the selected file associations."
                : errorMessage)
                + (defaultsRejected ? L"\n\nOpen Windows Default Apps now?" : L"");
            const UINT messageBoxType = defaultsRejected
                ? MB_YESNO | MB_ICONWARNING
                : MB_OK | MB_ICONERROR;
            const int result = MessageBoxW(hwnd, message.c_str(), state->title.c_str(), messageBoxType);
            if (defaultsRejected && result == IDYES && !LaunchDefaultAppsSettings(hwnd))
            {
                MessageBoxW(hwnd,
                            L"Windows Default Apps could not be opened.",
                            state->title.c_str(),
                            MB_OK | MB_ICONERROR);
            }
            if (state->firstFormatWindow)
            {
                SetFocus(state->firstFormatWindow);
            }
            return false;
        }

        return true;
    }

    LRESULT CALLBACK FileAssociationsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<FileAssociationsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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
            state = reinterpret_cast<FileAssociationsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            state->backgroundBrush = CreateSolidBrush(state->theme.windowBackground);
            const HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            const std::span<const hyperbrowse::decode::SupportedFileType> supportedFileTypes =
                hyperbrowse::decode::SupportedFileTypes();

            const HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                44,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogInstructionControlId)),
                hInstance,
                nullptr);
            const HWND selectAllButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Select all",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogSelectAllControlId)),
                hInstance,
                nullptr);
            const HWND clearAllButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Clear all",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogClearAllControlId)),
                hInstance,
                nullptr);
            HWND defaultAppsButton = nullptr;
            if (IsWindows11OrGreater())
            {
                defaultAppsButton = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Default &Apps...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0,
                    0,
                    kFileAssociationsDialogDefaultAppsButtonWidth,
                    kFileAssociationsDialogDefaultAppsButtonHeight,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogDefaultAppsControlId)),
                    hInstance,
                    nullptr);
            }

            state->formatCheckWindows.reserve(supportedFileTypes.size());
            for (std::size_t index = 0; index < supportedFileTypes.size(); ++index)
            {
                const auto& fileType = supportedFileTypes[index];
                std::wstring label = L".";
                label.append(fileType.extension);
                const HWND formatWindow = CreateWindowExW(
                    0,
                    L"BUTTON",
                    label.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    0,
                    0,
                    100,
                    22,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogFormatBaseControlId
                                                                  + static_cast<int>(index))),
                    hInstance,
                    nullptr);
                const HWND descriptionWindow = CreateWindowExW(
                    0,
                    L"STATIC",
                    fileType.description,
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
                    0,
                    0,
                    100,
                    20,
                    hwnd,
                    nullptr,
                    hInstance,
                    nullptr);
                state->formatCheckWindows.push_back(formatWindow);
                state->formatDescriptionWindows.push_back(descriptionWindow);
                if (!state->firstFormatWindow)
                {
                    state->firstFormatWindow = formatWindow;
                }
            }

            const HWND footnoteWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->footnote.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0,
                0,
                100,
                48,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogFootnoteControlId)),
                hInstance,
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                hInstance,
                nullptr);
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                hInstance,
                nullptr);

            const HWND windows[] = {
                instructionWindow,
                footnoteWindow,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            const HWND buttons[] = {
                selectAllButton,
                clearAllButton,
                defaultAppsButton,
                state->okButton,
                cancelButton,
            };
            for (HWND button : buttons)
            {
                if (button)
                {
                    if (state->darkMode)
                    {
                        SetWindowTheme(button, L"", L"");
                    }
                    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            for (const HWND formatWindow : state->formatCheckWindows)
            {
                if (formatWindow)
                {
                    SendMessageW(formatWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            for (const HWND descriptionWindow : state->formatDescriptionWindows)
            {
                if (descriptionWindow)
                {
                    SendMessageW(descriptionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            if (instructionWindow && state->bodyFont)
            {
                SendMessageW(instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }
            if (footnoteWindow && state->bodyFont)
            {
                SendMessageW(footnoteWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }

            LayoutFileAssociationsDialogControls(hwnd, *state);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutFileAssociationsDialogControls(hwnd, *state);
            }
            return 0;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = std::max<UINT>(96, HIWORD(wParam));
                const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
                DeleteFontIfOwned(state->bodyFont);
                state->bodyFont = CreateDialogUiFont(10, FW_NORMAL, state->appTextSize, state->dpi);
                const HFONT font = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                EnumChildWindows(hwnd,
                                 [](HWND child, LPARAM parameter) -> BOOL
                                 {
                                     SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), TRUE);
                                     return TRUE;
                                 },
                                 reinterpret_cast<LPARAM>(font));

                const std::size_t formatCount = state->formatCheckWindows.size();
                const int clientWidth = MeasureFileAssociationsDialogClientWidth(*state, formatCount);
                const int clientHeight = MeasureFileAssociationsDialogClientHeight(*state, clientWidth, formatCount);
                RECT desiredFrame{0, 0, clientWidth, clientHeight};
                AdjustDialogWindowRectForDpi(
                    &desiredFrame,
                    WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                    FALSE,
                    WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                    state->dpi);

                RECT adjustedRect = suggestedRect ? *suggestedRect : RECT{};
                if (!suggestedRect)
                {
                    GetWindowRect(hwnd, &adjustedRect);
                }
                adjustedRect.right = adjustedRect.left + (desiredFrame.right - desiredFrame.left);
                adjustedRect.bottom = adjustedRect.top + (desiredFrame.bottom - desiredFrame.top);
                adjustedRect = ClampDialogFrameToWorkArea(
                    adjustedRect,
                    MeasureDialogShellMetrics(hwnd, state->appTextSize).workArea);
                if (adjustedRect.right > adjustedRect.left && adjustedRect.bottom > adjustedRect.top)
                {
                    SetWindowPos(hwnd,
                                 nullptr,
                                 adjustedRect.left,
                                 adjustedRect.top,
                                 adjustedRect.right - adjustedRect.left,
                                 adjustedRect.bottom - adjustedRect.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                LayoutFileAssociationsDialogControls(hwnd, *state);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_PAINT:
            if (state)
            {
                PAINTSTRUCT paint{};
                BeginPaint(hwnd, &paint);
                RECT clientRect{};
                GetClientRect(hwnd, &clientRect);
                DrawFileAssociationsFormatGroup(paint.hdc, *state, clientRect.right - clientRect.left);
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state)
            {
                SetFocus(state->firstFormatWindow ? state->firstFormatWindow : state->okButton);
                return FALSE;
            }
            break;
        case WM_DRAWITEM:
            if (state)
            {
                const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                const int controlId = drawItem ? static_cast<int>(drawItem->CtlID) : 0;
                if (drawItem
                    && drawItem->CtlType == ODT_BUTTON
                    && controlId >= kFileAssociationsDialogFormatBaseControlId
                    && controlId < kFileAssociationsDialogFormatBaseControlId
                        + static_cast<int>(state->formatCheckWindows.size()))
                {
                    DrawFileAssociationsCheckbox(*drawItem, *state);
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLORDLG:
            return state && state->backgroundBrush
                ? reinterpret_cast<INT_PTR>(state->backgroundBrush)
                : 0;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLORBTN:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush)
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == kFileAssociationsDialogSelectAllControlId)
            {
                SetFileAssociationChecks(*state, true);
                return 0;
            }

            if (LOWORD(wParam) == kFileAssociationsDialogClearAllControlId)
            {
                SetFileAssociationChecks(*state, false);
                return 0;
            }

            if (HIWORD(wParam) == BN_CLICKED
                && LOWORD(wParam) >= kFileAssociationsDialogFormatBaseControlId
                && LOWORD(wParam) < kFileAssociationsDialogFormatBaseControlId
                    + static_cast<int>(state->formatCheckWindows.size()))
            {
                const int formatIndex = static_cast<int>(LOWORD(wParam)) - kFileAssociationsDialogFormatBaseControlId;
                if (formatIndex >= 0)
                {
                    ToggleFileAssociationCheck(*state, static_cast<std::size_t>(formatIndex));
                }
                return 0;
            }

            if (LOWORD(wParam) == kFileAssociationsDialogDefaultAppsControlId)
            {
                if (!LaunchDefaultAppsSettings(hwnd))
                {
                    MessageBoxW(hwnd,
                                L"Windows Default Apps could not be opened.",
                                state->title.c_str(),
                                MB_OK | MB_ICONERROR);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (CollectFileAssociationDialogResult(hwnd, state))
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
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
                if (state->backgroundBrush)
                {
                    DeleteObject(state->backgroundBrush);
                    state->backgroundBrush = nullptr;
                }
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForFileAssociations(HWND ownerWindow,
                                   HINSTANCE instance,
                                   hyperbrowse::util::AppTextSize appTextSize,
                                   bool darkTheme,
                                   const std::vector<bool>& initialDefaults,
                                   std::vector<bool>* selectedDefaults)
    {
        if (!selectedDefaults
            || initialDefaults.size() != hyperbrowse::decode::SupportedFileTypes().size())
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kFileAssociationsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &FileAssociationsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kFileAssociationsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        FileAssociationsDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.theme = hyperbrowse::ui::MakeDialogTheme(darkTheme);
        state.darkMode = darkTheme;
        state.bodyFont = CreateDialogUiFont(10, FW_NORMAL, state.appTextSize, state.dpi);
        state.title = L"File Associations";
        state.instruction = L"Select the formats HyperBrowse should open by default.";
        state.footnote = L"Checked formats become defaults; unchecked formats are left unchanged. Windows may protect an existing choice; use Default apps if a format is rejected.";
        state.initialDefaults = initialDefaults;
        state.checkedDefaults = initialDefaults;

        const std::size_t formatCount = hyperbrowse::decode::SupportedFileTypes().size();
        const int clientWidth = MeasureFileAssociationsDialogClientWidth(state, formatCount);
        const int clientHeight = MeasureFileAssociationsDialogClientHeight(state, clientWidth, formatCount);
        RECT windowRect{0, 0, clientWidth, clientHeight};
        AdjustDialogWindowRectForDpi(&windowRect,
                                     WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                     FALSE,
                                     WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                                     state.dpi);
        windowRect = ClampDialogFrameToWorkArea(
            windowRect,
            MeasureDialogShellMetrics(ownerWindow, state.appTextSize).workArea);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kFileAssociationsDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
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
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());
    ApplyWindowFrameTheme(dialogWindow,
                  darkTheme,
                  state.theme.windowBackground,
                  state.theme.text,
                  state.theme.border);
    RefreshWindowNonClientArea(dialogWindow);
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

        DeleteFontIfOwned(state.bodyFont);
        if (!state.accepted)
        {
            return false;
        }

        *selectedDefaults = std::move(state.selectedDefaults);
        return true;
    }

    bool CollectSlideshowSettingsDialogResult(HWND hwnd, SlideshowSettingsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        const int selectedIndex = state->transitionComboWindow
            ? static_cast<int>(SendMessageW(state->transitionComboWindow, CB_GETCURSEL, 0, 0))
            : -1;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(kSlideshowTransitionOptions.size()))
        {
            MessageBoxW(hwnd,
                        L"Select a transition type.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->transitionComboWindow)
            {
                SetFocus(state->transitionComboWindow);
            }
            return false;
        }

        UINT slideshowDurationMs = 0;
        if (!TryReadDialogUInt(state->durationEditWindow,
                               kSlideshowMinimumDurationMs,
                               kSlideshowMaximumDurationMs,
                               &slideshowDurationMs))
        {
            MessageBoxW(hwnd,
                        L"Slide duration must be between 250 and 60000 milliseconds.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->durationEditWindow)
            {
                SetFocus(state->durationEditWindow);
            }
            return false;
        }

        UINT transitionDurationMs = 0;
        if (!TryReadDialogUInt(state->transitionDurationEditWindow,
                               kSlideshowMinimumTransitionDurationMs,
                               kSlideshowMaximumTransitionDurationMs,
                               &transitionDurationMs))
        {
            MessageBoxW(hwnd,
                        L"Transition duration must be between 100 and 5000 milliseconds.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->transitionDurationEditWindow)
            {
                SetFocus(state->transitionDurationEditWindow);
            }
            return false;
        }

        state->transitionStyle = kSlideshowTransitionOptions[static_cast<std::size_t>(selectedIndex)].style;
        state->slideshowDurationMs = slideshowDurationMs;
        state->transitionDurationMs = transitionDurationMs;
        return true;
    }

    SlideshowSettingsDialogLayoutMetrics BuildSlideshowSettingsDialogLayoutMetrics(
        const SlideshowSettingsDialogState& state)
    {
        SlideshowSettingsDialogLayoutMetrics metrics;
        metrics.margin = ScaleDialogAppTextDimension(kTextInputDialogMargin, state.appTextSize, state.dpi);
        metrics.contentWidth = ScaleDialogAppTextDimension(
            kSlideshowSettingsDialogWidth,
            state.appTextSize,
            state.dpi) - metrics.margin * 2;
        metrics.lineHeight = MeasureSingleLineTextHeight(state.bodyFont, 20);
        metrics.instructionHeight = MeasureTextBlockHeight(state.bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           metrics.lineHeight + ScaleDialogAppTextDimension(4, state.appTextSize, state.dpi));
        metrics.instructionHeight = std::max(
            metrics.instructionHeight,
            ScaleDialogAppTextDimension(44, state.appTextSize, state.dpi));
        metrics.labelWidth = std::max(
            ScaleDialogAppTextDimension(170, state.appTextSize, state.dpi),
            std::max(MeasureDialogButtonWidth(state.bodyFont, L"Transition type:", 0),
                     MeasureDialogButtonWidth(state.bodyFont, L"Transition duration:", 0)));
        metrics.valueWidth = ScaleDialogAppTextDimension(300, state.appTextSize, state.dpi);
        metrics.numericEditWidth = ScaleDialogAppTextDimension(120, state.appTextSize, state.dpi);
        metrics.spinWidth = ScaleDialogAppTextDimension(22, state.appTextSize, state.dpi);
        metrics.controlHeight = std::max(
            ScaleDialogAppTextDimension(kTextInputEditHeight, state.appTextSize, state.dpi),
            metrics.lineHeight + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi));
        metrics.rowGap = ScaleDialogAppTextDimension(10, state.appTextSize, state.dpi);
        metrics.transitionTop = metrics.margin + std::max(
            ScaleDialogAppTextDimension(56, state.appTextSize, state.dpi),
            metrics.instructionHeight + ScaleDialogAppTextDimension(12, state.appTextSize, state.dpi));
        metrics.durationTop = metrics.transitionTop + metrics.controlHeight + metrics.rowGap;
        metrics.transitionDurationTop = metrics.durationTop + metrics.controlHeight + metrics.rowGap;
        metrics.footnoteTop = metrics.transitionDurationTop
            + metrics.controlHeight
            + ScaleDialogAppTextDimension(20, state.appTextSize, state.dpi);
        metrics.footnoteHeight = MeasureTextBlockHeight(state.bodyFont,
                                                        state.footnote,
                                                        metrics.contentWidth,
                                                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                        metrics.lineHeight);
        metrics.footnoteHeight = std::max(
            metrics.footnoteHeight,
            ScaleDialogAppTextDimension(54, state.appTextSize, state.dpi));
        metrics.dividerTop = metrics.footnoteTop
            + metrics.footnoteHeight
            + ScaleDialogAppTextDimension(56, state.appTextSize, state.dpi);
        metrics.buttonHeight = std::max(
            ScaleDialogAppTextDimension(kTextInputButtonHeight, state.appTextSize, state.dpi),
            metrics.lineHeight + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi));
        metrics.applyButtonWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Apply",
            ScaleDialogAppTextDimension(kTextInputButtonWidth, state.appTextSize, state.dpi));
        metrics.cancelButtonWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Cancel",
            ScaleDialogAppTextDimension(kTextInputButtonWidth, state.appTextSize, state.dpi));
        metrics.buttonTop = metrics.dividerTop + ScaleDialogAppTextDimension(20, state.appTextSize, state.dpi);
        metrics.minimumClientHeight = metrics.buttonTop
            + metrics.buttonHeight
            + ScaleDialogAppTextDimension(26, state.appTextSize, state.dpi);
        return metrics;
    }

    LRESULT CALLBACK SlideshowSettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<SlideshowSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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
            state = reinterpret_cast<SlideshowSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            state->backgroundBrush = CreateSolidBrush(state->theme.windowBackground);
            state->fieldBrush = CreateSolidBrush(state->theme.fieldBackground);
            const HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            const SlideshowSettingsDialogLayoutMetrics metrics = BuildSlideshowSettingsDialogLayoutMetrics(*state);
            const int contentLeft = metrics.margin;
            const int contentWidth = metrics.contentWidth;
            const int labelWidth = metrics.labelWidth;
            const int valueWidth = metrics.valueWidth;
            const int transitionTop = metrics.transitionTop;
            const int durationTop = metrics.durationTop;
            const int transitionDurationTop = metrics.transitionDurationTop;
            const int footnoteTop = metrics.footnoteTop;
            const int dividerTop = metrics.dividerTop;
            const int buttonTop = metrics.buttonTop;
            const int cancelLeft = ScaleDialogAppTextDimension(
                kSlideshowSettingsDialogWidth,
                state->appTextSize,
                state->dpi) - metrics.margin - metrics.cancelButtonWidth;
            const int okLeft = cancelLeft - metrics.rowGap - metrics.applyButtonWidth;
            const int numericEditWidth = metrics.numericEditWidth;
            const int spinWidth = metrics.spinWidth;
            const int labelTopOffset = (metrics.controlHeight - metrics.lineHeight) / 2;
            const int footnoteHeight = metrics.footnoteHeight;

            const HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                metrics.margin,
                contentWidth,
                metrics.instructionHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsInstructionControlId)),
                hInstance,
                nullptr);
            const HWND transitionLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Transition type:",
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                transitionTop + labelTopOffset,
                labelWidth,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionLabelControlId)),
                hInstance,
                nullptr);
            state->transitionComboWindow = CreateWindowExW(
                0,
                L"COMBOBOX",
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                contentLeft + labelWidth,
                transitionTop,
                valueWidth,
                ScaleDialogAppTextDimension(140, state->appTextSize, state->dpi),
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionComboControlId)),
                hInstance,
                nullptr);
            const HWND durationLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Slide duration:",
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                durationTop + labelTopOffset,
                labelWidth,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationLabelControlId)),
                hInstance,
                nullptr);
            state->durationEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                std::to_wstring(state->slideshowDurationMs).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                contentLeft + labelWidth,
                durationTop,
                numericEditWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationEditControlId)),
                hInstance,
                nullptr);
            state->durationSpinWindow = CreateWindowExW(
                0,
                UPDOWN_CLASSW,
                nullptr,
                WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                contentLeft + labelWidth + numericEditWidth,
                durationTop,
                spinWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationSpinControlId)),
                hInstance,
                nullptr);
            const HWND durationUnit = CreateWindowExW(
                0,
                L"STATIC",
                L"ms",
                WS_CHILD | WS_VISIBLE,
                contentLeft + labelWidth + numericEditWidth + spinWidth + 8,
                durationTop + labelTopOffset,
                ScaleDialogAppTextDimension(36, state->appTextSize, state->dpi),
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationUnitControlId)),
                hInstance,
                nullptr);
            const HWND transitionDurationLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Transition duration:",
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                transitionDurationTop + labelTopOffset,
                labelWidth,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationLabelControlId)),
                hInstance,
                nullptr);
            state->transitionDurationEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                std::to_wstring(state->transitionDurationMs).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                contentLeft + labelWidth,
                transitionDurationTop,
                numericEditWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationEditControlId)),
                hInstance,
                nullptr);
            state->transitionDurationSpinWindow = CreateWindowExW(
                0,
                UPDOWN_CLASSW,
                nullptr,
                WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                contentLeft + labelWidth + numericEditWidth,
                transitionDurationTop,
                spinWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationSpinControlId)),
                hInstance,
                nullptr);
            const HWND transitionDurationUnit = CreateWindowExW(
                0,
                L"STATIC",
                L"ms",
                WS_CHILD | WS_VISIBLE,
                contentLeft + labelWidth + numericEditWidth + spinWidth + ScaleDialogAppTextDimension(8, state->appTextSize, state->dpi),
                transitionDurationTop + labelTopOffset,
                ScaleDialogAppTextDimension(36, state->appTextSize, state->dpi),
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationUnitControlId)),
                hInstance,
                nullptr);
            const HWND footnoteWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->footnote.c_str(),
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                footnoteTop,
                contentWidth,
                footnoteHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsFootnoteControlId)),
                hInstance,
                nullptr);
            const HWND dividerWindow = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                contentLeft,
                dividerTop,
                contentWidth,
                ScaleDialogAppTextDimension(2, state->appTextSize, state->dpi),
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDividerControlId)),
                hInstance,
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                okLeft,
                buttonTop,
                metrics.applyButtonWidth,
                metrics.buttonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                hInstance,
                nullptr);
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                cancelLeft,
                buttonTop,
                metrics.cancelButtonWidth,
                metrics.buttonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                hInstance,
                nullptr);

            const HWND windows[] = {
                instructionWindow,
                transitionLabel,
                state->transitionComboWindow,
                durationLabel,
                state->durationEditWindow,
                state->durationSpinWindow,
                durationUnit,
                transitionDurationLabel,
                state->transitionDurationEditWindow,
                state->transitionDurationSpinWindow,
                transitionDurationUnit,
                footnoteWindow,
                dividerWindow,
                state->okButton,
                cancelButton,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }

            if (state->durationEditWindow)
            {
                SendMessageW(state->durationEditWindow, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            }
            if (state->durationSpinWindow)
            {
                SendMessageW(state->durationSpinWindow,
                             UDM_SETRANGE32,
                             kSlideshowMinimumDurationMs,
                             kSlideshowMaximumDurationMs);
                SendMessageW(state->durationSpinWindow,
                             UDM_SETBUDDY,
                             reinterpret_cast<WPARAM>(state->durationEditWindow),
                             0);
                SetDialogUIntEditAndSpin(state->durationEditWindow,
                                         state->durationSpinWindow,
                                         state->slideshowDurationMs);
                const UDACCEL accelerations[] = {
                    {0, 500},
                    {3, 1000},
                    {8, 5000},
                };
                SendMessageW(state->durationSpinWindow,
                             UDM_SETACCEL,
                             static_cast<WPARAM>(std::size(accelerations)),
                             reinterpret_cast<LPARAM>(accelerations));
            }
            if (state->transitionDurationEditWindow)
            {
                SendMessageW(state->transitionDurationEditWindow, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            }
            if (state->transitionDurationSpinWindow)
            {
                SendMessageW(state->transitionDurationSpinWindow,
                             UDM_SETRANGE32,
                             kSlideshowMinimumTransitionDurationMs,
                             kSlideshowMaximumTransitionDurationMs);
                SendMessageW(state->transitionDurationSpinWindow,
                             UDM_SETBUDDY,
                             reinterpret_cast<WPARAM>(state->transitionDurationEditWindow),
                             0);
                SetDialogUIntEditAndSpin(state->transitionDurationEditWindow,
                                         state->transitionDurationSpinWindow,
                                         state->transitionDurationMs);
                const UDACCEL accelerations[] = {
                    {0, 50},
                    {3, 100},
                    {8, 250},
                };
                SendMessageW(state->transitionDurationSpinWindow,
                             UDM_SETACCEL,
                             static_cast<WPARAM>(std::size(accelerations)),
                             reinterpret_cast<LPARAM>(accelerations));
            }

            if (state->transitionComboWindow)
            {
                for (const SlideshowTransitionOption& option : kSlideshowTransitionOptions)
                {
                    SendMessageW(state->transitionComboWindow, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label));
                }
                SendMessageW(state->transitionComboWindow,
                             CB_SETCURSEL,
                             SlideshowTransitionComboIndex(state->transitionStyle),
                             0);
            }

            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state)
            {
                SetFocus(state->transitionComboWindow ? state->transitionComboWindow : state->okButton);
                return FALSE;
            }
            break;
        case WM_DPICHANGED:
            if (state)
            {
                const UINT oldDpi = state->dpi;
                state->dpi = std::max<UINT>(96, HIWORD(wParam));
                const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
                if (suggestedRect)
                {
                    const RECT adjustedRect = ClampDialogFrameToWorkArea(
                        *suggestedRect,
                        MeasureDialogShellMetrics(hwnd, state->appTextSize).workArea);
                    SetWindowPos(hwnd, nullptr,
                                 adjustedRect.left,
                                 adjustedRect.top,
                                 adjustedRect.right - adjustedRect.left,
                                 adjustedRect.bottom - adjustedRect.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                ScaleDialogChildWindows(hwnd, oldDpi, state->dpi);
                DeleteFontIfOwned(state->bodyFont);
                state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
                const HFONT font = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                EnumChildWindows(hwnd,
                                 [](HWND child, LPARAM parameter) -> BOOL
                                 {
                                     SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), TRUE);
                                     return TRUE;
                                 },
                                 reinterpret_cast<LPARAM>(font));
            }
            return 0;
        case WM_CTLCOLORDLG:
            return state && state->backgroundBrush
                ? reinterpret_cast<INT_PTR>(state->backgroundBrush)
                : 0;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLOREDIT:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.fieldBackground);
                return reinterpret_cast<INT_PTR>(state->fieldBrush);
            }
            break;
        case WM_CTLCOLORLISTBOX:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.fieldBackground);
                return reinterpret_cast<INT_PTR>(state->fieldBrush);
            }
            break;
        case WM_CTLCOLORBTN:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush)
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_NOTIFY:
            if (!state)
            {
                break;
            }

            if (const auto* notify = reinterpret_cast<const NMHDR*>(lParam);
                notify && notify->code == UDN_DELTAPOS)
            {
                const auto* upDown = reinterpret_cast<const NMUPDOWN*>(lParam);
                if (notify->idFrom == kSlideshowSettingsDurationSpinControlId)
                {
                    const UINT nextValue = ComputeNextSpinValue(state->durationEditWindow,
                                                                upDown->iPos,
                                                                upDown->iDelta,
                                                                kSlideshowMinimumDurationMs,
                                                                kSlideshowMaximumDurationMs);
                    SetDialogUIntEditAndSpin(state->durationEditWindow, state->durationSpinWindow, nextValue);
                    return TRUE;
                }

                if (notify->idFrom == kSlideshowSettingsTransitionDurationSpinControlId)
                {
                    const UINT nextValue = ComputeNextSpinValue(state->transitionDurationEditWindow,
                                                                upDown->iPos,
                                                                upDown->iDelta,
                                                                kSlideshowMinimumTransitionDurationMs,
                                                                kSlideshowMaximumTransitionDurationMs);
                    SetDialogUIntEditAndSpin(state->transitionDurationEditWindow,
                                             state->transitionDurationSpinWindow,
                                             nextValue);
                    return TRUE;
                }
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (CollectSlideshowSettingsDialogResult(hwnd, state))
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
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
                if (state->backgroundBrush)
                {
                    DeleteObject(state->backgroundBrush);
                    state->backgroundBrush = nullptr;
                }
                if (state->fieldBrush)
                {
                    DeleteObject(state->fieldBrush);
                    state->fieldBrush = nullptr;
                }
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForSlideshowSettings(HWND ownerWindow,
                                    HINSTANCE instance,
                                    hyperbrowse::util::AppTextSize appTextSize,
                                    bool darkTheme,
                                    UINT initialSlideshowDurationMs,
                                    hyperbrowse::viewer::TransitionStyle initialTransitionStyle,
                                    UINT initialTransitionDurationMs,
                                    UINT* slideshowDurationMs,
                                    hyperbrowse::viewer::TransitionStyle* transitionStyle,
                                    UINT* transitionDurationMs)
    {
        if (!slideshowDurationMs || !transitionStyle || !transitionDurationMs)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kSlideshowSettingsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &SlideshowSettingsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kSlideshowSettingsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        SlideshowSettingsDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.theme = hyperbrowse::ui::MakeDialogTheme(darkTheme);
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize, state.dpi);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        state.title = L"Slideshow Settings";
        state.instruction = L"Choose transition style and precise slideshow timing.";
        state.footnote = L"Random selects from the animated transition styles for each transition. None uses hard cuts.";
        state.slideshowDurationMs = NormalizeSlideshowDuration(initialSlideshowDurationMs);
        state.transitionDurationMs = NormalizeSlideshowTransitionDuration(initialTransitionDurationMs);
        state.transitionStyle = initialTransitionStyle;

        const SlideshowSettingsDialogLayoutMetrics layoutMetrics = BuildSlideshowSettingsDialogLayoutMetrics(state);
        state.dialogHeight = std::max(
            ScaleDialogAppTextDimension(kSlideshowSettingsDialogHeight, state.appTextSize, state.dpi),
            layoutMetrics.minimumClientHeight);
        RECT windowRect{0, 0,
                        ScaleDialogAppTextDimension(kSlideshowSettingsDialogWidth, state.appTextSize, state.dpi),
                        state.dialogHeight};
        AdjustDialogWindowRectForDpi(&windowRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);
        windowRect = ClampDialogFrameToWorkArea(
            windowRect,
            MeasureDialogShellMetrics(ownerWindow, state.appTextSize).workArea);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kSlideshowSettingsDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
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
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

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

        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *slideshowDurationMs = state.slideshowDurationMs;
        *transitionStyle = state.transitionStyle;
        *transitionDurationMs = state.transitionDurationMs;
        return true;
    }

    bool IsWindows11OrGreater()
    {
        HKEY versionKey{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0,
                          KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                          &versionKey) != ERROR_SUCCESS)
        {
            return false;
        }

        wchar_t buildBuffer[32]{};
        DWORD valueType = 0;
        DWORD valueBytes = sizeof(buildBuffer);
        const LSTATUS result = RegQueryValueExW(versionKey,
                                                L"CurrentBuildNumber",
                                                nullptr,
                                                &valueType,
                                                reinterpret_cast<LPBYTE>(buildBuffer),
                                                &valueBytes);
        RegCloseKey(versionKey);
        if (result != ERROR_SUCCESS || valueType != REG_SZ)
        {
            return false;
        }

        wchar_t* parseEnd = nullptr;
        const unsigned long buildNumber = wcstoul(buildBuffer, &parseEnd, 10);
        return parseEnd != buildBuffer && *parseEnd == L'\0' && buildNumber >= 22000;
    }

    bool LaunchDefaultAppsSettings(HWND ownerWindow)
    {
        if (IsWindows11OrGreater()
            && LaunchShellTarget(ownerWindow, L"open", L"ms-settings:defaultapps?registeredAppUser=HyperBrowse"))
        {
            return true;
        }

        return LaunchShellTarget(ownerWindow, L"open", L"ms-settings:defaultapps");
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::ShowFileAssociationsDialog()
    {
        std::vector<bool> defaults;
        std::wstring errorMessage;
        if (!services::QueryFileAssociationDefaults(&defaults, &errorMessage))
        {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"Windows could not read the current file associations." : errorMessage.c_str(),
                        L"File Associations",
                        MB_OK | MB_ICONERROR);
            return;
        }

        std::vector<bool> selectedDefaults;
        PromptForFileAssociations(hwnd_,
                      instance_,
                      appTextSize_,
                      themeMode_ == ThemeMode::Dark,
                      defaults,
                      &selectedDefaults);
    }

    void MainWindow::ShowSlideshowSettingsDialog()
    {
        UINT slideshowDurationMs = NormalizeSlideshowDuration(slideshowIntervalMs_);
        viewer::TransitionStyle transitionStyle = slideshowTransitionStyle_;
        UINT transitionDurationMs = NormalizeSlideshowTransitionDuration(slideshowTransitionDurationMs_);
        if (!PromptForSlideshowSettings(hwnd_,
                                        instance_,
                                        appTextSize_,
                                        themeMode_ == ThemeMode::Dark,
                                        slideshowIntervalMs_,
                                        slideshowTransitionStyle_,
                                        slideshowTransitionDurationMs_,
                                        &slideshowDurationMs,
                                        &transitionStyle,
                                        &transitionDurationMs))
        {
            return;
        }

        const bool slideshowDurationChanged = slideshowIntervalMs_ != slideshowDurationMs;
        const bool transitionStyleChanged = slideshowTransitionStyle_ != transitionStyle;
        const bool transitionDurationChanged = slideshowTransitionDurationMs_ != transitionDurationMs;
        if (!slideshowDurationChanged && !transitionStyleChanged && !transitionDurationChanged)
        {
            return;
        }

        slideshowIntervalMs_ = NormalizeSlideshowDuration(slideshowDurationMs);
        slideshowTransitionStyle_ = transitionStyle;
        slideshowTransitionDurationMs_ = NormalizeSlideshowTransitionDuration(transitionDurationMs);
        ApplyViewerTransitionSettings();
        if (slideshowDurationChanged)
        {
            for (viewer::ViewerWindow* viewer : OpenViewerWindows())
            {
                if (viewer->IsSlideshowActive())
                {
                    viewer->StartSlideshow(slideshowIntervalMs_);
                }
            }
        }
        UpdateMenuState();
    }
}
