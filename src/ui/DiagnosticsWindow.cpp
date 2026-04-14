#include "ui/DiagnosticsWindow.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "app/resource.h"

namespace
{
    constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacyAttribute = 19;

    constexpr int kMargin = 18;
    constexpr int kSectionGap = 12;
    constexpr int kTitleHeight = 34;
    constexpr int kSummaryHeight = 84;
    constexpr int kSectionLabelHeight = 24;

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

    HFONT CreateUiFont(int pointSize, int weight)
    {
        HDC screenDc = GetDC(nullptr);
        const int logPixelsY = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        return CreateFontW(
            -MulDiv(pointSize, logPixelsY, 72),
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    void AddListColumn(HWND listView, int index, int width, const wchar_t* title)
    {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = LVCFMT_LEFT;
        column.cx = width;
        column.pszText = const_cast<LPWSTR>(title);
        ListView_InsertColumn(listView, index, &column);
    }

    void AddListItem(HWND listView, int rowIndex, const std::wstring& firstColumn)
    {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = rowIndex;
        item.pszText = const_cast<LPWSTR>(firstColumn.c_str());
        ListView_InsertItem(listView, &item);
    }

    std::wstring FormatMilliseconds(double milliseconds)
    {
        std::wostringstream stream;
        stream << std::fixed << std::setprecision(2) << milliseconds;
        return stream.str();
    }

    std::wstring BuildUpdatedTimestamp()
    {
        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);

        std::wostringstream stream;
        stream << std::setfill(L'0')
               << std::setw(2) << localTime.wHour << L":"
               << std::setw(2) << localTime.wMinute << L":"
               << std::setw(2) << localTime.wSecond;
        return stream.str();
    }

    std::wstring ReadWindowText(HWND hwnd)
    {
        const int length = GetWindowTextLengthW(hwnd);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        if (length > 0)
        {
            GetWindowTextW(hwnd, text.data(), length + 1);
            text.resize(static_cast<std::size_t>(length));
        }
        else
        {
            text.clear();
        }
        return text;
    }

    int MeasureWindowTextHeight(HWND hwnd, HFONT font, int width, UINT format, int minimumHeight)
    {
        if (!hwnd || width <= 0)
        {
            return minimumHeight;
        }

        std::wstring text = ReadWindowText(hwnd);
        if (text.empty())
        {
            text = L" ";
        }

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return minimumHeight;
        }

        const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
        RECT bounds{0, 0, width, 0};
        DrawTextW(screenDc, text.c_str(), -1, &bounds, format | DT_CALCRECT);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);
        const int measuredHeight = static_cast<int>(bounds.bottom - bounds.top);
        return std::max(minimumHeight, measuredHeight);
    }
}

namespace hyperbrowse::ui
{
    DiagnosticsWindow::DiagnosticsWindow(HINSTANCE instance)
        : instance_(instance)
    {
    }

    DiagnosticsWindow::~DiagnosticsWindow()
    {
        if (hwnd_)
        {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }

        ReleaseFonts();
        ReleaseBrushes();
    }

    void DiagnosticsWindow::Show(HWND owner,
                                 std::wstring jpegPath,
                                 std::wstring rawPath,
                                 std::wstring folderScope,
                                 util::DiagnosticsSnapshot snapshot,
                                 bool darkTheme)
    {
        owner_ = owner;
        jpegPath_ = std::move(jpegPath);
        rawPath_ = std::move(rawPath);
        folderScope_ = std::move(folderScope);
        snapshot_ = std::move(snapshot);
        darkTheme_ = darkTheme;

        if (!EnsureWindow(owner_))
        {
            return;
        }

        ApplyTheme();
        RefreshView();
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(hwnd_);
    }

