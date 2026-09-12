#pragma once

#include <windows.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/MainWindowDialogState.h"

namespace hyperbrowse::ui::dialog_detail
{
    struct SettingsLayoutMetrics
    {
        int width{};
        int height{};
        int margin{};
        int tabTop{};
        int tabHeight{};
        int contentTop{};
        int footerTop{};
        int footerHeight{};
        int left{};
        int right{};
        int valueLeft{};
        int valueRight{};
        int rowHeight{};
        int rowGap{};
        int buttonWidth{};
        int buttonGap{};
        int radioGap{};
        int radioWidth{};
        int labelColumnWidth{};
        RECT bodyViewport{};
        RECT footer{};
    };

    struct SettingsLayoutLabel
    {
        RECT bounds{};
        std::wstring text;
        bool muted{};
        ConsolidatedSettingsControl mnemonicControl{ConsolidatedSettingsControl::Count};
    };

    struct SettingsLayoutInput
    {
        ConsolidatedSettingsPage page{ConsolidatedSettingsPage::Slideshow};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        int availableWidth{};
        int availableHeight{};
        std::function<int(std::wstring_view)> measureTextWidth;
    };

    struct SettingsLayoutResult
    {
        SettingsLayoutMetrics metrics{};
        std::array<RECT, static_cast<std::size_t>(ConsolidatedSettingsPage::Count)> tabRects{};
        std::array<RECT, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> controlRects{};
        std::array<RECT, 5> numericEditRects{};
        std::array<RECT, 5> numericSpinRects{};
        std::vector<SettingsLayoutLabel> labels;
        int requiredContentHeight{};
        int scrollExtent{};
        bool requiresScroll{};
    };

    SettingsLayoutResult MeasureSettingsLayout(const SettingsLayoutInput& input);
}
