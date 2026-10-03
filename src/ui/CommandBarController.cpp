#include "ui/CommandBarController.h"

#include <algorithm>
#include <utility>

#include "ui/CommandIds.h"
#include "ui/ShortcutCatalog.h"

namespace hyperbrowse::ui
{
    namespace
    {
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

        addIcon(ID_VIEW_NAVIGATE_BACK_FOLDER, "back", L"Back to Previous Folder (Backspace / Alt+Left)");
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

        ToolbarItem clearFilterItem;
        clearFilterItem.commandId = ID_ACTION_CLEAR_FILTER;
        clearFilterItem.tooltip = L"Clear Filter";
        clearFilterItem.kind = ToolbarItemKind::FilterClear;
        clearFilterItem.alignment = ToolbarAlignment::Left;
        clearFilterItem.enabled = false;
        items_.push_back(std::move(clearFilterItem));

        addIcon(ID_FILE_SAVE_CURRENT_FILTER, "save", L"Save Current Filter");
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
        Layout(clientWidth,
               itemTop,
               MakeMenuMetrics(hyperbrowse::util::kDefaultAppTextSize),
               menuFont,
               measureTextWidth);
    }

    void CommandBarController::Layout(int clientWidth,
                                      int itemTop,
                                      const MenuMetrics& metrics,
                                      HFONT menuFont,
                                      const TextWidthHandler& measureTextWidth)
    {
        const int actionStripPaddingX = metrics.ScaleDip(metrics.commandBarPaddingXDip);
        const int itemSize = metrics.ScaleDip(metrics.commandBarItemSizeDip);
        const int separatorWidth = metrics.ScaleDip(metrics.commandBarSeparatorWidthDip);
        const int separatorGap = metrics.ScaleDip(metrics.commandBarSeparatorGapDip);
        const int menuButtonGap = metrics.ScaleDip(metrics.commandBarMenuButtonGapDip);
        const int menuButtonPadding = metrics.ScaleDip(metrics.commandBarMenuButtonPaddingDip);
        const int menuButtonMinWidth = metrics.ScaleDip(metrics.commandBarMenuButtonMinWidthDip);
        const int menuChevronWidth = metrics.ScaleDip(metrics.commandBarMenuChevronWidthDip);

        int leftCursor = actionStripPaddingX;
        int rightCursor = clientWidth - actionStripPaddingX;
        int filterItemIndex = -1;
        int filterClearItemIndex = -1;
        int saveFilterItemIndex = -1;

        for (auto& button : menuButtons_)
        {
            if (button.label.empty() || !button.menu)
            {
                button.rect = RECT{};
                continue;
            }

            const int textWidth = measureTextWidth ? measureTextWidth(menuFont, button.label) : 0;
            const int buttonWidth = std::max(menuButtonMinWidth,
                                             textWidth + (menuButtonPadding * 2)
                                                 + menuChevronWidth + metrics.ScaleDip(8));
            button.rect = RECT{leftCursor, itemTop, leftCursor + buttonWidth, itemTop + itemSize};
            leftCursor += buttonWidth + menuButtonGap;
        }

        leftCursor += metrics.ScaleDip(8);

        for (int index = 0; index < static_cast<int>(items_.size()); ++index)
        {
            auto& item = items_[static_cast<std::size_t>(index)];
            if (item.alignment != ToolbarAlignment::Left)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                item.rect = RECT{leftCursor + separatorGap,
                                 itemTop,
                                 leftCursor + separatorGap + metrics.ScaleDip(1),
                                 itemTop + itemSize};
                leftCursor += separatorWidth;
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterEdit)
            {
                filterItemIndex = index;
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterClear)
            {
                filterClearItemIndex = index;
                continue;
            }

            if (item.commandId == ID_FILE_SAVE_CURRENT_FILTER)
            {
                saveFilterItemIndex = index;
                continue;
            }

            item.rect = RECT{leftCursor, itemTop, leftCursor + itemSize, itemTop + itemSize};
            leftCursor += itemSize + metrics.ScaleDip(2);
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
                rightCursor -= separatorWidth;
                item.rect = RECT{rightCursor + separatorGap,
                                 itemTop,
                                 rightCursor + separatorGap + metrics.ScaleDip(1),
                                 itemTop + itemSize};
                continue;
            }

