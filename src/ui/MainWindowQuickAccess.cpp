#include "ui/MainWindowInternal.h"

namespace
{
    constexpr wchar_t kQuickSendStateMutexName[] = L"Local\\TheTheosopher.HyperBrowse.QuickSendState";
    constexpr DWORD kQuickSendStateLockTimeoutMs = 250;
    constexpr int kQuickAccessPanelScrollBarGap = 6;
    constexpr UINT kQuickSendPopupCommandBase = 5300;
    constexpr UINT kQuickSendPopupBrowseCommand = kQuickSendPopupCommandBase;
    constexpr UINT kQuickSendPopupDestinationBase = kQuickSendPopupCommandBase + 1;

    std::wstring FormatQuickAccessImageCount(std::uintmax_t imageCount)
    {
        return std::to_wstring(imageCount) + (imageCount == 1 ? L" image" : L" images");
    }

    bool TryCountSupportedImagesInFolder(std::wstring_view folderPath, std::uintmax_t* imageCount)
    {
        if (!imageCount)
        {
            return false;
        }

        *imageCount = 0;

        std::error_code error;
        const fs::path directoryPath(folderPath);
        if (!fs::is_directory(directoryPath, error) || error)
        {
            return false;
        }

        for (fs::directory_iterator iterator(directoryPath, fs::directory_options::skip_permission_denied, error), end;
             iterator != end;
             iterator.increment(error))
        {
            if (error)
            {
                return false;
            }

            std::error_code statusError;
            if (!iterator->is_regular_file(statusError) || statusError)
            {
                continue;
            }

            if (hyperbrowse::browser::IsSupportedImageExtension(iterator->path().extension().wstring()))
            {
                ++(*imageCount);
            }
        }

        return true;
    }

    std::wstring BuildQuickAccessDestinationMetadata(std::wstring_view folderPath, bool currentFolder)
    {
        std::wstring metadata;

        std::uintmax_t imageCount = 0;
        if (TryCountSupportedImagesInFolder(folderPath, &imageCount))
        {
            metadata.append(FormatQuickAccessImageCount(imageCount));
        }
        else
        {
            metadata.append(L"folder unavailable");
        }

        if (currentFolder)
        {
            metadata.append(L" | Current folder");
        }
        return metadata;
    }

