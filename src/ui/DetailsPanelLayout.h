#pragma once

#include <windows.h>

#include <array>

namespace hyperbrowse::ui
{
    class DetailsPanelLayout final
    {
    public:
        struct Input
        {
            RECT panelRect{};
            int margin{};
            int tabHeight{};
            int tabGap{};
            int tabButtonGap{};
            int tabButtonHorizontalPadding{};
            int tabMinButtonWidth{};
            int closeButtonSize{};
            int closeButtonMargin{};
            int closeButtonGap{};
            int tabLabelWidth{};
            int titleHeight{};
            int summaryHeight{};
            int histogramHeight{};
            int textTopGap{};
            bool fileDetailsActive{};
            bool histogramVisible{};
        };

        struct Result
        {
            RECT tabStripRect{};
            std::array<RECT, 2> tabRects{};
            RECT contentRect{};
            RECT histogramRect{};
            RECT closeButtonRect{};
            RECT textRect{};
        };

        static Result Build(const Input& input);
    };
}