            rightCursor -= itemSize;
            item.rect = RECT{rightCursor, itemTop, rightCursor + itemSize, itemTop + itemSize};
            rightCursor -= metrics.ScaleDip(2);
        }

        if (filterItemIndex >= 0)
        {
            const int filterLeft = leftCursor + metrics.ScaleDip(6);
            const int filterRightLimit = std::max(filterLeft, rightCursor - metrics.ScaleDip(6));
            int availableFilterWidth = filterRightLimit - filterLeft;
            const int saveButtonGap = metrics.ScaleDip(metrics.commandBarFilterSaveButtonGapDip);
            const int minimumFilterWidth = metrics.ScaleDip(80);
            bool showSaveFilter = false;
            if (saveFilterItemIndex >= 0)
            {
                const int availableFilterWidthWithSave =
                    rightCursor - itemSize - saveButtonGap - filterLeft;
                if (availableFilterWidthWithSave >= minimumFilterWidth)
                {
                    availableFilterWidth = availableFilterWidthWithSave;
                    showSaveFilter = true;
                }
            }

            const int filterWidth = std::min(availableFilterWidth,
                                             metrics.ScaleDip(metrics.commandBarFilterEditMaxWidthDip));
            const int filterFieldRight = filterLeft + filterWidth;
            items_[static_cast<std::size_t>(filterItemIndex)].rect =
                RECT{filterLeft, itemTop, filterFieldRight, itemTop + itemSize};

            if (filterClearItemIndex >= 0)
            {
                const int clearButtonSize = metrics.ScaleDip(metrics.commandBarFilterClearButtonSizeDip);
                const int clearButtonRight = std::max(
                    filterLeft,
                    filterFieldRight - metrics.ScaleDip(metrics.commandBarFilterEditHorizontalInsetDip));
                const int clearButtonLeft = std::max(filterLeft, clearButtonRight - clearButtonSize);
                const int clearButtonTop = itemTop + std::max(0, (itemSize - clearButtonSize) / 2);
                items_[static_cast<std::size_t>(filterClearItemIndex)].rect =
                    RECT{clearButtonLeft,
                         clearButtonTop,
                         clearButtonRight,
                         clearButtonTop + clearButtonSize};
            }

            if (saveFilterItemIndex >= 0)
            {
                auto& saveFilterItem = items_[static_cast<std::size_t>(saveFilterItemIndex)];
                if (showSaveFilter)
                {
                    const int saveButtonLeft = filterFieldRight + saveButtonGap;
                    saveFilterItem.rect =
                        RECT{saveButtonLeft, itemTop, saveButtonLeft + itemSize, itemTop + itemSize};
                }
                else
                {
                    saveFilterItem.rect = RECT{};
                }
            }
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
            case ID_FILE_SAVE_CURRENT_FILTER:
                item.enabled = state.saveFilterEnabled;
                break;
            case ID_ACTION_CLEAR_FILTER:
                item.enabled = state.clearFilterEnabled;
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

    CommandBarController::KeyboardInputResult CommandBarController::HandleKeyboardInput(
        UINT message,
        WPARAM wParam,
        const KeyboardInputState& state) const
    {
        KeyboardInputResult result;
        const int mnemonicIndex = MainMenuMnemonicIndexFromVirtualKey(static_cast<WORD>(wParam));

        if (message == WM_SYSCHAR)
        {
            result.handled = mnemonicIndex >= 0 || (state.active && wParam != L' ');
            return result;
        }

        if (message == WM_SYSKEYDOWN)
        {
            if (wParam == VK_F10 && !state.shiftPressed)
            {
                result.handled = true;
                if (!state.isRepeat)
                {
                    result.action = state.active ? KeyboardAction::Deactivate : KeyboardAction::Activate;
                    result.index = state.hotIndex >= 0 ? state.hotIndex : 0;
                }
                return result;
            }

            if (wParam == VK_MENU)
            {
                result.handled = true;
                if (!state.isRepeat)
                {
                    result.action = state.active ? KeyboardAction::Deactivate : KeyboardAction::Activate;
                    result.index = state.hotIndex >= 0 ? state.hotIndex : 0;
                }
                return result;
            }

            if (mnemonicIndex >= 0)
            {
                result.action = KeyboardAction::ActivateAndOpenMenu;
                result.index = mnemonicIndex;
                result.handled = true;
                return result;
            }
        }

        if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
        {
            return result;
        }

        if (!state.active)
        {
            return result;
        }

        const int menuCount = static_cast<int>(menuButtons_.size());
        switch (wParam)
        {
        case VK_LEFT:
            result.action = KeyboardAction::Activate;
            result.index = (state.hotIndex + menuCount - 1) % menuCount;
            result.handled = true;
            return result;
        case VK_RIGHT:
            result.action = KeyboardAction::Activate;
            result.index = (state.hotIndex + 1) % menuCount;
            result.handled = true;
            return result;
        case VK_HOME:
            result.action = KeyboardAction::Activate;
            result.index = 0;
            result.handled = true;
            return result;
        case VK_END:
            result.action = KeyboardAction::Activate;
            result.index = menuCount - 1;
            result.handled = true;
            return result;
        case VK_DOWN:
        case VK_RETURN:
            result.action = KeyboardAction::OpenMenu;
            result.index = state.hotIndex >= 0 ? state.hotIndex : 0;
            result.handled = true;
            return result;
        case VK_SPACE:
            if (message == WM_KEYDOWN)
            {
                result.action = KeyboardAction::OpenMenu;
                result.index = state.hotIndex >= 0 ? state.hotIndex : 0;
                result.handled = true;
            }
            return result;
        case VK_ESCAPE:
            result.action = KeyboardAction::Deactivate;
            result.handled = true;
            return result;
        default:
            break;
        }

        if (mnemonicIndex >= 0)
        {
            result.action = KeyboardAction::ActivateAndOpenMenu;
            result.index = mnemonicIndex;
            result.handled = true;
        }

        return result;
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
            if (item.kind == ToolbarItemKind::Separator
                || item.kind == ToolbarItemKind::FilterEdit
                || (item.kind == ToolbarItemKind::FilterClear && !item.enabled))
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

    std::array<CommandBarController::CommandBarMenuButton, 5>& CommandBarController::MenuButtons()
    {
        return menuButtons_;
    }

    const std::array<CommandBarController::CommandBarMenuButton, 5>& CommandBarController::MenuButtons() const
    {
        return menuButtons_;
    }
}