    std::wstring EscapeMenuMnemonicText(std::wstring_view text)
    {
        std::wstring escaped;
        escaped.reserve(text.size());
        for (const wchar_t character : text)
        {
            if (character == L'&')
            {
                escaped.push_back(L'&');
            }
            escaped.push_back(character);
        }
        return escaped;
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::RecordRecentFolder(std::wstring folderPath)
    {
        // Surface the folder in the taskbar jump list's "Recent" category.
        SHAddToRecentDocs(SHARD_PATHW, folderPath.c_str());

        if (QuickAccessPathList::Insert(&recentFolders_, std::move(folderPath), kQuickAccessFolderLimit, true) && menu_)
        {
            UpdateMenuState();
        }
    }

    bool MainWindow::NavigateBackToLastOpenedFolder()
    {
        if (!browserModel_ || !folderLoadCoordinator_)
        {
            return false;
        }

        return folderLoadCoordinator_->NavigateBack(
            browserModel_->FolderPath(),
            recursiveBrowsingEnabled_,
            showSubfoldersInBrowser_);
    }

    bool MainWindow::NavigateForwardToLastOpenedFolder()
    {
        if (!browserModel_ || !folderLoadCoordinator_)
        {
            return false;
        }

        return folderLoadCoordinator_->NavigateForward(
            browserModel_->FolderPath(),
            recursiveBrowsingEnabled_,
            showSubfoldersInBrowser_);
    }

    bool MainWindow::CanNavigateToParentFolder() const
    {
        if (!browserModel_ || browserModel_->FolderPath().empty() || !folderLoadCoordinator_
            || folderLoadCoordinator_->IsEnumerationActive()
            || folderLoadCoordinator_->HasPendingNavigation())
        {
            return false;
        }

        const fs::path currentFolder(browserModel_->FolderPath());
        const fs::path parentFolder = currentFolder.parent_path();
        return !parentFolder.empty()
            && !FolderPathsEqual(parentFolder.wstring(), currentFolder.wstring());
    }

    bool MainWindow::NavigateToParentFolder()
    {
        if (!CanNavigateToParentFolder())
        {
            return false;
        }

        const fs::path parentFolder = fs::path(browserModel_->FolderPath()).parent_path();
        LoadFolderAsync(NormalizeFolderPath(parentFolder.wstring()));
        return true;
    }

    void MainWindow::RecordRecentDestination(std::wstring folderPath)
    {
        if (QuickAccessPathList::Insert(&recentDestinationFolders_, std::move(folderPath), kQuickAccessFolderLimit, true) && menu_)
        {
            UpdateMenuState();
            if (hwnd_ && detailsStripVisible_)
            {
                LayoutChildren();
            }
        }
    }

    void MainWindow::SyncQuickSendModel()
    {
        quickSendModel_.SetShortcutAssignmentOrder(quickSendShortcutOrder_);
        quickSendModel_.SetFavoriteDestinations(favoriteDestinationFolders_);
    }

    void MainWindow::MutateQuickSendState(const std::function<void()>& mutation)
    {
        const hyperbrowse::util::NamedMutexLock lock(kQuickSendStateMutexName, kQuickSendStateLockTimeoutMs);
        if (lock.Held())
        {
            LoadQuickSendStateFromRegistry();
        }
        else if (lock.Result() == hyperbrowse::util::NamedMutexLockResult::TimedOut)
        {
            hyperbrowse::util::IncrementCounter(L"quick_send.state_lock.timeout");
        }

        mutation();
        SaveQuickSendStateToRegistry();
    }

    void MainWindow::LoadQuickSendStateFromRegistry()
    {
        HKEY key{};
        if (hyperbrowse::util::OpenSettingsRegistryKey(KEY_READ, &key) != ERROR_SUCCESS)
        {
            return;
        }

        const QuickSendPersistedState persistedState = QuickSendPersistence::Load(
            [&](std::wstring_view valueName, std::wstring* value)
            {
                const std::wstring registryValueName(valueName);
                return TryReadStringValue(key, registryValueName.c_str(), value);
            },
            kFavoriteDestinationLimit);
        favoriteDestinationFolders_ = persistedState.favoriteDestinationFolders;
        lastQuickSendDestination_ = persistedState.lastQuickSendDestination;
        quickSendShortcutOrder_ = persistedState.shortcutAssignmentOrder;
        SyncQuickSendModel();
        quickSendModel_.SetShortcutAssignments(persistedState.shortcutAssignments);
        SortFavoriteDestinationsByShortcutInMemory();
        RegCloseKey(key);
    }

    void MainWindow::SaveQuickSendStateToRegistry() const
    {
        HKEY key{};
        if (hyperbrowse::util::CreateSettingsRegistryKey(KEY_WRITE, &key) != ERROR_SUCCESS)
        {
            return;
        }

        QuickSendPersistedState state;
        state.favoriteDestinationFolders = favoriteDestinationFolders_;
        state.lastQuickSendDestination = lastQuickSendDestination_;
        state.shortcutAssignments = quickSendModel_.ShortcutAssignmentsByKey();
        state.shortcutAssignmentOrder = quickSendShortcutOrder_;
        QuickSendPersistence::Save(
            state,
            [&](std::wstring_view valueName, std::wstring_view value)
            {
                const std::wstring registryValueName(valueName);
                WriteStringValue(key, registryValueName.c_str(), value);
            },
            [&](std::wstring_view valueName)
            {
                const std::wstring registryValueName(valueName);
                RegDeleteValueW(key, registryValueName.c_str());
            });
        RegCloseKey(key);
    }

    void MainWindow::SortFavoriteDestinationsByShortcutInMemory()
    {
        quickSendModel_.SortFavoriteDestinationsByShortcut();
        favoriteDestinationFolders_ = quickSendModel_.FavoriteDestinations();
    }

    void MainWindow::SortFavoriteDestinationsByShortcut()
    {
        MutateQuickSendState([this]
        {
            SortFavoriteDestinationsByShortcutInMemory();
        });
    }

    void MainWindow::RefreshQuickAccessMenus()
    {
        if (!fileMenu_ || !openRecentFolderMenu_ || !copySelectionToMenu_ || !moveSelectionToMenu_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool allowMutatingFileCommands = browserPaneController_ && browserPaneController_->SelectedCount() > 0 && !fileOperationActive_;
        const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
        quickAccessMenuBuilder_.Refresh(
            fileMenu_,
            openRecentFolderMenu_,
            copySelectionToMenu_,
            moveSelectionToMenu_,
            hasFolder,
            hasFolder && IsFavoriteDestination(browserModel_->FolderPath()),
            allowMutatingFileCommands,
            recentFolders_,
            favoriteDestinationFolders_,
            recentDestinationPaths);
        RefreshSavedSearchMenu();
        RefreshPersistentMenuOwnerDraw();
    }

    void MainWindow::RefreshSavedSearchMenu()
    {
        if (!savedSearchMenu_) return;
        while (GetMenuItemCount(savedSearchMenu_) > 0)
            DeleteMenu(savedSearchMenu_, 0, MF_BYPOSITION);
        const bool available = savedSearchController_ && savedSearchController_->Ready()
            && !savedSearchController_->Busy();
        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        if (savedSearchController_ && !savedSearchController_->Searches().empty())
        {
            const auto& searches = savedSearchController_->Searches();
            for (std::size_t index = 0; index < searches.size(); ++index)
            {
                std::wstring label;
                for (wchar_t character : searches[index].name)
                {
                    if (character == L'&') label.push_back(L'&');
                    label.push_back(character);
                }
                const bool checked = savedSearchController_->Active()
                    && savedSearchController_->Active()->name == searches[index].name;
                AppendMenuW(savedSearchMenu_, MF_STRING | (available && hasFolder ? MF_ENABLED : MF_GRAYED)
                    | (checked ? MF_CHECKED : MF_UNCHECKED),
                    ID_FILE_OPEN_SAVED_SEARCH_BASE + static_cast<UINT>(index), label.c_str());
            }
        }
        else
        {
            const wchar_t* label = !savedSearchController_ || savedSearchController_->Busy()
                ? L"Loading saved searches..."
                : (savedSearchController_->Ready() ? L"No saved searches" : L"Saved searches unavailable");
            AppendMenuW(savedSearchMenu_, MF_STRING | MF_GRAYED, 0, label);
        }
        AppendMenuW(savedSearchMenu_, MF_SEPARATOR, 0, nullptr);
        const UINT activeFlags = MF_STRING | (available && savedSearchController_->Active() ? MF_ENABLED : MF_GRAYED);
        AppendMenuW(savedSearchMenu_, activeFlags, ID_FILE_UPDATE_SAVED_SEARCH, L"&Update Saved Search...");
        AppendMenuW(savedSearchMenu_, activeFlags, ID_FILE_RENAME_SAVED_SEARCH, L"&Rename Saved Search...");
        AppendMenuW(savedSearchMenu_, activeFlags, ID_FILE_DELETE_SAVED_SEARCH, L"&Delete Saved Search...");
        AppendMenuW(savedSearchMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(savedSearchMenu_, MF_STRING | (savedSearchController_ && !savedSearchController_->Busy()
                    ? MF_ENABLED : MF_GRAYED), ID_FILE_RELOAD_SAVED_SEARCHES, L"Reload Saved Searches");
        const bool canSave = available && browserPaneController_ && browserPaneController_->HasActiveFilter();
        EnableMenuItem(fileMenu_, ID_FILE_SAVE_CURRENT_FILTER, MF_BYCOMMAND | (canSave ? MF_ENABLED : MF_GRAYED));
    }

    void MainWindow::ApplySavedSearchFilter(std::wstring_view expression)
    {
        if (!browserPaneController_ || !filterEdit_) return;
        applyingSavedSearchFilter_ = true;
        SetWindowTextW(filterEdit_, std::wstring(expression).c_str());
        applyingSavedSearchFilter_ = false;
        browserPaneController_->SetFilterQuery(std::wstring(expression));
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, filterEdit_, OBJID_CLIENT, CHILDID_SELF);
        UpdateMenuState();
    }

    void MainWindow::ConsumeSavedSearchResult()
    {
        if (!savedSearchController_ || !savedSearchController_->ConsumeResult()) return;
        if (savedSearchController_->Active() && browserPaneController_
            && browserPaneController_->GetFilterQuery() != savedSearchController_->Active()->expression)
            ApplySavedSearchFilter(savedSearchController_->Active()->expression);
        UpdateStatusText();
    }

    void MainWindow::RefreshPersistentMenuOwnerDraw()
    {
        if (!menu_)
        {
            return;
        }

        menuPainter_.RefreshMenuMeasurements(menu_);
        std::vector<std::unique_ptr<MenuDrawItemData>> refreshedItems;
        PrepareMenuForOwnerDraw(menu_, refreshedItems, false);
        menuDrawItems_ = std::move(refreshedItems);
        if (hwnd_)
        {
            DrawMenuBar(hwnd_);
        }
    }

    void MainWindow::PrepareMenuForOwnerDraw(HMENU menu,
                                             std::vector<std::unique_ptr<MenuDrawItemData>>& storage,
                                             bool ownerDrawCurrentLevel) const
    {
        menuPainter_.PrepareMenuForOwnerDraw(menu, storage, ownerDrawCurrentLevel);
    }

    void MainWindow::RebuildQuickAccessDestinationRows(int innerLeft, int innerRight, int top)
    {
        quickAccessDestinationRows_.clear();
        quickAccessDestinationPanelRect_ = RECT{};
        quickAccessDestinationViewportRect_ = RECT{};
        quickAccessSortButtonRect_ = RECT{};
        quickAccessSortButtonHot_ = false;
        quickAccessSortButtonPressed_ = false;
        quickAccessHotRowIndex_ = -1;
        quickAccessHotButtonIndex_ = -1;
        quickAccessPressedRowIndex_ = -1;
        quickAccessPressedButtonIndex_ = -1;

        if (!detailsStripVisible_ || innerRight <= innerLeft)
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
            return;
        }

        std::vector<QuickAccessLayout::Destination> destinations =
            QuickAccessDestinationBuilder::Build(
                quickSendModel_.FavoriteDestinations(),
                [this](std::wstring_view favoritePath)
                {
                    return BuildQuickAccessDestinationMetadata(
                        favoritePath,
                        IsQuickAccessDestinationCurrentFolder(favoritePath));
                },
                [this](std::wstring_view favoritePath)
                {
                    return quickSendModel_.ShortcutForDestination(favoritePath);
                });

        if (destinations.empty())
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
            return;
        }

        const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(
            detailsPanelSummaryFont_,
            detailsPanelBodyFont_,
            MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u));
        const int panelBottom = detailsPanelContentRect_.bottom;
        const int viewportTop = top + metrics.headerHeight;
        if (panelBottom <= viewportTop)
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
            return;
        }

        const int totalRowsHeight = static_cast<int>(destinations.size()) * metrics.rowHeight
            + static_cast<int>((destinations.size() - 1) * metrics.rowGap);
        const int viewportHeight = panelBottom - viewportTop;
        const int maximumScrollOffset = std::max(0, totalRowsHeight - viewportHeight);
        int contentRight = innerRight;
        if (maximumScrollOffset > 0 && quickAccessScrollBar_)
        {
            const int scrollBarWidth = std::max(1, GetSystemMetrics(SM_CXVSCROLL));
            const int scrollBarLeft = innerRight - scrollBarWidth;
            contentRight = scrollBarLeft - kQuickAccessPanelScrollBarGap;
            if (contentRight > innerLeft)
            {
                SCROLLINFO scrollInfo{};
                scrollInfo.cbSize = sizeof(scrollInfo);
                scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                scrollInfo.nMin = 0;
                scrollInfo.nMax = std::max(0, totalRowsHeight - 1);
                scrollInfo.nPage = static_cast<UINT>(viewportHeight);
                scrollInfo.nPos = quickAccessScrollOffset_;
                SetScrollInfo(quickAccessScrollBar_, SB_CTL, &scrollInfo, TRUE);
                MoveWindow(quickAccessScrollBar_,
                           scrollBarLeft,
                           viewportTop,
                           scrollBarWidth,
                           viewportHeight,
                           TRUE);
                ShowWindow(quickAccessScrollBar_, SW_SHOW);
            }
            else
            {
                contentRight = innerRight;
            }
        }
        else if (quickAccessScrollBar_)
        {
            ShowWindow(quickAccessScrollBar_, SW_HIDE);
        }

        quickAccessDestinationPanelRect_ = RECT{innerLeft, top, innerRight, panelBottom};
        quickAccessDestinationViewportRect_ = RECT{innerLeft, viewportTop, contentRight, panelBottom};
        quickAccessScrollOffset_ = std::clamp(quickAccessScrollOffset_, 0, maximumScrollOffset);

        const HFONT headerFont = detailsPanelSummaryFont_
            ? detailsPanelSummaryFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        QuickAccessLayout::Input layoutInput;
        layoutInput.innerLeft = innerLeft;
        layoutInput.innerRight = innerRight;
        layoutInput.top = top;
        layoutInput.viewportTop = viewportTop;
        layoutInput.panelBottom = panelBottom;
        layoutInput.contentRight = contentRight;
        layoutInput.scrollOffset = quickAccessScrollOffset_;
        layoutInput.sortLabelWidth = MeasureTextWidth(headerFont, L"Quick Actions");
        layoutInput.sortButtonGap = MakeMenuMetrics(
            appTextSize_,
            hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u).ScaleDip(kQuickAccessPanelSortButtonGap);
        layoutInput.sortButtonSize = metrics.headerHeight;
        layoutInput.metrics = metrics;
        layoutInput.destinations = std::move(destinations);

        QuickAccessLayout::Result layout = QuickAccessLayout::Build(layoutInput);
        quickAccessDestinationPanelRect_ = layout.panelRect;
        quickAccessDestinationViewportRect_ = layout.viewportRect;
        quickAccessSortButtonRect_ = layout.sortButtonRect;
        quickAccessDestinationRows_ = std::move(layout.rows);
        UpdateQuickAccessSortTooltip();

        UpdateQuickAccessShortcutEditControls();
    }

    void MainWindow::UpdateQuickAccessSortTooltip()
    {
        if (!tooltipControl_ || !hwnd_)
        {
            return;
        }

        TTTOOLINFOW toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.uFlags = TTF_SUBCLASS;
        toolInfo.hwnd = hwnd_;
        toolInfo.uId = kQuickAccessSortTooltipId;
        toolInfo.rect = quickAccessSortButtonRect_;
        toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
        if (!quickAccessSortTooltipAdded_)
        {
            quickAccessSortTooltipAdded_ = SendMessageW(tooltipControl_,
                                                        TTM_ADDTOOLW,
                                                        0,
                                                        reinterpret_cast<LPARAM>(&toolInfo)) != FALSE;
        }
        else
        {
            SendMessageW(tooltipControl_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        }
    }

    void MainWindow::UpdateQuickAccessShortcutEditControls()
    {
        if (!hwnd_)
        {
            return;
        }

        const HFONT font = detailsPanelBodyFont_
            ? detailsPanelBodyFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        while (quickAccessShortcutEdits_.size() < quickAccessDestinationRows_.size())
        {
            const UINT controlId = kQuickAccessShortcutEditBaseId
                + static_cast<UINT>(quickAccessShortcutEdits_.size());
            HWND edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_TABSTOP | ES_CENTER,
                0,
                0,
                kQuickAccessPanelShortcutWidth,
                kTextInputButtonHeight,
                hwnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                instance_,
                nullptr);
            if (!edit)
            {
                break;
            }

            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(edit, EM_LIMITTEXT, 1, 0);
            SetWindowSubclass(edit,
                              &MainWindow::QuickAccessShortcutEditSubclassProc,
                              1,
                              reinterpret_cast<DWORD_PTR>(this));
            quickAccessShortcutEdits_.push_back(edit);

            if (tooltipControl_)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = reinterpret_cast<UINT_PTR>(edit);
                toolInfo.lpszText = const_cast<LPWSTR>(L"Quick Actions hotkey: enter one supported printable keyboard character. Function, control, and navigation keys are not supported.");
                SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }
        }

        updatingQuickAccessShortcutEdits_ = true;
        for (std::size_t index = 0; index < quickAccessShortcutEdits_.size(); ++index)
        {
            HWND edit = quickAccessShortcutEdits_[index];
            if (!edit)
            {
                continue;
            }

            if (index >= quickAccessDestinationRows_.size())
            {
                ShowWindow(edit, SW_HIDE);
                continue;
            }

            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[index];
            RECT visibleIntersection{};
            const bool visible = !IsRectEmpty(&row.shortcutRect)
                && IntersectRect(&visibleIntersection,
                                 &row.shortcutRect,
                                 &quickAccessDestinationViewportRect_) != FALSE
                && EqualRect(&visibleIntersection, &row.shortcutRect) != FALSE;
            if (!visible)
            {
                ShowWindow(edit, SW_HIDE);
                continue;
            }

            MoveWindow(edit,
                       row.shortcutRect.left,
                       row.shortcutRect.top,
                       row.shortcutRect.right - row.shortcutRect.left,
                       row.shortcutRect.bottom - row.shortcutRect.top,
                       TRUE);
            std::wstring shortcutText;
            const wchar_t shortcutCharacter = QuickSendModel::ShortcutCharacter(row.assignedShortcut);
            if (shortcutCharacter != L'\0')
            {
                shortcutText.push_back(shortcutCharacter);
            }
            SetWindowTextW(edit, shortcutText.c_str());
            ShowWindow(edit, SW_SHOW);
        }
        updatingQuickAccessShortcutEdits_ = false;
    }

    void MainWindow::HideQuickAccessShortcutEditControls()
    {
        for (HWND edit : quickAccessShortcutEdits_)
        {
            if (edit)
            {
                ShowWindow(edit, SW_HIDE);
            }
        }
    }

    bool MainWindow::IsQuickAccessShortcutEdit(HWND control) const
    {
        return control
            && std::find(quickAccessShortcutEdits_.begin(), quickAccessShortcutEdits_.end(), control)
                != quickAccessShortcutEdits_.end();
    }

    bool MainWindow::IsQuickAccessDestinationCurrentFolder(std::wstring_view folderPath) const
    {
        return browserModel_
            && !browserModel_->FolderPath().empty()
            && !folderPath.empty()
            && FolderPathsEqual(browserModel_->FolderPath(), folderPath);
    }

    bool MainWindow::CanNavigateToQuickAccessDestination(std::wstring_view folderPath) const
    {
        return !folderPath.empty()
            && !IsQuickAccessDestinationCurrentFolder(folderPath)
            && IsExistingDirectory(folderPath);
    }

    int MainWindow::HitTestQuickAccessDestinationRow(int x, int y) const
    {
        const POINT point{x, y};
        if (IsRectEmpty(&quickAccessDestinationViewportRect_)
            || PtInRect(&quickAccessDestinationViewportRect_, point) == FALSE)
        {
            return -1;
        }

        for (std::size_t rowIndex = 0; rowIndex < quickAccessDestinationRows_.size(); ++rowIndex)
        {
            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
            if (CanNavigateToQuickAccessDestination(row.destinationPath)
                && PtInRect(&row.rowRect, point) != FALSE)
            {
                return static_cast<int>(rowIndex);
            }
        }

        return -1;
    }

    bool MainWindow::CanUseQuickAccessDestinationActions(std::wstring_view folderPath) const
    {
        return browserPaneController_
            && browserPaneController_->SelectedCount() > 0
            && !fileOperationActive_
            && CanNavigateToQuickAccessDestination(folderPath);
    }

    int MainWindow::HitTestQuickAccessDestinationButton(int x, int y, services::FileOperationType* type) const
    {
        const POINT point{x, y};
        if (IsRectEmpty(&quickAccessDestinationViewportRect_)
            || PtInRect(&quickAccessDestinationViewportRect_, point) == FALSE)
        {
            return -1;
        }

        for (std::size_t rowIndex = 0; rowIndex < quickAccessDestinationRows_.size(); ++rowIndex)
        {
            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
            const bool actionsEnabled = CanUseQuickAccessDestinationActions(row.destinationPath);
            if (actionsEnabled && PtInRect(&row.copyRect, point) != FALSE)
            {
                if (type)
                {
                    *type = services::FileOperationType::Copy;
                }
                return static_cast<int>(rowIndex * 3);
            }

            if (actionsEnabled && PtInRect(&row.moveRect, point) != FALSE)
            {
                if (type)
                {
                    *type = services::FileOperationType::Move;
                }
                return static_cast<int>(rowIndex * 3 + 1);
            }

            if (PtInRect(&row.removeRect, point) != FALSE)
            {
                return static_cast<int>(rowIndex * 3 + 2);
            }
        }

        return -1;
    }

    void MainWindow::StartDetailsPanelPerformanceUpdates()
    {
        if (!hwnd_ || !detailsStripVisible_ || activeRightPaneTab_ != RightPaneTab::CacheStats)
        {
            return;
        }

        UpdateDetailsPanelPerformanceStats();
        StartPersistentThumbnailCacheStatistics(false);
        if (detailsPanelPerformanceTimerId_ == 0)
        {
            detailsPanelPerformanceTimerId_ = SetTimer(
                hwnd_,
                kDetailsPanelPerformanceTimerId,
                kDetailsPanelPerformanceIntervalMs,
                nullptr);
        }
    }

    void MainWindow::StopDetailsPanelPerformanceUpdates()
    {
        if (detailsPanelPerformanceTimerId_ != 0)
        {
            KillTimer(hwnd_, kDetailsPanelPerformanceTimerId);
            detailsPanelPerformanceTimerId_ = 0;
        }
    }

    void MainWindow::SelectRightPaneTab(RightPaneTab tab)
    {
        if (activeRightPaneTab_ == tab)
        {
            return;
        }

        activeRightPaneTab_ = tab;
        if (activeRightPaneTab_ == RightPaneTab::CacheStats)
        {
            StartDetailsPanelPerformanceUpdates();
        }
        else
        {
            StopDetailsPanelPerformanceUpdates();
        }
        LayoutChildren();
        if (!IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
    }

    void MainWindow::ToggleDetailsPanelVisibility()
    {
        const bool focusWasInDetails = GetFocus() == hwnd_
            && (keyboardFocusTarget_.kind == KeyboardFocusTargetKind::DetailsTab
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::DetailsCloseButton
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::DetailsText
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessSortButton
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessShortcutEdit
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessRow
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessButton
                || keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessScrollBar);
        detailsStripVisible_ = !detailsStripVisible_;
        if (detailsStripVisible_)
        {
            detailsPanelWidth_ = std::max(detailsPanelWidth_, kDetailsPanelMinWidth);
        }

        if (detailsPanelText_)
        {
            ShowWindow(detailsPanelText_, detailsStripVisible_ && activeRightPaneTab_ == RightPaneTab::FileDetails ? SW_SHOW : SW_HIDE);
        }

        detailsPanelCloseButtonHot_ = false;
        detailsPanelCloseButtonPressed_ = false;
        LayoutChildren();
        UpdateDetailsPanel();
        if (detailsStripVisible_ && activeRightPaneTab_ == RightPaneTab::CacheStats)
        {
            StartDetailsPanelPerformanceUpdates();
        }
        else
        {
            StopDetailsPanelPerformanceUpdates();
        }
        UpdateMenuState();
        if (!detailsStripVisible_ && focusWasInDetails)
        {
            FocusKeyboardTarget({KeyboardFocusTargetKind::BrowserPane});
        }
    }

    int MainWindow::HitTestDetailsPanelTab(int x, int y) const
    {
        return RightPaneHitTester::Tab(detailsStripVisible_, detailsPanelTabStripRect_, detailsPanelTabRects_, x, y);
    }

    int MainWindow::HitTestDetailsPanelCloseButton(int x, int y) const
    {
        return RightPaneHitTester::CloseButton(detailsStripVisible_, detailsPanelCloseButtonRect_, x, y);
    }

    int MainWindow::HitTestQuickAccessSortButton(int x, int y) const
    {
        return RightPaneHitTester::SortButton(quickAccessSortButtonRect_, x, y);
    }

    bool MainWindow::ChooseQuickSendDestination(services::FileOperationType operationType,
                                                 POINT popupPoint,
                                                 HWND dialogOwner,
                                                 std::wstring* destinationFolder)
    {
        if (!destinationFolder || !hwnd_)
        {
            return false;
        }

        const HWND browseOwner = dialogOwner && IsWindow(dialogOwner) ? dialogOwner : hwnd_;
        const std::vector<std::wstring> destinations = quickSendModel_.FavoriteDestinations();
        HMENU popupMenu = CreatePopupMenu();
        if (!popupMenu)
        {
            return false;
        }

        quickSendPopupInitialDownCount_ = destinations.empty() ? 0 : 1;
        if (!lastQuickSendDestination_.empty())
        {
            for (std::size_t index = 0; index < destinations.size(); ++index)
            {
                if (FolderPathsEqual(destinations[index], lastQuickSendDestination_)
                    && IsExistingDirectory(destinations[index]))
                {
                    quickSendPopupInitialDownCount_ = index + 2;
                    break;
                }
            }
        }

        AppendMenuW(popupMenu,
                    MF_STRING,
                    kQuickSendPopupBrowseCommand,
                    operationType == services::FileOperationType::Move
                        ? L"Move to..."
                        : L"Copy to...");
        AppendMenuW(popupMenu, MF_SEPARATOR, 0, nullptr);

        for (std::size_t index = 0; index < destinations.size(); ++index)
        {
            std::wstring label;
            if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(destinations[index]))
            {
                label.push_back(L'&');
                label.push_back(QuickSendModel::ShortcutCharacter(*assignedShortcut));
                label.append(L"  ");
            }
            label.append(EscapeMenuMnemonicText(QuickAccessMenuBuilder::FormatFolderShortcutMenuLabel(destinations[index])));

            const UINT flags = IsExistingDirectory(destinations[index]) ? MF_ENABLED : MF_GRAYED;
            AppendMenuW(popupMenu,
                        MF_STRING | flags,
                        kQuickSendPopupDestinationBase + static_cast<UINT>(index),
                        label.c_str());
        }

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareMenuForOwnerDraw(popupMenu, menuDrawItems, true);
        for (std::size_t index = 0; index < destinations.size(); ++index)
        {
            const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(destinations[index]);
            if (!assignedShortcut)
            {
                continue;
            }

            MENUITEMINFOW itemInfo{};
            itemInfo.cbSize = sizeof(itemInfo);
            itemInfo.fMask = MIIM_DATA;
            if (!GetMenuItemInfoW(popupMenu, static_cast<UINT>(index + 2), TRUE, &itemInfo))
            {
                continue;
            }

            auto* drawData = reinterpret_cast<MenuDrawItemData*>(itemInfo.dwItemData);
            if (drawData)
            {
                drawData->mnemonic = QuickSendModel::ShortcutCharacter(*assignedShortcut);
                drawData->mnemonicDisplayIndex = 0;
            }
        }
        if (menuBackgroundBrush_)
        {
            MENUINFO menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIM_BACKGROUND;
            menuInfo.hbrBack = menuBackgroundBrush_;
            SetMenuInfo(popupMenu, &menuInfo);
        }

        quickSendPopupActive_ = true;
        const int selectedCommand = TrackPopupMenuEx(
            popupMenu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON,
            popupPoint.x,
            popupPoint.y,
            hwnd_,
            nullptr);
        quickSendPopupActive_ = false;
        quickSendPopupInitialDownCount_ = 0;
        DestroyMenu(popupMenu);
        SetForegroundWindow(browseOwner);

        if (selectedCommand == static_cast<int>(kQuickSendPopupBrowseCommand))
        {
            return ChooseFolder(destinationFolder, browseOwner) && !destinationFolder->empty();
        }

        if (selectedCommand < static_cast<int>(kQuickSendPopupDestinationBase)
            || selectedCommand >= static_cast<int>(kQuickSendPopupDestinationBase + destinations.size()))
        {
            return false;
        }

        const std::wstring& selectedDestination = destinations[
            static_cast<std::size_t>(selectedCommand - static_cast<int>(kQuickSendPopupDestinationBase))];
        if (!IsExistingDirectory(selectedDestination))
        {
            return false;
        }

        *destinationFolder = selectedDestination;
        return true;
    }

    void MainWindow::StartQuickSendForSelection(services::FileOperationType type)
    {
        if (fileOperationActive_ || !browserPaneController_ || SelectedFileOperationPathsSnapshot().empty())
        {
            return;
        }

        RECT mainRect{};
        GetClientRect(hwnd_, &mainRect);
        POINT popupPoint{
            (mainRect.left + mainRect.right) / 2,
            (mainRect.top + mainRect.bottom) / 2,
        };
        ClientToScreen(hwnd_, &popupPoint);

        std::wstring destinationFolder;
        if (ChooseQuickSendDestination(type, popupPoint, hwnd_, &destinationFolder))
        {
            StartSelectionFileOperationToDestination(type, std::move(destinationFolder), true);
        }
    }

    LRESULT MainWindow::OnViewerQuickSendRequest(WPARAM wParam, LPARAM lParam)
    {
        viewer::ViewerWindow* viewer = FindViewerByHwnd(reinterpret_cast<HWND>(lParam));
        if (!viewer || fileOperationActive_
            || quickSendPopupActive_)
        {
            return 0;
        }

        activeViewerWindow_ = viewer->Hwnd();

        services::FileOperationType operationType{};
        if (wParam == static_cast<WPARAM>(viewer::QuickSendOperation::Move))
        {
            operationType = services::FileOperationType::Move;
        }
        else if (wParam == static_cast<WPARAM>(viewer::QuickSendOperation::Copy))
        {
            operationType = services::FileOperationType::Copy;
        }
        else
        {
            return 0;
        }

        const std::wstring sourcePath = viewer->CurrentFilePath();
        if (sourcePath.empty())
        {
            return 0;
        }

        RECT viewerRect{};
        GetClientRect(viewer->Hwnd(), &viewerRect);
        POINT popupPoint{
            (viewerRect.left + viewerRect.right) / 2,
            (viewerRect.top + viewerRect.bottom) / 2,
        };
        ClientToScreen(viewer->Hwnd(), &popupPoint);

        std::wstring destinationFolder;
        if (ChooseQuickSendDestination(operationType,
                                       popupPoint,
                                       viewer->Hwnd(),
                                       &destinationFolder))
        {
            StartViewerQuickSendOperation(*viewer, operationType, std::move(destinationFolder));
        }
        return 0;
    }

    bool MainWindow::StartViewerQuickSendOperation(viewer::ViewerWindow& viewer,
                                                    services::FileOperationType type,
                                                    std::wstring destinationFolder)
    {
        if (!viewer.IsOpen() || fileOperationActive_)
        {
            return false;
        }

        const HWND viewerHwnd = viewer.Hwnd();
        const std::wstring sourcePath = NormalizeFolderPath(viewer.CurrentFilePath());
        if (sourcePath.empty())
        {
            return false;
        }

        std::vector<std::wstring> sourcePaths = ExpandRawJpegPairedPaths({sourcePath});
        if (sourcePaths.empty())
        {
            return false;
        }

        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(viewerHwnd,
                        L"The selected destination folder is no longer available.",
                        L"Quick Actions",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        const std::wstring sourceParent = NormalizeFolderPath(fs::path(sourcePath).parent_path().wstring());
        if (FolderPathsEqual(sourceParent, destinationFolder))
        {
            MessageBoxW(viewerHwnd,
                        L"The selected destination is already the current folder.",
                        L"Quick Actions",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        const std::wstring resumeTargetPath = type == services::FileOperationType::Move
            ? viewer.FilingResumeTargetPathForMove()
            : sourcePath;
        FilingResumeFileIdentity resumeFolderIdentity;
        FilingResumeFileIdentity resumeTargetIdentity;
        FilingResumePersistence::TryGetFileIdentity(sourceParent, &resumeFolderIdentity);
        if (!resumeTargetPath.empty())
        {
            FilingResumePersistence::TryGetFileIdentity(resumeTargetPath, &resumeTargetIdentity);
        }

        const services::FileConflictPlan conflictPlan = services::PlanDestinationConflicts(
            sourcePaths,
            destinationFolder,
            services::FileConflictPolicy::OverwriteExisting);
        services::FileConflictPolicy conflictPolicy = services::FileConflictPolicy::PromptShell;
        if (!PromptForFileConflictPolicy(viewerHwnd, type, conflictPlan.conflictCount, &conflictPolicy))
        {
            return false;
        }

        std::vector<std::wstring> targetLeafNames;
        if (conflictPolicy == services::FileConflictPolicy::AutoRenameNumericSuffix)
        {
            targetLeafNames = services::PlanDestinationConflicts(
                sourcePaths,
                destinationFolder,
                conflictPolicy).targetLeafNames;
        }

        PendingViewerQuickSend pending;
        pending.viewerHwnd = viewerHwnd;
        pending.type = type;
        pending.sourcePath = sourcePath;
        pending.sourcePaths = sourcePaths;
        pending.destinationFolder = destinationFolder;
        pending.resumeFolderPath = sourceParent;
        pending.resumeTargetPath = resumeTargetPath;
        pending.resumeFolderIdentity = resumeFolderIdentity;
        pending.resumeTargetIdentity = resumeTargetIdentity;
        if (const std::optional<int> shortcut = quickSendModel_.ShortcutForDestination(destinationFolder))
        {
            pending.destinationShortcut = QuickSendModel::ShortcutCharacter(*shortcut);
        }
        viewerPendingOperations_.SetQuickSend(std::move(pending));

        const std::wstring quickSendDestination = destinationFolder;
        if (!StartFileOperation(type,
                                sourcePaths,
                                destinationFolder,
                                conflictPolicy,
                                std::move(targetLeafNames),
                                viewerHwnd))
        {
            viewerPendingOperations_.ClearQuickSend();
            return false;
        }
        MutateQuickSendState([&]
        {
            lastQuickSendDestination_ = quickSendDestination;
        });

        if (type == services::FileOperationType::Move)
        {
            if (PendingViewerQuickSend* activeQuickSend = viewerPendingOperations_.ActiveQuickSend())
            {
                activeQuickSend->viewerAdvanced = viewer.AdvanceAfterDeleteCurrent();
            }
        }
        return true;
    }

    void MainWindow::RecordFilingResume(const FilingResumeRecord& record)
    {
        auto sameFolder = [&](const FilingResumeRecord& existing)
        {
            return (record.folderIdentity.IsValid()
                    && existing.folderIdentity.IsValid()
                    && record.folderIdentity.volumeSerial == existing.folderIdentity.volumeSerial
                    && record.folderIdentity.fileIndex == existing.folderIdentity.fileIndex)
                || FolderPathsEqual(record.folderPath, existing.folderPath);
        };
        filingResumeState_.records.erase(
            std::remove_if(filingResumeState_.records.begin(), filingResumeState_.records.end(), sameFolder),
            filingResumeState_.records.end());
        filingResumeState_.records.insert(filingResumeState_.records.begin(), record);
        if (filingResumeState_.records.size() > FilingResumePersistence::kMaxRecordCount)
        {
            filingResumeState_.records.resize(FilingResumePersistence::kMaxRecordCount);
        }
        SaveFilingResumeStateToRegistry();
    }

    void MainWindow::ResumeFilingPosition()
    {
        if (!browserModel_ || !browserPaneController_ || browserModel_->FolderPath().empty())
        {
            MessageBoxW(hwnd_,
                        L"Open a folder before resuming a filing position.",
                        L"Resume Filing",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring currentFolder = NormalizeFolderPath(browserModel_->FolderPath());
        FilingResumeFileIdentity currentFolderIdentity;
        FilingResumePersistence::TryGetFileIdentity(currentFolder, &currentFolderIdentity);

        const FilingResumeRecord* matchingRecord = nullptr;
        for (const FilingResumeRecord& record : filingResumeState_.records)
        {
            const bool sameIdentity = currentFolderIdentity.IsValid()
                && record.folderIdentity.IsValid()
                && currentFolderIdentity.volumeSerial == record.folderIdentity.volumeSerial
                && currentFolderIdentity.fileIndex == record.folderIdentity.fileIndex;
            if (sameIdentity || FolderPathsEqual(currentFolder, record.folderPath))
            {
                matchingRecord = &record;
                break;
            }
        }

        if (!matchingRecord)
        {
            MessageBoxW(hwnd_,
                        L"There is no saved filing position for the current folder.",
                        L"Resume Filing",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (matchingRecord->targetPath.empty() && !matchingRecord->targetIdentity.IsValid())
        {
            MessageBoxW(hwnd_,
                        L"The saved filing position has no target image.",
                        L"Resume Filing",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring targetPath;
        for (const browser::BrowserItem& item : browserModel_->Items())
        {
            const bool samePath = !matchingRecord->targetPath.empty()
                && browser::FilePathsEqual(item.filePath, matchingRecord->targetPath);
            FilingResumeFileIdentity itemIdentity;
            const bool sameIdentity = matchingRecord->targetIdentity.IsValid()
                && FilingResumePersistence::TryGetFileIdentity(item.filePath, &itemIdentity)
                && itemIdentity.volumeSerial == matchingRecord->targetIdentity.volumeSerial
                && itemIdentity.fileIndex == matchingRecord->targetIdentity.fileIndex;
            if (samePath || sameIdentity)
            {
                targetPath = item.filePath;
                break;
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_,
                        L"The saved filing image is not available in the current view.",
                        L"Resume Filing",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        browserPaneController_->RestoreSelectionByFilePaths({targetPath}, targetPath);
        browserPaneController_->EnsureFocusedItemVisible();
    }
}
