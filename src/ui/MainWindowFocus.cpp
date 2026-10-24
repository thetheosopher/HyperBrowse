#include "ui/MainWindowInternal.h"

namespace
{

    thread_local hyperbrowse::ui::MainWindow* g_commandBarMenuFilterWindow = nullptr;
    constexpr UINT kCommandBarMenuTrackingIntervalMs = 50;
}

namespace hyperbrowse::ui
{

    void MainWindow::MeasureOwnerDrawMenuItem(MEASUREITEMSTRUCT* measureItem) const
    {
        const UINT dpi = hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u;
        menuPainter_.MeasureOwnerDrawMenuItem(measureItem, MakeMenuMetrics(appTextSize_, dpi), appTextUiFont_);
    }

    void MainWindow::DrawOwnerDrawMenuItem(const DRAWITEMSTRUCT& drawItem) const
    {
        const ThemePalette palette = GetThemePalette();
        const MenuPainterPalette painterPalette{
            palette.windowBackground,
            palette.paneBackground,
            palette.text,
            palette.mutedText,
            palette.actionStripBackground,
            palette.actionStripBorder,
            palette.accent,
            palette.accentFill,
            palette.accentText};
        menuPainter_.DrawOwnerDrawMenuItem(
            drawItem,
            painterPalette,
            MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u),
            appTextUiFont_,
            themeMode_ == ThemeMode::Dark);
    }

    int MainWindow::CommandBarMenuHitTest(int x, int y) const
    {
        return commandBarController_.MenuHitTest(x, y);
    }

    void MainWindow::ActivateCommandBarKeyboardMode(int index)
    {
        if (commandBarMenuButtons_.empty())
        {
            return;
        }

        const int targetIndex = std::clamp(index, 0, static_cast<int>(commandBarMenuButtons_.size()) - 1);
        if (!commandBarKeyboardActive_)
        {
            HWND focusWindow = GetFocus();
            if (focusWindow && (focusWindow == hwnd_ || IsChild(hwnd_, focusWindow)))
            {
                commandBarPreviousFocus_ = focusWindow;
            }
            else
            {
                commandBarPreviousFocus_ = nullptr;
            }
        }

        commandBarKeyboardActive_ = true;
        commandBarPressedIndex_ = -1;
        commandBarHotIndex_ = targetIndex;
        InvalidateToolbarStrip();

        if (hwnd_ && GetFocus() != hwnd_)
        {
            SetFocus(hwnd_);
        }
        if (accessibility_)
        {
            accessibility_->NotifyFocusChanged();
        }
    }

    void MainWindow::DeactivateCommandBarKeyboardMode(bool restoreFocus)
    {
        const HWND restoreWindow = commandBarPreviousFocus_;
        const bool hadVisualState = commandBarKeyboardActive_ || commandBarHotIndex_ >= 0 || commandBarPressedIndex_ >= 0;

        commandBarKeyboardActive_ = false;
        commandBarPressedIndex_ = -1;
        commandBarHotIndex_ = -1;
        commandBarPreviousFocus_ = nullptr;

        if (hadVisualState)
        {
            InvalidateToolbarStrip();
        }

        if (restoreFocus && restoreWindow && restoreWindow != hwnd_ && IsWindow(restoreWindow)
            && (restoreWindow == hwnd_ || IsChild(hwnd_, restoreWindow)))
        {
            SetFocus(restoreWindow);
        }
        if (accessibility_)
        {
            accessibility_->NotifyFocusChanged();
        }
    }

    bool MainWindow::HandleCommandBarKeyboardInput(UINT message, WPARAM wParam, LPARAM lParam)
    {
        const CommandBarController::KeyboardInputState state{
            commandBarKeyboardActive_,
            commandBarHotIndex_,
            (GetKeyState(VK_SHIFT) & 0x8000) != 0,
            (lParam & 0x40000000) != 0,
        };
        const CommandBarController::KeyboardInputResult result =
            commandBarController_.HandleKeyboardInput(message, wParam, state);
        if (!result.handled)
        {
            return false;
        }

        switch (result.action)
        {
        case CommandBarController::KeyboardAction::Activate:
            ActivateCommandBarKeyboardMode(result.index);
            break;
        case CommandBarController::KeyboardAction::ActivateAndOpenMenu:
            ActivateCommandBarKeyboardMode(result.index);
            OpenCommandBarMenu(result.index);
            break;
        case CommandBarController::KeyboardAction::OpenMenu:
            OpenCommandBarMenu(result.index);
            break;
        case CommandBarController::KeyboardAction::Deactivate:
            DeactivateCommandBarKeyboardMode(true);
            break;
        case CommandBarController::KeyboardAction::None:
            break;
        }

        return true;
    }

    std::vector<MainWindow::KeyboardFocusTarget> MainWindow::BuildKeyboardFocusSequence() const
    {
        std::vector<KeyboardFocusTarget> sequence;
        const auto addTarget = [this, &sequence](KeyboardFocusTarget target)
        {
            if (IsKeyboardFocusTargetAvailable(target))
            {
                sequence.push_back(target);
            }
        };

        addTarget({KeyboardFocusTargetKind::FilterEdit});
        for (int index = 0; index < static_cast<int>(toolbarItems_.size()); ++index)
        {
            addTarget({KeyboardFocusTargetKind::ToolbarItem, index});
        }

        addTarget({KeyboardFocusTargetKind::FolderTree});
        addTarget({KeyboardFocusTargetKind::BrowserPane});

        if (detailsStripVisible_)
        {
            for (int index = 0; index < static_cast<int>(detailsPanelTabRects_.size()); ++index)
            {
                addTarget({KeyboardFocusTargetKind::DetailsTab, index});
            }
            addTarget({KeyboardFocusTargetKind::DetailsCloseButton});

            if (activeRightPaneTab_ == RightPaneTab::FileDetails)
            {
                addTarget({KeyboardFocusTargetKind::DetailsText});
            }
            else if (activeRightPaneTab_ == RightPaneTab::QuickSend)
            {
                addTarget({KeyboardFocusTargetKind::QuickAccessSortButton});
                for (int rowIndex = 0; rowIndex < static_cast<int>(quickAccessDestinationRows_.size()); ++rowIndex)
                {
                    addTarget({KeyboardFocusTargetKind::QuickAccessShortcutEdit, rowIndex});
                    addTarget({KeyboardFocusTargetKind::QuickAccessRow, rowIndex});
                    addTarget({KeyboardFocusTargetKind::QuickAccessButton, rowIndex * 3});
                    addTarget({KeyboardFocusTargetKind::QuickAccessButton, rowIndex * 3 + 1});
                    addTarget({KeyboardFocusTargetKind::QuickAccessButton, rowIndex * 3 + 2});
                }
                addTarget({KeyboardFocusTargetKind::QuickAccessScrollBar});
            }
        }

        return sequence;
    }

    std::vector<MainWindow::AccessibilityTarget> MainWindow::BuildAccessibilityTargets() const
    {
        std::vector<AccessibilityTarget> targets;
        const auto addKeyboardTarget = [&targets](KeyboardFocusTarget target)
        {
            targets.push_back({AccessibilityTargetKind::KeyboardFocus, -1, target});
        };

        for (int index = 0; index < static_cast<int>(commandBarMenuButtons_.size()); ++index)
        {
            const auto& button = commandBarMenuButtons_[static_cast<std::size_t>(index)];
            if (!button.label.empty() && !IsRectEmpty(&button.rect))
            {
                targets.push_back({AccessibilityTargetKind::CommandBarMenu, index, {}});
            }
        }

        for (int index = 0; index < static_cast<int>(toolbarItems_.size()); ++index)
        {
            const auto& item = toolbarItems_[static_cast<std::size_t>(index)];
            if (item.kind != ToolbarItemKind::Separator
                && item.kind != ToolbarItemKind::FilterEdit
                && !IsRectEmpty(&item.rect))
            {
                addKeyboardTarget({KeyboardFocusTargetKind::ToolbarItem, index});
            }
        }

        if (browserPane_ && IsWindowVisible(browserPane_) != FALSE)
        {
            addKeyboardTarget({KeyboardFocusTargetKind::BrowserPane});
        }

        if (!detailsStripVisible_)
        {
            return targets;
        }

        for (int index = 0; index < static_cast<int>(detailsPanelTabRects_.size()); ++index)
        {
            if (!IsRectEmpty(&detailsPanelTabRects_[static_cast<std::size_t>(index)]))
            {
                addKeyboardTarget({KeyboardFocusTargetKind::DetailsTab, index});
            }
        }
        if (!IsRectEmpty(&detailsPanelCloseButtonRect_))
        {
            addKeyboardTarget({KeyboardFocusTargetKind::DetailsCloseButton});
        }

        if (activeRightPaneTab_ == RightPaneTab::QuickSend)
        {
            if (!IsRectEmpty(&quickAccessSortButtonRect_))
            {
                addKeyboardTarget({KeyboardFocusTargetKind::QuickAccessSortButton});
            }
            for (int rowIndex = 0; rowIndex < static_cast<int>(quickAccessDestinationRows_.size()); ++rowIndex)
            {
                const auto& row = quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)];
                if (!IsRectEmpty(&row.rowRect))
                {
                    addKeyboardTarget({KeyboardFocusTargetKind::QuickAccessRow, rowIndex});
                }
                if (!IsRectEmpty(&row.copyRect))
                {
                    addKeyboardTarget({KeyboardFocusTargetKind::QuickAccessButton, rowIndex * 3});
                }
                if (!IsRectEmpty(&row.moveRect))
                {
                    addKeyboardTarget({KeyboardFocusTargetKind::QuickAccessButton, rowIndex * 3 + 1});
                }
                if (!IsRectEmpty(&row.removeRect))
                {
                    addKeyboardTarget({KeyboardFocusTargetKind::QuickAccessButton, rowIndex * 3 + 2});
                }
            }
        }

        return targets;
    }

    std::vector<MainWindowAccessibility::Item> MainWindow::BuildAccessibilityItems() const
    {
        const auto targets = BuildAccessibilityTargets();
        std::vector<MainWindowAccessibility::Item> items;
        items.reserve(targets.size());

        const auto addState = [](long state, DWORD flag)
        {
            return static_cast<long>(static_cast<unsigned long>(state) | static_cast<unsigned long>(flag));
        };
        const auto clientToScreenRect = [this](RECT bounds)
        {
            if (!hwnd_ || IsRectEmpty(&bounds))
            {
                return RECT{};
            }
            const POINT topLeft{bounds.left, bounds.top};
            POINT screenTopLeft = topLeft;
            if (!ClientToScreen(hwnd_, &screenTopLeft))
            {
                return RECT{};
            }
            OffsetRect(&bounds, screenTopLeft.x - topLeft.x, screenTopLeft.y - topLeft.y);
            return bounds;
        };
        const auto quickAccessLabel = [](const QuickAccessDestinationRow& row)
        {
            return row.displayLabel.empty() ? row.destinationPath : row.displayLabel;
        };
        const auto addItem = [&items](MainWindowAccessibility::Item item)
        {
            if (!item.name.empty() && !IsRectEmpty(&item.bounds))
            {
                items.push_back(std::move(item));
            }
        };

        for (const auto& target : targets)
        {
            if (target.kind == AccessibilityTargetKind::CommandBarMenu)
            {
                const auto& button = commandBarMenuButtons_[static_cast<std::size_t>(target.index)];
                MainWindowAccessibility::Item item;
                item.name = button.label;
                item.description = L"Open the " + button.label + L" menu";
                item.defaultAction = L"Open menu";
                item.bounds = clientToScreenRect(button.rect);
                item.role = ROLE_SYSTEM_MENUITEM;
                addItem(std::move(item));
                continue;
            }

            const KeyboardFocusTarget& focusTarget = target.keyboardFocus;
            MainWindowAccessibility::Item item;
            item.state = STATE_SYSTEM_FOCUSABLE;
            switch (focusTarget.kind)
            {
            case KeyboardFocusTargetKind::ToolbarItem:
            {
                const auto& toolbarItem = toolbarItems_[static_cast<std::size_t>(focusTarget.index)];
                item.name = toolbarItem.tooltip.empty() ? L"Toolbar action" : toolbarItem.tooltip;
                item.description = item.name;
                item.defaultAction = toolbarItem.kind == ToolbarItemKind::IconDropdown
                    ? L"Open menu"
                    : L"Activate";
                item.bounds = clientToScreenRect(toolbarItem.rect);
                item.role = toolbarItem.kind == ToolbarItemKind::IconDropdown
                    ? ROLE_SYSTEM_BUTTONDROPDOWN
                    : toolbarItem.kind == ToolbarItemKind::IconToggle
                        ? ROLE_SYSTEM_CHECKBUTTON
                        : ROLE_SYSTEM_PUSHBUTTON;
                if (!toolbarItem.enabled)
                {
                    item.state = addState(item.state, STATE_SYSTEM_UNAVAILABLE);
                }
                if (toolbarItem.checked)
                {
                    item.state = addState(item.state, STATE_SYSTEM_CHECKED);
                    item.value = L"On";
                }
                else if (toolbarItem.kind == ToolbarItemKind::IconToggle)
                {
                    item.value = L"Off";
                }
                break;
            }
            case KeyboardFocusTargetKind::BrowserPane:
                item.name = L"Image browser";
                item.description = L"Browse and select images";
                item.defaultAction = L"Focus image browser";
                item.role = ROLE_SYSTEM_LIST;
                GetWindowRect(browserPane_, &item.bounds);
                break;
            case KeyboardFocusTargetKind::DetailsTab:
                item.name = focusTarget.index == static_cast<int>(RightPaneTab::QuickSend)
                    ? L"Quick Actions"
                    : focusTarget.index == static_cast<int>(RightPaneTab::CacheStats)
                        ? L"Cache Stats"
                        : L"File Details";
                item.description = L"Select details panel view";
                item.defaultAction = L"Select tab";
                item.bounds = clientToScreenRect(detailsPanelTabRects_[static_cast<std::size_t>(focusTarget.index)]);
                item.role = ROLE_SYSTEM_PAGETAB;
                if (focusTarget.index == static_cast<int>(activeRightPaneTab_))
                {
                    item.state = addState(item.state, STATE_SYSTEM_SELECTED);
                    item.value = L"Selected";
                }
                else
                {
                    item.value = L"Not selected";
                }
                break;
            case KeyboardFocusTargetKind::DetailsCloseButton:
                item.name = L"Close details panel";
                item.description = item.name;
                item.defaultAction = L"Close";
                item.bounds = clientToScreenRect(detailsPanelCloseButtonRect_);
                item.role = ROLE_SYSTEM_PUSHBUTTON;
                break;
            case KeyboardFocusTargetKind::QuickAccessSortButton:
                item.name = L"Sort Quick Actions by shortcut";
                item.description = item.name;
                item.defaultAction = L"Sort";
                item.bounds = clientToScreenRect(quickAccessSortButtonRect_);
                item.role = ROLE_SYSTEM_PUSHBUTTON;
                break;
            case KeyboardFocusTargetKind::QuickAccessRow:
            {
                if (focusTarget.index < 0
                    || focusTarget.index >= static_cast<int>(quickAccessDestinationRows_.size()))
                {
                    continue;
                }
                const auto& row = quickAccessDestinationRows_[static_cast<std::size_t>(focusTarget.index)];
                const std::wstring label = quickAccessLabel(row);
                item.name = L"Quick Actions folder: " + label;
                item.description = row.destinationPath;
                item.defaultAction = L"Open folder";
                item.value = row.destinationPath;
                item.bounds = clientToScreenRect(row.rowRect);
                item.role = ROLE_SYSTEM_LISTITEM;
                if (!CanNavigateToQuickAccessDestination(row.destinationPath))
                {
                    item.state = addState(item.state, STATE_SYSTEM_UNAVAILABLE);
                }
                break;
            }
            case KeyboardFocusTargetKind::QuickAccessButton:
            {
                const int rowIndex = focusTarget.index / 3;
                const int actionIndex = focusTarget.index % 3;
                if (rowIndex < 0 || rowIndex >= static_cast<int>(quickAccessDestinationRows_.size()))
                {
                    continue;
                }
                const auto& row = quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)];
                const std::wstring label = quickAccessLabel(row);
                item.name = actionIndex == 0
                    ? L"Copy files to " + label
                    : actionIndex == 1
                        ? L"Move files to " + label
                        : L"Remove " + label + L" from Quick Actions";
                item.description = row.destinationPath;
                item.defaultAction = actionIndex == 0
                    ? L"Copy"
                    : actionIndex == 1
                        ? L"Move"
                        : L"Remove";
                item.bounds = clientToScreenRect(actionIndex == 0
                    ? row.copyRect
                    : actionIndex == 1 ? row.moveRect : row.removeRect);
                item.role = ROLE_SYSTEM_PUSHBUTTON;
                if (actionIndex < 2 && !CanUseQuickAccessDestinationActions(row.destinationPath))
                {
                    item.state = addState(item.state, STATE_SYSTEM_UNAVAILABLE);
                }
                break;
            }
            case KeyboardFocusTargetKind::None:
            case KeyboardFocusTargetKind::FilterEdit:
            case KeyboardFocusTargetKind::FolderTree:
            case KeyboardFocusTargetKind::DetailsText:
            case KeyboardFocusTargetKind::QuickAccessShortcutEdit:
            case KeyboardFocusTargetKind::QuickAccessScrollBar:
            default:
                continue;
            }
            addItem(std::move(item));
        }

        return items;
    }

    long MainWindow::AccessibilityFocusedChildId() const
    {
        const auto targets = BuildAccessibilityTargets();
        if (commandBarKeyboardActive_ && GetFocus() == hwnd_)
        {
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                if (targets[index].kind == AccessibilityTargetKind::CommandBarMenu
                    && targets[index].index == commandBarHotIndex_)
                {
                    return static_cast<long>(index + 1);
                }
            }
        }

        const KeyboardFocusTarget current = CurrentKeyboardFocusTarget();
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            if (targets[index].kind == AccessibilityTargetKind::KeyboardFocus
                && targets[index].keyboardFocus == current)
            {
                return static_cast<long>(index + 1);
            }
        }
        return CHILDID_SELF;
    }

    bool MainWindow::FocusAccessibilityChild(long childId)
    {
        if (childId == CHILDID_SELF)
        {
            if (hwnd_ && IsWindowEnabled(hwnd_))
            {
                SetFocus(hwnd_);
                if (accessibility_)
                {
                    accessibility_->NotifyFocusChanged();
                }
                return GetFocus() == hwnd_;
            }
            return false;
        }

        const auto targets = BuildAccessibilityTargets();
        if (childId <= CHILDID_SELF || childId > static_cast<long>(targets.size()))
        {
            return false;
        }
        const AccessibilityTarget& target = targets[static_cast<std::size_t>(childId - 1)];
        if (target.kind == AccessibilityTargetKind::CommandBarMenu)
        {
            ActivateCommandBarKeyboardMode(target.index);
            return commandBarKeyboardActive_ && GetFocus() == hwnd_;
        }
        return FocusKeyboardTarget(target.keyboardFocus);
    }

    bool MainWindow::ActivateAccessibilityChild(long childId)
    {
        const auto targets = BuildAccessibilityTargets();
        if (childId <= CHILDID_SELF || childId > static_cast<long>(targets.size()))
        {
            return false;
        }
        const AccessibilityTarget& target = targets[static_cast<std::size_t>(childId - 1)];
        if (target.kind == AccessibilityTargetKind::CommandBarMenu)
        {
            ActivateCommandBarKeyboardMode(target.index);
            OpenCommandBarMenu(target.index);
            return commandBarKeyboardActive_ && GetFocus() == hwnd_;
        }

        if (!IsKeyboardFocusTargetAvailable(target.keyboardFocus)
            || !FocusKeyboardTarget(target.keyboardFocus))
        {
            return false;
        }
        if (target.keyboardFocus.kind == KeyboardFocusTargetKind::BrowserPane)
        {
            return true;
        }
        ActivateKeyboardFocusTarget(target.keyboardFocus);
        return true;
    }

    MainWindow::KeyboardFocusTarget MainWindow::CurrentKeyboardFocusTarget() const
    {
        const HWND focus = GetFocus();
        if (!focus)
        {
            return {};
        }

        if (focus == filterEdit_)
        {
            return {KeyboardFocusTargetKind::FilterEdit};
        }
        if (focus == treePane_ || (treePane_ && IsChild(treePane_, focus)))
        {
            return {KeyboardFocusTargetKind::FolderTree};
        }
        if (focus == browserPane_ || (browserPane_ && IsChild(browserPane_, focus)))
        {
            return {KeyboardFocusTargetKind::BrowserPane};
        }
        if (focus == detailsPanelText_)
        {
            return {KeyboardFocusTargetKind::DetailsText};
        }
        if (focus == quickAccessScrollBar_)
        {
            return {KeyboardFocusTargetKind::QuickAccessScrollBar};
        }
        for (int index = 0; index < static_cast<int>(quickAccessShortcutEdits_.size()); ++index)
        {
            if (focus == quickAccessShortcutEdits_[static_cast<std::size_t>(index)])
            {
                return {KeyboardFocusTargetKind::QuickAccessShortcutEdit, index};
            }
        }
        if (focus == hwnd_ && IsKeyboardFocusTargetAvailable(keyboardFocusTarget_))
        {
            return keyboardFocusTarget_;
        }

        return {};
    }

    bool MainWindow::IsKeyboardFocusTargetAvailable(const KeyboardFocusTarget& target) const
    {
        const auto isWindowTargetAvailable = [](HWND window)
        {
            return window && IsWindow(window) != FALSE
                && IsWindowVisible(window) != FALSE
                && IsWindowEnabled(window) != FALSE;
        };

        switch (target.kind)
        {
        case KeyboardFocusTargetKind::FilterEdit:
            return isWindowTargetAvailable(filterEdit_);
        case KeyboardFocusTargetKind::ToolbarItem:
            return target.index >= 0
                && target.index < static_cast<int>(toolbarItems_.size())
                && toolbarItems_[static_cast<std::size_t>(target.index)].kind != ToolbarItemKind::Separator
                && toolbarItems_[static_cast<std::size_t>(target.index)].kind != ToolbarItemKind::FilterEdit
                && toolbarItems_[static_cast<std::size_t>(target.index)].enabled
                && !IsRectEmpty(&toolbarItems_[static_cast<std::size_t>(target.index)].rect);
        case KeyboardFocusTargetKind::FolderTree:
            return isWindowTargetAvailable(treePane_);
        case KeyboardFocusTargetKind::BrowserPane:
            return isWindowTargetAvailable(browserPane_);
        case KeyboardFocusTargetKind::DetailsTab:
            return detailsStripVisible_
                && target.index >= 0
                && target.index < static_cast<int>(detailsPanelTabRects_.size())
                && !IsRectEmpty(&detailsPanelTabRects_[static_cast<std::size_t>(target.index)]);
        case KeyboardFocusTargetKind::DetailsCloseButton:
            return detailsStripVisible_ && !IsRectEmpty(&detailsPanelCloseButtonRect_);
        case KeyboardFocusTargetKind::DetailsText:
            return activeRightPaneTab_ == RightPaneTab::FileDetails
                && isWindowTargetAvailable(detailsPanelText_);
        case KeyboardFocusTargetKind::QuickAccessSortButton:
            return activeRightPaneTab_ == RightPaneTab::QuickSend
                && !IsRectEmpty(&quickAccessSortButtonRect_);
        case KeyboardFocusTargetKind::QuickAccessShortcutEdit:
            return activeRightPaneTab_ == RightPaneTab::QuickSend
                && target.index >= 0
                && target.index < static_cast<int>(quickAccessDestinationRows_.size())
                && target.index < static_cast<int>(quickAccessShortcutEdits_.size())
                && quickAccessShortcutEdits_[static_cast<std::size_t>(target.index)]
                && IsWindow(quickAccessShortcutEdits_[static_cast<std::size_t>(target.index)]) != FALSE
                && IsWindowEnabled(quickAccessShortcutEdits_[static_cast<std::size_t>(target.index)]) != FALSE;
        case KeyboardFocusTargetKind::QuickAccessRow:
            return activeRightPaneTab_ == RightPaneTab::QuickSend
                && target.index >= 0
                && target.index < static_cast<int>(quickAccessDestinationRows_.size())
                && CanNavigateToQuickAccessDestination(
                    quickAccessDestinationRows_[static_cast<std::size_t>(target.index)].destinationPath);
        case KeyboardFocusTargetKind::QuickAccessButton:
        {
            if (activeRightPaneTab_ != RightPaneTab::QuickSend || target.index < 0)
            {
                return false;
            }
            const int rowIndex = target.index / 3;
            const int actionIndex = target.index % 3;
            if (rowIndex < 0 || rowIndex >= static_cast<int>(quickAccessDestinationRows_.size()))
            {
                return false;
            }
            if (actionIndex == 2)
            {
                return true;
            }
            return CanUseQuickAccessDestinationActions(
                quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)].destinationPath);
        }
        case KeyboardFocusTargetKind::QuickAccessScrollBar:
            return isWindowTargetAvailable(quickAccessScrollBar_);
        case KeyboardFocusTargetKind::None:
        default:
            return false;
        }
    }

    void MainWindow::EnsureQuickAccessTargetVisible(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(quickAccessDestinationRows_.size())
            || IsRectEmpty(&quickAccessDestinationViewportRect_))
        {
            return;
        }

        const RECT& rowRect = quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)].rowRect;
        int nextOffset = quickAccessScrollOffset_;
        if (rowRect.top < quickAccessDestinationViewportRect_.top)
        {
            nextOffset += rowRect.top - quickAccessDestinationViewportRect_.top;
        }
        else if (rowRect.bottom > quickAccessDestinationViewportRect_.bottom)
        {
            nextOffset += rowRect.bottom - quickAccessDestinationViewportRect_.bottom;
        }

        const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(
            detailsPanelSummaryFont_,
            detailsPanelBodyFont_,
            MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u));
        const int totalRowsHeight = static_cast<int>(quickAccessDestinationRows_.size()) * metrics.rowHeight
            + static_cast<int>((quickAccessDestinationRows_.size() - 1) * metrics.rowGap);
        const int maximumScrollOffset = std::max(
            0,
            totalRowsHeight - static_cast<int>(quickAccessDestinationViewportRect_.bottom
                                               - quickAccessDestinationViewportRect_.top));
        nextOffset = std::clamp(nextOffset, 0, maximumScrollOffset);
        if (nextOffset != quickAccessScrollOffset_)
        {
            quickAccessScrollOffset_ = nextOffset;
            LayoutChildren();
        }
    }

    bool MainWindow::FocusKeyboardTarget(const KeyboardFocusTarget& target)
    {
        if (target.kind == KeyboardFocusTargetKind::QuickAccessShortcutEdit
            || target.kind == KeyboardFocusTargetKind::QuickAccessRow
            || target.kind == KeyboardFocusTargetKind::QuickAccessButton)
        {
            EnsureQuickAccessTargetVisible(target.index / (target.kind == KeyboardFocusTargetKind::QuickAccessButton ? 3 : 1));
        }

        if (!IsKeyboardFocusTargetAvailable(target))
        {
            return false;
        }

        HWND focusWindow = hwnd_;
        switch (target.kind)
        {
        case KeyboardFocusTargetKind::FilterEdit:
            focusWindow = filterEdit_;
            break;
        case KeyboardFocusTargetKind::FolderTree:
            focusWindow = treePane_;
            break;
        case KeyboardFocusTargetKind::BrowserPane:
            focusWindow = browserPane_;
            break;
        case KeyboardFocusTargetKind::DetailsText:
            focusWindow = detailsPanelText_;
            break;
        case KeyboardFocusTargetKind::QuickAccessShortcutEdit:
            focusWindow = quickAccessShortcutEdits_[static_cast<std::size_t>(target.index)];
            break;
        case KeyboardFocusTargetKind::QuickAccessScrollBar:
            focusWindow = quickAccessScrollBar_;
            break;
        case KeyboardFocusTargetKind::ToolbarItem:
        case KeyboardFocusTargetKind::DetailsTab:
        case KeyboardFocusTargetKind::DetailsCloseButton:
        case KeyboardFocusTargetKind::QuickAccessSortButton:
        case KeyboardFocusTargetKind::QuickAccessRow:
        case KeyboardFocusTargetKind::QuickAccessButton:
        case KeyboardFocusTargetKind::None:
        default:
            break;
        }

        const KeyboardFocusTarget previousTarget = keyboardFocusTarget_;
        keyboardFocusTarget_ = target;
        if (GetFocus() != focusWindow)
        {
            SetFocus(focusWindow);
        }

        const bool focused = GetFocus() == focusWindow
            || (target.kind == KeyboardFocusTargetKind::BrowserPane
                && browserPane_ && IsChild(browserPane_, GetFocus()));
        if (!focused)
        {
            keyboardFocusTarget_ = previousTarget;
            return false;
        }

        if (previousTarget != target || target.kind == KeyboardFocusTargetKind::ToolbarItem
            || target.kind == KeyboardFocusTargetKind::DetailsTab
            || target.kind == KeyboardFocusTargetKind::DetailsCloseButton
            || target.kind == KeyboardFocusTargetKind::QuickAccessSortButton
            || target.kind == KeyboardFocusTargetKind::QuickAccessRow
            || target.kind == KeyboardFocusTargetKind::QuickAccessButton)
        {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (accessibility_)
        {
            accessibility_->NotifyFocusChanged();
        }
        return true;
    }

    void MainWindow::ActivateKeyboardFocusTarget(const KeyboardFocusTarget& target)
    {
        switch (target.kind)
        {
        case KeyboardFocusTargetKind::ToolbarItem:
            ToolbarHandleClick(target.index);
            break;
        case KeyboardFocusTargetKind::DetailsTab:
            SelectRightPaneTab(static_cast<RightPaneTab>(target.index));
            break;
        case KeyboardFocusTargetKind::DetailsCloseButton:
            ToggleDetailsPanelVisibility();
            if (!detailsStripVisible_)
            {
                FocusKeyboardTarget({KeyboardFocusTargetKind::BrowserPane});
                return;
            }
            break;
        case KeyboardFocusTargetKind::QuickAccessSortButton:
            SortFavoriteDestinationsByShortcut();
            if (hwnd_ && detailsStripVisible_)
            {
                LayoutChildren();
            }
            UpdateMenuState();
            break;
        case KeyboardFocusTargetKind::QuickAccessRow:
            if (target.index >= 0 && target.index < static_cast<int>(quickAccessDestinationRows_.size()))
            {
                LoadFolderAsync(quickAccessDestinationRows_[static_cast<std::size_t>(target.index)].destinationPath);
            }
            break;
        case KeyboardFocusTargetKind::QuickAccessButton:
        {
            const int rowIndex = target.index / 3;
            const int actionIndex = target.index % 3;
            if (rowIndex < 0 || rowIndex >= static_cast<int>(quickAccessDestinationRows_.size()))
            {
                break;
            }

            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)];
            if (actionIndex == 2)
            {
                if (row.favorite)
                {
                    RemoveFavoriteDestination(row.destinationPath);
                }
                else
                {
                    RemoveRecentDestination(row.destinationPath);
                }
            }
            else
            {
                StartSelectionFileOperationToDestination(
                    actionIndex == 0 ? services::FileOperationType::Copy : services::FileOperationType::Move,
                    row.destinationPath);
            }
            break;
        }
        case KeyboardFocusTargetKind::FilterEdit:
        case KeyboardFocusTargetKind::FolderTree:
        case KeyboardFocusTargetKind::BrowserPane:
        case KeyboardFocusTargetKind::DetailsText:
        case KeyboardFocusTargetKind::QuickAccessShortcutEdit:
        case KeyboardFocusTargetKind::QuickAccessScrollBar:
        case KeyboardFocusTargetKind::None:
        default:
            break;
        }

        if (!IsKeyboardFocusTargetAvailable(target))
        {
            const std::vector<KeyboardFocusTarget> sequence = BuildKeyboardFocusSequence();
            if (!sequence.empty())
            {
                FocusKeyboardTarget(sequence.front());
            }
        }
    }

    bool MainWindow::HandleKeyboardFocusInput(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if ((message != WM_KEYDOWN && message != WM_SYSKEYDOWN) || !hwnd_)
        {
            return false;
        }

        const HWND focus = GetFocus();
        const bool knownTextInput = focus == filterEdit_
            || focus == detailsPanelText_
            || IsQuickAccessShortcutEdit(focus);
        if (focus && IsTextInputControlWindow(focus) && !knownTextInput)
        {
            return false;
        }

        if (commandBarKeyboardActive_ && wParam == VK_TAB)
        {
            DeactivateCommandBarKeyboardMode(false);
        }

        if (wParam == VK_F6)
        {
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0
                || (GetKeyState(VK_MENU) & 0x8000) != 0)
            {
                return false;
            }
            if ((lParam & (1LL << 30)) != 0)
            {
                return true;
            }
            if (commandBarKeyboardActive_)
            {
                DeactivateCommandBarKeyboardMode(false);
            }
            return CycleKeyboardPaneFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0);
        }

        const KeyboardFocusTarget currentTarget = CurrentKeyboardFocusTarget();
        if (wParam == VK_TAB)
        {
            const std::vector<KeyboardFocusTarget> sequence = BuildKeyboardFocusSequence();
            if (sequence.empty())
            {
                return false;
            }

            const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const auto current = std::find(sequence.begin(), sequence.end(), currentTarget);
            const int currentIndex = current == sequence.end()
                ? (reverse ? 0 : static_cast<int>(sequence.size()) - 1)
                : static_cast<int>(std::distance(sequence.begin(), current));
            const int direction = reverse ? -1 : 1;
            const int nextIndex = (currentIndex + direction + static_cast<int>(sequence.size()))
                % static_cast<int>(sequence.size());
            return FocusKeyboardTarget(sequence[static_cast<std::size_t>(nextIndex)]);
        }

        if (wParam == VK_RETURN || wParam == VK_SPACE)
        {
            switch (currentTarget.kind)
            {
            case KeyboardFocusTargetKind::ToolbarItem:
            case KeyboardFocusTargetKind::DetailsTab:
            case KeyboardFocusTargetKind::DetailsCloseButton:
            case KeyboardFocusTargetKind::QuickAccessSortButton:
            case KeyboardFocusTargetKind::QuickAccessRow:
            case KeyboardFocusTargetKind::QuickAccessButton:
                if (wParam == VK_SPACE && message != WM_KEYDOWN)
                {
                    return false;
                }
                ActivateKeyboardFocusTarget(currentTarget);
                return true;
            default:
                break;
            }
        }

        (void)lParam;
        return false;
    }

    bool MainWindow::CycleKeyboardPaneFocus(bool reverse)
    {
        std::vector<KeyboardFocusTarget> paneTargets;
        const auto addAvailable = [this, &paneTargets](KeyboardFocusTarget target)
        {
            if (IsKeyboardFocusTargetAvailable(target))
            {
                paneTargets.push_back(target);
            }
        };

        addAvailable({KeyboardFocusTargetKind::FolderTree});
        addAvailable({KeyboardFocusTargetKind::BrowserPane});
        if (detailsStripVisible_)
        {
            addAvailable({KeyboardFocusTargetKind::DetailsTab, static_cast<int>(activeRightPaneTab_)});
        }
        if (paneTargets.empty())
        {
            return false;
        }

        const KeyboardFocusTarget currentTarget = CurrentKeyboardFocusTarget();
        int currentIndex = -1;
        for (int index = 0; index < static_cast<int>(paneTargets.size()); ++index)
        {
            if (paneTargets[static_cast<std::size_t>(index)] == currentTarget)
            {
                currentIndex = index;
                break;
            }
        }

        const bool focusInRightPane = currentTarget.kind == KeyboardFocusTargetKind::DetailsTab
            || currentTarget.kind == KeyboardFocusTargetKind::DetailsCloseButton
            || currentTarget.kind == KeyboardFocusTargetKind::DetailsText
            || currentTarget.kind == KeyboardFocusTargetKind::QuickAccessSortButton
            || currentTarget.kind == KeyboardFocusTargetKind::QuickAccessShortcutEdit
            || currentTarget.kind == KeyboardFocusTargetKind::QuickAccessRow
            || currentTarget.kind == KeyboardFocusTargetKind::QuickAccessButton
            || currentTarget.kind == KeyboardFocusTargetKind::QuickAccessScrollBar;
        if (currentIndex < 0 && focusInRightPane && detailsStripVisible_)
        {
            const KeyboardFocusTarget rightPaneTarget{
                KeyboardFocusTargetKind::DetailsTab,
                static_cast<int>(activeRightPaneTab_)};
            const auto rightPane = std::find(paneTargets.begin(), paneTargets.end(), rightPaneTarget);
            if (rightPane != paneTargets.end())
            {
                currentIndex = static_cast<int>(std::distance(paneTargets.begin(), rightPane));
            }
        }

        const int direction = reverse ? -1 : 1;
        const int targetIndex = currentIndex < 0
            ? (reverse ? static_cast<int>(paneTargets.size()) - 1 : 0)
            : (currentIndex + direction + static_cast<int>(paneTargets.size()))
                % static_cast<int>(paneTargets.size());
        return FocusKeyboardTarget(paneTargets[static_cast<std::size_t>(targetIndex)]);
    }

    void MainWindow::HandleCommandBarMenuTrackingTimer()
    {
        if (commandBarMenuTrackingIndex_ < 0
            || commandBarMenuTrackingIndex_ >= static_cast<int>(commandBarMenuButtons_.size()))
        {
            return;
        }

        POINT screenPoint{};
        if (!GetCursorPos(&screenPoint) || !ScreenToClient(hwnd_, &screenPoint))
        {
            return;
        }

        const int menuIndex = CommandBarMenuHitTest(screenPoint.x, screenPoint.y);
        if (menuIndex < 0 || menuIndex == commandBarMenuTrackingIndex_)
        {
            return;
        }

        commandBarMenuNavigationIndex_ = menuIndex;
        commandBarHotIndex_ = menuIndex;
        InvalidateToolbarStrip();
        if (!EndMenu())
        {
            commandBarMenuNavigationIndex_ = -1;
        }
    }

    void MainWindow::OpenCommandBarMenu(int index)
    {
        if (index < 0 || index >= static_cast<int>(commandBarMenuButtons_.size()))
        {
            return;
        }

        const bool keyboardOpen = commandBarKeyboardActive_;
        int menuIndex = index;
        for (;;)
        {
            const CommandBarMenuButton& button = commandBarMenuButtons_[static_cast<std::size_t>(menuIndex)];
            if (!button.menu || IsRectEmpty(&button.rect))
            {
                return;
            }

            if (keyboardOpen)
            {
                commandBarHotIndex_ = menuIndex;
                InvalidateToolbarStrip();
                if (GetFocus() != hwnd_)
                {
                    SetFocus(hwnd_);
                }
            }

            RECT screenRect = button.rect;
            MapWindowPoints(hwnd_, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&screenRect), 2);
            commandBarMenuNavigationIndex_ = -1;
            commandBarMenuTrackingIndex_ = menuIndex;
            SetTimer(hwnd_, kCommandBarMenuTrackingTimerId, kCommandBarMenuTrackingIntervalMs, nullptr);
            SetForegroundWindow(hwnd_);

            HHOOK menuFilterHook = nullptr;
            if (keyboardOpen)
            {
                g_commandBarMenuFilterWindow = this;
                menuFilterHook = SetWindowsHookExW(WH_MSGFILTER,
                                                   &MainWindow::CommandBarMenuFilterProc,
                                                   nullptr,
                                                   GetCurrentThreadId());
            }

            const UINT commandId = TrackPopupMenuEx(button.menu,
                                                    TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                                                    screenRect.left,
                                                    screenRect.bottom,
                                                    hwnd_,
                                                    nullptr);

            KillTimer(hwnd_, kCommandBarMenuTrackingTimerId);
            commandBarMenuTrackingIndex_ = -1;
            if (menuFilterHook)
            {
                UnhookWindowsHookEx(menuFilterHook);
            }
            g_commandBarMenuFilterWindow = nullptr;
            PostMessageW(hwnd_, WM_NULL, 0, 0);

            if (commandBarMenuNavigationIndex_ >= 0)
            {
                menuIndex = commandBarMenuNavigationIndex_;
                continue;
            }

            if (commandId != 0)
            {
                if (keyboardOpen)
                {
                    DeactivateCommandBarKeyboardMode(true);
                }
                HandleCommand(commandId);
            }
            else if (keyboardOpen && hwnd_ && GetFocus() != hwnd_)
            {
                SetFocus(hwnd_);
            }
            return;
        }
    }

    LRESULT CALLBACK MainWindow::CommandBarMenuFilterProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == MSGF_MENU && g_commandBarMenuFilterWindow)
        {
            const auto* message = reinterpret_cast<const MSG*>(lParam);
            if (message
                && (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
                && (message->wParam == VK_LEFT || message->wParam == VK_RIGHT))
            {
                const int menuCount = static_cast<int>(g_commandBarMenuFilterWindow->commandBarMenuButtons_.size());
                const int currentIndex = std::clamp(g_commandBarMenuFilterWindow->commandBarHotIndex_, 0, menuCount - 1);
                g_commandBarMenuFilterWindow->commandBarMenuNavigationIndex_ = message->wParam == VK_LEFT
                    ? (currentIndex + menuCount - 1) % menuCount
                    : (currentIndex + 1) % menuCount;
                if (EndMenu())
                {
                    return 1;
                }
                g_commandBarMenuFilterWindow->commandBarMenuNavigationIndex_ = -1;
            }
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}
