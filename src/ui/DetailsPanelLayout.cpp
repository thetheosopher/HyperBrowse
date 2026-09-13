#include "ui/DetailsPanelLayout.h"

#include <algorithm>

namespace hyperbrowse::ui
{
    DetailsPanelLayout::Result DetailsPanelLayout::Build(const Input& input)
    {
        Result result;

        const int innerLeft = input.panelRect.left + input.margin;
        const int innerRight = input.panelRect.right - input.margin;
        const int innerWidth = (std::max)(0, innerRight - innerLeft);
        const int tabTop = input.panelRect.top + input.margin;
        const int availableTabHeight = (std::max)(0,
                                                  static_cast<int>(input.panelRect.bottom) - tabTop - input.margin);
        const int actualTabHeight = (std::min)(input.tabHeight, availableTabHeight);
        const int closeButtonRight = input.panelRect.right - input.closeButtonMargin;
        const int closeButtonLeft = closeButtonRight - input.closeButtonSize;
        const int closeButtonTop = input.panelRect.top + input.closeButtonMargin;
        const int closeButtonBottom = closeButtonTop + input.closeButtonSize;

        if (innerWidth > 0 && actualTabHeight > 0)
        {
            const int desiredButtonWidth = (std::max)(
                input.tabMinButtonWidth,
                input.tabLabelWidth + (input.tabButtonHorizontalPadding * 2));
            const int maxButtonWidth = (std::max)(1, (std::max)(0, innerWidth - (input.tabButtonGap * 2)) / 3);
            const int reservedTabRight = closeButtonLeft - input.closeButtonGap;
            const int reservedTabWidth = (std::max)(0, reservedTabRight - innerLeft);
            const int maxButtonWidthBeforeClose = (std::max)(
                1,
                (std::max)(0, reservedTabWidth - (input.tabButtonGap * 2)) / 3);
            const int reservedButtonWidth = (std::min)(desiredButtonWidth, maxButtonWidthBeforeClose);
            const int reservedTabStripRight = innerLeft
                + (reservedButtonWidth * 3)
                + (input.tabButtonGap * 2);
            const int minimumButtonWidthForClose = (std::max)(1, input.tabMinButtonWidth / 2);
            const bool canReserveCloseButton = reservedButtonWidth >= minimumButtonWidthForClose
                && closeButtonLeft >= reservedTabStripRight + input.closeButtonGap;
            const int buttonWidth = canReserveCloseButton
                ? reservedButtonWidth
                : (std::min)(desiredButtonWidth, maxButtonWidth);
            for (std::size_t index = 0; index < result.tabRects.size(); ++index)
            {
                const int buttonLeft = innerLeft + static_cast<int>(index) * (buttonWidth + input.tabButtonGap);
                result.tabRects[index] = RECT{buttonLeft,
                                              tabTop,
                                              buttonLeft + buttonWidth,
                                              tabTop + actualTabHeight};
            }
            result.tabStripRect = RECT{result.tabRects[0].left,
                                       result.tabRects[0].top,
                                       result.tabRects.back().right,
                                       result.tabRects[0].bottom};
        }

        result.contentRect = RECT{innerLeft,
                                  tabTop + actualTabHeight + input.tabGap,
                                  innerRight,
                                  input.panelRect.bottom - input.margin};

        if (closeButtonLeft >= result.tabStripRect.right + input.closeButtonGap)
        {
            result.closeButtonRect = RECT{closeButtonLeft,
                                          closeButtonTop,
                                          closeButtonRight,
                                          closeButtonBottom};
        }

        if (input.fileDetailsActive
            && result.contentRect.right > result.contentRect.left
            && result.contentRect.bottom > result.contentRect.top)
        {
            int textTop = result.contentRect.top + input.titleHeight + 6;
            if (input.summaryHeight > 0)
            {
                textTop += input.summaryHeight + 8;
            }

            if (input.histogramVisible)
            {
                result.histogramRect = RECT{result.contentRect.left,
                                            textTop,
                                            result.contentRect.right,
                                            textTop + input.histogramHeight};
                textTop = result.histogramRect.bottom + input.textTopGap;
            }

            result.textRect = RECT{result.contentRect.left,
                                   textTop,
                                   result.contentRect.right,
                                   textTop + (std::max)(0,
                                                       static_cast<int>(result.contentRect.bottom) - textTop)};
        }

        return result;
    }
}
