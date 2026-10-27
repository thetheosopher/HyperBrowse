#include "ui/MainWindowInternal.h"

namespace
{

    std::vector<std::wstring> FileLeafNames(const std::vector<std::wstring>& paths)
    {
        std::vector<std::wstring> names;
        names.reserve(paths.size());
        for (const std::wstring& path : paths)
        {
            names.push_back(fs::path(path).filename().wstring());
        }
        return names;
    }

    bool AllFileOperationPathStatesValid(const std::vector<hyperbrowse::ui::FileOperationPathState>& states)
    {
        return std::all_of(states.begin(), states.end(), [](const hyperbrowse::ui::FileOperationPathState& state)
        {
            return state.valid;
        });
    }

    std::wstring RewritePathPrefix(std::wstring_view path, std::wstring_view oldPrefix, std::wstring_view newPrefix)
    {
        if (!hyperbrowse::browser::PathHasPrefix(path, oldPrefix))
        {
            return std::wstring(path);
        }

        std::wstring rewrittenPath(newPrefix);
        std::wstring suffix(std::wstring(path).substr(oldPrefix.size()));
        if (!rewrittenPath.empty() && !suffix.empty() && rewrittenPath.back() == L'\\' && suffix.front() == L'\\')
        {
            suffix.erase(suffix.begin());
        }

        rewrittenPath.append(suffix);
        return NormalizeFolderPath(std::move(rewrittenPath));
    }

    std::wstring LongestCommonPrefix(const std::vector<std::wstring>& values)
    {
        if (values.empty())
        {
            return {};
        }

        std::wstring prefix = values.front();
        for (std::size_t index = 1; index < values.size() && !prefix.empty(); ++index)
        {
            const std::wstring& value = values[index];
            const std::size_t maxCommonLength = std::min(prefix.size(), value.size());
            std::size_t commonLength = 0;
            while (commonLength < maxCommonLength && prefix[commonLength] == value[commonLength])
            {
                ++commonLength;
            }

            prefix.resize(commonLength);
        }

        return prefix;
    }

    std::wstring TrimTrailingFolderNameSeparators(std::wstring value)
    {
        while (!value.empty())
        {
            const wchar_t ch = value.back();
            if (ch == L' ' || ch == L'_' || ch == L'-' || ch == L'.')
            {
                value.pop_back();
                continue;
            }

            break;
        }

        return value;
    }

    std::wstring BuildFolderDeleteConfirmationMessage(std::wstring_view folderPath, bool permanent)
    {
        const std::wstring folderLabel = hyperbrowse::ui::QuickAccessMenuBuilder::FolderDisplayName(folderPath);
        return permanent
            ? L"Permanently delete the folder \"" + folderLabel + L"\"?\n\nThis cannot be undone."
            : L"Move the folder \"" + folderLabel + L"\" to the Recycle Bin?";
    }

