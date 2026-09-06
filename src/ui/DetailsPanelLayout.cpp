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

        if (innerWidth > 0 && actualTabHeight > 0)
        {
            const int desiredButtonWidth = (std::max)(
                input.tabMinButtonWidth,
                input.tabLabelWidth + (input.tabButtonHorizontalPadding * 2));
            const int maxButtonWidth = (std::max)(1, (std::max)(0, innerWidth - input.tabButtonGap) / 2);
            const int buttonWidth = (std::min)(desiredButtonWidth, maxButtonWidth);
            const int secondButtonLeft = innerLeft + buttonWidth + input.tabButtonGap;

            result.tabRects[0] = RECT{innerLeft, tabTop, innerLeft + buttonWidth, tabTop + actualTabHeight};
            result.tabRects[1] = RECT{secondButtonLeft,
                                      tabTop,
                                      secondButtonLeft + buttonWidth,
                                      tabTop + actualTabHeight};
            result.tabStripRect = RECT{result.tabRects[0].left,
                                       result.tabRects[0].top,
                                       result.tabRects[1].right,
                                       result.tabRects[0].bottom};
        }

        result.contentRect = RECT{innerLeft,
                                  tabTop + actualTabHeight + input.tabGap,
                                  innerRight,
                                  input.panelRect.bottom - input.margin};

        const int closeButtonRight = input.panelRect.right - input.closeButtonMargin;
        const int closeButtonLeft = closeButtonRight - input.closeButtonSize;
        const int closeButtonTop = input.panelRect.top + input.closeButtonMargin;
        const int closeButtonBottom = closeButtonTop + input.closeButtonSize;
        if (closeButtonLeft > result.tabStripRect.right + input.closeButtonGap)
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