    void DiagnosticsWindow::SetDarkTheme(bool enabled)
    {
        darkTheme_ = enabled;
        if (hwnd_)
        {
            ApplyTheme();
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    }

    bool DiagnosticsWindow::IsOpen() const noexcept
    {
        return hwnd_ != nullptr;
    }

    bool DiagnosticsWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kWindowClassName, &windowClass) != FALSE)
        {
            return true;
        }

        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &DiagnosticsWindow::WindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kWindowClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        windowClass.hIconSm = static_cast<HICON>(
            LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON,
                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        return RegisterClassExW(&windowClass) != 0;
    }

    bool DiagnosticsWindow::EnsureWindow(HWND owner)
    {
        if (hwnd_)
        {
            return true;
        }

        if (!RegisterWindowClass())
        {
            return false;
        }

        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kWindowClassName,
            L"HyperBrowse Diagnostics",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1040,
            760,
            owner,
            nullptr,
            instance_,
            this);

        return hwnd_ != nullptr;
    }

    bool DiagnosticsWindow::CreateChildWindows()
    {
        titleLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"HyperBrowse Diagnostics",
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
            0, 0, 0, 0,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        summaryLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
            0, 0, 0, 0,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        timingsLabel_ = CreateWindowExW(0, L"STATIC", L"Timings", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        countersLabel_ = CreateWindowExW(0, L"STATIC", L"Counters", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        derivedLabel_ = CreateWindowExW(0, L"STATIC", L"Derived", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

        const DWORD listStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
        timingsList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, listStyle, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        countersList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, listStyle, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        derivedList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr, listStyle, 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

        if (!titleLabel_ || !summaryLabel_ || !timingsLabel_ || !countersLabel_ || !derivedLabel_
            || !timingsList_ || !countersList_ || !derivedList_)
        {
            return false;
        }

        CreateFonts();
        if (titleFont_)
        {
            SendMessageW(titleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
        }
        if (sectionFont_)
        {
            SendMessageW(timingsLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
            SendMessageW(countersLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
            SendMessageW(derivedLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        }
        if (bodyFont_)
        {
            SendMessageW(summaryLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
            SendMessageW(timingsList_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
            SendMessageW(countersList_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
            SendMessageW(derivedList_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        }

        const DWORD extendedStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP;
        ListView_SetExtendedListViewStyle(timingsList_, extendedStyle);
        ListView_SetExtendedListViewStyle(countersList_, extendedStyle);
        ListView_SetExtendedListViewStyle(derivedList_, extendedStyle);

        AddListColumn(timingsList_, 0, 270, L"Metric");
        AddListColumn(timingsList_, 1, 90, L"Count");
        AddListColumn(timingsList_, 2, 100, L"Avg (ms)");
        AddListColumn(timingsList_, 3, 100, L"Last (ms)");
        AddListColumn(timingsList_, 4, 100, L"Min (ms)");
        AddListColumn(timingsList_, 5, 100, L"Max (ms)");

        AddListColumn(countersList_, 0, 300, L"Counter");
        AddListColumn(countersList_, 1, 120, L"Value");

        AddListColumn(derivedList_, 0, 320, L"Metric");
        AddListColumn(derivedList_, 1, 160, L"Value");

        return true;
    }

    void DiagnosticsWindow::CreateFonts()
    {
        if (!titleFont_)
        {
            titleFont_ = CreateUiFont(16, FW_SEMIBOLD);
        }
        if (!sectionFont_)
        {
            sectionFont_ = CreateUiFont(10, FW_SEMIBOLD);
        }
        if (!bodyFont_)
        {
            bodyFont_ = CreateUiFont(9, FW_NORMAL);
        }
    }

    void DiagnosticsWindow::ReleaseFonts()
    {
        if (titleFont_)
        {
            DeleteObject(titleFont_);
            titleFont_ = nullptr;
        }
        if (sectionFont_)
        {
            DeleteObject(sectionFont_);
            sectionFont_ = nullptr;
        }
        if (bodyFont_)
        {
            DeleteObject(bodyFont_);
            bodyFont_ = nullptr;
        }
    }

    void DiagnosticsWindow::ReleaseBrushes()
    {
        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
            backgroundBrush_ = nullptr;
        }
    }

    void DiagnosticsWindow::LayoutChildren()
    {
        if (!hwnd_)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientWidth = client.right - client.left;
        const int clientHeight = client.bottom - client.top;

        const int topWidth = clientWidth - (kMargin * 2);
        int top = kMargin;

        const int titleHeight = MeasureWindowTextHeight(titleLabel_, titleFont_, topWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, kTitleHeight);
        const int summaryHeight = MeasureWindowTextHeight(summaryLabel_, bodyFont_, topWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, kSummaryHeight);
        const int sectionLabelHeight = std::max({MeasureWindowTextHeight(timingsLabel_, sectionFont_, topWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, kSectionLabelHeight),
                             MeasureWindowTextHeight(countersLabel_, sectionFont_, topWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, kSectionLabelHeight),
                             MeasureWindowTextHeight(derivedLabel_, sectionFont_, topWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, kSectionLabelHeight)});

        MoveWindow(titleLabel_, kMargin, top, topWidth, titleHeight, TRUE);
        top += titleHeight + 6;
        MoveWindow(summaryLabel_, kMargin, top, topWidth, summaryHeight, TRUE);
        top += summaryHeight + kSectionGap;

        MoveWindow(timingsLabel_, kMargin, top, topWidth, sectionLabelHeight, TRUE);
        top += sectionLabelHeight + 4;

        const int remainingHeight = clientHeight - top - kMargin;
        const int bottomSectionHeight = std::max(150, (remainingHeight - kSectionGap - sectionLabelHeight) / 2);
        const int timingsHeight = std::max(180, remainingHeight - bottomSectionHeight - kSectionGap - sectionLabelHeight - 4);

        MoveWindow(timingsList_, kMargin, top, topWidth, timingsHeight, TRUE);
        top += timingsHeight + kSectionGap;

        const int halfWidth = (topWidth - kSectionGap) / 2;
        MoveWindow(countersLabel_, kMargin, top, halfWidth, sectionLabelHeight, TRUE);
        MoveWindow(derivedLabel_, kMargin + halfWidth + kSectionGap, top, halfWidth, sectionLabelHeight, TRUE);
        top += sectionLabelHeight + 4;

        const int bottomHeight = clientHeight - top - kMargin;
        MoveWindow(countersList_, kMargin, top, halfWidth, bottomHeight, TRUE);
        MoveWindow(derivedList_, kMargin + halfWidth + kSectionGap, top, halfWidth, bottomHeight, TRUE);
    }

    void DiagnosticsWindow::ApplyTheme()
    {
        const ThemeColors colors = GetThemeColors();
        ReleaseBrushes();
        backgroundBrush_ = CreateSolidBrush(colors.windowBackground);

        ApplyWindowFrameTheme(hwnd_, darkTheme_);

        const HWND labels[] = {titleLabel_, summaryLabel_, timingsLabel_, countersLabel_, derivedLabel_};
        for (HWND label : labels)
        {
            if (label)
            {
                InvalidateRect(label, nullptr, TRUE);
            }
        }

        const HWND lists[] = {timingsList_, countersList_, derivedList_};
        for (HWND list : lists)
        {
            if (!list)
            {
                continue;
            }

            ListView_SetBkColor(list, colors.panelBackground);
            ListView_SetTextBkColor(list, colors.panelBackground);
            ListView_SetTextColor(list, colors.text);
            InvalidateRect(list, nullptr, TRUE);
        }

        if (hwnd_)
        {
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        }
    }

    void DiagnosticsWindow::RefreshView()
    {
        if (!hwnd_)
        {
            return;
        }

        PopulateSummary();
        PopulateTimingList();
        PopulateCounterList();
        PopulateDerivedList();
        LayoutChildren();
    }

    void DiagnosticsWindow::PopulateSummary()
    {
        std::wstring summary = L"Updated: ";
        summary.append(BuildUpdatedTimestamp());
        summary.append(L"\r\nJPEG Decode: ");
        summary.append(jpegPath_);
        summary.append(L"\r\nRAW Decode: ");
        summary.append(rawPath_);
        summary.append(L"\r\nFolder Scope: ");
        summary.append(folderScope_.empty() ? std::wstring(L"(none)") : folderScope_);
        SetWindowTextW(summaryLabel_, summary.c_str());
    }

    void DiagnosticsWindow::PopulateTimingList()
    {
        ListView_DeleteAllItems(timingsList_);
        if (snapshot_.timings.empty())
        {
            AddListItem(timingsList_, 0, L"No timings recorded yet.");
            ListView_SetItemText(timingsList_, 0, 1, const_cast<LPWSTR>(L"-"));
            ListView_SetItemText(timingsList_, 0, 2, const_cast<LPWSTR>(L"-"));
            ListView_SetItemText(timingsList_, 0, 3, const_cast<LPWSTR>(L"-"));
            ListView_SetItemText(timingsList_, 0, 4, const_cast<LPWSTR>(L"-"));
            ListView_SetItemText(timingsList_, 0, 5, const_cast<LPWSTR>(L"-"));
            return;
        }

        for (int index = 0; index < static_cast<int>(snapshot_.timings.size()); ++index)
        {
            const util::DiagnosticTimingRow& row = snapshot_.timings[static_cast<std::size_t>(index)];
            AddListItem(timingsList_, index, row.name);

            const std::wstring count = std::to_wstring(row.count);
            const std::wstring average = FormatMilliseconds(row.averageMs);
            const std::wstring last = FormatMilliseconds(row.lastMs);
            const std::wstring minimum = FormatMilliseconds(row.minMs);
            const std::wstring maximum = FormatMilliseconds(row.maxMs);
            ListView_SetItemText(timingsList_, index, 1, const_cast<LPWSTR>(count.c_str()));
            ListView_SetItemText(timingsList_, index, 2, const_cast<LPWSTR>(average.c_str()));
            ListView_SetItemText(timingsList_, index, 3, const_cast<LPWSTR>(last.c_str()));
            ListView_SetItemText(timingsList_, index, 4, const_cast<LPWSTR>(minimum.c_str()));
            ListView_SetItemText(timingsList_, index, 5, const_cast<LPWSTR>(maximum.c_str()));
        }
    }

    void DiagnosticsWindow::PopulateCounterList()
    {
        ListView_DeleteAllItems(countersList_);
        if (snapshot_.counters.empty())
        {
            AddListItem(countersList_, 0, L"No counters recorded yet.");
            ListView_SetItemText(countersList_, 0, 1, const_cast<LPWSTR>(L"-"));
            return;
        }

        for (int index = 0; index < static_cast<int>(snapshot_.counters.size()); ++index)
        {
            const util::DiagnosticCounterRow& row = snapshot_.counters[static_cast<std::size_t>(index)];
            AddListItem(countersList_, index, row.name);
            const std::wstring value = std::to_wstring(row.value);
            ListView_SetItemText(countersList_, index, 1, const_cast<LPWSTR>(value.c_str()));
        }
    }

    void DiagnosticsWindow::PopulateDerivedList()
    {
        ListView_DeleteAllItems(derivedList_);
        if (snapshot_.derived.empty())
        {
            AddListItem(derivedList_, 0, L"No derived metrics available.");
            ListView_SetItemText(derivedList_, 0, 1, const_cast<LPWSTR>(L"-"));
            return;
        }

        for (int index = 0; index < static_cast<int>(snapshot_.derived.size()); ++index)
        {
            const util::DiagnosticValueRow& row = snapshot_.derived[static_cast<std::size_t>(index)];
            AddListItem(derivedList_, index, row.name);
            ListView_SetItemText(derivedList_, index, 1, const_cast<LPWSTR>(row.value.c_str()));
        }
    }

    DiagnosticsWindow::ThemeColors DiagnosticsWindow::GetThemeColors() const
    {
        if (darkTheme_)
        {
            return ThemeColors{
                RGB(24, 28, 32),
                RGB(31, 36, 42),
                RGB(236, 240, 245),
                RGB(168, 176, 186),
                RGB(78, 84, 92),
            };
        }

        return ThemeColors{
            RGB(243, 245, 248),
            RGB(255, 255, 255),
            RGB(32, 36, 40),
            RGB(96, 105, 115),
            RGB(210, 215, 223),
        };
    }

    LRESULT DiagnosticsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            return CreateChildWindows() ? 0 : -1;
        case WM_SIZE:
            LayoutChildren();
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = kMinWindowWidth;
            minMaxInfo->ptMinTrackSize.y = kMinWindowHeight;
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        {
            const ThemeColors colors = GetThemeColors();
            SetTextColor(
                reinterpret_cast<HDC>(wParam),
                reinterpret_cast<HWND>(lParam) == summaryLabel_ ? colors.mutedText : colors.text);
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<INT_PTR>(backgroundBrush_);
        }
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, backgroundBrush_);
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paintStruct{};
            HDC hdc = BeginPaint(hwnd_, &paintStruct);
            RECT client{};
            GetClientRect(hwnd_, &client);
            FillRect(hdc, &client, backgroundBrush_);
            EndPaint(hwnd_, &paintStruct);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK DiagnosticsWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        DiagnosticsWindow* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<DiagnosticsWindow*>(createStruct->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<DiagnosticsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}