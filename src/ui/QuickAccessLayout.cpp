#include "ui/QuickAccessLayout.h"

#include <algorithm>
#include <utility>

namespace hyperbrowse::ui
{
    QuickAccessLayout::Result QuickAccessLayout::Build(const Input& input)
    {
        Result result;
        result.panelRect = RECT{input.innerLeft, input.top, input.innerRight, input.panelBottom};
        result.viewportRect = RECT{input.innerLeft,
                                   input.viewportTop,
                                   input.contentRight,
                                   input.panelBottom};

        const int sortButtonLeft = input.innerLeft + input.sortLabelWidth + input.sortButtonGap;
        const int sortButtonTop = input.top
            + (std::max)(0, (input.metrics.headerHeight - input.sortButtonSize) / 2);
        if (input.sortButtonSize > 0
            && sortButtonLeft + input.sortButtonSize <= input.innerRight)
        {
            result.sortButtonRect = RECT{sortButtonLeft,
                                         sortButtonTop,
                                         sortButtonLeft + input.sortButtonSize,
                                         sortButtonTop + input.sortButtonSize};
        }

        result.rows.reserve(input.destinations.size());
        int rowTop = input.viewportTop - input.scrollOffset;
        for (const Destination& destination : input.destinations)
        {
            Row row;
            row.destinationPath = destination.destinationPath;
            row.displayLabel = destination.displayLabel;
            row.metadataLabel = destination.metadataLabel;
            row.assignedShortcut = destination.assignedShortcut;
            row.favorite = destination.favorite;
            row.rowRect = RECT{input.innerLeft,
                               rowTop,
                               input.contentRight,
                               rowTop + input.metrics.rowHeight};

            const int buttonTop = rowTop + input.metrics.buttonTopInset;
            int buttonRight = input.contentRight - input.metrics.buttonRightInset;
            row.removeRect = RECT{buttonRight - input.metrics.removeButtonWidth,
                                  buttonTop,
                                  buttonRight,
                                  buttonTop + input.metrics.buttonHeight};
            buttonRight = row.removeRect.left - input.metrics.buttonGap;
            row.moveRect = RECT{buttonRight - input.metrics.buttonWidth,
                                buttonTop,
                                buttonRight,
                                buttonTop + input.metrics.buttonHeight};
            row.copyRect = RECT{row.moveRect.left - input.metrics.buttonGap - input.metrics.buttonWidth,
                                row.moveRect.top,
                                row.moveRect.left - input.metrics.buttonGap,
                                row.moveRect.bottom};
            const int shortcutRight = row.copyRect.left - input.metrics.shortcutGap;
            row.shortcutRect = RECT{shortcutRight - input.metrics.shortcutWidth,
                                    buttonTop,
                                    shortcutRight,
                                    buttonTop + input.metrics.buttonHeight};
            result.rows.push_back(std::move(row));
            rowTop += input.metrics.rowHeight + input.metrics.rowGap;
        }

        return result;
    }
}
