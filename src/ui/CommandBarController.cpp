#include "ui/CommandBarController.h"

#include <algorithm>
#include <utility>

#include "ui/CommandIds.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr int kActionStripPaddingX = 8;
        constexpr int kToolbarItemSize = 32;
        constexpr int kToolbarSeparatorWidth = 9;
        constexpr int kToolbarSeparatorGap = 4;
        constexpr int kCommandBarMenuButtonGap = 4;
        constexpr int kCommandBarMenuButtonPadding = 12;
        constexpr int kCommandBarMenuButtonMinWidth = 56;
        constexpr int kCommandBarMenuChevronWidth = 8;
    }

    using namespace command_ids;

    void CommandBarController::InitializeItems()
    {
        items_.clear();

        const auto addIcon = [this](UINT commandId,
                                    std::string iconName,
                                    std::wstring tooltip,
                                    ToolbarItemKind kind = ToolbarItemKind::IconButton,
                                    ToolbarAlignment alignment = ToolbarAlignment::Left)
        {
            ToolbarItem item;
            item.commandId = commandId;
            item.iconName = std::move(iconName);
            item.tooltip = std::move(tooltip);
            item.kind = kind;
            item.alignment = alignment;
            items_.push_back(std::move(item));
        };

        const auto addSeparator = [this](ToolbarAlignment alignment = ToolbarAlignment::Left)
        {
            ToolbarItem separator;
            separator.kind = ToolbarItemKind::Separator;
            separator.alignment = alignment;
            items_.push_back(std::move(separator));
        };

        addIcon(ID_VIEW_NAVIGATE_BACK_FOLDER, "back", L"Back to Previous Folder (Backspace)");
        addIcon(ID_VIEW_NAVIGATE_FORWARD_FOLDER, "forward", L"Forward to Next Folder (Alt+Right)");
        addIcon(ID_FILE_OPEN_FOLDER, "open-folder", L"Open Folder (Ctrl+O)");
        addIcon(ID_VIEW_RECURSIVE, "recursive", L"Recursive Browsing (Ctrl+R)", ToolbarItemKind::IconToggle);
        addSeparator();
        addIcon(ID_VIEW_THUMBNAILS, "view-grid", L"Thumbnail Mode (Ctrl+1)", ToolbarItemKind::IconToggle);
        addIcon(ID_VIEW_DETAILS, "view-list", L"Details Mode (Ctrl+2)", ToolbarItemKind::IconToggle);
        addSeparator();
        addIcon(ID_ACTION_SORT_MENU, "sort", L"Sort By", ToolbarItemKind::IconDropdown);
        addIcon(ID_ACTION_THUMBNAIL_SIZE_MENU, "thumbnail-size", L"Thumbnail Size", ToolbarItemKind::IconDropdown);
        addSeparator();

        ToolbarItem filterItem;
        filterItem.kind = ToolbarItemKind::FilterEdit;
        filterItem.alignment = ToolbarAlignment::Left;
        items_.push_back(std::move(filterItem));

        addSeparator(ToolbarAlignment::Right);
        addIcon(ID_FILE_COMPARE_SELECTED, "compare", L"Compare Selected", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_COPY_SELECTION, "copy", L"Copy Selection", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_MOVE_SELECTION, "move", L"Move Selection", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_DELETE_SELECTION, "delete", L"Delete (Del)", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
    }

    void CommandBarController::SetMenuButton(std::size_t index, std::wstring label, wchar_t mnemonic, HMENU menu)
    {
        if (index >= menuButtons_.size())
        {
            return;
        }

        auto& button = menuButtons_[index];
        button.label = std::move(label);
        button.mnemonic = mnemonic;
        button.menu = menu;
    }

    void CommandBarController::Layout(int clientWidth,
                                      int itemTop,
                                      HFONT menuFont,
                                      const TextWidthHandler& measureTextWidth)
    {
        int leftCursor = kActionStripPaddingX;
        int rightCursor = clientWidth - kActionStripPaddingX;
        int filterItemIndex = -1;

        for (auto& button : menuButtons_)
        {
            if (button.label.empty() || !button.menu)
            {
                button.rect = RECT{};
                continue;
            }

            const int textWidth = measureTextWidth ? measureTextWidth(menuFont, button.label) : 0;
            const int buttonWidth = std::max(kCommandBarMenuButtonMinWidth,
                                             textWidth + (kCommandBarMenuButtonPadding * 2)
                                                 + kCommandBarMenuChevronWidth + 8);
            button.rect = RECT{leftCursor, itemTop, leftCursor + buttonWidth, itemTop + kToolbarItemSize};
            leftCursor += buttonWidth + kCommandBarMenuButtonGap;
        }

        leftCursor += 8;

        for (int index = 0; index < static_cast<int>(items_.size()); ++index)
        {
            auto& item = items_[static_cast<std::size_t>(index)];
            if (item.alignment != ToolbarAlignment::Left)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                item.rect = RECT{leftCursor + kToolbarSeparatorGap,
                                 itemTop,
                                 leftCursor + kToolbarSeparatorGap + 1,
                                 itemTop + kToolbarItemSize};
                leftCursor += kToolbarSeparatorWidth;
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterEdit)
            {
                filterItemIndex = index;
                continue;
            }

            item.rect = RECT{leftCursor, itemTop, leftCursor + kToolbarItemSize, itemTop + kToolbarItemSize};
            leftCursor += kToolbarItemSize + 2;
        }

        for (int index = static_cast<int>(items_.size()) - 1; index >= 0; --index)
        {
            auto& item = items_[static_cast<std::size_t>(index)];
            if (item.alignment != ToolbarAlignment::Right)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                rightCursor -= kToolbarSeparatorWidth;
                item.rect = RECT{rightCursor + kToolbarSeparatorGap,
                                 itemTop,
                                 rightCursor + kToolbarSeparatorGap + 1,
                                 itemTop + kToolbarItemSize};
                continue;
            }

            rightCursor -= kToolbarItemSize;
            item.rect = RECT{rightCursor, itemTop, rightCursor + kToolbarItemSize, itemTop + kToolbarItemSize};
            rightCursor -= 2;
        }

        if (filterItemIndex >= 0)
        {
            const int filterLeft = leftCursor + 6;
            const int filterRight = rightCursor - 6;
            const int filterWidth = std::max(0, filterRight - filterLeft);
            items_[static_cast<std::size_t>(filterItemIndex)].rect =
                RECT{filterLeft, itemTop, filterLeft + filterWidth, itemTop + kToolbarItemSize};
        }
    }

    void CommandBarController::UpdateItemStates(const ToolbarState& state)
    {
        for (auto& item : items_)
        {
            switch (item.commandId)
            {
            case ID_VIEW_NAVIGATE_BACK_FOLDER:
                item.enabled = state.canNavigateBack && !state.pendingNavigation && !state.folderEnumerationActive;
                break;
            case ID_VIEW_NAVIGATE_FORWARD_FOLDER:
                item.enabled = state.canNavigateForward && !state.pendingNavigation && !state.folderEnumerationActive;
                break;
            case ID_VIEW_RECURSIVE:
                item.checked = state.recursiveChecked;
                break;
            case ID_VIEW_THUMBNAILS:
                item.checked = state.thumbnailsChecked;
                break;
            case ID_VIEW_DETAILS:
                item.checked = state.detailsChecked;
                break;
            case ID_ACTION_THUMBNAIL_SIZE_MENU:
                item.enabled = state.thumbnailSizeEnabled;
                break;
            case ID_FILE_COMPARE_SELECTED:
                item.enabled = state.compareEnabled;
                break;
            case ID_FILE_COPY_SELECTION:
            case ID_FILE_MOVE_SELECTION:
            case ID_FILE_DELETE_SELECTION:
                item.enabled = state.selectionActionsEnabled;
                break;
            default:
                break;
            }
        }
    }

    int CommandBarController::MenuHitTest(int x, int y) const
    {
        const POINT point{x, y};
        for (int index = 0; index < static_cast<int>(menuButtons_.size()); ++index)
        {
            if (PtInRect(&menuButtons_[static_cast<std::size_t>(index)].rect, point) != FALSE)
            {
                return index;
            }
        }

        return -1;
    }

    int CommandBarController::ToolbarHitTest(int x, int y) const
    {
        const POINT point{x, y};
        for (int index = 0; index < static_cast<int>(items_.size()); ++index)
        {
            const auto& item = items_[static_cast<std::size_t>(index)];
            if (item.kind == ToolbarItemKind::Separator || item.kind == ToolbarItemKind::FilterEdit)
            {
                continue;
            }

            if (PtInRect(&item.rect, point))
            {
                return index;
            }
        }

        return -1;
    }

    std::vector<CommandBarController::ToolbarItem>& CommandBarController::Items()
    {
        return items_;
    }

    const std::vector<CommandBarController::ToolbarItem>& CommandBarController::Items() const
    {
        return items_;
    }

    std::array<CommandBarController::CommandBarMenuButton, 4>& CommandBarController::MenuButtons()
    {
        return menuButtons_;
    }

    const std::array<CommandBarController::CommandBarMenuButton, 4>& CommandBarController::MenuButtons() const
    {
        return menuButtons_;
    }
}