    bool ConfirmFolderDeletion(HWND ownerWindow, std::wstring_view folderPath, bool permanent)
    {
        const std::wstring prompt = BuildFolderDeleteConfirmationMessage(folderPath, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete Folder" : L"Delete Folder",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::StartCopySelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartSelectionFileOperationToDestination(services::FileOperationType::Copy, std::move(destinationFolder));
    }

    void MainWindow::StartRenameSelectedImage()
    {
        if (!browserModel_ || !browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedCount() != 1)
        {
            MessageBoxW(hwnd_, L"Select a single image to rename.", L"Rename Image", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        const auto& items = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        const std::wstring sourcePath = items[static_cast<std::size_t>(modelIndex)].filePath;
        const std::wstring sourceLeafName = items[static_cast<std::size_t>(modelIndex)].fileName;
        std::wstring renamedLeafName;
        if (!PromptForRenameLeafName(hwnd_,
                                     instance_,
                                     appTextSize_,
                                     themeMode_ == ThemeMode::Dark,
                                     L"Rename File",
                                     L"Enter a new file name.",
                                     sourceLeafName,
                                     true,
                                     &renamedLeafName))
        {
            return;
        }

        const bool sourceStillCurrent = std::any_of(
            browserModel_->Items().begin(),
            browserModel_->Items().end(),
            [&sourcePath](const browser::BrowserItem& currentItem)
            {
                return util::NormalizedPathEquals(currentItem.filePath, sourcePath);
            });
        if (!sourceStillCurrent || GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            MessageBoxW(hwnd_,
                        L"The selected image changed or is no longer available. Refresh the folder and try again.",
                        L"Rename Image",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        StartFileOperation(services::FileOperationType::Rename,
                           {sourcePath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {renamedLeafName});
    }

    void MainWindow::StartBatchRenameSelection()
    {
        if (!browserModel_ || !browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<int> selectedModelIndices = browserPaneController_->OrderedSelectedModelIndicesSnapshot();
        if (selectedModelIndices.size() < 2)
        {
            MessageBoxW(hwnd_, L"Select two or more images to batch rename.", L"Batch Rename", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& items = browserModel_->Items();
        std::vector<browser::BrowserItem> selectedItems;
        std::vector<std::wstring> sourcePaths;
        selectedItems.reserve(selectedModelIndices.size());
        sourcePaths.reserve(selectedModelIndices.size());
        for (const int modelIndex : selectedModelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
            {
                continue;
            }

            const browser::BrowserItem& item = items[static_cast<std::size_t>(modelIndex)];
            selectedItems.push_back(item);
            sourcePaths.push_back(item.filePath);
        }

        if (sourcePaths.size() < 2)
        {
            MessageBoxW(hwnd_, L"Select two or more images to batch rename.", L"Batch Rename", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring initialPattern = TrimWhitespaceCopy(fs::path(selectedItems.front().fileName).stem().wstring());
        if (initialPattern.empty())
        {
            initialPattern = L"Image";
        }

        std::vector<std::wstring> targetLeafNames;
        if (!PromptForBatchRenamePattern(hwnd_,
                                         instance_,
                                         appTextSize_,
                                         themeMode_ == ThemeMode::Dark,
                                         std::move(initialPattern),
                                         std::move(selectedItems),
                                         &targetLeafNames))
        {
            return;
        }

        StartFileOperation(services::FileOperationType::Rename,
                           std::move(sourcePaths),
                           {},
                           services::FileConflictPolicy::PromptShell,
                           std::move(targetLeafNames));
    }

    void MainWindow::StartMoveSelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Move Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartSelectionFileOperationToDestination(services::FileOperationType::Move, std::move(destinationFolder));
    }

    void MainWindow::StartMoveSelectionToNewChildFolder()
    {
        if (!browserPaneController_ || !browserModel_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.size() < 2)
        {
            MessageBoxW(hwnd_, L"Select two or more images first.", L"Move to New Child Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring parentPath = NormalizeFolderPath(browserModel_->FolderPath());
        if (parentPath.empty())
        {
            MessageBoxW(hwnd_, L"Open a folder first.", L"Move to New Child Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::error_code parentError;
        if (!fs::is_directory(fs::path(parentPath), parentError) || parentError)
        {
            MessageBoxW(hwnd_, L"The active folder is no longer available.", L"Move to New Child Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::vector<std::wstring> selectedLeafNames;
        selectedLeafNames.reserve(selectedPaths.size());
        for (const std::wstring& selectedPath : selectedPaths)
        {
            const std::wstring leafName = TrimWhitespaceCopy(fs::path(selectedPath).stem().wstring());
            if (!leafName.empty())
            {
                selectedLeafNames.push_back(leafName);
            }
        }

        std::wstring initialFolderName = TrimTrailingFolderNameSeparators(
            TrimWhitespaceCopy(LongestCommonPrefix(selectedLeafNames)));
        std::wstring validationError;
        if (initialFolderName.empty() || !IsValidFolderName(initialFolderName, &validationError))
        {
            initialFolderName = L"New Folder";
        }

        std::wstring folderName;
        while (PromptForSingleLineText(hwnd_,
                                       instance_,
                                       appTextSize_,
                                       themeMode_ == ThemeMode::Dark,
                                       L"Move to New Child Folder",
                                       L"Enter a name for the new child folder.",
                                       L"Create and Move",
                                       initialFolderName,
                                       0,
                                       static_cast<int>(initialFolderName.size()),
                                       &folderName,
                                       L"Parent folder: " + parentPath))
        {
            std::wstring errorMessage;
            if (!IsValidFolderName(folderName, &errorMessage))
            {
                MessageBoxW(hwnd_, errorMessage.c_str(), L"Move to New Child Folder", MB_OK | MB_ICONWARNING);
                continue;
            }

            const std::wstring destinationFolder = NormalizeFolderPath((fs::path(parentPath) / fs::path(folderName)).wstring());
            std::error_code createError;
            if (!fs::create_directory(fs::path(destinationFolder), createError) || createError)
            {
                MessageBoxW(hwnd_,
                            L"Failed to create the folder. Check that the name is unique and that you have permission.",
                            L"Move to New Child Folder",
                            MB_OK | MB_ICONERROR);
                continue;
            }

            StartSelectionFileOperationToDestination(services::FileOperationType::Move, destinationFolder);
            break;
        }
    }

    void MainWindow::StartDeleteSelection(bool permanent)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", permanent ? L"Permanent Delete" : L"Delete", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (ShouldConfirmDeletion(permanent) && !ConfirmFileDeletion(hwnd_, sourcePaths.size(), permanent))
        {
            return;
        }

        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           std::vector<std::wstring>(sourcePaths),
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::StartFolderTreeRename(std::wstring folderPath)
    {
        if (folderPath.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        std::error_code error;
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_, L"The selected folder is no longer available.", L"Rename Folder", MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        const std::wstring currentLeafName = fs::path(folderPath).filename().wstring();
        std::wstring renamedLeafName;
        if (!PromptForRenameLeafName(hwnd_,
                                     instance_,
                                     appTextSize_,
                                     themeMode_ == ThemeMode::Dark,
                                     L"Rename Folder",
                                     L"Enter a new folder name.",
                                     currentLeafName,
                                     false,
                                     &renamedLeafName))
        {
            return;
        }

        error.clear();
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder changed or is no longer available. Refresh the folder tree and try again.",
                        L"Rename Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        activeTreeFolderRenamePath_ = folderPath;
        if (!StartFileOperation(services::FileOperationType::Rename,
                                {folderPath},
                                {},
                                services::FileConflictPolicy::PromptShell,
                                {renamedLeafName}))
        {
            activeTreeFolderRenamePath_.clear();
            pendingInlineRenameOriginalPath_.clear();
            pendingTreeMouseSelectionPath_.clear();
        }
    }

    bool MainWindow::BeginFolderTreeInlineRename(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty() || fileOperationActive_)
        {
            return false;
        }

        HTREEITEM item = FindFolderTreeItemByPath(folderPath);
        if (!item || !TreeView_GetParent(treePane_, item))
        {
            return false;
        }

        TreeView_SelectItem(treePane_, item);
        TreeView_EnsureVisible(treePane_, item);
        return TreeView_EditLabel(treePane_, item) != nullptr;
    }

    void MainWindow::StartFolderTreeDelete(std::wstring folderPath, bool permanent)
    {
        if (folderPath.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));

        std::error_code error;
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        permanent ? L"Permanent Delete Folder" : L"Delete Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        if (ShouldConfirmDeletion(permanent) && !ConfirmFolderDeletion(hwnd_, folderPath, permanent))
        {
            return;
        }

        activeTreeFolderOperationPath_ = folderPath;
        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           {folderPath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::StartCreateNewFolder(std::wstring parentPath)
    {
        if (parentPath.empty() || fileOperationActive_)
        {
            return;
        }

        parentPath = NormalizeFolderPath(std::move(parentPath));

        std::error_code parentError;
        if (!fs::is_directory(fs::path(parentPath), parentError) || parentError)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        L"New Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        std::wstring folderName;
        const std::wstring initialName = L"New Folder";
        while (PromptForSingleLineText(hwnd_,
                                       instance_,
                                       appTextSize_,
                                       themeMode_ == ThemeMode::Dark,
                                       L"New Folder",
                                       L"Enter a name for the new folder.",
                                       L"Create",
                                       initialName,
                                       0,
                                       static_cast<int>(initialName.size()),
                                       &folderName,
                                       L"Parent folder: " + parentPath))
        {
            std::wstring errorMessage;
            if (!IsValidFolderName(folderName, &errorMessage))
            {
                MessageBoxW(hwnd_, errorMessage.c_str(), L"New Folder", MB_OK | MB_ICONWARNING);
                continue;
            }

            const std::wstring newFolderPath = NormalizeFolderPath(
                (fs::path(parentPath) / fs::path(folderName)).wstring());

            std::error_code createError;
            if (!fs::create_directory(fs::path(newFolderPath), createError) || createError)
            {
                MessageBoxW(hwnd_,
                            L"Failed to create the folder. Check that the name is valid and you have permission.",
                            L"New Folder",
                            MB_OK | MB_ICONERROR);
                continue;
            }

            InsertFolderTreeFolderIfParentLoaded(newFolderPath);
            if (browserModel_ && !browserModel_->FolderPath().empty()
                && FolderPathsEqual(browserModel_->FolderPath(), parentPath))
            {
                LoadFolderAsync(parentPath);
            }
            if (folderTreeController_)
            {
                folderTreeController_->SelectFolder(newFolderPath);
            }
            break;
        }
    }

    void MainWindow::StartFolderTreeMoveToDestination(std::wstring folderPath, std::wstring destinationFolder)
    {
        if (folderPath.empty() || destinationFolder.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));

        std::error_code sourceError;
        if (!fs::is_directory(fs::path(folderPath), sourceError) || sourceError)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        L"Move Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        L"Move Folder",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring sourceParentPath = NormalizeFolderPath(
            fs::path(folderPath).parent_path().wstring());
        if (!FolderTreeDropPolicy::IsValid({
            folderPath,
            destinationFolder,
            sourceParentPath,
            true,
            AreFoldersOnSameDrive(folderPath, destinationFolder)}))
        {
            MessageBoxW(hwnd_,
                        L"Choose a valid destination on the same drive. The folder cannot move into itself, one of its children, or its current parent.",
                        L"Move Folder",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        activeTreeFolderMoveSourcePath_ = folderPath;
        activeTreeFolderMoveDestinationFolder_ = destinationFolder;
        StartFileOperation(services::FileOperationType::Move,
                           {folderPath},
                           std::move(destinationFolder),
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    bool MainWindow::StartFileOperation(services::FileOperationType type,
                                        std::vector<std::wstring> sourcePaths,
                                        std::wstring destinationFolder,
                                        services::FileConflictPolicy conflictPolicy,
                                        std::vector<std::wstring> targetLeafNames,
                                        HWND ownerWindow)
    {
        if (!fileOperationService_ || sourcePaths.empty() || fileOperationActive_)
        {
            return false;
        }

        activeFileOperationLabel_ = services::FileOperationTypeToActivityLabel(type);
        activeFileOperationLabel_.append(L" ");
        activeFileOperationLabel_.append(std::to_wstring(sourcePaths.size()));
        activeFileOperationLabel_.append(L" item(s)");
        fileOperationActive_ = true;
        // Remember who was active first, so shell-operation completion can hand
        // activation back when the operation was initiated from the viewer.
        foregroundWindowAtFileOperationStart_ = GetForegroundWindow();
        const HWND currentFocusWindow = GetFocus();
        HWND pendingViewerHwnd = nullptr;
        if (const PendingViewerDelete* activeDelete = viewerPendingOperations_.ActiveDelete())
        {
            pendingViewerHwnd = activeDelete->viewerHwnd;
        }
        else if (const PendingViewerQuickSend* activeQuickSend = viewerPendingOperations_.ActiveQuickSend())
        {
            pendingViewerHwnd = activeQuickSend->viewerHwnd;
        }
        viewer::ViewerWindow* focusedViewer = FindViewerByHwnd(pendingViewerHwnd);
        if (!focusedViewer && currentFocusWindow)
        {
            focusedViewer = FindViewerByHwnd(currentFocusWindow);
        }
        const bool viewerOperationOrigin = focusedViewer != nullptr;
        if (viewerOperationOrigin)
        {
            focusWindowAtFileOperationStart_ = currentFocusWindow
                && (currentFocusWindow == focusedViewer->Hwnd()
                    || IsChild(focusedViewer->Hwnd(), currentFocusWindow))
                ? currentFocusWindow
                : focusedViewer->Hwnd();
        }
        else if (!activeTreeFolderOperationPath_.empty() && treePane_)
        {
            focusWindowAtFileOperationStart_ = treePane_;
        }
        else if (browserPaneController_)
        {
            focusWindowAtFileOperationStart_ = browserPaneController_->Hwnd();
            FocusKeyboardTarget({KeyboardFocusTargetKind::BrowserPane});
        }
        else if (currentFocusWindow
            && (currentFocusWindow == hwnd_ || IsChild(hwnd_, currentFocusWindow)))
        {
            focusWindowAtFileOperationStart_ = currentFocusWindow;
        }
        const HWND operationOwnerWindow = ownerWindow && IsWindow(ownerWindow)
            ? ownerWindow
            : hwnd_;
        activeFileOperationRequestId_ = fileOperationService_->Start(
            hwnd_,
            operationOwnerWindow,
            type,
            std::move(sourcePaths),
            std::move(destinationFolder),
            conflictPolicy,
            std::move(targetLeafNames),
            !viewerOperationOrigin);
        UpdateTaskbarProgress(0, 1); // show the bar immediately (0%) until ticks arrive
        UpdateStatusText();
        UpdateMenuState();
        return true;
    }

    void MainWindow::StartDuplicateSelection()
    {
        if (!browserPaneController_ || !browserModel_ || fileOperationActive_)
        {
            return;
        }

        std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Duplicate", MB_OK | MB_ICONINFORMATION);
            return;
        }

        // Duplicate copies each selection to an available numeric-suffix name.
        const std::wstring destinationFolder = browserModel_->FolderPath();
        services::FileConflictPlan duplicatePlan = services::PlanDestinationConflicts(
            selectedPaths,
            destinationFolder,
            services::FileConflictPolicy::AutoRenameNumericSuffix);
        if (duplicatePlan.renamedCount != selectedPaths.size())
        {
            MessageBoxW(hwnd_,
                        L"Failed to generate unique names for the selected files.",
                        L"Duplicate",
                        MB_OK | MB_ICONERROR);
            return;
        }

        StartFileOperation(services::FileOperationType::Copy,
                           std::move(selectedPaths),
                           destinationFolder,
                           services::FileConflictPolicy::AutoRenameNumericSuffix,
                           std::move(duplicatePlan.targetLeafNames));
    }

    void MainWindow::ShowImageInformationForPath(const std::wstring& filePath, HWND ownerWindow)
    {
        if (!browserPaneController_ || !browserModel_ || filePath.empty())
        {
            return;
        }

        const int modelIndex = browserModel_->FindItemIndexByPath(filePath);
        if (modelIndex < 0)
        {
            MessageBoxW(hwnd_, L"The image is no longer available in the current folder.", L"Image Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (browserPaneController_->PrimarySelectedModelIndex() != modelIndex)
        {
            browserPaneController_->RestoreSelectionByFilePaths({filePath}, filePath);
        }

        ShowImageInformation(ownerWindow);
    }

    void MainWindow::SetSelectionRating(int rating)
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Set Rating", MB_OK | MB_ICONINFORMATION);
            return;
        }

        userMetadataStore_->SetRating(selectedPaths, rating);
        RefreshBrowserPane();
        UpdateMenuState();
    }

    void MainWindow::EditSelectionTags()
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Edit Tags", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring initialTags;
        bool firstEntry = true;
        bool mixedTags = false;
        for (const std::wstring& path : selectedPaths)
        {
            const std::wstring& tags = userMetadataStore_->EntryForPath(path).tags;
            if (firstEntry)
            {
                initialTags = tags;
                firstEntry = false;
                continue;
            }

            if (!util::EqualsIgnoreCaseOrdinal(initialTags, tags))
            {
                mixedTags = true;
                break;
            }
        }

        std::wstring editedTags = mixedTags ? std::wstring{} : initialTags;
        if (!PromptForSingleLineText(hwnd_,
                                     instance_,
                                     appTextSize_,
                                     themeMode_ == ThemeMode::Dark,
                                     L"Edit Tags",
                                     L"Enter comma-separated tags for the selected images. Leave blank to clear tags.",
                                     L"Apply",
                                     editedTags,
                                     0,
                                     -1,
                                     &editedTags))
        {
            return;
        }

        userMetadataStore_->SetTags(selectedPaths, editedTags);
        RefreshBrowserPane();
        UpdateMenuState();
    }

    int MainWindow::CommonSelectionRating() const
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return -1;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            return -1;
        }

        std::vector<int> ratings;
        ratings.reserve(selectedPaths.size());
        for (const std::wstring& path : selectedPaths)
        {
            ratings.push_back(userMetadataStore_->EntryForPath(path).rating);
        }

        return SelectionRatingPolicy::CommonRating(ratings);
    }

    void MainWindow::StartSlideshow(bool selectionScope)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        if (!selectionScope)
        {
            StartFolderSlideshow();
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Slideshow",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        OpenItemsInViewer(std::move(items), 0, true, ShouldDefaultViewerToSecondaryMonitor());
    }

    void MainWindow::StartFolderSlideshow(std::wstring_view preferredPath)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(false);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        L"Open a folder with images first.",
                        L"Slideshow",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        int selectedIndex = -1;
        if (!preferredPath.empty())
        {
            for (int index = 0; index < static_cast<int>(items.size()); ++index)
            {
                if (browser::FilePathsEqual(items[static_cast<std::size_t>(index)].filePath, preferredPath))
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        if (selectedIndex < 0)
        {
            const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
            const auto orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
            for (int index = 0; index < static_cast<int>(orderedModelIndices.size()); ++index)
            {
                if (orderedModelIndices[static_cast<std::size_t>(index)] == primaryModelIndex)
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        if (selectedIndex < 0)
        {
            selectedIndex = 0;
        }

        OpenItemsInViewer(std::move(items), selectedIndex, true, ShouldDefaultViewerToSecondaryMonitor());
    }

    void MainWindow::StartBatchConvert(bool selectionScope, services::BatchConvertFormat format)
    {
        if (!batchConvertService_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Batch Convert",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring outputFolder;
        if (!ChooseFolder(&outputFolder) || outputFolder.empty())
        {
            return;
        }

        batchConvertOutputFolder_ = outputFolder;
        batchConvertCompleted_ = 0;
        batchConvertTotal_ = items.size();
        batchConvertFailed_ = 0;
        batchConvertCurrentFile_.clear();
        batchConvertActive_ = true;
        batchConvertCancelling_ = false;
        activeBatchConvertRequestId_ = batchConvertService_->Start(hwnd_, std::move(items), outputFolder, format);
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::AdjustSelectedJpegOrientation(int quarterTurnsDelta)
    {
        if (!browserModel_ || !browserPaneController_ || !imageCommandService_)
        {
            return;
        }

        const std::vector<browser::BrowserItem> items = CollectItemsForScope(true);
        if (items.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more JPEG images first.", L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const bool hasJpeg = std::any_of(items.begin(), items.end(), [](const browser::BrowserItem& item)
        {
            return _wcsicmp(item.fileType.c_str(), L"JPG") == 0
                || _wcsicmp(item.fileType.c_str(), L"JPEG") == 0;
        });
        if (!hasJpeg)
        {
            MessageBoxW(hwnd_, L"Select one or more JPEG images first.", L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
            return;
        }

        imageCommandActive_ = true;
        imageCommandCancelling_ = false;
        imageCommandActivityLabel_ = L"Adjusting JPEG orientation";
        activeImageCommandRequestId_ = imageCommandService_->StartJpegOrientation(
            hwnd_,
            hwnd_,
            items,
            quarterTurnsDelta);
        UpdateStatusText();
    }

    void MainWindow::RecordUndoableOperation(const services::FileOperationUpdate& update)
    {
        // Recycle-bin deletes are already undoable via the shell (FOF_ALLOWUNDO), and
        // permanent deletes cannot be undone; only journal Copy / Move / Rename.
        if (applyingUndoRedo_)
        {
            return;
        }

        const auto type = update.type;
        const bool isCopy = type == services::FileOperationType::Copy;
        const bool isMove = type == services::FileOperationType::Move;
        const bool isRename = type == services::FileOperationType::Rename;
        if (!isCopy && !isMove && !isRename)
        {
            return;
        }

        // Overwrite destroys the prior destination contents. Without a backup there is
        // no safe inverse, so do not advertise that operation as undoable.
        if (update.conflictPolicy == services::FileConflictPolicy::OverwriteExisting)
        {
            return;
        }

        if (update.succeededSourcePaths.empty())
        {
            return;
        }

        FileOperationJournalEntry operation;
        operation.type = static_cast<int>(type);
        operation.sourcePaths = update.succeededSourcePaths;
        operation.createdPaths = update.createdPaths;
        operation.undoLeafNames = FileLeafNames(operation.sourcePaths);
        operation.redoLeafNames = FileLeafNames(operation.createdPaths);
        operation.destinationFolder = update.destinationFolder;
        operation.description = services::FileOperationTypeToActivityLabel(type);

        // Every reversible operation needs a complete source/destination mapping.
        // Without it, an inverse could target an unrelated source path.
        if (operation.createdPaths.size() != operation.sourcePaths.size())
        {
            return;
        }

        operation.createdStates = CaptureFileOperationPathStates(operation.createdPaths);
        if (!AllFileOperationPathStatesValid(operation.createdStates))
        {
            return;
        }
        if (isCopy)
        {
            operation.sourceStates = CaptureFileOperationPathStates(operation.sourcePaths);
            if (!AllFileOperationPathStatesValid(operation.sourceStates))
            {
                return;
            }
        }

        if (isMove)
        {
            const std::wstring sourceFolder = NormalizeFolderPath(
                fs::path(operation.sourcePaths.front()).parent_path().wstring());
            if (sourceFolder.empty()
                || std::any_of(operation.sourcePaths.begin() + 1,
                               operation.sourcePaths.end(),
                               [&](const std::wstring& sourcePath)
                               {
                                   return !FolderPathsEqual(
                                       NormalizeFolderPath(fs::path(sourcePath).parent_path().wstring()),
                                       sourceFolder);
                               }))
            {
                return;
            }
        }

        fileOperationJournal_.Record(std::move(operation));
        UpdateUndoRedoMenuState();
    }

    void MainWindow::PerformUndo()
    {
        const FileOperationJournalEntry* operation = fileOperationJournal_.UndoEntry();
        if (!operation || fileOperationActive_)
        {
            return;
        }

        const auto type = static_cast<services::FileOperationType>(operation->type);
        std::vector<std::wstring> undoSources;
        std::wstring undoDestination;
        std::vector<std::wstring> undoLeafNames;
        bool started = false;

        std::wstring changedPath;
        if (!FileOperationPathStatesMatch(operation->createdPaths, operation->createdStates, &changedPath))
        {
            std::wstring message = L"HyperBrowse will not undo this operation because a result changed outside the application.";
            if (!changedPath.empty())
            {
                message.append(L"\n\nChanged path:\n");
                message.append(changedPath);
            }
            MessageBoxW(hwnd_, message.c_str(), L"Undo is no longer safe", MB_OK | MB_ICONWARNING);
            fileOperationJournal_.Discard(UndoRedoOperation::Undo);
            UpdateUndoRedoMenuState();
            return;
        }

        if (type == services::FileOperationType::Copy)
        {
            // Undo a copy = delete the created copies (recycle bin for safety).
            if (operation->createdPaths.empty())
            {
                return;
            }
            undoSources = operation->createdPaths;
            applyingUndoRedo_ = true;
            fileOperationJournal_.Begin(UndoRedoOperation::Undo);
            started = StartFileOperation(services::FileOperationType::DeleteRecycleBin,
                                         undoSources,
                                         {},
                                         services::FileConflictPolicy::PromptShell,
                                         {});
        }
        else if (type == services::FileOperationType::Move)
        {
            // Undo a move = move the created files back to the original folder.
            // createdPaths[i] is the post-move location; move each back beside the
            // original source's parent folder.
            if (operation->createdPaths.empty())
            {
                return;
            }
            const std::wstring originalFolder = fs::path(operation->sourcePaths.front()).parent_path().wstring();
            applyingUndoRedo_ = true;
            fileOperationJournal_.Begin(UndoRedoOperation::Undo);
            started = StartFileOperation(services::FileOperationType::Move,
                                         operation->createdPaths,
                                         originalFolder,
                                         services::FileConflictPolicy::PromptShell,
                                         operation->undoLeafNames);
        }
        else // Rename
        {
            // Undo a rename = rename back to the original leaf name.
            applyingUndoRedo_ = true;
            fileOperationJournal_.Begin(UndoRedoOperation::Undo);
            started = StartFileOperation(services::FileOperationType::Rename,
                                         operation->createdPaths,
                                         {},
                                         services::FileConflictPolicy::PromptShell,
                                         operation->undoLeafNames);
        }

        if (!started)
        {
            applyingUndoRedo_ = false;
            fileOperationJournal_.CancelPending();
        }
        UpdateUndoRedoMenuState();
    }

    void MainWindow::PerformRedo()
    {
        const FileOperationJournalEntry* operation = fileOperationJournal_.RedoEntry();
        if (!operation || fileOperationActive_)
        {
            return;
        }

        const auto type = static_cast<services::FileOperationType>(operation->type);
        std::wstring changedPath;
        if (!FileOperationPathStatesMatch(operation->sourcePaths, operation->sourceStates, &changedPath))
        {
            std::wstring message = L"HyperBrowse will not redo this operation because a source changed outside the application.";
            if (!changedPath.empty())
            {
                message.append(L"\n\nChanged path:\n");
                message.append(changedPath);
            }
            MessageBoxW(hwnd_, message.c_str(), L"Redo is no longer safe", MB_OK | MB_ICONWARNING);
            fileOperationJournal_.Discard(UndoRedoOperation::Redo);
            UpdateUndoRedoMenuState();
            return;
        }
        applyingUndoRedo_ = true;
        fileOperationJournal_.Begin(UndoRedoOperation::Redo);
        bool started = false;
        if (type == services::FileOperationType::Copy)
        {
            started = StartFileOperation(services::FileOperationType::Copy,
                                         operation->sourcePaths,
                                         operation->destinationFolder,
                                         services::FileConflictPolicy::PromptShell,
                                         operation->redoLeafNames);
        }
        else if (type == services::FileOperationType::Move)
        {
            started = StartFileOperation(services::FileOperationType::Move,
                                         operation->sourcePaths,
                                         operation->destinationFolder,
                                         services::FileConflictPolicy::PromptShell,
                                         operation->redoLeafNames);
        }
        else // Rename: redo renames back to the new leaf names.
        {
            started = StartFileOperation(services::FileOperationType::Rename,
                                         operation->sourcePaths,
                                         {},
                                         services::FileConflictPolicy::PromptShell,
                                         operation->redoLeafNames);
        }

        if (!started)
        {
            applyingUndoRedo_ = false;
            fileOperationJournal_.CancelPending();
        }
        UpdateUndoRedoMenuState();
    }

    void MainWindow::UpdateUndoRedoMenuState()
    {
        if (!menu_)
        {
            return;
        }

        const bool canUndo = fileOperationJournal_.CanUndo() && !fileOperationActive_;
        const bool canRedo = fileOperationJournal_.CanRedo() && !fileOperationActive_;
        EnableMenuItem(menu_, ID_EDIT_UNDO, MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_EDIT_REDO, MF_BYCOMMAND | (canRedo ? MF_ENABLED : MF_GRAYED));
    }

    MainWindow::FileOperationCompletionContext MainWindow::CaptureFileOperationCompletionContext()
    {
        FileOperationCompletionContext context;
        context.undoRedo.completedOperation = fileOperationJournal_.PendingOperation();
        fileOperationActive_ = false;
        activeFileOperationLabel_.clear();
        applyingUndoRedo_ = false;

        context.activation.activationRestoreWindow = foregroundWindowAtFileOperationStart_;
        foregroundWindowAtFileOperationStart_ = nullptr;
        context.activation.focusRestoreWindow = focusWindowAtFileOperationStart_;
        focusWindowAtFileOperationStart_ = nullptr;

        if (const std::optional<PendingViewerDelete> activeDelete = viewerPendingOperations_.TakeActiveDelete())
        {
            context.viewer.viewerHwnd = activeDelete->viewerHwnd;
            context.viewer.viewerDeleteSourcePath = activeDelete->sourcePath;
            context.viewer.viewerDeleteSourcePaths = activeDelete->sourcePaths;
            context.viewer.viewerDeletePreferredFocusPath = activeDelete->preferredFocusPath;
        }
        if (const std::optional<PendingViewerQuickSend> quickSend = viewerPendingOperations_.TakeQuickSend())
        {
            context.viewer.viewerHwnd = quickSend->viewerHwnd;
            context.viewer.viewerQuickSend = *quickSend;
        }
        if (pendingBrowserQuickSend_)
        {
            context.browserQuickSend = std::move(pendingBrowserQuickSend_);
            pendingBrowserQuickSend_.reset();
        }

        if (folderLoadCoordinator_)
        {
            const FolderLoadCoordinator::DeferredWatchEffects deferredWatchEffects =
                folderLoadCoordinator_->TakeDeferredWatchEffects();
            context.deferredWatch.deferredFolderWatchReloadPath = std::move(deferredWatchEffects.reloadPath);
            context.deferredWatch.deferredFolderWatchTreeRefresh = deferredWatchEffects.treeRefresh;
        }

        context.tree.treeFolderOperationPath = activeTreeFolderOperationPath_;
        activeTreeFolderOperationPath_.clear();
        context.tree.treeFolderRenamePath = activeTreeFolderRenamePath_;
        activeTreeFolderRenamePath_.clear();
        context.tree.treeFolderMoveSourcePath = activeTreeFolderMoveSourcePath_;
        activeTreeFolderMoveSourcePath_.clear();
        context.tree.treeFolderMoveDestinationFolder = activeTreeFolderMoveDestinationFolder_;
        activeTreeFolderMoveDestinationFolder_.clear();
        return context;
    }

    bool MainWindow::ApplyViewerFileOperationEffects(
        const services::FileOperationUpdate& update,
        const FileOperationCompletionContext& completionContext,
        bool viewerDeleteOperation,
        bool viewerQuickSendOperation)
    {
        bool viewerCloseRequested = false;
        const FileOperationViewerContext& viewerContext = completionContext.viewer;
        const std::wstring& viewerDeleteSourcePath = viewerContext.viewerDeleteSourcePath;
        const std::vector<std::wstring>& viewerDeleteSourcePaths = viewerContext.viewerDeleteSourcePaths;
        const std::wstring& viewerDeletePreferredFocusPath = viewerContext.viewerDeletePreferredFocusPath;
        const PendingViewerQuickSend& viewerQuickSend = viewerContext.viewerQuickSend;

        if (viewerDeleteOperation)
        {
            const auto deletePathSucceeded = [&](const std::wstring& sourcePath)
            {
                const bool deleteSucceeded = std::any_of(
                    update.succeededSourcePaths.begin(),
                    update.succeededSourcePaths.end(),
                    [&](const std::wstring& succeededPath)
                    {
                        return browser::FilePathsEqual(succeededPath, sourcePath);
                    });
                if (deleteSucceeded)
                {
                    return true;
                }

                std::error_code error;
                const bool exists = fs::exists(fs::path(sourcePath), error);
                return !update.aborted && !exists && !error;
            };
            const bool viewerDeleteSucceeded = deletePathSucceeded(viewerDeleteSourcePath);

            bool browserModelChanged = false;
            if (browserModel_)
            {
                const std::vector<std::wstring>& deletedPaths = viewerDeleteSourcePaths.empty()
                    ? std::vector<std::wstring>{viewerDeleteSourcePath}
                    : viewerDeleteSourcePaths;
                for (const std::wstring& deletedPath : deletedPaths)
                {
                    if (deletedPath.empty() || !deletePathSucceeded(deletedPath))
                    {
                        continue;
                    }

                    browserModelChanged = browserModel_->RemoveItemByPath(deletedPath) || browserModelChanged;
                }
            }

            if (browserModelChanged)
            {
                // The incremental update above already refreshed the browser pane
                // for anything it removed; this only covers items it could not see
                // (e.g. no browser pane controller), so just refresh the title.
                UpdateWindowTitle();
            }

            viewer::ViewerWindow* originViewer = FindViewerByHwnd(viewerContext.viewerHwnd);
            if (originViewer && originViewer->IsOpen())
            {
                if (viewerDeleteSucceeded && viewerDeletePreferredFocusPath.empty())
                {
                    const HWND viewerHwnd = originViewer->Hwnd();
                    if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
                    {
                        viewerCloseRequested = true;
                        PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
                    }
                }
                else if (!viewerDeleteSucceeded)
                {
                    // The delete failed (or the file no longer exists for another
                    // reason); resync the viewer to the actual model state since
                    // AdvanceAfterDeleteCurrent optimistically advanced past an item
                    // that, in fact, was never removed.
                    SyncViewerToBrowserModel(*originViewer, viewerDeleteSourcePath);
                }
                // else: the delete succeeded and the viewer already advanced past the
                // deleted item locally (see AdvanceAfterDeleteCurrent, called at
                // keypress time). Calling SyncViewerToBrowserModel here would rebuild
                // the viewer's item list and force ReplaceItems() to blank the display
                // and fully re-decode the already-visible image - visible as a
                // delayed flash every time a delete completes. Skip it.
            }
            else if (!viewerDeleteSucceeded && browserModel_)
            {
                const int modelIndex = browserModel_->FindItemIndexByPath(viewerDeleteSourcePath);
                if (modelIndex >= 0)
                {
                    OpenItemInViewer(modelIndex);
                }
            }
        }

        if (viewerQuickSendOperation && viewerQuickSend.type == services::FileOperationType::Move)
        {
            const bool primaryPathSucceeded = std::any_of(
                update.succeededSourcePaths.begin(),
                update.succeededSourcePaths.end(),
                [&](const std::wstring& succeededPath)
                {
                    return browser::FilePathsEqual(succeededPath, viewerQuickSend.sourcePath);
                });
            const bool primaryPathMissingAfterOperation = [&]()
            {
                if (update.aborted || primaryPathSucceeded)
                {
                    return false;
                }

                std::error_code error;
                return !fs::exists(fs::path(viewerQuickSend.sourcePath), error) && !error;
            }();
            const bool viewerQuickSendSucceeded = primaryPathSucceeded || primaryPathMissingAfterOperation;

            if (!viewerQuickSendSucceeded)
            {
                viewer::ViewerWindow* originViewer = FindViewerByHwnd(viewerQuickSend.viewerHwnd);
                if (originViewer && originViewer->IsOpen())
                {
                    SyncViewerToBrowserModel(*originViewer, viewerQuickSend.sourcePath);
                }
                else if (browserModel_)
                {
                    const int sourceModelIndex = browserModel_->FindItemIndexByPath(viewerQuickSend.sourcePath);
                    if (sourceModelIndex >= 0)
                    {
                        OpenItemInViewer(sourceModelIndex);
                    }
                }
            }
        }

        return viewerCloseRequested;
    }

    void MainWindow::ApplyBrowserFileOperationEffects(
        const services::FileOperationUpdate& update,
        const FileOperationCompletionContext& completionContext,
        const FileOperationTreeEffects& treeEffects,
        bool reloadCurrentFolder,
        bool viewerDeleteOperation,
        bool viewerQuickSendOperation,
        const std::wstring& fallbackFolderPath)
    {
        const FileOperationTreeContext& treeContext = completionContext.tree;
        const std::wstring& treeFolderOperationPath = treeContext.treeFolderOperationPath;
        const std::wstring& treeFolderRenamePath = treeContext.treeFolderRenamePath;
        const std::wstring& treeFolderMoveSourcePath = treeContext.treeFolderMoveSourcePath;

        if (!reloadCurrentFolder && browserModel_ && browserPaneController_)
        {
            std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

            // Deleting the whole selection leaves nothing selected, which drops
            // keyboard focus entirely. Remember the nearest surviving neighbour so
            // focus can land there once the removed items are gone.
            const std::wstring deleteFallbackFocusPath = FindDeleteFallbackFocusPath(
                update,
                viewerDeleteOperation,
                browserModel_.get(),
                browserPaneController_.get());

            if (update.type == services::FileOperationType::Rename)
            {
                const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
                for (std::size_t index = 0; index < renamePairCount; ++index)
                {
                    const std::wstring& sourcePath = update.succeededSourcePaths[index];
                    const std::wstring& createdPath = update.createdPaths[index];
                    for (std::wstring& selectedPath : selectedPaths)
                    {
                        if (browser::FilePathsEqual(selectedPath, sourcePath))
                        {
                            selectedPath = createdPath;
                        }
                    }

                    if (browser::FilePathsEqual(focusedPath, sourcePath))
                    {
                        focusedPath = createdPath;
                    }
                }
            }

            bool modelChanged = false;
            auto upsertVisiblePath = [&](const std::wstring& path)
            {
                if (!IsPathInCurrentScope(path))
                {
                    return;
                }

                const fs::path filePath(path);
                std::error_code error;
                if (fs::is_directory(filePath, error) && !error)
                {
                    if (!showSubfoldersInBrowser_
                        || !FolderPathsEqual(filePath.parent_path().wstring(), browserModel_->FolderPath()))
                    {
                        return;
                    }

                    modelChanged = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(filePath)) || modelChanged;
                    return;
                }

                if (!browser::IsSupportedImageExtension(filePath.extension().wstring()))
                {
                    return;
                }

                if (!fs::is_regular_file(filePath, error) || error)
                {
                    return;
                }

                modelChanged = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(filePath)) || modelChanged;
            };

            switch (update.type)
            {
            case services::FileOperationType::Copy:
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::Move:
                for (const std::wstring& sourcePath : update.succeededSourcePaths)
                {
                    const bool removeByPrefix = treeEffects.treeFolderMoveOperation
                        && FolderPathsEqual(sourcePath, treeFolderMoveSourcePath);
                    modelChanged = (removeByPrefix
                        ? browserModel_->RemoveItemsByPathPrefix(sourcePath)
                        : browserModel_->RemoveItemByPath(sourcePath)) || modelChanged;
                }
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::DeleteRecycleBin:
            case services::FileOperationType::DeletePermanent:
                if (treeEffects.treeFolderDeleteOperation)
                {
                    for (const std::wstring& sourcePath : update.succeededSourcePaths)
                    {
                        modelChanged = (FolderPathsEqual(sourcePath, treeFolderOperationPath)
                            ? browserModel_->RemoveItemsByPathPrefix(sourcePath)
                            : browserModel_->RemoveItemByPath(sourcePath)) || modelChanged;
                    }
                }
                else
                {
                    modelChanged = browserModel_->RemoveItemsByPath(update.succeededSourcePaths) || modelChanged;
                }
                break;
            case services::FileOperationType::Rename:
            {
                const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
                for (std::size_t index = 0; index < renamePairCount; ++index)
                {
                    const std::wstring& sourcePath = update.succeededSourcePaths[index];
                    const std::wstring& createdPath = update.createdPaths[index];
                    if (!treeFolderRenamePath.empty() && FolderPathsEqual(sourcePath, treeFolderRenamePath))
                    {
                        continue;
                    }

                    modelChanged = browserModel_->RemoveItemByPath(sourcePath) || modelChanged;
                    upsertVisiblePath(createdPath);
                }
                break;
            }
            default:
                break;
            }

            const std::vector<std::wstring> affectedPaths = BuildMediaCacheInvalidationPaths(
                update,
                browserModel_->FolderPath(),
                [this](std::wstring_view path)
                {
                    return IsPathInCurrentScope(path);
                });
            if (!affectedPaths.empty())
            {
                util::ScopedTimer invalidateTimer(L"ApplyCompletedFileOperation InvalidateMediaCacheForPaths");
                browserPaneController_->InvalidateMediaCacheForPaths(affectedPaths);
            }

            if (modelChanged && fallbackFolderPath.empty())
            {
                util::ScopedTimer refreshTimer(L"ApplyCompletedFileOperation RefreshBrowserPane");
                RefreshBrowserPane();
                browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
                if (!deleteFallbackFocusPath.empty() && browserPaneController_->SelectedCount() == 0)
                {
                    browserPaneController_->RestoreSelectionByFilePaths({deleteFallbackFocusPath}, deleteFallbackFocusPath);
                    browserPaneController_->EnsureFocusedItemVisible();
                }
                UpdateWindowTitle();

                // Keep an open viewer consistent with the browser model after a
                // browser-initiated operation. Viewer deletes are excluded: the
                // viewer already advanced locally and re-syncing it here would
                // blank and re-decode the image that is already on screen.
                if (!viewerDeleteOperation
                    && !viewerQuickSendOperation
                    && viewerWindow_
                    && viewerWindow_->IsOpen())
                {
                    SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
                }
            }
        }
    }

    void MainWindow::ApplyCompletedFileOperation(const services::FileOperationUpdate& update)
    {
        util::ScopedTimer applyTimer(L"MainWindow::ApplyCompletedFileOperation");
        const FileOperationCompletionContext completionContext = CaptureFileOperationCompletionContext();
        const UndoRedoOperation completedUndoRedoOperation = completionContext.undoRedo.completedOperation;
        const HWND activationRestoreWindow = completionContext.activation.activationRestoreWindow;
        const HWND focusRestoreWindow = completionContext.activation.focusRestoreWindow;

        if (completedUndoRedoOperation == UndoRedoOperation::None)
        {
            RecordUndoableOperation(update);
        }
        for (const std::wstring& path : update.succeededSourcePaths)
        {
            InvalidateFolderTreeChildPresence(path);
        }
        for (const std::wstring& path : update.createdPaths)
        {
            InvalidateFolderTreeChildPresence(path);
        }

        const std::wstring& viewerDeleteSourcePath = completionContext.viewer.viewerDeleteSourcePath;

        const PendingViewerQuickSend& viewerQuickSend = completionContext.viewer.viewerQuickSend;
        const bool viewerQuickSendOperation = viewerQuickSend.active
            && viewerQuickSend.type == update.type;

        // Only deletes that originated in the viewer get the "do not re-enumerate"
        // treatment below; browser-initiated deletes must still update the model.
        const bool viewerDeleteOperation = !viewerDeleteSourcePath.empty()
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);
        const bool browserItemDeleteOperation = !viewerDeleteOperation
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);

        const bool deferredFolderWatchTreeRefresh = completionContext.deferredWatch.deferredFolderWatchTreeRefresh;

        const std::wstring& treeFolderRenamePath = completionContext.tree.treeFolderRenamePath;
        const std::wstring& treeFolderMoveSourcePath = completionContext.tree.treeFolderMoveSourcePath;
        const FileOperationTreeEffects treeEffects = BuildFileOperationTreeEffects(
            update,
            completionContext.tree.treeFolderOperationPath,
            completionContext.tree.treeFolderRenamePath,
            completionContext.tree.treeFolderMoveSourcePath,
            completionContext.tree.treeFolderMoveDestinationFolder,
            browserModel_.get());
        const bool treeFolderMoveSucceeded = treeEffects.treeFolderMoveSucceeded;
        const bool treeFolderRenameSucceeded = treeEffects.treeFolderRenameSucceeded;
        bool refreshFolderTree = treeEffects.refreshFolderTree;
        std::wstring fallbackFolderPath = treeEffects.fallbackFolderPath;
        std::wstring treeFolderReloadPath = treeEffects.treeFolderReloadPath;
        const std::wstring& treeFolderMoveCreatedPath = treeEffects.treeFolderMoveCreatedPath;
        const std::wstring& treeFolderRenameCreatedPath = treeEffects.treeFolderRenameCreatedPath;

        if (treeFolderRenameSucceeded && !pendingTreeMouseSelectionPath_.empty())
        {
            if (FolderPathsEqual(pendingTreeMouseSelectionPath_, treeFolderRenamePath)
                || browser::PathHasPrefix(pendingTreeMouseSelectionPath_, treeFolderRenamePath))
            {
                pendingTreeMouseSelectionPath_ = RewritePathPrefix(
                    pendingTreeMouseSelectionPath_,
                    treeFolderRenamePath,
                    treeFolderRenameCreatedPath);
            }

            treeFolderReloadPath.clear();
            if (!browserModel_
                || browserModel_->FolderPath().empty()
                || !FolderPathsEqual(browserModel_->FolderPath(), pendingTreeMouseSelectionPath_))
            {
                treeFolderReloadPath = pendingTreeMouseSelectionPath_;
            }
        }

        if (treeFolderMoveSucceeded && browserModel_ && !browserModel_->FolderPath().empty())
        {
            if (browser::PathHasPrefix(browserModel_->FolderPath(), treeFolderMoveSourcePath))
            {
                treeFolderReloadPath = RewritePathPrefix(
                    browserModel_->FolderPath(),
                    treeFolderMoveSourcePath,
                    treeFolderMoveCreatedPath);
            }
        }

        if (userMetadataStore_)
        {
            userMetadataStore_->ApplyFileOperationUpdate(update.type, update.succeededSourcePaths, update.createdPaths);
        }

        const bool reloadCurrentFolder = ShouldReloadCurrentFolderForFileOperation(
            update,
            browserModel_.get(),
            completionContext.deferredWatch.deferredFolderWatchReloadPath,
            treeEffects,
            viewerDeleteOperation,
            browserItemDeleteOperation,
            [this](std::wstring_view path)
            {
                return IsPathInCurrentScope(path);
            });

        ApplyBrowserFileOperationEffects(
            update,
            completionContext,
            treeEffects,
            reloadCurrentFolder,
            viewerDeleteOperation,
            viewerQuickSendOperation,
            fallbackFolderPath);

        if ((update.type == services::FileOperationType::Copy || update.type == services::FileOperationType::Move)
            && !update.destinationFolder.empty())
        {
            std::error_code destinationError;
            if (fs::is_directory(fs::path(update.destinationFolder), destinationError) && !destinationError)
            {
                if (!update.succeededSourcePaths.empty() || !update.createdPaths.empty())
                {
                    RecordRecentDestination(update.destinationFolder);
                }
                InsertFolderTreeFolderIfParentLoaded(update.destinationFolder);
            }
        }

        const bool viewerCloseRequested = ApplyViewerFileOperationEffects(
            update,
            completionContext,
            viewerDeleteOperation,
            viewerQuickSendOperation);

        if (viewerQuickSendOperation)
        {
            const PendingViewerQuickSend& quickSend = completionContext.viewer.viewerQuickSend;
            bool primaryPathSucceeded = std::any_of(
                update.succeededSourcePaths.begin(),
                update.succeededSourcePaths.end(),
                [&](const std::wstring& succeededPath)
                {
                    return browser::FilePathsEqual(succeededPath, quickSend.sourcePath);
                });
            if (!primaryPathSucceeded
                && quickSend.type == services::FileOperationType::Move
                && !update.aborted)
            {
                std::error_code error;
                primaryPathSucceeded = !fs::exists(fs::path(quickSend.sourcePath), error) && !error;
            }

            if (const std::optional<std::wstring> confirmation = BuildQuickSendConfirmation({
                    quickSend.type,
                    1,
                    primaryPathSucceeded ? 1U : 0U,
                    quickSend.sourcePath,
                    quickSend.destinationFolder,
                    quickSend.destinationShortcut}); confirmation)
            {
                ShowQuickSendConfirmation(*confirmation, quickSend.viewerHwnd);
            }
        }
        else if (completionContext.browserQuickSend)
        {
            const PendingBrowserQuickSend& quickSend = *completionContext.browserQuickSend;
            const std::wstring displaySourcePath = quickSend.sourcePaths.size() == 1
                ? quickSend.sourcePaths.front()
                : std::wstring{};
            if (const std::optional<std::wstring> confirmation = BuildQuickSendConfirmation({
                    quickSend.type,
                    quickSend.sourcePaths.size(),
                    update.succeededSourcePaths.size(),
                    displaySourcePath,
                    quickSend.destinationFolder,
                    quickSend.destinationShortcut}); confirmation)
            {
                ShowQuickSendConfirmation(*confirmation);
            }
        }

        if (viewerQuickSendOperation
            && !update.aborted
            && update.failedCount == 0
            && std::any_of(
                update.succeededSourcePaths.begin(),
                update.succeededSourcePaths.end(),
                [&](const std::wstring& succeededPath)
                {
                    return browser::FilePathsEqual(succeededPath, viewerQuickSend.sourcePath);
                }))
        {
            RecordFilingResume(FilingResumeRecord{
                viewerQuickSend.resumeFolderPath,
                viewerQuickSend.resumeFolderIdentity,
                viewerQuickSend.resumeTargetPath,
                viewerQuickSend.resumeTargetIdentity,
                static_cast<int>(viewerQuickSend.type)});
        }

        if (refreshFolderTree || deferredFolderWatchTreeRefresh)
        {
            RefreshFolderTree();
        }

        pendingInlineRenameOriginalPath_.clear();
        pendingTreeMouseSelectionPath_.clear();

        if (!fallbackFolderPath.empty())
        {
            LoadFolderAsync(fallbackFolderPath);
        }
        else if (!treeFolderReloadPath.empty())
        {
            LoadFolderAsync(treeFolderReloadPath);
        }
        else if (reloadCurrentFolder && browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
        }

        if (completedUndoRedoOperation != UndoRedoOperation::None)
        {
            bool undoRedoSucceeded = update.finished
                && !update.aborted
                && update.failedCount == 0
                && update.succeededSourcePaths.size() == update.requestedCount
                && (update.type == services::FileOperationType::DeleteRecycleBin
                    || update.createdPaths.size() == update.requestedCount);

            std::optional<FileOperationJournalEntry> completedEntry;
            const FileOperationJournalEntry* pendingEntry = completedUndoRedoOperation == UndoRedoOperation::Undo
                ? fileOperationJournal_.UndoEntry()
                : fileOperationJournal_.RedoEntry();
            if (undoRedoSucceeded && pendingEntry)
            {
                completedEntry = *pendingEntry;
                const auto originalType = static_cast<services::FileOperationType>(completedEntry->type);
                if (completedUndoRedoOperation == UndoRedoOperation::Undo)
                {
                    if (originalType == services::FileOperationType::Move
                        || originalType == services::FileOperationType::Rename)
                    {
                        completedEntry->sourcePaths = update.createdPaths;
                        completedEntry->sourceStates = CaptureFileOperationPathStates(completedEntry->sourcePaths);
                        completedEntry->undoLeafNames = FileLeafNames(completedEntry->sourcePaths);
                        undoRedoSucceeded = AllFileOperationPathStatesValid(completedEntry->sourceStates);
                    }
                }
                else
                {
                    completedEntry->sourcePaths = update.succeededSourcePaths;
                    completedEntry->createdPaths = update.createdPaths;
                    completedEntry->createdStates = CaptureFileOperationPathStates(completedEntry->createdPaths);
                    completedEntry->undoLeafNames = FileLeafNames(completedEntry->sourcePaths);
                    completedEntry->redoLeafNames = FileLeafNames(completedEntry->createdPaths);
                    undoRedoSucceeded = AllFileOperationPathStatesValid(completedEntry->createdStates);
                    if (originalType == services::FileOperationType::Copy)
                    {
                        completedEntry->sourceStates = CaptureFileOperationPathStates(completedEntry->sourcePaths);
                        undoRedoSucceeded = undoRedoSucceeded
                            && AllFileOperationPathStatesValid(completedEntry->sourceStates);
                    }
                    else
                    {
                        completedEntry->sourceStates.clear();
                    }
                }
            }

            if (undoRedoSucceeded)
            {
                fileOperationJournal_.Complete(
                    completedUndoRedoOperation,
                    true,
                    std::move(completedEntry));
            }
            else if (update.finished && !update.aborted && update.failedCount == 0)
            {
                // The filesystem operation completed but its identity could not be
                // captured. Drop the stale history entry instead of offering an unsafe
                // inverse on a later pathname match.
                fileOperationJournal_.Discard(completedUndoRedoOperation);
            }
            else
            {
                fileOperationJournal_.Complete(completedUndoRedoOperation, false);
            }
            applyingUndoRedo_ = false;
        }

        UpdateStatusText();
        UpdateMenuState();

        if (!update.message.empty() && update.failedCount > 0)
        {
            MessageBoxW(hwnd_, update.message.c_str(), L"File Operation", MB_OK | MB_ICONWARNING);
        }

        // Hand activation back to whichever of our windows the user was working in when
        // the operation began. Without this a viewer-initiated delete leaves the main
        // window active and the viewer stops responding to the keyboard until clicked.
        if (activationRestoreWindow
            && !viewerCloseRequested
            && activationRestoreWindow != hwnd_
            && IsWindow(activationRestoreWindow) != FALSE
            && IsWindowVisible(activationRestoreWindow) != FALSE
            && FindViewerByHwnd(activationRestoreWindow)
            && GetForegroundWindow() != activationRestoreWindow)
        {
            util::LogInfo(L"ApplyCompletedFileOperation restoring viewer activation");
            SetForegroundWindow(activationRestoreWindow);
            SetFocus(activationRestoreWindow);
        }

        if (!viewerCloseRequested
            && focusRestoreWindow
            && focusRestoreWindow != activationRestoreWindow
            && IsWindow(focusRestoreWindow) != FALSE
            && IsWindowVisible(focusRestoreWindow) != FALSE
            && GetForegroundWindow() == hwnd_
            && (GetFocus() == hwnd_ || !GetFocus()))
        {
            SetFocus(focusRestoreWindow);
        }

        // Dispatch the next queued viewer delete, if any, now that the file
        // operation slot is free.  Use the viewer's current path as the sync
        // target so we never navigate the viewer back to an image it has already
        // advanced past.
        if (closePending_)
        {
            if (closeWaitNoticeShown_)
            {
                closeWaitNoticeShown_ = false;
            }
            if (hwnd_)
            {
                KillTimer(hwnd_, kFileOperationShutdownTimerId);
            }
            closePendingSinceTick_ = 0;
            viewerPendingOperations_.ClearQueuedDeletes();
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        else if (const std::optional<PendingViewerDelete> nextRequest = viewerPendingOperations_.TakeNextDelete())
        {
            PendingViewerDelete next = *nextRequest;
            viewer::ViewerWindow* nextViewer = FindViewerByHwnd(next.viewerHwnd);
            next.preferredFocusPath = nextViewer && nextViewer->IsOpen()
                ? nextViewer->CurrentFilePath()
                : next.preferredFocusPath;
            std::vector<std::wstring> sourcePaths = next.sourcePaths;
            viewerPendingOperations_.SetActiveDelete(std::move(next));
            const PendingViewerDelete* activeDelete = viewerPendingOperations_.ActiveDelete();
            StartFileOperation(
                activeDelete && activeDelete->permanent
                    ? services::FileOperationType::DeletePermanent
                    : services::FileOperationType::DeleteRecycleBin,
                std::move(sourcePaths),
                {},
                services::FileConflictPolicy::PromptShell,
                {},
                nextViewer ? nextViewer->Hwnd() : nullptr);
        }
    }
}
