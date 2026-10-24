#include "ui/MainWindowInternal.h"

namespace
{
    constexpr unsigned int kMemoryPressureRecoverySamplesRequired = 2;
    constexpr std::size_t kDeferredLargeFolderPresentationItemLimit = 2048;
}

namespace hyperbrowse::ui
{

    void MainWindow::LoadFolderAsync(std::wstring folderPath, bool historyNavigation)
    {
        if (folderPath.empty() || !browserModel_ || !folderLoadCoordinator_)
        {
            return;
        }

        folderLoadCoordinator_->Start(
            std::move(folderPath),
            historyNavigation,
            recursiveBrowsingEnabled_,
            showSubfoldersInBrowser_);
    }

    bool MainWindow::FlushFolderEnumerationPresentation(bool clearStartupPathsIfNotFound)
    {
        if (!clearStartupPathsIfNotFound
            && folderLoadCoordinator_
            && folderLoadCoordinator_->IsEnumerationActive()
            && browserModel_
            && browserModel_->IsRecursive()
            && browserModel_->Items().size() > kDeferredLargeFolderPresentationItemLimit)
        {
            return false;
        }

        util::ScopedTimer timer(L"MainWindow::FlushFolderEnumerationPresentation");
        RefreshBrowserPane();
        if (folderLoadCoordinator_)
        {
            folderLoadCoordinator_->RestorePendingPresentation(clearStartupPathsIfNotFound);
        }
        UpdateStatusText();
        UpdateWindowTitle();
        return true;
    }

    LRESULT MainWindow::OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        if (browserPaneController_)
        {
            browserPaneController_->AcknowledgeStateChangedMessage();
            sortMode_ = browserPaneController_->GetSortMode();
            sortAscending_ = browserPaneController_->IsSortAscending();
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateDetailsPanel();
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        OpenItemInViewer(static_cast<int>(lParam), ShouldDefaultViewerToSecondaryMonitor());
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        // Shift+right-click opens the real Windows shell context menu (with the
        // user's shell extensions); a plain right-click shows HyperBrowse's menu.
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && ShowShellContextMenuForSelection(screenPoint))
        {
            return 0;
        }
        ShowBrowserContextMenu(screenPoint);
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneQuickSendDragMessage(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;

        if (reinterpret_cast<HWND>(wParam) != browserPane_
            || dragMode_ != DragMode::None
            || !browserPaneController_
            || browserPaneController_->SelectedCount() == 0
            || fileOperationActive_)
        {
            return 0;
        }

        dragMode_ = DragMode::QuickAccessInternal;
        quickAccessPressedRowIndex_ = -1;
        quickAccessPressedButtonIndex_ = -1;
        internalSelectionTreeDropItem_ = nullptr;
        internalSelectionTreeDropPath_.clear();

        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        SetCapture(hwnd_);

        UpdateInternalSelectionDrag(point);
        return TRUE;
    }

