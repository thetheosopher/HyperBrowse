#include "ui/MainWindowInternal.h"

namespace
{
    constexpr UINT kDetailsPanelCopyPromptCommandId = 5705;

    bool IsJpegBrowserItem(const hyperbrowse::browser::BrowserItem& item)
    {
        return hyperbrowse::decode::IsWicFileType(item.fileType)
            && (_wcsicmp(item.fileType.c_str(), L"JPG") == 0 || _wcsicmp(item.fileType.c_str(), L"JPEG") == 0);
    }
}

namespace hyperbrowse::ui
{

    std::vector<browser::BrowserItem> MainWindow::CollectItemsForScope(bool selectionScope) const
    {
        if (!browserModel_)
        {
            return {};
        }

        const auto& modelItems = browserModel_->Items();
        const std::vector<int> orderedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedModelIndicesSnapshot()
            : std::vector<int>{};
        const std::vector<int> selectedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedSelectedModelIndicesSnapshot()
            : std::vector<int>{};
        return BrowserItemScopeCollector::Collect({
            modelItems,
            orderedModelIndices,
            selectedModelIndices,
            selectionScope,
            browserPaneController_ != nullptr});
    }

    std::vector<std::wstring> MainWindow::ExpandRawJpegPairedPaths(const std::vector<std::wstring>& paths,
                                                                   std::size_t* pairedCompanionCount) const
    {
        if (pairedCompanionCount)
        {
            *pairedCompanionCount = 0;
        }

        if (!rawJpegPairedOperationsEnabled_ || !browserModel_ || paths.empty())
        {
            return paths;
        }

        const std::vector<std::wstring> expandedPaths = PairedRawJpegResolver::ExpandPaths(
            paths,
            browserModel_->Items(),
            [](std::wstring_view lhs, std::wstring_view rhs)
            {
                return FolderPathsEqual(lhs, rhs);
            });

        if (pairedCompanionCount && expandedPaths.size() > paths.size())
        {
            *pairedCompanionCount = expandedPaths.size() - paths.size();
        }

        return expandedPaths;
    }

    std::vector<std::wstring> MainWindow::SelectedFileOperationPathsSnapshot(std::size_t* pairedCompanionCount) const
    {
        if (!browserPaneController_)
        {
            return {};
        }

        return ExpandRawJpegPairedPaths(browserPaneController_->SelectedFilePathsSnapshot(), pairedCompanionCount);
    }

    void MainWindow::OpenFolder()
    {
        std::wstring folderPath;
        if (!ChooseFolder(&folderPath) || folderPath.empty())
        {
            return;
        }

        LoadFolderAsync(std::move(folderPath));
    }

