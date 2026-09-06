#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hyperbrowse::ui
{
    class QuickAccessLayout final
    {
    public:
        struct Metrics
        {
            int headerHeight{};
            int rowHeight{};
            int labelTopInset{};
            int labelHeight{};
            int metadataTopInset{};
            int metadataBottomInset{};
            int buttonHeight{};
            int buttonTopInset{};
            int rowGap{};
            int buttonWidth{};
            int buttonGap{};
            int buttonRightInset{};
            int removeButtonWidth{};
            int shortcutWidth{};
            int shortcutGap{};
        };

        struct Destination
        {
            std::wstring destinationPath;
            std::wstring displayLabel;
            std::wstring metadataLabel;
            int assignedShortcut{-1};
            bool favorite{};
        };

        struct Row : Destination
        {
            RECT rowRect{};
            RECT shortcutRect{};
            RECT copyRect{};
            RECT moveRect{};
            RECT removeRect{};
        };

        struct Input
        {
            int innerLeft{};
            int innerRight{};
            int top{};
            int viewportTop{};
            int panelBottom{};
            int contentRight{};
            int scrollOffset{};
            int sortLabelWidth{};
            int sortButtonGap{};
            int sortButtonSize{};
            Metrics metrics{};
            std::vector<Destination> destinations;
        };

        struct Result
        {
            RECT panelRect{};
            RECT viewportRect{};
            RECT sortButtonRect{};
            std::vector<Row> rows;
        };

        static Result Build(const Input& input);
    };
}