    LRESULT MainWindow::OnBatchConvertMessage(LPARAM lParam)
    {
        std::unique_ptr<services::BatchConvertUpdate> update(
            reinterpret_cast<services::BatchConvertUpdate*>(lParam));
        if (!update || update->requestId != activeBatchConvertRequestId_)
        {
            return 0;
        }

        batchConvertCompleted_ = update->completedCount;
        batchConvertTotal_ = update->totalCount;
        batchConvertFailed_ = update->failedCount;
        batchConvertCurrentFile_ = update->currentFileName;
        batchConvertOutputFolder_ = update->outputFolder;

        if (update->finished)
        {
            batchConvertActive_ = false;
            batchConvertCancelling_ = false;
            UpdateMenuState();

            std::wstring summary;
            if (!update->message.empty())
            {
                summary = update->message;
                summary.append(L"\n\n");
            }

            summary.append(L"Converted ");
            summary.append(std::to_wstring(update->succeededCount));
            summary.append(L" of ");
            summary.append(std::to_wstring(update->totalCount));
            summary.append(L" image(s).");
            if (update->failedCount > 0)
            {
                summary.append(L" Failures: ");
                summary.append(std::to_wstring(update->failedCount));
                summary.append(L".");
            }

            if (update->cancelled)
            {
                NotifyLongOperationComplete(L"Batch Convert", L"Batch conversion was cancelled.");
            }
            else
            {
                NotifyLongOperationComplete(L"Batch Convert Complete", summary);
                if (update->failedCount > 0)
                {
                    MessageBoxW(hwnd_, summary.c_str(), L"Batch Convert", MB_OK | MB_ICONWARNING);
                }
            }
        }

        if (update->finished && closePending_)
        {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }

        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnImageCommandMessage(LPARAM lParam)
    {
        std::unique_ptr<services::ImageCommandUpdate> update(
            reinterpret_cast<services::ImageCommandUpdate*>(lParam));
        if (!update || update->requestId != activeImageCommandRequestId_)
        {
            return 0;
        }

        if (!update->finished)
        {
            imageCommandActivityLabel_ = L"Adjusting JPEG orientation: "
                + std::to_wstring(update->completedCount)
                + L" / "
                + std::to_wstring(update->totalCount);
            UpdateStatusText();
            return 0;
        }

        const bool discardResult = imageCommandCancelling_ || update->cancelled;
        imageCommandActive_ = false;
        imageCommandCancelling_ = false;
        imageCommandActivityLabel_.clear();
        UpdateStatusText();

        if (!discardResult)
        {
            const HWND dialogOwner = update->ownerWindow && IsWindow(update->ownerWindow)
                ? update->ownerWindow
                : hwnd_;
            switch (update->kind)
            {
            case services::ImageCommandKind::Information:
                if (update->metadata)
                {
                    PresentImageInformation(update->item, std::move(update->metadata), dialogOwner);
                }
                else
                {
                    MessageBoxW(
                        dialogOwner,
                        update->message.empty() ? L"No metadata is available for the selected image." : update->message.c_str(),
                        L"Image Information",
                        MB_OK | MB_ICONINFORMATION);
                }
                break;
            case services::ImageCommandKind::CopyPixels:
                if (update->image)
                {
                    CopyDecodedImageToClipboard(std::move(update->image), dialogOwner);
                }
                else
                {
                    MessageBoxW(
                        dialogOwner,
                        update->message.empty() ? L"Unable to decode the selected image." : update->message.c_str(),
                        L"Copy Image",
                        MB_OK | MB_ICONERROR);
                }
                break;
            case services::ImageCommandKind::AdjustJpegOrientation:
            {
                if (!update->updatedPaths.empty() && browserPaneController_ && browserModel_)
                {
                    const std::vector<std::wstring> currentSelection = browserPaneController_->SelectedFilePathsSnapshot();
                    const std::wstring currentFocus = browserPaneController_->FocusedFilePathSnapshot();
                    for (const std::wstring& path : update->updatedPaths)
                    {
                        browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(fs::path(path)));
                    }
                    browserPaneController_->InvalidateMediaCacheForPaths(update->updatedPaths);
                    RefreshBrowserPane();
                    browserPaneController_->RestoreSelectionByFilePaths(currentSelection, currentFocus);
                }

                std::wstring summary = L"Updated orientation metadata for "
                    + std::to_wstring(update->succeededCount)
                    + L" JPEG file(s).";
                if (update->failedCount > 0)
                {
                    summary.append(L"\nFailed: ");
                    summary.append(std::to_wstring(update->failedCount));
                    summary.append(L".");
                    if (!update->message.empty())
                    {
                        summary.append(L"\nReason: ");
                        summary.append(update->message);
                    }
                }
                MessageBoxW(dialogOwner, summary.c_str(), L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
                break;
            }
            default:
                break;
            }
        }

        if (closePending_)
        {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        return 0;
    }

    LRESULT MainWindow::OnDetailsPanelThumbnailMessage(LPARAM lParam)
    {
        std::unique_ptr<services::ThumbnailReadyUpdate> update(
            reinterpret_cast<services::ThumbnailReadyUpdate*>(lParam));
        if (!update
            || !detailsPanelThumbnailScheduler_
            || update->sessionId != detailsPanelThumbnailSessionId_
            || update->requestEpoch != detailsPanelThumbnailRequestEpoch_
            || update->modelIndex != detailsPanelHistogramModelIndex_
            || !StringsEqualInsensitive(update->cacheKey.filePath, detailsPanelHistogramPath_)
            || update->cacheKey.modifiedTimestampUtc != detailsPanelHistogramModifiedTimestampUtc_)
        {
            return 0;
        }

        detailsPanelHistogramLoading_ = false;
        if (update->success)
        {
            if (const auto thumbnail = detailsPanelThumbnailScheduler_->FindCachedThumbnail(update->cacheKey))
            {
                ApplyDetailsPanelHistogram(*thumbnail);
            }
        }
        else
        {
            detailsPanelHistogramVisible_ = false;
            detailsPanelHistogramPeak_ = 0;
        }

        LayoutChildren();
        if (hwnd_ && !IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
        return 0;
    }

    LRESULT MainWindow::OnFileOperationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FileOperationUpdate> update(
            reinterpret_cast<services::FileOperationUpdate*>(lParam));
        if (!update || update->requestId != activeFileOperationRequestId_)
        {
            return 0;
        }

        ApplyCompletedFileOperation(*update);
        ClearTaskbarProgress();
        return 0;
    }

    LRESULT MainWindow::OnViewerZoomMessage(WPARAM wParam, LPARAM lParam)
    {
        if (!FindViewerByHwnd(reinterpret_cast<HWND>(wParam)))
        {
            return 0;
        }

        activeViewerWindow_ = reinterpret_cast<HWND>(wParam);
        viewerZoomPercent_ = static_cast<int>(lParam);
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerActivityMessage(WPARAM wParam, LPARAM lParam)
    {
        if (!FindViewerByHwnd(reinterpret_cast<HWND>(wParam)))
        {
            return 0;
        }

        activeViewerWindow_ = reinterpret_cast<HWND>(wParam);
        viewerWindowActive_ = lParam != 0;
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerCurrentItemChangedMessage(WPARAM wParam)
    {
        viewer::ViewerWindow* viewer = FindViewerByHwnd(reinterpret_cast<HWND>(wParam));
        if (!viewer || !browserPaneController_)
        {
            return 0;
        }

        activeViewerWindow_ = viewer->Hwnd();
        const std::wstring currentPath = viewer->CurrentFilePath();
        if (currentPath.empty())
        {
            return 0;
        }

        browserPaneController_->RestoreSelectionByFilePaths({currentPath}, currentPath);
        browserPaneController_->EnsureFocusedItemVisible();
        return 0;
    }

    LRESULT MainWindow::OnViewerDeleteRequested(WPARAM wParam, LPARAM lParam)
    {
        util::LogInfo(L"MainWindow::OnViewerDeleteRequested entered");
        util::ScopedTimer functionTimer(L"MainWindow::OnViewerDeleteRequested");
        viewer::ViewerWindow* viewer = FindViewerByHwnd(reinterpret_cast<HWND>(lParam));
        if (!viewer || !fileOperationService_)
        {
            return 0;
        }

        activeViewerWindow_ = viewer->Hwnd();

        std::wstring sourcePath;
        std::wstring preferredFocusPath;
        if (!viewer->GetDeleteCurrentPaths(&sourcePath, &preferredFocusPath) || sourcePath.empty())
        {
            return 0;
        }

        std::vector<std::wstring> sourcePaths;
        {
            util::ScopedTimer expandTimer(L"MainWindow::OnViewerDeleteRequested ExpandRawJpegPairedPaths");
            sourcePaths = ExpandRawJpegPairedPaths({sourcePath});
        }
        if (sourcePaths.empty())
        {
            return 0;
        }

        const bool permanentDelete = (wParam & viewer::ViewerWindow::kDeleteRequestPermanent) != 0;
        {
            util::ScopedTimer confirmTimer(L"MainWindow::OnViewerDeleteRequested ConfirmFileDeletion");
            if (ShouldConfirmDeletion(permanentDelete)
                && !ConfirmFileDeletion(hwnd_, sourcePaths.size(), permanentDelete))
            {
                return 0;
            }
        }

        // Advance the viewer immediately so the next image is visible at once,
        // regardless of how long the file operation takes.
        viewer->AdvanceAfterDeleteCurrent();

        if (fileOperationActive_)
        {
            util::LogInfo(L"MainWindow::OnViewerDeleteRequested queuing delete (file operation already active)");
            // A file operation is already running. Queue this delete so it is
            // dispatched as soon as the current operation completes.
            PendingViewerDelete queued;
            queued.viewerHwnd = viewer->Hwnd();
            queued.sourcePath = sourcePath;
            queued.sourcePaths = sourcePaths;
            queued.preferredFocusPath = preferredFocusPath;
            queued.permanent = permanentDelete;
            viewerPendingOperations_.QueueDelete(std::move(queued));
            return 0;
        }

        PendingViewerDelete active;
        active.viewerHwnd = viewer->Hwnd();
        active.sourcePath = std::move(sourcePath);
        active.sourcePaths = sourcePaths;
        active.preferredFocusPath = std::move(preferredFocusPath);
        active.permanent = permanentDelete;
        viewerPendingOperations_.SetActiveDelete(std::move(active));
        {
            util::ScopedTimer fileOpTimer(L"MainWindow::OnViewerDeleteRequested StartFileOperation");
            StartFileOperation(
                permanentDelete ? services::FileOperationType::DeletePermanent
                                : services::FileOperationType::DeleteRecycleBin,
                sourcePaths,
                {},
                services::FileConflictPolicy::PromptShell,
                {},
                viewer->Hwnd());
        }
        return 0;
    }

    LRESULT MainWindow::OnViewerContextMenuCommand(WPARAM wParam, LPARAM lParam)
    {
        viewer::ViewerWindow* viewer = FindViewerByHwnd(reinterpret_cast<HWND>(lParam));
        if (!viewer)
        {
            return 0;
        }

        activeViewerWindow_ = viewer->Hwnd();
        const std::wstring currentPath = viewer->CurrentFilePath();
        if (currentPath.empty())
        {
            return 0;
        }

        const UINT commandId = static_cast<UINT>(wParam);
        if (commandId == viewer::ViewerWindow::kContextMenuRefreshCompareTileMetadata)
        {
            if (userMetadataStore_)
            {
                const services::UserMetadataEntry entry = userMetadataStore_->EntryForPath(currentPath);
                viewer->UpdateCompareTileMetadata(currentPath, {entry.rating, entry.tags});
            }
            return 0;
        }
        if (commandId >= viewer::ViewerWindow::kContextMenuSetCompareRatingBase
            && commandId < viewer::ViewerWindow::kContextMenuSetCompareRatingBase + 6)
        {
            if (userMetadataStore_)
            {
                const int rating = static_cast<int>(commandId - viewer::ViewerWindow::kContextMenuSetCompareRatingBase);
                userMetadataStore_->SetRating({currentPath}, rating);
                const services::UserMetadataEntry entry = userMetadataStore_->EntryForPath(currentPath);
                viewer->UpdateCompareTileMetadata(currentPath, {entry.rating, entry.tags});
                RefreshBrowserPane();
                UpdateMenuState();
            }
            return 0;
        }
        if (commandId == viewer::ViewerWindow::kContextMenuEditCompareTags)
        {
            if (userMetadataStore_)
            {
                std::wstring editedTags = userMetadataStore_->EntryForPath(currentPath).tags;
                if (PromptForSingleLineText(hwnd_,
                                            instance_,
                                            appTextSize_,
                                            themeMode_ == ThemeMode::Dark,
                                            L"Edit Image Tags",
                                            L"Enter comma-separated tags for this image. Leave blank to clear tags.",
                                            L"Apply",
                                            editedTags,
                                            0,
                                            -1,
                                            &editedTags))
                {
                    userMetadataStore_->SetTags({currentPath}, editedTags);
                    const services::UserMetadataEntry entry = userMetadataStore_->EntryForPath(currentPath);
                    viewer->UpdateCompareTileMetadata(currentPath, {entry.rating, entry.tags});
                    RefreshBrowserPane();
                    UpdateMenuState();
                }
            }
            return 0;
        }

        switch (commandId)
        {
        case viewer::ViewerWindow::kContextMenuCopyImage:
            CopySelectedImagePixelsToClipboard(currentPath);
            break;
        case viewer::ViewerWindow::kContextMenuImageInformation:
            ShowImageInformationForPath(currentPath, viewer->Hwnd());
            break;
        case viewer::ViewerWindow::kContextMenuSetWallpaper:
            SetDesktopWallpaperFromImageFile(currentPath);
            break;
        case viewer::ViewerWindow::kContextMenuProperties:
        {
            SHELLEXECUTEINFOW executeInfo{};
            executeInfo.cbSize = sizeof(executeInfo);
            executeInfo.fMask = SEE_MASK_INVOKEIDLIST;
            executeInfo.hwnd = hwnd_;
            executeInfo.lpVerb = L"properties";
            executeInfo.lpFile = currentPath.c_str();
            executeInfo.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&executeInfo) == FALSE)
            {
                MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
            }
            break;
        }
        default:
            break;
        }
        return 0;
    }

    LRESULT MainWindow::OnViewerDroppedFileMessage(LPARAM lParam)
    {
        std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
        if (!path || path->empty())
        {
            return 0;
        }

        OpenViewerAtPath(*path);
        return 0;
    }

    LRESULT MainWindow::OnViewerStartFolderSlideshowMessage(WPARAM wParam)
    {
        viewer::ViewerWindow* viewer = FindViewerByHwnd(reinterpret_cast<HWND>(wParam));
        if (!viewer)
        {
            return 0;
        }

        activeViewerWindow_ = viewer->Hwnd();
        StartFolderSlideshow(viewer->CurrentFilePath());
        return 0;
    }

    LRESULT MainWindow::OnViewerClosedMessage(WPARAM wParam)
    {
        const HWND closingViewerHwnd = reinterpret_cast<HWND>(wParam);
        if (closingViewerHwnd && (!viewerWindow_ || closingViewerHwnd != viewerWindow_->Hwnd()))
        {
            const auto additionalViewer = std::find_if(
                additionalViewerWindows_.begin(),
                additionalViewerWindows_.end(),
                [closingViewerHwnd](const std::unique_ptr<viewer::ViewerWindow>& viewer)
                {
                    return viewer && viewer->Hwnd() == closingViewerHwnd;
                });
            if (additionalViewer != additionalViewerWindows_.end())
            {
                const std::wstring viewerPath = NormalizeFolderPath((*additionalViewer)->CurrentFilePath());
                additionalViewerWindows_.erase(additionalViewer);
                if (activeViewerWindow_ == closingViewerHwnd)
                {
                    activeViewerWindow_ = viewerWindow_ && viewerWindow_->IsOpen()
                        ? viewerWindow_->Hwnd()
                        : (additionalViewerWindows_.empty() ? nullptr : additionalViewerWindows_.back()->Hwnd());
                }
                if (!viewerPath.empty() && browserPaneController_)
                {
                    browserPaneController_->RestoreSelectionByFilePaths({viewerPath}, viewerPath);
                    browserPaneController_->EnsureFocusedItemVisible();
                }
                UpdateStatusText();
                UpdateMenuState();
                return 0;
            }
        }

        const HWND primaryViewerHwnd = viewerWindow_ ? viewerWindow_->Hwnd() : nullptr;
        if (!viewerWindow_ || (closingViewerHwnd && closingViewerHwnd != primaryViewerHwnd))
        {
            return 0;
        }
        const std::wstring viewerPath = viewerWindow_ ? NormalizeFolderPath(viewerWindow_->CurrentFilePath()) : std::wstring{};
        viewerPendingOperations_.Clear();
        if (focusWindowAtFileOperationStart_ == closingViewerHwnd)
        {
            focusWindowAtFileOperationStart_ = nullptr;
        }
        if (foregroundWindowAtFileOperationStart_ == closingViewerHwnd)
        {
            foregroundWindowAtFileOperationStart_ = nullptr;
        }
        if (!viewerPath.empty() && browserPaneController_)
        {
            browserPaneController_->RestoreSelectionByFilePaths({viewerPath}, viewerPath);
            browserPaneController_->EnsureFocusedItemVisible();
        }
        if (browserPaneController_ && browserPaneController_->Hwnd())
        {
            SetFocus(browserPaneController_->Hwnd());
        }

        viewerWindowActive_ = false;
        activeViewerWindow_ = additionalViewerWindows_.empty()
            ? nullptr
            : additionalViewerWindows_.back()->Hwnd();
        viewerZoomPercent_ = 0;
        UpdateStatusText();
        UpdateMenuState();
        return 0;
    }

    LRESULT MainWindow::OnMemoryPressureSampleMessage(LPARAM lParam)
    {
        std::unique_ptr<MemoryPressureSampleResult> update(reinterpret_cast<MemoryPressureSampleResult*>(lParam));
        memoryPressureSampleQueued_ = false;
        if (!update)
        {
            return 0;
        }

        bool nextPressureActive = thumbnailMemoryPressureActive_;
        if (update->pressureDetected)
        {
            nextPressureActive = true;
            memoryPressureRecoveryClearSampleCount_ = 0;
        }
        else if (thumbnailMemoryPressureActive_ && update->recoveryCandidate)
        {
            ++memoryPressureRecoveryClearSampleCount_;
            if (memoryPressureRecoveryClearSampleCount_ >= kMemoryPressureRecoverySamplesRequired)
            {
                nextPressureActive = false;
                memoryPressureRecoveryClearSampleCount_ = 0;
            }
        }
        else
        {
            memoryPressureRecoveryClearSampleCount_ = 0;
        }

        if (nextPressureActive != thumbnailMemoryPressureActive_)
        {
            thumbnailMemoryPressureActive_ = nextPressureActive;
            ApplyThumbnailMemoryPressureState();
        }
        return 0;
    }
}
