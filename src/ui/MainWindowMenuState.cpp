#include "ui/MainWindowInternal.h"

namespace hyperbrowse::ui
{

    bool MainWindow::IsPathInCurrentScope(std::wstring_view path) const
    {
        if (!browserModel_ || browserModel_->FolderPath().empty() || path.empty())
        {
            return false;
        }

        if (browserModel_->IsRecursive())
        {
            return browser::PathHasPrefix(path, browserModel_->FolderPath());
        }

        return FolderPathsEqual(fs::path(path).parent_path().wstring(), browserModel_->FolderPath());
    }

    void MainWindow::ApplyFolderWatchChanges(const services::FolderWatchUpdate& update)
    {
        if (!browserModel_ || !browserPaneController_ || !folderWatchChangeCoordinator_)
        {
            return;
        }

        folderWatchChangeCoordinator_->Apply(
            *browserModel_,
            *browserPaneController_,
            update,
            fileOperationActive_,
            recursiveBrowsingEnabled_,
            showSubfoldersInBrowser_);
    }

    void MainWindow::UpdateMenuState()
    {
        if (!menu_)
        {
            return;
        }

        if (menuLoopActive_)
        {
            menuStateRefreshPending_ = true;
            return;
        }

        ConsumeSavedSearchResult();
        RefreshQuickAccessMenus();
        UpdateUndoRedoMenuState();

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSingleSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 1;
        const bool hasBatchRenameSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 1;
        const bool hasCompareSelection = browserPaneController_
            && viewer::IsSupportedCompareTileCount(browserPaneController_->SelectedCount());
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool hasSecondaryMonitor = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        const bool allowMetadataEdit = hasSelection && !fileOperationActive_;
        const int commonSelectionRating = hasSelection ? CommonSelectionRating() : -1;
        const browser::ThumbnailSizePreset thumbnailSizePreset = browserPaneController_
            ? browserPaneController_->GetThumbnailSizePreset()
            : thumbnailSizePreset_;
        const bool thumbnailDetailsVisible = browserPaneController_
            ? browserPaneController_->AreThumbnailDetailsVisible()
            : thumbnailDetailsVisible_;
        const bool compactThumbnailLayout = browserPaneController_
            ? browserPaneController_->IsCompactThumbnailLayoutEnabled()
            : compactThumbnailLayout_;
        const bool folderEnumerationActive = folderLoadCoordinator_
            && folderLoadCoordinator_->IsEnumerationActive();
        const bool historyNavigationSettled = !folderEnumerationActive
            && (!folderLoadCoordinator_ || !folderLoadCoordinator_->HasPendingNavigation());
        const bool canNavigateBack = hasFolder && historyNavigationSettled
            && folderLoadCoordinator_ && folderLoadCoordinator_->CanNavigateBack();
        const bool canNavigateForward = hasFolder && historyNavigationSettled
            && folderLoadCoordinator_ && folderLoadCoordinator_->CanNavigateForward();
        const bool canNavigateParent = hasFolder && CanNavigateToParentFolder();
        const bool thumbnailSteppingEnabled = hasFolder && browserMode_ == BrowserMode::Thumbnails
            && !folderEnumerationActive;

        EnableMenuItem(menu_, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COMPARE_SELECTED, MF_BYCOMMAND | (hasCompareSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_VIEW_ON_SECONDARY_MONITOR,
                   MF_BYCOMMAND | ((hasSelection && hasSecondaryMonitor) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_PATH, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_FILES_TO_CLIPBOARD, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_EDIT_CUT, MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_IMAGE_PIXELS, MF_BYCOMMAND | (hasSingleSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_NEW_FOLDER, MF_BYCOMMAND | (hasFolder && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_PASTE_FILES, MF_BYCOMMAND | (hasFolder && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DUPLICATE_SELECTION, MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_SELECT_ALL, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_RENAME_SELECTED, MF_BYCOMMAND | (hasSingleSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_RENAME_SELECTION, MF_BYCOMMAND | (hasBatchRenameSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_PROPERTIES, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_EDIT_TAGS, MF_BYCOMMAND | (allowMetadataEdit ? MF_ENABLED : MF_GRAYED));
        for (UINT ratingCommandId = ID_FILE_SET_RATING_0; ratingCommandId <= ID_FILE_SET_RATING_5; ++ratingCommandId)
        {
            EnableMenuItem(menu_, ratingCommandId, MF_BYCOMMAND | (allowMetadataEdit ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(menu_,
                          ratingCommandId,
                          MF_BYCOMMAND | (commonSelectionRating >= 0 && ratingCommandId == CommandIdFromRating(commonSelectionRating)
                              ? MF_CHECKED
                              : MF_UNCHECKED));
        }
        EnableMenuItem(menu_, ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_CLEAR_FAVORITE_DESTINATIONS,
                   MF_BYCOMMAND | (!favoriteDestinationFolders_.empty() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu_, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS,
                  MF_BYCOMMAND | (rawJpegPairedOperationsEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu_, ID_FILE_COPY_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_MOVE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION_PERMANENT,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_JPEG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_PNG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_TIFF,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_FOLDER, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SETTINGS, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu_, ID_VIEW_NAVIGATE_BACK_FOLDER, MF_BYCOMMAND | (canNavigateBack ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_NAVIGATE_FORWARD_FOLDER, MF_BYCOMMAND | (canNavigateForward ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_NAVIGATE_PARENT_FOLDER, MF_BYCOMMAND | (canNavigateParent ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_THUMBNAIL_SIZE_INCREASE, MF_BYCOMMAND | (thumbnailSteppingEnabled ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_THUMBNAIL_SIZE_DECREASE, MF_BYCOMMAND | (thumbnailSteppingEnabled ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAILS,
            ID_VIEW_DETAILS,
            browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
            MF_BYCOMMAND);

        CheckMenuItem(
            menu_,
            ID_VIEW_RECURSIVE,
            MF_BYCOMMAND | (recursiveBrowsingEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_SHOW_SUBFOLDERS,
            MF_BYCOMMAND | (showSubfoldersInBrowser_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_THUMBNAIL_LAYOUT_COMPACT,
            MF_BYCOMMAND | (compactThumbnailLayout ? MF_CHECKED : MF_UNCHECKED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAIL_SIZE_96,
            ID_VIEW_THUMBNAIL_SIZE_640,
            CommandIdFromThumbnailSizePreset(thumbnailSizePreset),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_THUMBNAIL_DETAILS,
            MF_BYCOMMAND | (thumbnailDetailsVisible ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_DETAILS_STRIP,
            MF_BYCOMMAND | (detailsStripVisible_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_PERFORMANCE_HUD,
            MF_BYCOMMAND | (performanceHudEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_COLOR_MANAGEMENT,
            MF_BYCOMMAND | (colorManagementEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(
            menu_,
            ID_FILE_BATCH_CONVERT_CANCEL,
            MF_BYCOMMAND | (batchConvertActive_ && !batchConvertCancelling_ ? MF_ENABLED : MF_GRAYED));

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        const bool sortAscending = browserPaneController_
            ? browserPaneController_->IsSortAscending()
            : true;
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_TAGS,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_SORT_DIRECTION,
            MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

        UpdateToolbarItemStates();
    }

    void MainWindow::UpdateToolbarItemStates()
    {
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasCompareSelection = browserPaneController_
            && viewer::IsSupportedCompareTileCount(browserPaneController_->SelectedCount());
        const bool folderEnumerationActive = folderLoadCoordinator_
            && folderLoadCoordinator_->IsEnumerationActive();
        CommandBarController::ToolbarState state;
        state.canNavigateBack = browserModel_
            && folderLoadCoordinator_
            && folderLoadCoordinator_->CanNavigateBack();
        state.canNavigateForward = browserModel_
            && folderLoadCoordinator_
            && folderLoadCoordinator_->CanNavigateForward();
        state.folderEnumerationActive = folderEnumerationActive;
        state.pendingNavigation = folderLoadCoordinator_
            && folderLoadCoordinator_->HasPendingNavigation();
        state.thumbnailsChecked = browserMode_ == BrowserMode::Thumbnails;
        state.detailsChecked = browserMode_ == BrowserMode::Details;
        state.thumbnailSizeEnabled = browserMode_ == BrowserMode::Thumbnails;
        state.compareEnabled = hasCompareSelection;
        state.selectionActionsEnabled = hasSelection && !fileOperationActive_;
        state.saveFilterEnabled = savedSearchController_ && savedSearchController_->Ready()
            && !savedSearchController_->Busy() && browserPaneController_ && browserPaneController_->HasActiveFilter();
        state.clearFilterEnabled = filterEdit_ && GetWindowTextLengthW(filterEdit_) > 0;
        commandBarController_.UpdateItemStates(state);

        InvalidateToolbarStrip();
        if (accessibility_)
        {
            accessibility_->NotifyStateChanged(CHILDID_SELF);
        }
    }

    void MainWindow::InvalidateToolbarStrip()
    {
        if (!hwnd_)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        RECT stripRect{0, 0, client.right, actionStripHeight_};
        InvalidateRect(hwnd_, &stripRect, FALSE);
    }

    void MainWindow::HandleDisplaySurfaceChange()
    {
        if (displaySurfaceRecoveryTimerId_ == 0)
        {
            RecoverDisplaySurfaces(true);
        }

        ScheduleDisplaySurfaceRecoveryRetries();
    }

    void MainWindow::RecoverDisplaySurfaces(bool relayout)
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        if (relayout && !IsIconic(hwnd_))
        {
            LayoutChildren();
        }

        EnsureActiveFolderVisible();

        if (browserPaneController_)
        {
            browserPaneController_->RecoverDisplaySurface();
        }

        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            if (displaySurfaceRecoveryTimerId_ != 0
                && !displaySurfaceRecoveryPolicy_.ShouldRecoverViewer(
                    reinterpret_cast<std::uintptr_t>(viewer->Hwnd())))
            {
                continue;
            }
            viewer->RecoverDisplaySurface();
        }

        if (diagnosticsWindow_ && diagnosticsWindow_->IsOpen())
        {
            diagnosticsWindow_->RecoverDisplaySurface();
        }

        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    }

    void MainWindow::ScheduleDisplaySurfaceRecoveryRetries()
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        displaySurfaceRecoveryPolicy_.BeginRetries();
        if (displaySurfaceRecoveryTimerId_ != 0)
        {
            return;
        }

        std::vector<std::uintptr_t> viewerTargets;
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            if (viewer->Hwnd())
            {
                viewerTargets.push_back(reinterpret_cast<std::uintptr_t>(viewer->Hwnd()));
            }
        }
        displaySurfaceRecoveryPolicy_.SetViewerTargets(viewerTargets);

        displaySurfaceRecoveryTimerId_ = SetTimer(
            hwnd_,
            kDisplaySurfaceRecoveryTimerId,
            kDisplaySurfaceRecoveryIntervalMs,
            nullptr);
    }

    void MainWindow::StopDisplaySurfaceRecoveryRetries()
    {
        if (displaySurfaceRecoveryTimerId_ != 0 && hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            KillTimer(hwnd_, kDisplaySurfaceRecoveryTimerId);
        }

        displaySurfaceRecoveryTimerId_ = 0;
        displaySurfaceRecoveryPolicy_.BeginRetries();
        displaySurfaceRecoveryPolicy_.ClearViewerTargets();
    }

    bool MainWindow::HandleCommand(UINT commandId)
    {
        if (commandId == command_ids::ID_ACTION_CLEAR_FILTER)
        {
            if (filterEdit_)
            {
                SetWindowTextW(filterEdit_, L"");
                SetFocus(filterEdit_);
            }
            return true;
        }

        if (HandleSavedSearchCommand(commandId)) return true;
        if (fileCommandController_.Handle(commandId))
        {
            return true;
        }

        if (viewCommandController_.Handle(commandId))
        {
            return true;
        }

        return false;

    }

    bool MainWindow::HandleSavedSearchCommand(UINT commandId)
    {
        const bool open = commandId >= ID_FILE_OPEN_SAVED_SEARCH_BASE && commandId <= ID_FILE_OPEN_SAVED_SEARCH_LAST;
        const bool action = commandId >= ID_FILE_SAVE_CURRENT_FILTER && commandId <= ID_FILE_RELOAD_SAVED_SEARCHES;
        if (!open && !action) return false;
        if (!savedSearchController_ || savedSearchController_->Busy()) return true;
        if (commandId == ID_FILE_RELOAD_SAVED_SEARCHES)
        {
            savedSearchController_->Load();
        }
        else if (open)
        {
            if (browserModel_ && !browserModel_->FolderPath().empty())
            {
                const auto search = savedSearchController_->Activate(commandId - ID_FILE_OPEN_SAVED_SEARCH_BASE);
                if (search)
                {
                    ApplySavedSearchFilter(search->expression);
                    SetFocus(browserPane_);
                }
            }
        }
        else if (savedSearchController_->Ready() && browserPaneController_)
        {
            std::wstring text;
            if (commandId == ID_FILE_SAVE_CURRENT_FILTER && browserPaneController_->HasActiveFilter())
            {
                const std::wstring expression = browserPaneController_->GetFilterQuery();
                if (PromptForSingleLineText(hwnd_, instance_, appTextSize_, themeMode_ == ThemeMode::Dark,
                    L"Save Current Filter", L"Saved search name", L"Save", {}, 0, -1, &text))
                {
                    savedSearchController_->Add({std::move(text), expression});
                    if (browserPaneController_->GetFilterQuery() != expression)
                        savedSearchController_->FilterEdited();
                }
            }
            else if (savedSearchController_->Active())
            {
                const services::SavedSearch active = *savedSearchController_->Active();
                if (commandId == ID_FILE_UPDATE_SAVED_SEARCH)
                {
                    if (PromptForSingleLineText(hwnd_, instance_, appTextSize_, themeMode_ == ThemeMode::Dark,
                        L"Update Saved Search", L"Filter expression", L"Update", active.expression, 0, -1, &text))
                        savedSearchController_->UpdateActive(std::move(text));
                }
                else if (commandId == ID_FILE_RENAME_SAVED_SEARCH)
                {
                    if (PromptForSingleLineText(hwnd_, instance_, appTextSize_, themeMode_ == ThemeMode::Dark,
                        L"Rename Saved Search", L"Saved search name", L"Rename", active.name, 0, -1, &text))
                        savedSearchController_->RenameActive(std::move(text));
                }
                else if (commandId == ID_FILE_DELETE_SAVED_SEARCH)
                {
                    const std::wstring question = L"Delete saved search \"" + active.name + L"\"?";
                    if (MessageBoxW(hwnd_, question.c_str(), L"Delete Saved Search",
                                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                        savedSearchController_->RemoveActive();
                }
            }
        }
        UpdateStatusText();
        UpdateMenuState();
        return true;
    }

    void MainWindow::SetBrowserMode(BrowserMode mode)
    {
        if (browserMode_ == mode)
        {
            return;
        }

        browserMode_ = mode;
        util::LogInfo(browserMode_ == BrowserMode::Thumbnails
            ? L"Switched shell to thumbnail mode"
            : L"Switched shell to details mode");

        if (browserPaneController_)
        {
            browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
                ? browser::BrowserViewMode::Thumbnails
                : browser::BrowserViewMode::Details);
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    void MainWindow::StepThumbnailSize(int direction)
    {
        if (direction == 0 || browserMode_ != BrowserMode::Thumbnails || !browserPaneController_)
        {
            return;
        }

        const browser::ThumbnailSizePreset currentPreset = browserPaneController_->GetThumbnailSizePreset();
        const auto currentIterator = std::find(kThumbnailSizePresets.begin(), kThumbnailSizePresets.end(), currentPreset);
        if (currentIterator == kThumbnailSizePresets.end())
        {
            return;
        }

        std::size_t currentIndex = static_cast<std::size_t>(currentIterator - kThumbnailSizePresets.begin());
        if (direction > 0)
        {
            currentIndex = std::min(currentIndex + 1, kThumbnailSizePresets.size() - 1);
        }
        else
        {
            currentIndex = currentIndex == 0 ? 0 : currentIndex - 1;
        }

        const browser::ThumbnailSizePreset nextPreset = kThumbnailSizePresets[currentIndex];
        if (nextPreset == currentPreset)
        {
            return;
        }

        thumbnailSizePreset_ = nextPreset;
        ApplyThumbnailDisplaySettings();
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::ToggleRecursiveBrowsing()
    {
        recursiveBrowsingEnabled_ = !recursiveBrowsingEnabled_;
        util::LogInfo(recursiveBrowsingEnabled_
            ? L"Enabled recursive browsing for folder enumeration"
            : L"Disabled recursive browsing for folder enumeration");
        UpdateMenuState();

        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::ToggleShowSubfoldersInBrowser()
    {
        showSubfoldersInBrowser_ = !showSubfoldersInBrowser_;
        util::LogInfo(showSubfoldersInBrowser_
            ? L"Enabled subfolder entries in the browser"
            : L"Disabled subfolder entries in the browser");
        UpdateMenuState();

        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
    }
}
