#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "util/Diagnostics.h"

namespace hyperbrowse::ui
{
    class DiagnosticsWindow
    {
    public:
        explicit DiagnosticsWindow(HINSTANCE instance);
        ~DiagnosticsWindow();

        void Show(HWND owner,
                  std::wstring jpegPath,
                  std::wstring rawPath,
                  std::wstring folderScope,
                  util::DiagnosticsSnapshot snapshot,
                  bool darkTheme);
        void SetDarkTheme(bool enabled);
        bool IsOpen() const noexcept;

    private:
        static constexpr const wchar_t* kWindowClassName = L"HyperBrowseDiagnosticsWindow";
        static constexpr int kMinWindowWidth = 760;
        static constexpr int kMinWindowHeight = 560;

        struct ThemeColors
        {
            COLORREF windowBackground;
            COLORREF panelBackground;
            COLORREF text;
            COLORREF mutedText;
            COLORREF border;
        };

        bool RegisterWindowClass() const;
        bool EnsureWindow(HWND owner);
        bool CreateChildWindows();
        void CreateFonts();
        void ReleaseFonts();
        void ReleaseBrushes();
        void LayoutChildren();
        void ApplyTheme();
        void RefreshView();
        void PopulateSummary();
        void PopulateTimingList();
        void PopulateCounterList();
        void PopulateDerivedList();
        ThemeColors GetThemeColors() const;

        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HINSTANCE instance_{};
        HWND hwnd_{};
        HWND owner_{};
        HWND titleLabel_{};
        HWND summaryLabel_{};
        HWND timingsLabel_{};
        HWND countersLabel_{};
        HWND derivedLabel_{};
        HWND timingsList_{};
        HWND countersList_{};
        HWND derivedList_{};
        HFONT titleFont_{};
        HFONT sectionFont_{};
        HFONT bodyFont_{};
        HBRUSH backgroundBrush_{};
        bool darkTheme_{};
        std::wstring jpegPath_;
        std::wstring rawPath_;
        std::wstring folderScope_;
        util::DiagnosticsSnapshot snapshot_;
    };
}