    bool MainWindow::ChooseFolder(std::wstring* folderPath, HWND ownerWindow) const
    {
        if (!folderPath)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,
                                          nullptr,
                                          CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result) || !dialog)
        {
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        result = dialog->Show(ownerWindow ? ownerWindow : hwnd_);
        if (FAILED(result))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellItem> shellItem;
        result = dialog->GetResult(&shellItem);
        if (FAILED(result) || !shellItem)
        {
            return false;
        }

        PWSTR rawFolderPath = nullptr;
        result = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &rawFolderPath);
        if (FAILED(result) || !rawFolderPath)
        {
            return false;
        }

        *folderPath = rawFolderPath;
        CoTaskMemFree(rawFolderPath);
        return true;
    }

    bool MainWindow::HasSelectedJpegItems() const
    {
        for (const browser::BrowserItem& item : CollectItemsForScope(true))
        {
            if (IsJpegBrowserItem(item))
            {
                return true;
            }
        }

        return false;
    }

    void MainWindow::StartCompareSelected()
    {
        if (!browserModel_ || !browserPaneController_ || !viewerWindow_)
        {
            return;
        }

        std::vector<browser::BrowserItem> selectedItems = CollectItemsForScope(true);
        if (!viewer::IsSupportedCompareTileCount(selectedItems.size())
            || std::any_of(selectedItems.begin(), selectedItems.end(), [](const browser::BrowserItem& item)
            {
                return item.isDirectory;
            }))
        {
            MessageBoxW(hwnd_, L"Select 2 to 4 images to compare. Folders are not included.",
                        L"Compare Selected", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::vector<browser::BrowserItem> candidateItems = CollectItemsForScope(false);
        candidateItems.erase(std::remove_if(candidateItems.begin(), candidateItems.end(), [](const browser::BrowserItem& item)
        {
            return item.isDirectory;
        }), candidateItems.end());
        if (candidateItems.size() < selectedItems.size())
        {
            MessageBoxW(hwnd_, L"The selected images are no longer available in the current view.",
                        L"Compare Selected", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::vector<int> tileIndexes;
        tileIndexes.reserve(selectedItems.size());
        for (const browser::BrowserItem& selectedItem : selectedItems)
        {
            const auto candidate = std::find_if(candidateItems.begin(), candidateItems.end(), [&](const browser::BrowserItem& item)
            {
                return browser::FilePathsEqual(item.filePath, selectedItem.filePath);
            });
            if (candidate == candidateItems.end())
            {
                MessageBoxW(hwnd_, L"The selected images are no longer available in the current view.",
                            L"Compare Selected", MB_OK | MB_ICONINFORMATION);
                return;
            }
            const int candidateIndex = static_cast<int>(std::distance(candidateItems.begin(), candidate));
            if (std::find(tileIndexes.begin(), tileIndexes.end(), candidateIndex) != tileIndexes.end())
            {
                continue;
            }
            tileIndexes.push_back(candidateIndex);
        }
        if (tileIndexes.size() < 2 || tileIndexes.size() > 4)
        {
            MessageBoxW(hwnd_, L"Select 2 to 4 distinct images to compare.",
                        L"Compare Selected", MB_OK | MB_ICONINFORMATION);
            return;
        }

        int selectedIndex = tileIndexes.front();
        const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
        const auto& modelItems = browserModel_->Items();
        if (primaryModelIndex >= 0 && primaryModelIndex < static_cast<int>(modelItems.size()))
        {
            const std::wstring& primaryPath = modelItems[static_cast<std::size_t>(primaryModelIndex)].filePath;
            const auto primaryCandidate = std::find_if(candidateItems.begin(), candidateItems.end(), [&](const browser::BrowserItem& item)
            {
                return browser::FilePathsEqual(item.filePath, primaryPath);
            });
            if (primaryCandidate != candidateItems.end())
            {
                selectedIndex = static_cast<int>(std::distance(candidateItems.begin(), primaryCandidate));
            }
        }

        std::vector<viewer::CompareTileMetadata> candidateMetadata(candidateItems.size());
        if (userMetadataStore_)
        {
            for (const int tileIndex : tileIndexes)
            {
                const services::UserMetadataEntry entry = userMetadataStore_->EntryForPath(
                    candidateItems[static_cast<std::size_t>(tileIndex)].filePath);
                candidateMetadata[static_cast<std::size_t>(tileIndex)] = {entry.rating, entry.tags};
            }
        }

        if (OpenItemsInViewer(std::move(candidateItems), selectedIndex, false, false, false)
            && !viewerWindow_->BeginCompareSession(tileIndexes, candidateMetadata))
        {
            viewerWindow_->SetCompareMode(false);
        }
    }

    bool MainWindow::IsFavoriteDestination(std::wstring_view folderPath) const
    {
        return std::any_of(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });
    }

    std::vector<std::wstring> MainWindow::RecentDestinationShortcutPaths() const
    {
        std::vector<std::wstring> paths;
        for (const std::wstring& recentPath : recentDestinationFolders_)
        {
            if (std::any_of(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& favoritePath)
            {
                return FolderPathsEqual(favoritePath, recentPath);
            }))
            {
                continue;
            }

            QuickAccessPathList::Insert(&paths, recentPath, kQuickAccessFolderLimit, false);
        }

        return paths;
    }

    void MainWindow::RemoveFavoriteDestination(std::wstring_view folderPath)
    {
        bool removed = false;
        MutateQuickSendState([&]
        {
            const auto newEnd = std::remove_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(),
                                               [&](const std::wstring& candidate)
                                               {
                                                   return FolderPathsEqual(candidate, folderPath);
                                               });
            if (newEnd == favoriteDestinationFolders_.end())
            {
                return;
            }

            favoriteDestinationFolders_.erase(newEnd, favoriteDestinationFolders_.end());
            SyncQuickSendModel();
            removed = true;
        });
        if (!removed)
        {
            return;
        }

        if (treePane_)
        {
            InvalidateRect(treePane_, nullptr, FALSE);
        }
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::RemoveRecentDestination(std::wstring_view folderPath)
    {
        const auto newEnd = std::remove_if(recentDestinationFolders_.begin(), recentDestinationFolders_.end(),
                                           [&](const std::wstring& candidate)
                                           {
                                               return FolderPathsEqual(candidate, folderPath);
                                           });
        if (newEnd == recentDestinationFolders_.end())
        {
            return;
        }

        recentDestinationFolders_.erase(newEnd, recentDestinationFolders_.end());
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::ClearFavoriteDestinations()
    {
        bool cleared = false;
        MutateQuickSendState([&]
        {
            if (favoriteDestinationFolders_.empty())
            {
                return;
            }

            favoriteDestinationFolders_.clear();
            SyncQuickSendModel();
            cleared = true;
        });
        if (!cleared)
        {
            return;
        }

        if (treePane_)
        {
            InvalidateRect(treePane_, nullptr, FALSE);
        }
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::ClearRecentFolders()
    {
        if (recentFolders_.empty())
        {
            return;
        }

        recentFolders_.clear();
        UpdateMenuState();
    }

    void MainWindow::ClearRecentDestinations()
    {
        if (recentDestinationFolders_.empty())
        {
            return;
        }

        recentDestinationFolders_.clear();
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::ToggleCurrentFolderFavoriteDestination()
    {
        if (!browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        const std::wstring folderPath = NormalizeFolderPath(browserModel_->FolderPath());
        bool addedFavorite = false;
        MutateQuickSendState([&]
        {
            const auto existing = std::find_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
            {
                return FolderPathsEqual(candidate, folderPath);
            });

            if (existing != favoriteDestinationFolders_.end())
            {
                favoriteDestinationFolders_.erase(existing);
            }
            else
            {
                addedFavorite = QuickAccessPathList::Insert(&favoriteDestinationFolders_, folderPath, kFavoriteDestinationLimit, false);
            }
            SyncQuickSendModel();
            if (addedFavorite)
            {
                quickSendModel_.AssignNextAvailableShortcut(folderPath);
            }
            SortFavoriteDestinationsByShortcutInMemory();
        });

        if (treePane_)
        {
            InvalidateRect(treePane_, nullptr, FALSE);
        }
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::StartSelectionFileOperationToDestination(services::FileOperationType type,
                                                               std::wstring destinationFolder,
                                                               bool quickSend)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_,
                        L"Select one or more images first.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (IsQuickAccessDestinationCurrentFolder(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination is already the current folder.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const services::FileConflictPlan conflictPlan = services::PlanDestinationConflicts(
            sourcePaths,
            destinationFolder,
            services::FileConflictPolicy::OverwriteExisting);

        services::FileConflictPolicy conflictPolicy = services::FileConflictPolicy::PromptShell;
        std::vector<std::wstring> targetLeafNames;
        if (!PromptForFileConflictPolicy(hwnd_, type, conflictPlan.conflictCount, &conflictPolicy))
        {
            return;
        }

        if (conflictPolicy == services::FileConflictPolicy::AutoRenameNumericSuffix)
        {
            targetLeafNames = services::PlanDestinationConflicts(
                sourcePaths,
                destinationFolder,
                conflictPolicy).targetLeafNames;
        }

        const std::wstring quickSendDestination = destinationFolder;
        const std::optional<int> quickSendShortcut = quickSendModel_.ShortcutForDestination(quickSendDestination);
        if (StartFileOperation(type,
                               std::vector<std::wstring>(sourcePaths),
                               std::move(destinationFolder),
                               conflictPolicy,
                               std::move(targetLeafNames)))
        {
            if (quickSend)
            {
                pendingBrowserQuickSend_ = PendingBrowserQuickSend{
                    type,
                    sourcePaths,
                    quickSendDestination,
                    quickSendShortcut
                        ? std::optional<wchar_t>(QuickSendModel::ShortcutCharacter(*quickSendShortcut))
                        : std::nullopt};
            }

            MutateQuickSendState([&]
            {
                lastQuickSendDestination_ = quickSendDestination;
            });
        }
    }

    bool MainWindow::ShowShellContextMenuForSelection(POINT screenPoint)
    {
        if (!hwnd_ || !browserPaneController_)
        {
            return false;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            return false;
        }

        // All items must share a parent folder for a single shell context menu.
        const std::wstring parentPath = fs::path(selectedPaths.front()).parent_path().wstring();
        for (const std::wstring& path : selectedPaths)
        {
            if (!FolderPathsEqual(parentPath, fs::path(path).parent_path().wstring()))
            {
                return false;
            }
        }

        PIDLIST_ABSOLUTE folderPidl = ILCreateFromPathW(parentPath.c_str());
        if (!folderPidl)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellFolder> parentFolder;
        PCIDLIST_ABSOLUTE childIdl = nullptr;
        HRESULT result = SHBindToParent(folderPidl, IID_PPV_ARGS(parentFolder.GetAddressOf()), &childIdl);
        if (FAILED(result) || !parentFolder)
        {
            ILFree(folderPidl);
            return false;
        }

        // Resolve each item's child PIDL within the parent folder.
        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCUITEMID_CHILD> childPidls;
        for (const std::wstring& path : selectedPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }
            itemPidls.push_back(itemPidl);
            childPidls.push_back(ILFindLastID(itemPidl));
        }

        Microsoft::WRL::ComPtr<IContextMenu> contextMenu;
        result = parentFolder->GetUIObjectOf(hwnd_,
                                             static_cast<UINT>(childPidls.size()),
                                             childPidls.data(),
                                             IID_IContextMenu,
                                             nullptr,
                                             reinterpret_cast<void**>(contextMenu.GetAddressOf()));
        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }
        ILFree(folderPidl);

        if (FAILED(result) || !contextMenu)
        {
            return false;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return false;
        }

        // CMF_NORMAL lets the shell add its own verbs; CMF_EXTENDEDVERCS surfaces the
        // extended (shift) verbs since this path is triggered by Shift+right-click.
        result = contextMenu->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXTENDEDVERBS);
        if (FAILED(result))
        {
            DestroyMenu(menu);
            return false;
        }

        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);

        if (commandId > 0)
        {
            CMINVOKECOMMANDINFO invoke{};
            invoke.cbSize = sizeof(invoke);
            invoke.hwnd = hwnd_;
            invoke.lpVerb = MAKEINTRESOURCEA(commandId - 1);
            invoke.nShow = SW_SHOWNORMAL;
            contextMenu->InvokeCommand(&invoke);
        }

        DestroyMenu(menu);
        return true;
    }

    void MainWindow::ShowDetailsPanelContextMenu(POINT screenPoint)
    {
        if (!hwnd_ || detailsPanelPromptText_.empty())
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        AppendMenuW(menu, MF_STRING, kDetailsPanelCopyPromptCommandId, L"Copy Prompt");
        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(menu,
                                                TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                                screenPoint.x,
                                                screenPoint.y,
                                                hwnd_,
                                                nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);

        if (commandId == kDetailsPanelCopyPromptCommandId
            && !CopyTextToClipboard(hwnd_, detailsPanelPromptText_))
        {
            MessageBoxW(hwnd_,
                        L"Failed to copy the image prompt to the clipboard.",
                        L"Copy Prompt",
                        MB_OK | MB_ICONERROR);
        }

        DestroyMenu(menu);
    }

    void MainWindow::ShowBrowserContextMenu(POINT screenPoint)
    {
        if (!hwnd_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSingleSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 1;
        const bool hasBatchRenameSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 1;
        const bool hasCompareSelection = browserPaneController_
            && viewer::IsSupportedCompareTileCount(browserPaneController_->SelectedCount());
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool allowMutatingFileCommands = hasSelection && !fileOperationActive_;
        const int commonSelectionRating = hasSelection ? CommonSelectionRating() : -1;
        const bool allowRenameSelected = hasSingleSelection && !fileOperationActive_;
        const bool allowBatchRenameSelected = hasBatchRenameSelection && !fileOperationActive_;
        const bool hasSecondaryMonitor = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        const bool sizeCommandsEnabled = hasFolder && browserMode_ == BrowserMode::Thumbnails;

        HMENU menu = CreatePopupMenu();
        HMENU metadataMenu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU ratingMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        HMENU thumbnailSizeMenu = CreatePopupMenu();
        if (!menu || !metadataMenu || !batchConvertSelectionMenu || !ratingMenu || !sortMenu || !thumbnailSizeMenu)
        {
            if (thumbnailSizeMenu)
            {
                DestroyMenu(thumbnailSizeMenu);
            }
            if (sortMenu)
            {
                DestroyMenu(sortMenu);
            }
            if (ratingMenu)
            {
                DestroyMenu(ratingMenu);
            }
            if (batchConvertSelectionMenu)
            {
                DestroyMenu(batchConvertSelectionMenu);
            }
            if (metadataMenu)
            {
                DestroyMenu(metadataMenu);
            }
            if (menu)
            {
                DestroyMenu(menu);
            }
            return;
        }

        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_0, L"&Clear Rating");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_1, L"&1 Star");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_2, L"&2 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_3, L"&3 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_4, L"&4 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_5, L"&5 Stars");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date &Taken");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RATING, L"By &Rating");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TAGS, L"By Ta&gs");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"&Descending");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_96, L"&96 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_128, L"1&28 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_160, L"1&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_192, L"1&92 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_256, L"2&56 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_320, L"3&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_360, L"3&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_420, L"4&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_480, L"4&80 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_560, L"5&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_640, L"6&40 px");

        AppendMenuW(menu, MF_STRING, ID_FILE_NEW_FOLDER, L"New &Folder...\tCtrl+Shift+N");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        if (hasSelection)
        {
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open\tEnter");
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, L"Open in New Viewer &Window\tCtrl+Shift+Enter");
            AppendMenuW(menu, MF_STRING, ID_FILE_COMPARE_SELECTED, L"&Compare Selected");
            AppendMenuW(menu, MF_STRING, ID_FILE_VIEW_ON_SECONDARY_MONITOR, L"View on Secondary &Monitor");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"Slideshow from &Selection");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer");
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_FILES_TO_CLIPBOARD, L"&Copy\tCtrl+C");
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_IMAGE_PIXELS, L"Copy &Image\tCtrl+Shift+I");
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h\tCtrl+Shift+C");
            AppendMenuW(menu, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information");
            AppendMenuW(menu, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties");
            AppendMenuW(metadataMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set &Rating");
            AppendMenuW(metadataMenu, MF_STRING, ID_FILE_EDIT_TAGS, L"Edit &Tags...");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(metadataMenu), L"&Metadata");
            AppendMenuW(menu, MF_STRING, ID_FILE_RENAME_SELECTED, L"Re&name...");
            AppendMenuW(menu, MF_STRING, ID_FILE_BATCH_RENAME_SELECTION, L"Batch R&ename...\tCtrl+Shift+R");
            AppendMenuW(menu, MF_STRING, ID_FILE_DUPLICATE_SELECTION, L"Dup&licate\tCtrl+D");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_SELECTION, L"Cop&y Selection...");
            AppendMenuW(menu, MF_STRING, ID_FILE_MOVE_SELECTION, L"Mo&ve Selection...");
            if (hasBatchRenameSelection)
            {
                AppendMenuW(menu, MF_STRING, ID_FILE_MOVE_SELECTION_TO_NEW_CHILD_FOLDER, L"Move to New Child &Folder...");
            }
            AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete");
            AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
            AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
            AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");

            EnableMenuItem(menu, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_COMPARE_SELECTED,
                           MF_BYCOMMAND | (hasCompareSelection ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_VIEW_ON_SECONDARY_MONITOR,
                           MF_BYCOMMAND | ((hasSelection && hasSecondaryMonitor) ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_COPY_FILES_TO_CLIPBOARD, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_COPY_IMAGE_PIXELS, MF_BYCOMMAND | (hasSingleSelection ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_COPY_PATH, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_PROPERTIES, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_EDIT_TAGS, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_RENAME_SELECTED, MF_BYCOMMAND | (allowRenameSelected ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_BATCH_RENAME_SELECTION, MF_BYCOMMAND | (allowBatchRenameSelected ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_DUPLICATE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_COPY_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_MOVE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            if (hasBatchRenameSelection)
            {
                EnableMenuItem(menu,
                               ID_FILE_MOVE_SELECTION_TO_NEW_CHILD_FOLDER,
                               MF_BYCOMMAND | (allowBatchRenameSelected ? MF_ENABLED : MF_GRAYED));
            }
            EnableMenuItem(menu, ID_FILE_DELETE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_DELETE_SELECTION_PERMANENT, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        }
        else
        {
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_FOLDER, L"Open &Folder...");
            AppendMenuW(menu, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree");
            AppendMenuW(menu, MF_STRING, ID_FILE_PASTE_FILES, L"&Paste\tCtrl+V");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode");
            AppendMenuW(menu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode");
            AppendMenuW(menu, MF_STRING, ID_VIEW_GO_TO_ITEM_NUMBER, kGoToItemNumberMenuLabel.data());
            AppendMenuW(menu, MF_STRING, ID_VIEW_RECURSIVE, kRecursiveBrowsingMenuLabel.data());
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_DETAILS, kThumbnailDetailsMenuLabel.data());
            AppendMenuW(menu, MF_STRING, ID_VIEW_DETAILS_STRIP, L"Show &Details Panel");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(thumbnailSizeMenu), L"Thumbnail Si&ze");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SLIDESHOW_FOLDER, L"Slideshow from &Folder");

            EnableMenuItem(menu, ID_FILE_REFRESH_TREE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_PASTE_FILES, MF_BYCOMMAND | (hasFolder && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_DETAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_RECURSIVE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_THUMBNAIL_DETAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_DETAILS_STRIP, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_SLIDESHOW_FOLDER, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));

            CheckMenuRadioItem(
                menu,
                ID_VIEW_THUMBNAILS,
                ID_VIEW_DETAILS,
                browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
                MF_BYCOMMAND);
            CheckMenuItem(
                menu,
                ID_VIEW_RECURSIVE,
                MF_BYCOMMAND | (recursiveBrowsingEnabled_ ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(
                menu,
                ID_VIEW_THUMBNAIL_DETAILS,
                MF_BYCOMMAND | (thumbnailDetailsVisible_ ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(
                menu,
                ID_VIEW_DETAILS_STRIP,
                MF_BYCOMMAND | (detailsStripVisible_ ? MF_CHECKED : MF_UNCHECKED));

            EnableMenuItem(sortMenu, ID_VIEW_SORT_FILENAME, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_MODIFIED, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_SIZE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_DIMENSIONS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_TYPE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_RANDOM, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_DATETAKEN, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_RATING, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_TAGS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            for (UINT thumbnailSizeCommandId = ID_VIEW_THUMBNAIL_SIZE_96; thumbnailSizeCommandId <= ID_VIEW_THUMBNAIL_SIZE_640; ++thumbnailSizeCommandId)
            {
                EnableMenuItem(thumbnailSizeMenu, thumbnailSizeCommandId, MF_BYCOMMAND | (sizeCommandsEnabled ? MF_ENABLED : MF_GRAYED));
            }
        }

        EnableMenuItem(menu, ID_FILE_NEW_FOLDER, MF_BYCOMMAND | (hasFolder && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        for (UINT ratingCommandId = ID_FILE_SET_RATING_0; ratingCommandId <= ID_FILE_SET_RATING_5; ++ratingCommandId)
        {
            EnableMenuItem(ratingMenu, ratingCommandId, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(ratingMenu,
                          ratingCommandId,
                          MF_BYCOMMAND | (commonSelectionRating >= 0 && ratingCommandId == CommandIdFromRating(commonSelectionRating)
                              ? MF_CHECKED
                              : MF_UNCHECKED));
        }
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            thumbnailSizeMenu,
            ID_VIEW_THUMBNAIL_SIZE_96,
            ID_VIEW_THUMBNAIL_SIZE_640,
            CommandIdFromThumbnailSizePreset(thumbnailSizePreset_),
            MF_BYCOMMAND);

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        const bool sortAscending = browserPaneController_
            ? browserPaneController_->IsSortAscending()
            : true;
        CheckMenuRadioItem(
            sortMenu,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_TAGS,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);
        CheckMenuItem(
            sortMenu,
            ID_VIEW_SORT_DIRECTION,
            MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

        SetForegroundWindow(hwnd_);
        TrackPopupMenu(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            screenPoint.x,
            screenPoint.y,
            0,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void MainWindow::ShowFolderTreeContextMenu(POINT screenPoint, HTREEITEM item)
    {
        if (!hwnd_ || !treePane_ || !item)
        {
            return;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->path.empty() || !TreeView_GetParent(treePane_, item))
        {
            return;
        }

        const std::wstring folderPath = nodeData->path;
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        constexpr UINT kRenameFolderCommandId = 1;
        constexpr UINT kDeleteFolderCommandId = 2;
        constexpr UINT kDeleteFolderPermanentCommandId = 3;
        constexpr UINT kOpenInExplorerCommandId = 4;
        constexpr UINT kCopyPathCommandId = 5;
        constexpr UINT kMoveFolderBrowseCommandId = 6;
        constexpr UINT kMoveFolderFavoriteBaseCommandId = 40;
        constexpr UINT kMoveFolderFavoriteLastCommandId = kMoveFolderFavoriteBaseCommandId
            + static_cast<UINT>(kFavoriteDestinationLimit) - 1;
        constexpr UINT kMoveFolderRecentBaseCommandId = 80;
        constexpr UINT kMoveFolderRecentLastCommandId = 87;
        constexpr UINT kNewFolderCommandId = 30;
        constexpr UINT kToggleFavoriteCommandId = 31;
        constexpr UINT kFolderPropertiesCommandId = 32;

        std::vector<std::wstring> favoriteMoveDestinations;
        std::vector<std::wstring> recentMoveDestinations;
        const std::wstring sourceParentPath = NormalizeFolderPath(fs::path(folderPath).parent_path().wstring());
        const auto canOfferMoveDestination = [&](const std::wstring& destinationPath)
        {
            const std::wstring normalizedDestination = NormalizeFolderPath(destinationPath);
            if (!IsExistingDirectory(normalizedDestination))
            {
                return false;
            }

            if (!AreFoldersOnSameDrive(folderPath, normalizedDestination))
            {
                return false;
            }

            if (FolderPathsEqual(normalizedDestination, folderPath)
                || hyperbrowse::browser::PathHasPrefix(normalizedDestination, folderPath)
                || (!sourceParentPath.empty() && FolderPathsEqual(normalizedDestination, sourceParentPath)))
            {
                return false;
            }

            return true;
        };

        for (const std::wstring& favoritePath : favoriteDestinationFolders_)
        {
            if (canOfferMoveDestination(favoritePath))
            {
                favoriteMoveDestinations.push_back(NormalizeFolderPath(favoritePath));
            }
        }

        for (const std::wstring& recentPath : RecentDestinationShortcutPaths())
        {
            if (!canOfferMoveDestination(recentPath))
            {
                continue;
            }

            const std::wstring normalizedRecentPath = NormalizeFolderPath(recentPath);
            const bool duplicate = std::any_of(favoriteMoveDestinations.begin(), favoriteMoveDestinations.end(), [&](const std::wstring& favoritePath)
            {
                return FolderPathsEqual(favoritePath, normalizedRecentPath);
            });
            if (!duplicate)
            {
                recentMoveDestinations.push_back(normalizedRecentPath);
            }
        }

        HMENU moveFolderMenu = CreatePopupMenu();
        if (!moveFolderMenu)
        {
            DestroyMenu(menu);
            return;
        }

        AppendMenuW(moveFolderMenu, MF_STRING, kMoveFolderBrowseCommandId, L"Choose &Folder...");
        if (!favoriteMoveDestinations.empty() || !recentMoveDestinations.empty())
        {
            AppendMenuW(moveFolderMenu, MF_SEPARATOR, 0, nullptr);
            const std::size_t favoriteCapacity = kMoveFolderFavoriteLastCommandId - kMoveFolderFavoriteBaseCommandId + 1;
            const std::size_t favoriteCount = std::min<std::size_t>(favoriteMoveDestinations.size(), favoriteCapacity);
            for (std::size_t index = 0; index < favoriteCount; ++index)
            {
                AppendMenuW(moveFolderMenu,
                            MF_STRING,
                            kMoveFolderFavoriteBaseCommandId + static_cast<UINT>(index),
                            QuickAccessMenuBuilder::FormatFolderShortcutMenuLabel(favoriteMoveDestinations[index]).c_str());
            }

            const std::size_t recentCapacity = kMoveFolderRecentLastCommandId - kMoveFolderRecentBaseCommandId + 1;
            const std::size_t recentCount = std::min<std::size_t>(recentMoveDestinations.size(), recentCapacity);
            for (std::size_t index = 0; index < recentCount; ++index)
            {
                AppendMenuW(moveFolderMenu,
                            MF_STRING,
                            kMoveFolderRecentBaseCommandId + static_cast<UINT>(index),
                            QuickAccessMenuBuilder::FormatFolderShortcutMenuLabel(recentMoveDestinations[index]).c_str());
            }
        }
        else
        {
            AppendMenuW(moveFolderMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(moveFolderMenu, MF_STRING | MF_GRAYED, 0, L"(No compatible quick destinations)");
        }

        const bool isFavoriteDestination = IsFavoriteDestination(folderPath);
        const wchar_t* toggleFavoriteLabel = isFavoriteDestination ? L"Remove from &Quick Actions" : L"&Add to Quick Actions";

        AppendMenuW(menu, MF_STRING, kNewFolderCommandId, L"&New Folder...");
        AppendMenuW(menu, MF_STRING, kRenameFolderCommandId, L"Re&name Folder...");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(moveFolderMenu), L"Mo&ve Folder To");
        AppendMenuW(menu, MF_STRING, kOpenInExplorerCommandId, L"Open in &Explorer");
        AppendMenuW(menu, MF_STRING, kCopyPathCommandId, L"Copy Pat&h");
        AppendMenuW(menu, MF_STRING, kToggleFavoriteCommandId, toggleFavoriteLabel);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kDeleteFolderCommandId, L"&Delete Folder");
        AppendMenuW(menu, MF_STRING, kDeleteFolderPermanentCommandId, L"Delete Folder &Permanently");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kFolderPropertiesCommandId, L"P&roperties");

        const UINT enableState = fileOperationActive_ ? MF_GRAYED : MF_ENABLED;
        EnableMenuItem(menu, kNewFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kRenameFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kOpenInExplorerCommandId, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, kCopyPathCommandId, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, kToggleFavoriteCommandId, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, kDeleteFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kDeleteFolderPermanentCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(moveFolderMenu, kMoveFolderBrowseCommandId, MF_BYCOMMAND | enableState);

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);

        switch (commandId)
        {
        case kNewFolderCommandId:
            StartCreateNewFolder(folderPath);
            break;
        case kRenameFolderCommandId:
            if (!BeginFolderTreeInlineRename(folderPath))
            {
                StartFolderTreeRename(folderPath);
            }
            break;
        case kMoveFolderBrowseCommandId:
        {
            std::wstring destinationFolder;
            if (ChooseFolder(&destinationFolder) && !destinationFolder.empty())
            {
                StartFolderTreeMoveToDestination(folderPath, std::move(destinationFolder));
            }
            break;
        }
        case kOpenInExplorerCommandId:
            if (!LaunchShellTarget(hwnd_, L"open", folderPath))
            {
                MessageBoxW(hwnd_, L"Failed to open the folder in Explorer.", L"Open in Explorer", MB_OK | MB_ICONERROR);
            }
            break;
        case kCopyPathCommandId:
            if (!CopyTextToClipboard(hwnd_, folderPath))
            {
                MessageBoxW(hwnd_, L"Failed to copy the folder path to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
            }
            break;
        case kToggleFavoriteCommandId:
        {
            bool addedFavorite = false;
            MutateQuickSendState([&]
            {
                const auto existing = std::find_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
                {
                    return FolderPathsEqual(candidate, folderPath);
                });
                if (existing != favoriteDestinationFolders_.end())
                {
                    favoriteDestinationFolders_.erase(existing);
                }
                else
                {
                    addedFavorite = QuickAccessPathList::Insert(&favoriteDestinationFolders_, folderPath, kFavoriteDestinationLimit, false);
                }
                SyncQuickSendModel();
                if (addedFavorite)
                {
                    quickSendModel_.AssignNextAvailableShortcut(folderPath);
                }
                SortFavoriteDestinationsByShortcutInMemory();
            });
            if (treePane_)
            {
                InvalidateRect(treePane_, nullptr, FALSE);
            }
            UpdateMenuState();
            if (hwnd_ && detailsStripVisible_)
            {
                LayoutChildren();
            }
            break;
        }
        case kDeleteFolderCommandId:
            StartFolderTreeDelete(folderPath, false);
            break;
        case kDeleteFolderPermanentCommandId:
            StartFolderTreeDelete(folderPath, true);
            break;
        case kFolderPropertiesCommandId:
        {
            SHELLEXECUTEINFOW executeInfo{};
            executeInfo.cbSize = sizeof(executeInfo);
            executeInfo.fMask = SEE_MASK_INVOKEIDLIST;
            executeInfo.hwnd = hwnd_;
            executeInfo.lpVerb = L"properties";
            executeInfo.lpFile = folderPath.c_str();
            executeInfo.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&executeInfo) == FALSE)
            {
                MessageBoxW(hwnd_, L"Failed to open the folder properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
            }
            break;
        }
        default:
            if (commandId >= kMoveFolderFavoriteBaseCommandId && commandId <= kMoveFolderFavoriteLastCommandId)
            {
                const std::size_t index = static_cast<std::size_t>(commandId - kMoveFolderFavoriteBaseCommandId);
                if (index < favoriteMoveDestinations.size())
                {
                    StartFolderTreeMoveToDestination(folderPath, favoriteMoveDestinations[index]);
                }
                break;
            }

            if (commandId >= kMoveFolderRecentBaseCommandId && commandId <= kMoveFolderRecentLastCommandId)
            {
                const std::size_t index = static_cast<std::size_t>(commandId - kMoveFolderRecentBaseCommandId);
                if (index < recentMoveDestinations.size())
                {
                    StartFolderTreeMoveToDestination(folderPath, recentMoveDestinations[index]);
                }
                break;
            }
            break;
        }
    }
}
