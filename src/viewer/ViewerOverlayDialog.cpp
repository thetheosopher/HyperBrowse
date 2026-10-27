#include "viewer/ViewerOverlayDialog.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <uxtheme.h>
#include <dwmapi.h>
#include <commctrl.h>

#include "viewer/ViewerWindow.h"
#include "ui/DialogShell.h"
#include "ui/DialogTheme.h"
#include "util/Log.h"

namespace hyperbrowse::viewer
{
    namespace
    {
        enum Control : int { Panes = 100, Windowed, FullScreen, Position, Importance, Toward, Away,
            TextSize, Master, PreviewMode, Preview, Summary, Defaults, Apply, Content = 120 };
        struct State
        {
            ViewerOverlaySettings draft;
            ViewerOverlaySettings committed;
            SIZE viewport{};
            UINT previewDpi{96};
            UINT dpi{96};
            util::AppTextSize appTextSize{util::kDefaultAppTextSize};
            bool dark{};
            bool applied{};
            HFONT font{};
            HWND content{};
            int scrollX{};
            int scrollY{};
            HBRUSH background{};
            HBRUSH field{};
            HBRUSH border{};
            ui::DialogTheme theme{};
            ApplyViewerOverlays apply;
        };

        HWND Child(HWND window, int id)
        {
            if (const HWND direct = GetDlgItem(window, id)) return direct;
            return GetDlgItem(GetDlgItem(window, Content), id);
        }
        int Selection(HWND window, int id) { return static_cast<int>(SendMessageW(Child(window, id), CB_GETCURSEL, 0, 0)); }
        std::size_t PaneIndex(HWND window)
        {
            const auto selected = SendMessageW(Child(window, Panes), LB_GETCURSEL, 0, 0);
            return selected >= 0 && selected < static_cast<LRESULT>(kOverlayPaneCount)
                ? static_cast<std::size_t>(selected) : 0;
        }
        void Select(HWND window, int id, int value) { SendMessageW(Child(window, id), CB_SETCURSEL, value, 0); }
        void SetChecked(HWND window, int id, bool value) { SendMessageW(Child(window, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
        bool Checked(HWND window, int id) { return SendMessageW(Child(window, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
        void RebuildFont(HWND window, State& state)
        {
            NONCLIENTMETRICSW metrics{sizeof(metrics)};
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
            metrics.lfMessageFont.lfHeight = static_cast<LONG>(std::lround(
                MulDiv(metrics.lfMessageFont.lfHeight, state.dpi, GetDpiForSystem())
                * util::AppTextSizeScale(state.appTextSize)));
            HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
            if (!font)
            {
                util::LogError(L"Unable to create the viewer overlay dialog font.");
                return;
            }
            EnumChildWindows(window, [](HWND child, LPARAM data)
            {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(data), TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(font));
            if (state.font) DeleteObject(state.font);
            state.font = font;
            TEXTMETRICW text{};
            const HDC dc = GetDC(window);
            const auto previous = SelectObject(dc, font);
            GetTextMetricsW(dc, &text);
            SelectObject(dc, previous);
            ReleaseDC(window, dc);
            for (const int id : {Position, Importance, TextSize, PreviewMode})
            {
                const int height = text.tmHeight + MulDiv(8, state.dpi, 96);
                SendMessageW(Child(window, id), CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), height);
                SendMessageW(Child(window, id), CB_SETITEMHEIGHT, 0, height);
            }
        }

        OverlayLayout PreviewLayout(HWND window, const State& state)
        {
            return CalculateOverlayLayout(state.viewport, state.draft, Selection(window, PreviewMode) == 1,
                ViewerWindow::MeasureOverlaySizes(state.viewport, state.previewDpi, state.draft.textSize),
                MulDiv(16, state.previewDpi, 96), MulDiv(10, state.previewDpi, 96));
        }
        void Refresh(HWND window, State& state)
        {
            const auto& pane = state.draft.panes[PaneIndex(window)];
            SetChecked(window, Windowed, pane.windowedEnabled);
            SetChecked(window, FullScreen, pane.fullScreenEnabled);
            SetChecked(window, Master, state.draft.masterVisible);
            Select(window, Position, static_cast<int>(pane.corner));
            Select(window, Importance, static_cast<int>(pane.importance));
            Select(window, TextSize, static_cast<int>(state.draft.textSize));
            EnableWindow(Child(window, Apply), state.draft != state.committed);
            EnableWindow(Child(window, Toward), pane.order > 0);
            const bool hasNext = std::any_of(state.draft.panes.begin(), state.draft.panes.end(),
                [&](const auto& other) { return other.corner == pane.corner && other.order == pane.order + 1; });
            EnableWindow(Child(window, Away), hasNext);
            const auto layout = PreviewLayout(window, state);
            const wchar_t* corners[]{L"top left", L"top right", L"bottom left", L"bottom right"};
            std::wstring summary;
            for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
            {
                if (i != 0) summary += L"\r\n";
                const auto& configuration = state.draft.panes[i];
                summary += std::to_wstring(i + 1) + L". " + OverlayPaneName(static_cast<OverlayPane>(i)) + L": "
                    + corners[static_cast<int>(configuration.corner)] + L", order "
                    + std::to_wstring(configuration.order + 1) + L" - ";
                switch (layout[i].visibility)
                {
                case OverlayVisibility::Disabled: summary += L"disabled"; break;
                case OverlayVisibility::MasterHidden: summary += L"master hidden"; break;
                case OverlayVisibility::HiddenToFit: summary += L"hidden to fit"; break;
                case OverlayVisibility::Shown: summary += L"shown"; break;
                }
            }
            SetWindowTextW(Child(window, Summary), summary.c_str());
            InvalidateRect(Child(window, Preview), nullptr, TRUE);
        }

        void Layout(HWND window, State& state);

        LRESULT CALLBACK FocusProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
        {
            auto& state = *reinterpret_cast<State*>(data);
            if (message == WM_SETFOCUS)
            {
                RECT rect{}, client{};
                GetWindowRect(control, &rect);
                MapWindowPoints(nullptr, state.content, reinterpret_cast<POINT*>(&rect), 2);
                GetClientRect(state.content, &client);
                const int dx = rect.left < 0 ? rect.left : rect.right > client.right ? rect.right - client.right : 0;
                const int dy = rect.top < 0 ? rect.top : rect.bottom > client.bottom ? rect.bottom - client.bottom : 0;
                if (dx || dy)
                {
                    state.scrollX += dx;
                    state.scrollY += dy;
                    Layout(GetParent(state.content), state);
                }
            }
            return DefSubclassProc(control, message, wParam, lParam);
        }

        LRESULT CALLBACK ContentProc(HWND content, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
        {
            auto& state = *reinterpret_cast<State*>(data);
            if (message == WM_ERASEBKGND && state.background)
            {
                RECT rect{};
                GetClientRect(content, &rect);
                FillRect(reinterpret_cast<HDC>(wParam), &rect, state.background);
                return 1;
            }
            if (message == WM_VSCROLL || message == WM_HSCROLL || message == WM_MOUSEWHEEL)
            {
                const int bar = message == WM_HSCROLL ? SB_HORZ : SB_VERT;
                SCROLLINFO info{sizeof(info), SIF_ALL};
                GetScrollInfo(content, bar, &info);
                int position = info.nPos;
                if (message == WM_MOUSEWHEEL)
                    position -= MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), MulDiv(48, state.dpi, 96), WHEEL_DELTA);
                else switch (LOWORD(wParam))
                {
                case SB_LINEUP: position -= MulDiv(24, state.dpi, 96); break;
                case SB_LINEDOWN: position += MulDiv(24, state.dpi, 96); break;
                case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK: position = info.nTrackPos; break;
                case SB_TOP: position = info.nMin; break;
                case SB_BOTTOM: position = info.nMax; break;
                default: break;
                }
                (bar == SB_VERT ? state.scrollY : state.scrollX) = position;
                Layout(GetParent(content), state);
                return 0;
            }
            if (message == WM_COMMAND || message == WM_DRAWITEM || message == WM_MEASUREITEM
                || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN
                || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLOREDIT)
                return SendMessageW(GetParent(content), message, wParam, lParam);
            return DefSubclassProc(content, message, wParam, lParam);
        }

        void Layout(HWND window, State& state)
        {
            if (!state.content) return;
            RECT client{};
            GetClientRect(window, &client);
            const auto s = [&](int value)
            {
                return MulDiv(util::ScaleAppTextDimension(value, state.appTextSize), state.dpi, 96);
            };
            const bool stackedFooter = client.right < s(500);
            const int footer = (std::max)(s(40), static_cast<int>(client.bottom) - s(stackedFooter ? 88 : 48));
            MoveWindow(state.content, 0, 0, client.right, footer - s(8), TRUE);
            RECT body{};
            int width{};
            const auto scroll = [&](int bar, int extent, int page, int& position)
            {
                position = std::clamp(position, 0, (std::max)(0, extent - page));
                SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
                info.nMax = extent - 1;
                info.nPage = static_cast<UINT>(page);
                info.nPos = position;
                SetScrollInfo(state.content, bar, &info, TRUE);
            };
            for (int pass = 0; pass < 3; ++pass)
            {
                GetClientRect(state.content, &body);
                width = (std::max)(static_cast<int>(body.right), s(660));
                scroll(SB_HORZ, width, body.right, state.scrollX);
                scroll(SB_VERT, s(532), body.bottom, state.scrollY);
            }
            GetClientRect(state.content, &body);
            width = (std::max)(static_cast<int>(body.right), s(660));
            const int left = s(16), right = width - s(16);
            const int column = (std::max)(s(220), width / 3);
            const auto place = [&](int id, int x, int y, int w, int h)
            {
                MoveWindow(Child(window, id), x - state.scrollX, s(y) - state.scrollY, w, s(h), TRUE);
            };
            place(200, left, 12, column - left - s(12), 22);
            place(Panes, left, 38, column - left - s(12), 140);
            place(Windowed, column, 12, right - column, 26);
            place(FullScreen, column, 42, right - column, 26);
            place(201, column, 76, s(100), 26);
            place(Position, column + s(110), 72, right - column - s(110), 150);
            place(202, column, 112, s(100), 26);
            place(Importance, column + s(110), 108, right - column - s(110), 130);
            const int buttonWidth = (right - column - s(10)) / 2;
            place(Toward, column, 148, buttonWidth, 30);
            place(Away, column + buttonWidth + s(10), 148, buttonWidth, 30);
            place(Master, left, 188, column - left, 26);
            place(203, column, 192, s(100), 26);
            place(TextSize, column + s(110), 188, right - column - s(110), 120);
            place(205, left, 228, right - left, 36);
            place(204, left, 278, s(140), 26);
            place(PreviewMode, left + s(150), 274, s(180), 120);
            place(Preview, left, 316, right - left, 112);
            place(Summary, left, 440, right - left, 80);
            MoveWindow(Child(window, Defaults), left, footer, s(160), s(30), TRUE);
            const int actionsTop = footer + (stackedFooter ? s(38) : 0);
            MoveWindow(Child(window, IDOK), client.right - s(316), actionsTop, s(92), s(30), TRUE);
            MoveWindow(Child(window, IDCANCEL), client.right - s(216), actionsTop, s(96), s(30), TRUE);
            MoveWindow(Child(window, Apply), client.right - s(112), actionsTop, s(96), s(30), TRUE);
            RedrawWindow(state.content, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }

        void Theme(HWND window, State& state)
        {
            state.theme = ui::MakeDialogTheme(state.dark);
            if (state.background) DeleteObject(state.background);
            if (state.field) DeleteObject(state.field);
            if (state.border) DeleteObject(state.border);
            state.background = CreateSolidBrush(state.theme.windowBackground);
            state.field = CreateSolidBrush(state.theme.fieldBackground);
            state.border = CreateSolidBrush(state.theme.border);
            const bool dark = state.dark && !ui::IsHighContrastEnabled();
            const BOOL darkFrame = dark;
            DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
            EnumChildWindows(window, [](HWND child, LPARAM data)
            {
                SetWindowTheme(child, data ? L"DarkMode_Explorer" : L"Explorer", nullptr);
                return TRUE;
            }, dark);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }

        INT_PTR CALLBACK DialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
            switch (message)
            {
            case WM_INITDIALOG:
            {
                state = reinterpret_cast<State*>(lParam);
                SetWindowLongPtrW(window, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
                state->dpi = GetDpiForWindow(window);
                SetWindowTextW(window, L"Configure overlays");
                state->content = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL | WS_HSCROLL,
                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Content)), GetModuleHandleW(nullptr), nullptr);
                SetWindowSubclass(state->content, ContentProc, 1, reinterpret_cast<DWORD_PTR>(state));
                const auto add = [&](const wchar_t* className, const wchar_t* text, int id, DWORD style)
                {
                    const HWND parent = id == Defaults || id == IDOK || id == Apply || id == IDCANCEL ? window : state->content;
                    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                        GetModuleHandleW(nullptr), nullptr);
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                    if (parent == state->content)
                        SetWindowSubclass(control, FocusProc, 1, reinterpret_cast<DWORD_PTR>(state));
                    return control;
                };
                add(L"STATIC", L"&Pane", 200, 0);
                HWND list = add(L"LISTBOX", L"Overlay panes", Panes, WS_TABSTOP | WS_BORDER | LBS_NOTIFY);
                for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(OverlayPaneName(static_cast<OverlayPane>(i))));
                SendMessageW(list, LB_SETCURSEL, 0, 0);
                add(L"BUTTON", L"Enabled in &windowed mode", Windowed, WS_TABSTOP | BS_AUTOCHECKBOX);
                add(L"BUTTON", L"Enabled in &full-screen mode", FullScreen, WS_TABSTOP | BS_AUTOCHECKBOX);
                const auto combo = [&](int label, const wchar_t* text, int id, std::initializer_list<const wchar_t*> values)
                {
                    add(L"STATIC", text, label, 0);
                    HWND control = add(L"COMBOBOX", text, id,
                        WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL);
                    for (const auto* value : values)
                        SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
                    SendMessageW(control, CB_SETCURSEL, 0, 0);
                };
                combo(201, L"P&osition", Position, {L"Top left", L"Top right", L"Bottom left", L"Bottom right"});
                combo(202, L"&Importance", Importance, {L"Low", L"Normal", L"High"});
                add(L"BUTTON", L"&Toward corner", Toward, WS_TABSTOP | BS_PUSHBUTTON);
                add(L"BUTTON", L"A&way from corner", Away, WS_TABSTOP | BS_PUSHBUTTON);
                add(L"BUTTON", L"Show &overlays (Tab)", Master, WS_TABSTOP | BS_AUTOCHECKBOX);
                combo(203, L"Te&xt size", TextSize, {L"Small", L"Medium", L"Large"});
                combo(204, L"Preview &mode", PreviewMode, {L"Windowed", L"Full screen"});
                add(L"STATIC", L"Order starts at the corner; importance controls which panes yield space. Hidden panes return when space allows.",
                    205, 0);
                add(L"STATIC", L"Overlay layout preview", Preview, SS_OWNERDRAW);
                add(L"STATIC", L"", Summary, 0);
                add(L"BUTTON", L"&Restore defaults", Defaults, WS_TABSTOP | BS_PUSHBUTTON);
                add(L"BUTTON", L"OK", IDOK, WS_TABSTOP | BS_DEFPUSHBUTTON);
                add(L"BUTTON", L"Cancel", IDCANCEL, WS_TABSTOP | BS_PUSHBUTTON);
                add(L"BUTTON", L"&Apply", Apply, WS_TABSTOP | BS_PUSHBUTTON);
                SendMessageW(window, DM_SETDEFID, IDOK, 0);
                RebuildFont(window, *state);
                const auto shell = ui::MeasureDialogShellMetrics(GetWindow(window, GW_OWNER), state->appTextSize);
                RECT frame{0, 0, MulDiv(shell.Scale(720), state->dpi, 96), MulDiv(shell.Scale(588), state->dpi, 96)};
                AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                    static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), state->dpi);
                const int width = (std::min)(static_cast<int>(frame.right - frame.left), shell.workAreaWidth);
                const int height = (std::min)(static_cast<int>(frame.bottom - frame.top), shell.workAreaHeight);
                SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
                ui::CenterDialogInWorkArea(window, shell.workArea);
                Theme(window, *state);
                Refresh(window, *state);
                Layout(window, *state);
                SetFocus(list);
                return FALSE;
            }
            case WM_COMMAND:
                if (!state) break;
                if (LOWORD(wParam) == IDCANCEL) { EndDialog(window, IDCANCEL); return TRUE; }
                if (LOWORD(wParam) == Apply || LOWORD(wParam) == IDOK)
                {
                    if (state->draft != state->committed)
                    {
                        if (state->apply && !state->apply(state->draft))
                        {
                            MessageBoxW(window, L"Unable to save overlay settings. Changes have not been applied.",
                                L"Configure overlays", MB_OK | MB_ICONERROR);
                            return TRUE;
                        }
                        state->committed = state->draft;
                        state->applied = true;
                        const bool applyFocused = GetFocus() == Child(window, Apply);
                        Refresh(window, *state);
                        if (applyFocused) SetFocus(Child(window, IDOK));
                    }
                    if (LOWORD(wParam) == IDOK) EndDialog(window, IDOK);
                    return TRUE;
                }
                if (LOWORD(wParam) == Defaults) state->draft = {};
                else if (LOWORD(wParam) == Toward || LOWORD(wParam) == Away)
                    MoveOverlayPane(state->draft, static_cast<OverlayPane>(PaneIndex(window)), LOWORD(wParam) == Toward);
                else if (LOWORD(wParam) == Panes && HIWORD(wParam) == LBN_SELCHANGE) {}
                else if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE)
                {
                    auto& pane = state->draft.panes[PaneIndex(window)];
                    switch (LOWORD(wParam))
                    {
                    case Windowed: pane.windowedEnabled = Checked(window, Windowed); break;
                    case FullScreen: pane.fullScreenEnabled = Checked(window, FullScreen); break;
                    case Master: state->draft.masterVisible = Checked(window, Master); break;
                    case Position:
                        pane.corner = static_cast<OverlayCorner>(Selection(window, Position));
                        pane.order = static_cast<int>(kOverlayPaneCount) - 1;
                        break;
                    case Importance: pane.importance = static_cast<OverlayImportance>(Selection(window, Importance)); break;
                    case TextSize: state->draft.textSize = static_cast<InfoOverlayTextSize>(Selection(window, TextSize)); break;
                    default: break;
                    }
                }
                else break;
                state->draft = NormalizeOverlaySettings(state->draft);
                Refresh(window, *state);
                return TRUE;
            case WM_MEASUREITEM:
                if (state)
                {
                    auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
                    if (measure->CtlType == ODT_COMBOBOX)
                    {
                        measure->itemHeight = MulDiv(util::ScaleAppTextDimension(24, state->appTextSize), state->dpi, 96);
                        return TRUE;
                    }
                }
                break;
            case WM_DRAWITEM:
                if (state && reinterpret_cast<DRAWITEMSTRUCT*>(lParam)->CtlType == ODT_COMBOBOX)
                {
                    const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                    const bool selected = (draw.itemState & ODS_SELECTED) && !(draw.itemState & ODS_COMBOBOXEDIT);
                    HBRUSH fill = selected ? CreateSolidBrush(state->theme.accentFill) : state->field;
                    FillRect(draw.hDC, &draw.rcItem, fill);
                    if (selected) DeleteObject(fill);
                    const auto previous = SelectObject(draw.hDC, state->font);
                    SetBkMode(draw.hDC, TRANSPARENT);
                    SetTextColor(draw.hDC, draw.itemState & ODS_DISABLED ? state->theme.mutedText
                        : selected ? state->theme.accentText : state->theme.text);
                    if (draw.itemID != static_cast<UINT>(-1))
                    {
                        const auto length = SendMessageW(draw.hwndItem, CB_GETLBTEXTLEN, draw.itemID, 0);
                        if (length != CB_ERR)
                        {
                            std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                            SendMessageW(draw.hwndItem, CB_GETLBTEXT, draw.itemID, reinterpret_cast<LPARAM>(text.data()));
                            RECT rect = draw.rcItem;
                            rect.left += MulDiv(6, state->dpi, 96);
                            DrawTextW(draw.hDC, text.c_str(), -1, &rect,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                        }
                    }
                    if (draw.itemState & ODS_FOCUS) DrawFocusRect(draw.hDC, &draw.rcItem);
                    SelectObject(draw.hDC, previous);
                    return TRUE;
                }
                if (state && wParam == Preview)
                {
                    const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                    const int saved = SaveDC(draw->hDC);
                    if (!saved)
                    {
                        util::LogError(L"Unable to save the overlay preview drawing context.");
                        return TRUE;
                    }
                    IntersectClipRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
                        draw->rcItem.right, draw->rcItem.bottom);
                    FillRect(draw->hDC, &draw->rcItem, state->field);
                    const auto layout = PreviewLayout(window, *state);
                    const int padding = MulDiv(8, state->dpi, 96);
                    const int width = (std::max)(1L, draw->rcItem.right - draw->rcItem.left - padding * 2);
                    const int height = (std::max)(1L, draw->rcItem.bottom - draw->rcItem.top - padding * 2);
                    const double scale = (std::min)(
                        static_cast<double>(width) / (std::max)(1L, state->viewport.cx),
                        static_cast<double>(height) / (std::max)(1L, state->viewport.cy));
                    const LONG previewWidth = static_cast<LONG>(std::lround(state->viewport.cx * scale));
                    const LONG previewHeight = static_cast<LONG>(std::lround(state->viewport.cy * scale));
                    const LONG left = draw->rcItem.left + (draw->rcItem.right - draw->rcItem.left - previewWidth) / 2;
                    const LONG top = draw->rcItem.top + (draw->rcItem.bottom - draw->rcItem.top - previewHeight) / 2;
                    const RECT viewport{left, top, left + previewWidth, top + previewHeight};
                    FillRect(draw->hDC, &viewport, state->background);
                    FrameRect(draw->hDC, &viewport, state->border);
                    IntersectClipRect(draw->hDC, viewport.left, viewport.top, viewport.right, viewport.bottom);
                    SetBkMode(draw->hDC, TRANSPARENT);
                    SelectObject(draw->hDC, state->font);
                    TEXTMETRICW textMetrics{};
                    GetTextMetricsW(draw->hDC, &textMetrics);
                    const LONG badgeSize = textMetrics.tmHeight + MulDiv(4, state->dpi, 96);
                    for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                    {
                        if (layout[i].visibility != OverlayVisibility::Shown) continue;
                        const auto& r = layout[i].rect;
                        RECT rect{left + static_cast<LONG>(std::floor(r.left * scale)),
                            top + static_cast<LONG>(std::floor(r.top * scale)),
                            left + static_cast<LONG>(std::ceil(r.right * scale)),
                            top + static_cast<LONG>(std::ceil(r.bottom * scale))};
                        const bool selected = i == PaneIndex(window);
                        HBRUSH fill = CreateSolidBrush(selected ? state->theme.accentFill : state->theme.surfaceBackground);
                        FillRect(draw->hDC, &rect, fill);
                        FrameRect(draw->hDC, &rect, state->border);
                        RECT badge{(std::min)(rect.left, viewport.right - badgeSize),
                            (std::min)(rect.top, viewport.bottom - badgeSize), 0, 0};
                        badge.left = (std::max)(badge.left, viewport.left);
                        badge.top = (std::max)(badge.top, viewport.top);
                        badge.right = (std::min)(badge.left + badgeSize, viewport.right);
                        badge.bottom = (std::min)(badge.top + badgeSize, viewport.bottom);
                        FillRect(draw->hDC, &badge, fill);
                        DeleteObject(fill);
                        SetTextColor(draw->hDC, selected ? state->theme.accentText : state->theme.text);
                        const auto text = std::to_wstring(i + 1);
                        DrawTextW(draw->hDC, text.c_str(), -1, &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }
                    RestoreDC(draw->hDC, saved);
                    return TRUE;
                }
                break;
            case WM_SIZE:
                if (state) Layout(window, *state);
                return TRUE;
            case WM_MOUSEWHEEL:
                if (state) SendMessageW(state->content, message, wParam, lParam);
                return TRUE;
            case WM_GETMINMAXINFO:
                if (state)
                {
                    auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                    info->ptMinTrackSize = {MulDiv(util::ScaleAppTextDimension(440, state->appTextSize), state->dpi, 96),
                        MulDiv(util::ScaleAppTextDimension(300, state->appTextSize), state->dpi, 96)};
                }
                return TRUE;
            case WM_DPICHANGED:
                if (state)
                {
                    state->dpi = HIWORD(wParam);
                    RebuildFont(window, *state);
                    const auto* rect = reinterpret_cast<RECT*>(lParam);
                    SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                        rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
                    Layout(window, *state);
                }
                return TRUE;
            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLOREDIT:
                if (state)
                {
                    SetTextColor(reinterpret_cast<HDC>(wParam), state->theme.text);
                    const bool field = message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLOREDIT;
                    SetBkMode(reinterpret_cast<HDC>(wParam), field ? OPAQUE : TRANSPARENT);
                    SetBkColor(reinterpret_cast<HDC>(wParam), field ? state->theme.fieldBackground : state->theme.windowBackground);
                    return reinterpret_cast<INT_PTR>(field ? state->field : state->background);
                }
                break;
            case WM_SETTINGCHANGE:
            case WM_THEMECHANGED:
            case WM_SYSCOLORCHANGE:
                if (state) Theme(window, *state);
                return TRUE;
            case WM_CLOSE: EndDialog(window, IDCANCEL); return TRUE;
            }
            return FALSE;
        }
    }

    bool ShowViewerOverlayDialog(HWND owner, HINSTANCE instance, bool darkTheme, util::AppTextSize appTextSize,
                                 ViewerOverlaySettings settings, SIZE previewViewport, UINT previewDpi,
                                 const ApplyViewerOverlays& apply)
    {
        State state;
        state.draft = NormalizeOverlaySettings(settings);
        state.committed = state.draft;
        state.viewport = previewViewport.cx > 0 && previewViewport.cy > 0 ? previewViewport : SIZE{1280, 800};
        state.previewDpi = previewDpi ? previewDpi : 96;
        state.dark = darkTheme;
        state.appTextSize = appTextSize;
        state.apply = apply;
        struct Template
        {
            DLGTEMPLATE dialog{};
            WORD menu{};
            WORD className{};
            WORD title{};
        } layout;
        layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | DS_CENTER;
        layout.dialog.style |= WS_CLIPCHILDREN;
        layout.dialog.dwExtendedStyle = WS_EX_CONTROLPARENT;
        layout.dialog.cx = 400;
        layout.dialog.cy = 350;
        const auto result = DialogBoxIndirectParamW(instance, &layout.dialog, owner, DialogProc,
            reinterpret_cast<LPARAM>(&state));
        if (result == -1)
        {
            util::LogError(L"Unable to create the viewer overlay configuration dialog.");
            MessageBoxW(owner, L"Unable to create the overlay configuration dialog.", L"Configure overlays", MB_OK | MB_ICONERROR);
        }
        if (state.font) DeleteObject(state.font);
        if (state.background) DeleteObject(state.background);
        if (state.field) DeleteObject(state.field);
        if (state.border) DeleteObject(state.border);
        return state.applied;
    }
}
