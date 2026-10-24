#include "ui/MainWindowInternal.h"

namespace
{

    constexpr LONG kWindowCascadeOffset = 32;
    constexpr UINT_PTR kMemoryPressureTimerId = 9101;
    constexpr UINT kMemoryPressureIntervalMs = 1500;
    constexpr UINT_PTR kGamepadTimerId = 9109;
    constexpr GUID kConsoleDisplayStateGuid{
        0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
    constexpr GUID kMonitorPowerOnGuid{
        0x02731015, 0x4510, 0x4526, {0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea}};
    constexpr int kToolbarIconSize = 18;
    constexpr int kToolbarDropdownChevronSize = 10;
    constexpr int kToolbarSeparatorWidth = 9;
    constexpr int kToolbarSeparatorGap = 4;
    constexpr wchar_t kFilterEditTooltip[] =
        L"Case-insensitive; all terms must match.\r\n"
        L"Words match filenames.\r\n"
        L"Tags: tag:value or tags:value\r\n"
        L"Types: type:raw (RAW family) or type:jpg (exact extension)\r\n"
        L"Ratings (0-5): rating:rated, rating:unrated, rating:4,\r\n"
        L"rating:>=3, or rating:<2";
    constexpr int kFilterEditMinWidth = 160;
    constexpr int kDetailsStripHeight = 22;
    constexpr UINT kStatusStripControlId = 5001;
    constexpr int kStatusStripHorizontalPadding = 12;
    constexpr int kCommandBarMenuButtonGap = 4;
    constexpr int kCommandBarMenuButtonMinWidth = 56;
    constexpr int kDetailsPanelSectionGap = 12;
    constexpr int kDetailsPanelHistogramBins = 64;
    constexpr int kQuickAccessPanelTopGap = 12;
    constexpr std::size_t kIncrementalFolderWatchEventLimit = 64;
    constexpr std::size_t kIncrementalFileOperationPathLimit = 64;
    constexpr ULONGLONG kFileOperationShutdownNoticeDelayMs = 5000;
    HWND FindPopupMenuWindow(HMENU menu)
    {
        struct SearchContext
        {
            HMENU menu{};
            HWND window{};
        } context{menu};

        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM lParam)
        {
            auto* context = reinterpret_cast<SearchContext*>(lParam);
            if (!context || GetMenu(window) != context->menu)
            {
                return TRUE;
            }

            wchar_t className[32]{};
            if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0
                && _wcsicmp(className, L"#32768") == 0)
            {
                context->window = window;
                return FALSE;
            }

            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        return context.window;
    }
    constexpr int kTextInputDialogInstructionMinHeight = 42;
    constexpr int kTextInputDialogEditTopGap = 8;
    constexpr int kTextInputDialogDividerTopGap = 14;
    constexpr int kTextInputDialogButtonTopGap = 10;
    constexpr int kTextInputEditControlId = 100;
    constexpr int kAboutDialogLinkButtonWidth = 172;
    constexpr int kAboutDialogSupportButtonWidth = 196;
    constexpr int kFileAssociationsDialogFormatGroupControlId = 362;

    BOOL CALLBACK CountMainWindowInstances(HWND window, LPARAM parameter)
    {
        wchar_t className[64]{};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0
            && wcscmp(className, L"HyperBrowseMainWindow") == 0)
        {
            ++*reinterpret_cast<int*>(parameter);
        }

        return TRUE;
    }

    void CascadeWindowBoundsForAdditionalInstance(RECT* bounds)
    {
        if (!bounds)
        {
            return;
        }

        int existingWindowCount = 0;
        EnumWindows(&CountMainWindowInstances, reinterpret_cast<LPARAM>(&existingWindowCount));
        if (existingWindowCount <= 0)
        {
            return;
        }

        const LONG width = bounds->right - bounds->left;
        const LONG height = bounds->bottom - bounds->top;
        HMONITOR monitor = MonitorFromRect(bounds, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
        {
            return;
        }

        const RECT& workArea = monitorInfo.rcWork;
        const LONG horizontalRange = std::max<LONG>(0, (workArea.right - workArea.left) - width);
        const LONG verticalRange = std::max<LONG>(0, (workArea.bottom - workArea.top) - height);
        const long long offset = static_cast<long long>(existingWindowCount) * kWindowCascadeOffset;
        const long long horizontalPosition = static_cast<long long>(bounds->left - workArea.left) + offset;
        const long long verticalPosition = static_cast<long long>(bounds->top - workArea.top) + offset;
        bounds->left = workArea.left + static_cast<LONG>(horizontalRange == 0 ? 0 : horizontalPosition % (horizontalRange + 1));
        bounds->top = workArea.top + static_cast<LONG>(verticalRange == 0 ? 0 : verticalPosition % (verticalRange + 1));
        bounds->right = bounds->left + width;
        bounds->bottom = bounds->top + height;
    }

    void RedrawWindowNoErase(HWND hwnd)
    {
        if (!hwnd)
        {
            return;
        }

        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }

    void RemoveAllMenuItems(HMENU menu)
    {
        if (!menu)
        {
            return;
        }

        while (GetMenuItemCount(menu) > 0)
        {
            DeleteMenu(menu, 0, MF_BYPOSITION);
        }
    }

    void AlphaBlendBitmap(HDC targetDC, HDC scratchDC, HBITMAP bitmap, int x, int y, int width, int height)
    {
        if (!targetDC || !scratchDC || !bitmap || width <= 0 || height <= 0)
        {
            return;
        }

        const HGDIOBJ oldBitmap = SelectObject(scratchDC, bitmap);
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(targetDC, x, y, width, height, scratchDC, 0, 0, width, height, blend);
        SelectObject(scratchDC, oldBitmap);
    }

    std::wstring CompactSortLabel(hyperbrowse::browser::BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case hyperbrowse::browser::BrowserSortMode::FileName:
            return L"Name";
        case hyperbrowse::browser::BrowserSortMode::ModifiedDate:
            return L"Date";
        case hyperbrowse::browser::BrowserSortMode::FileSize:
            return L"Size";
        case hyperbrowse::browser::BrowserSortMode::Dimensions:
            return L"Pixels";
        case hyperbrowse::browser::BrowserSortMode::FileType:
            return L"Type";
        case hyperbrowse::browser::BrowserSortMode::DateTaken:
            return L"Taken";
        case hyperbrowse::browser::BrowserSortMode::Rating:
            return L"Rating";
        case hyperbrowse::browser::BrowserSortMode::Tags:
            return L"Tags";
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return L"Random";
        }
    }

    void ExperimentalSettingsSetControlRect(ExperimentalSettingsDialogState& state,
                                            ConsolidatedSettingsControl control,
                                            int left,
                                            int top,
                                            int right,
                                            int bottom)
    {
        state.controlRects[static_cast<std::size_t>(control)] = {left, top, right, bottom};
    }

    bool ConfirmFavoriteDestinationClear(HWND ownerWindow, std::size_t favoriteCount)
    {
        const std::wstring prompt = favoriteCount == 1
            ? L"Remove the only quick action destination?"
            : L"Remove all " + std::to_wstring(favoriteCount) + L" quick action destinations?";
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       L"Clear Quick Actions",
                                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDYES;
    }

    hyperbrowse::browser::BrowserSortMode SortModeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_SORT_FILENAME:
            return hyperbrowse::browser::BrowserSortMode::FileName;
        case ID_VIEW_SORT_MODIFIED:
            return hyperbrowse::browser::BrowserSortMode::ModifiedDate;
        case ID_VIEW_SORT_SIZE:
            return hyperbrowse::browser::BrowserSortMode::FileSize;
        case ID_VIEW_SORT_DIMENSIONS:
            return hyperbrowse::browser::BrowserSortMode::Dimensions;
        case ID_VIEW_SORT_TYPE:
            return hyperbrowse::browser::BrowserSortMode::FileType;
        case ID_VIEW_SORT_DATETAKEN:
            return hyperbrowse::browser::BrowserSortMode::DateTaken;
        case ID_VIEW_SORT_RATING:
            return hyperbrowse::browser::BrowserSortMode::Rating;
        case ID_VIEW_SORT_TAGS:
            return hyperbrowse::browser::BrowserSortMode::Tags;
        case ID_VIEW_SORT_RANDOM:
        default:
            return hyperbrowse::browser::BrowserSortMode::Random;
        }
    }

    hyperbrowse::browser::ThumbnailSizePreset ThumbnailSizePresetFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_THUMBNAIL_SIZE_96:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels96;
        case ID_VIEW_THUMBNAIL_SIZE_128:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels128;
        case ID_VIEW_THUMBNAIL_SIZE_160:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels160;
        case ID_VIEW_THUMBNAIL_SIZE_256:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels256;
        case ID_VIEW_THUMBNAIL_SIZE_320:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels320;
        case ID_VIEW_THUMBNAIL_SIZE_360:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels360;
        case ID_VIEW_THUMBNAIL_SIZE_420:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels420;
        case ID_VIEW_THUMBNAIL_SIZE_480:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels480;
        case ID_VIEW_THUMBNAIL_SIZE_560:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels560;
        case ID_VIEW_THUMBNAIL_SIZE_640:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels640;
        case ID_VIEW_THUMBNAIL_SIZE_192:
        default:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels192;
        }
    }

    hyperbrowse::viewer::MouseWheelBehavior ViewerMouseWheelBehaviorFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE:
            return hyperbrowse::viewer::MouseWheelBehavior::Navigate;
        case ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM:
        default:
            return hyperbrowse::viewer::MouseWheelBehavior::Zoom;
        }
    }

    UINT CommandIdFromViewerMouseWheelBehavior(hyperbrowse::viewer::MouseWheelBehavior behavior)
    {
        switch (behavior)
        {
        case hyperbrowse::viewer::MouseWheelBehavior::Navigate:
            return ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE;
        case hyperbrowse::viewer::MouseWheelBehavior::Zoom:
        default:
            return ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM;
        }
    }

    bool IsViewerMouseWheelBehaviorCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM
            && commandId <= ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE;
    }

    hyperbrowse::viewer::InfoOverlayTextSize ViewerOverlayTextSizeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_VIEWER_OVERLAY_TEXT_MEDIUM:
            return hyperbrowse::viewer::InfoOverlayTextSize::Medium;
        case ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE:
            return hyperbrowse::viewer::InfoOverlayTextSize::Large;
        case ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL:
        default:
            return hyperbrowse::viewer::InfoOverlayTextSize::Small;
        }
    }

    UINT CommandIdFromViewerOverlayTextSize(hyperbrowse::viewer::InfoOverlayTextSize size)
    {
        switch (size)
        {
        case hyperbrowse::viewer::InfoOverlayTextSize::Medium:
            return ID_VIEW_VIEWER_OVERLAY_TEXT_MEDIUM;
        case hyperbrowse::viewer::InfoOverlayTextSize::Large:
            return ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE;
        case hyperbrowse::viewer::InfoOverlayTextSize::Small:
        default:
            return ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL;
        }
    }

    bool IsViewerOverlayTextSizeCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL
            && commandId <= ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE;
    }

    hyperbrowse::util::AppTextSize AppTextSizeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_APP_TEXT_SIZE_SMALL:
            return hyperbrowse::util::AppTextSize::Small;
        case ID_VIEW_APP_TEXT_SIZE_LARGE:
            return hyperbrowse::util::AppTextSize::Large;
        case ID_VIEW_APP_TEXT_SIZE_MEDIUM:
        default:
            return hyperbrowse::util::AppTextSize::Medium;
        }
    }

    UINT CommandIdFromAppTextSize(hyperbrowse::util::AppTextSize size)
    {
        switch (hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(size)))
        {
        case hyperbrowse::util::AppTextSize::Small:
            return ID_VIEW_APP_TEXT_SIZE_SMALL;
        case hyperbrowse::util::AppTextSize::Large:
            return ID_VIEW_APP_TEXT_SIZE_LARGE;
        case hyperbrowse::util::AppTextSize::Medium:
        default:
            return ID_VIEW_APP_TEXT_SIZE_MEDIUM;
        }
    }

    bool IsAppTextSizeCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_APP_TEXT_SIZE_SMALL
            && commandId <= ID_VIEW_APP_TEXT_SIZE_LARGE;
    }

    hyperbrowse::util::ResourceProfile ResourceProfileFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE:
            return hyperbrowse::util::ResourceProfile::Conservative;
        case ID_HELP_PERFORMANCE_PROFILE_PERFORMANCE:
            return hyperbrowse::util::ResourceProfile::Performance;
        case ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE:
            return hyperbrowse::util::ResourceProfile::Aggressive;
        case ID_HELP_PERFORMANCE_PROFILE_BALANCED:
        default:
            return hyperbrowse::util::ResourceProfile::Balanced;
        }
    }

    UINT CommandIdFromResourceProfile(hyperbrowse::util::ResourceProfile resourceProfile)
    {
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            return ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE;
        case hyperbrowse::util::ResourceProfile::Performance:
            return ID_HELP_PERFORMANCE_PROFILE_PERFORMANCE;
        case hyperbrowse::util::ResourceProfile::Aggressive:
            return ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE;
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            return ID_HELP_PERFORMANCE_PROFILE_BALANCED;
        }
    }

    bool IsCommandInRange(UINT commandId, UINT firstCommandId, UINT lastCommandId)
    {
        return commandId >= firstCommandId && commandId <= lastCommandId;
    }

    int RatingFromCommandId(UINT commandId)
    {
        if (commandId < ID_FILE_SET_RATING_0 || commandId > ID_FILE_SET_RATING_5)
        {
            return 0;
        }

        return static_cast<int>(commandId - ID_FILE_SET_RATING_0);
    }

    bool IsRatingCommand(UINT commandId)
    {
        return commandId >= ID_FILE_SET_RATING_0 && commandId <= ID_FILE_SET_RATING_5;
    }
}

namespace hyperbrowse::ui
{
    MainWindow::MainWindow(HINSTANCE instance)
        : instance_(instance)
        , commandBarMenuButtons_(commandBarController_.MenuButtons())
        , toolbarItems_(commandBarController_.Items())
        , browserModel_(std::make_unique<browser::BrowserModel>())
        , browserPaneController_(std::make_unique<browser::BrowserPane>(instance))
        , batchConvertService_(std::make_unique<services::BatchConvertService>())
        , imageCommandService_(std::make_unique<services::ImageCommandService>())
        , fileOperationService_(std::make_unique<services::FileOperationService>())
        , folderLoadCoordinator_(std::make_unique<FolderLoadCoordinator>())
        , folderWatchChangeCoordinator_(std::make_unique<FolderWatchChangeCoordinator>())
        , folderTreeController_(std::make_unique<FolderTreeController>())
        , userMetadataStore_(std::make_unique<services::UserMetadataStore>())
        , diagnosticsWindow_(std::make_unique<DiagnosticsWindow>(instance))
        , viewerWindow_(std::make_unique<viewer::ViewerWindow>(instance))
        , slideshowTransitionStyle_(viewer::TransitionStyle::Crossfade)
    {
        WindowAsyncMessageRouter::Handlers messageHandlers;
        messageHandlers.onFolderEnumeration = [this](LPARAM lParam)
        {
            if (folderLoadCoordinator_)
            {
                folderLoadCoordinator_->HandleEnumerationMessage(lParam);
            }
            return static_cast<LRESULT>(0);
        };
        messageHandlers.onFolderTreeEnumeration = [this](LPARAM lParam)
        {
            return folderTreeController_ ? folderTreeController_->HandleEnumerationMessage(lParam) : 0;
        };
        messageHandlers.onFolderWatch = [this](LPARAM lParam)
        {
            return folderLoadCoordinator_ ? folderLoadCoordinator_->HandleWatchMessage(lParam) : 0;
        };
        messageHandlers.onBrowserPaneState = std::bind_front(&MainWindow::OnBrowserPaneStateMessage, this);
        messageHandlers.onBrowserPaneOpenItem = std::bind_front(&MainWindow::OnBrowserPaneOpenItemMessage, this);
        messageHandlers.onBrowserPaneContextMenu = std::bind_front(&MainWindow::OnBrowserPaneContextMenuMessage, this);
        messageHandlers.onBrowserPaneQuickSendDrag = std::bind_front(&MainWindow::OnBrowserPaneQuickSendDragMessage, this);
        messageHandlers.onBatchConvert = std::bind_front(&MainWindow::OnBatchConvertMessage, this);
        messageHandlers.onImageCommand = std::bind_front(&MainWindow::OnImageCommandMessage, this);
        messageHandlers.onFileOperation = std::bind_front(&MainWindow::OnFileOperationMessage, this);
        messageHandlers.onFileOperationProgress = std::bind_front(&MainWindow::OnFileOperationProgressMessage, this);
        messageHandlers.onUserMetadataSaveError = [this]()
        {
            const std::wstring error = userMetadataStore_ ? userMetadataStore_->LastSaveError() : std::wstring{};
            if (!error.empty() && hwnd_)
            {
                const std::wstring message = L"HyperBrowse could not save ratings and tags. Your current changes remain available while the application is open, and the next edit will retry the save.\n\n" + error;
                MessageBoxW(hwnd_, message.c_str(), L"Ratings and tags were not saved", MB_OK | MB_ICONWARNING);
            }
            return static_cast<LRESULT>(0);
        };
        messageHandlers.onSavedSearchChanged = [this]()
        {
            UpdateMenuState();
            UpdateStatusText();
            return static_cast<LRESULT>(0);
        };
        messageHandlers.onDetailsPanelThumbnail = std::bind_front(&MainWindow::OnDetailsPanelThumbnailMessage, this);
        messageHandlers.onViewerZoom = std::bind_front(&MainWindow::OnViewerZoomMessage, this);
        messageHandlers.onViewerActivity = std::bind_front(&MainWindow::OnViewerActivityMessage, this);
        messageHandlers.onViewerCurrentItemChanged = std::bind_front(&MainWindow::OnViewerCurrentItemChangedMessage, this);
        messageHandlers.onViewerDeleteRequested = std::bind_front(&MainWindow::OnViewerDeleteRequested, this);
        messageHandlers.onViewerQuickSendRequest = std::bind_front(&MainWindow::OnViewerQuickSendRequest, this);
        messageHandlers.onViewerStartFolderSlideshow = std::bind_front(&MainWindow::OnViewerStartFolderSlideshowMessage, this);
        messageHandlers.onViewerContextMenuCommand = std::bind_front(&MainWindow::OnViewerContextMenuCommand, this);
        messageHandlers.onViewerDroppedFile = std::bind_front(&MainWindow::OnViewerDroppedFileMessage, this);
        messageHandlers.onViewerClosed = std::bind_front(&MainWindow::OnViewerClosedMessage, this);
        messageHandlers.onExternalLaunch = [this](LPARAM lParam)
        {
            std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
            if (path)
            {
                HandleExternalLaunchPath(*path);
            }
            return static_cast<LRESULT>(0);
        };
        messageHandlers.onMemoryPressureSampled = std::bind_front(&MainWindow::OnMemoryPressureSampleMessage, this);
        messageHandlers.onPersistentThumbnailCacheMaintenance = std::bind_front(&MainWindow::OnPersistentThumbnailCacheMaintenanceMessage, this);
        messageHandlers.onDeferredMenuState = [this]()
        {
            menuStateRefreshPosted_ = false;
            if (!menuLoopActive_ && menuStateRefreshPending_)
            {
                menuStateRefreshPending_ = false;
                UpdateMenuState();
            }
            return static_cast<LRESULT>(0);
        };
        asyncMessageRouter_.Configure(
            WindowAsyncMessageRouter::MessageIds{
                kExternalLaunchMessage,
                kMemoryPressureSampledMessage,
                kPersistentThumbnailCacheMaintenanceMessage,
                kDeferredMenuStateMessage},
            std::move(messageHandlers));

        WindowTimerRouter::Handlers timerHandlers;
        timerHandlers.onFileOperationShutdown = [this]() -> std::optional<LRESULT>
        {
            if (!closePending_)
            {
                return std::nullopt;
            }

            const ULONGLONG elapsed = closePendingSinceTick_ == 0
                ? 0
                : GetTickCount64() - closePendingSinceTick_;
            if (elapsed >= kFileOperationShutdownNoticeDelayMs && !closeWaitNoticeShown_)
            {
                closeWaitNoticeShown_ = true;
                util::IncrementCounter(L"file_operation.shutdown.wait_notice");
                if (fileOperationActive_)
                {
                    activeFileOperationLabel_ = L"Waiting for Windows to finish file operation";
                }
                else if (batchConvertActive_)
                {
                    activeFileOperationLabel_ = L"Waiting for conversion to finish";
                }
                else if (imageCommandActive_)
                {
                    imageCommandActivityLabel_ = L"Waiting for the image command to finish";
                }
                UpdateStatusText();
            }
            return static_cast<LRESULT>(0);
        };
        timerHandlers.onFolderPresentation = [this]() -> std::optional<LRESULT>
        {
            if (!folderLoadCoordinator_)
            {
                return std::nullopt;
            }

            folderLoadCoordinator_->HandlePresentationTimer();
            return static_cast<LRESULT>(0);
        };
        timerHandlers.onMemoryPressure = [this]() -> std::optional<LRESULT>
        {
            if (memoryPressureTimerId_ == 0)
            {
                return std::nullopt;
            }

            QueueMemoryPressureSample();
            return static_cast<LRESULT>(0);
        };
        timerHandlers.onDisplaySurfaceRecovery = [this]() -> std::optional<LRESULT>
        {
            if (displaySurfaceRecoveryTimerId_ == 0)
            {
                return std::nullopt;
            }

            displaySurfaceRecoveryPolicy_.AdvanceRetry();
            RecoverDisplaySurfaces(displaySurfaceRecoveryPolicy_.ShouldRelayout());
            if (displaySurfaceRecoveryPolicy_.Exhausted())
            {
                StopDisplaySurfaceRecoveryRetries();
            }
            return static_cast<LRESULT>(0);
        };
        timerHandlers.onQuickSendConfirmation = [this]() -> std::optional<LRESULT>
        {
            if (quickSendConfirmationTimerId_ == 0)
            {
                return std::nullopt;
            }

            KillTimer(hwnd_, quickSendConfirmationTimerId_);
            quickSendConfirmationTimerId_ = 0;
            quickSendConfirmationText_.clear();
            if (quickSendConfirmationToastWindow_)
            {
                ShowWindow(quickSendConfirmationToastWindow_, SW_HIDE);
            }
            UpdateStatusText();
            return static_cast<LRESULT>(0);
        };
        timerHandlers.onCommandBarMenuTracking = [this]() -> std::optional<LRESULT>
        {
            if (commandBarMenuTrackingIndex_ < 0)
            {
                return std::nullopt;
            }

            HandleCommandBarMenuTrackingTimer();
            return static_cast<LRESULT>(0);
        };
        timerHandlers.onDetailsPanelPerformance = [this]() -> std::optional<LRESULT>
        {
            if (detailsPanelPerformanceTimerId_ == 0
                || !detailsStripVisible_
                || activeRightPaneTab_ != RightPaneTab::CacheStats)
            {
                return std::nullopt;
            }

            UpdateDetailsPanelPerformanceStats();
            StartPersistentThumbnailCacheStatistics(false);
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }
            return static_cast<LRESULT>(0);
        };
        timerRouter_.Configure(
            WindowTimerRouter::TimerIds{
                kFileOperationShutdownTimerId,
                FolderLoadCoordinator::kPresentationTimerId,
                kMemoryPressureTimerId,
                kDisplaySurfaceRecoveryTimerId,
                kQuickSendConfirmationTimerId,
                kCommandBarMenuTrackingTimerId,
                kDetailsPanelPerformanceTimerId},
            std::move(timerHandlers));

        FileCommandController::Handlers fileCommandHandlers;
        fileCommandHandlers.onOpenRecentFolder = [this](std::size_t index)
        {
            if (index < recentFolders_.size())
            {
                const std::wstring folderPath = recentFolders_[index];
                if (!IsExistingDirectory(folderPath))
                {
                    MessageBoxW(hwnd_, L"The selected recent folder is no longer available.", L"Open Recent Folder", MB_OK | MB_ICONINFORMATION);
                }
                else
                {
                    LoadFolderAsync(folderPath);
                }
            }
        };
        fileCommandHandlers.onCopySelectionToFavorite = [this](std::size_t index)
        {
            if (index < favoriteDestinationFolders_.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Copy, favoriteDestinationFolders_[index]);
            }
        };
        fileCommandHandlers.onCopySelectionToRecent = [this](std::size_t index)
        {
            const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
            if (index < recentDestinationPaths.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Copy, recentDestinationPaths[index]);
            }
        };
        fileCommandHandlers.onMoveSelectionToFavorite = [this](std::size_t index)
        {
            if (index < favoriteDestinationFolders_.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Move, favoriteDestinationFolders_[index]);
            }
        };
        fileCommandHandlers.onMoveSelectionToRecent = [this](std::size_t index)
        {
            const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
            if (index < recentDestinationPaths.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Move, recentDestinationPaths[index]);
            }
        };
        fileCommandHandlers.onOpenFolder = std::bind_front(&MainWindow::OpenFolder, this);
        fileCommandHandlers.onNewFolder = [this]
        {
            if (browserModel_ && !browserModel_->FolderPath().empty())
            {
                StartCreateNewFolder(browserModel_->FolderPath());
            }
        };
        fileCommandHandlers.onNavigateBackFolder = std::bind_front(&MainWindow::NavigateBackToLastOpenedFolder, this);
        fileCommandHandlers.onNavigateForwardFolder = std::bind_front(&MainWindow::NavigateForwardToLastOpenedFolder, this);
        fileCommandHandlers.onNavigateParentFolder = std::bind_front(&MainWindow::NavigateToParentFolder, this);
        fileCommandHandlers.onToggleCurrentFolderFavorite = std::bind_front(&MainWindow::ToggleCurrentFolderFavoriteDestination, this);
        fileCommandHandlers.onClearFavoriteDestinations = [this]
        {
            if (ConfirmFavoriteDestinationClear(hwnd_, favoriteDestinationFolders_.size()))
            {
                ClearFavoriteDestinations();
            }
        };
        fileCommandHandlers.onClearRecentFolders = std::bind_front(&MainWindow::ClearRecentFolders, this);
        fileCommandHandlers.onClearRecentDestinations = std::bind_front(&MainWindow::ClearRecentDestinations, this);
        fileCommandHandlers.onExit = [this]
        {
            RequestApplicationExit();
        };
        fileCommandHandlers.onEscape = [this]
        {
            if (imageCommandActive_ && imageCommandService_)
            {
                imageCommandService_->Cancel();
                imageCommandCancelling_ = true;
                imageCommandActivityLabel_ = L"Cancelling image command";
                UpdateStatusText();
                return;
            }
            if (closeMainWindowOnEscape_)
            {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
        };
        fileCommandHandlers.onMinimize = [this]
        {
            ShowWindow(hwnd_, SW_MINIMIZE);
        };
        fileCommandHandlers.onToggleCloseMainWindowOnEscape = [this]
        {
            closeMainWindowOnEscape_ = !closeMainWindowOnEscape_;
            ModifyMenuW(fileMenu_,
                        ID_FILE_ESCAPE,
                        MF_BYCOMMAND | MF_STRING,
                        ID_FILE_ESCAPE,
                        closeMainWindowOnEscape_ ? L"&Close\tEsc" : L"&Ignore\tEsc");
            RefreshPersistentMenuOwnerDraw();
            UpdateMenuState();
        };
        fileCommandHandlers.onRefreshTree = [this]
        {
            if (folderTreeController_)
            {
                folderTreeController_->ClearChildPresenceCache();
            }
            RefreshFolderTree();
        };
        fileCommandHandlers.onOpenSelected = [this]
        {
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1,
                             ShouldDefaultViewerToSecondaryMonitor());
        };
        fileCommandHandlers.onOpenSelectedInNewViewerWindow = [this]
        {
            OpenItemInNewViewerWindow(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1,
                                      ShouldDefaultViewerToSecondaryMonitor());
        };
        fileCommandHandlers.onCompareSelected = std::bind_front(&MainWindow::StartCompareSelected, this);
        fileCommandHandlers.onViewOnSecondaryMonitor = [this]
        {
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1, true);
        };
        fileCommandHandlers.onImageInformation = [this]
        {
            ShowImageInformation();
        };
        fileCommandHandlers.onQuickSendMove = [this]
        {
            StartQuickSendForSelection(services::FileOperationType::Move);
        };
        fileCommandHandlers.onQuickSendCopy = [this]
        {
            StartQuickSendForSelection(services::FileOperationType::Copy);
        };
        fileCommandHandlers.onResumeFiling = std::bind_front(&MainWindow::ResumeFilingPosition, this);
        fileCommandHandlers.onCopySelection = std::bind_front(&MainWindow::StartCopySelection, this);
        fileCommandHandlers.onRenameSelected = std::bind_front(&MainWindow::StartRenameSelectedImage, this);
        fileCommandHandlers.onBatchRenameSelection = std::bind_front(&MainWindow::StartBatchRenameSelection, this);
        fileCommandHandlers.onMoveSelection = std::bind_front(&MainWindow::StartMoveSelection, this);
        fileCommandHandlers.onMoveSelectionToNewChildFolder = std::bind_front(&MainWindow::StartMoveSelectionToNewChildFolder, this);
        fileCommandHandlers.onTogglePairedRawJpeg = [this]
        {
            rawJpegPairedOperationsEnabled_ = !rawJpegPairedOperationsEnabled_;
            ApplyRawJpegPairingSettings();
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
            }
            UpdateStatusText();
            UpdateMenuState();
        };
        fileCommandHandlers.onDeleteSelection = std::bind_front(&MainWindow::StartDeleteSelection, this);
        fileCommandHandlers.onRevealInExplorer = std::bind_front(&MainWindow::RevealSelectedInExplorer, this);
        fileCommandHandlers.onOpenContainingFolder = std::bind_front(&MainWindow::OpenSelectedContainingFolder, this);
        fileCommandHandlers.onCopyPath = std::bind_front(&MainWindow::CopySelectedPathsToClipboard, this);
        fileCommandHandlers.onCopyFiles = std::bind_front(&MainWindow::CopySelectionFilesToClipboard, this, false);
        fileCommandHandlers.onCut = std::bind_front(&MainWindow::CopySelectionFilesToClipboard, this, true);
        fileCommandHandlers.onCopyImagePixels = [this]
        {
            CopySelectedImagePixelsToClipboard();
        };
        fileCommandHandlers.onPasteFiles = std::bind_front(&MainWindow::PasteClipboardFilesIntoCurrentFolder, this);
        fileCommandHandlers.onUndo = std::bind_front(&MainWindow::PerformUndo, this);
        fileCommandHandlers.onRedo = std::bind_front(&MainWindow::PerformRedo, this);
        fileCommandHandlers.onDuplicateSelection = std::bind_front(&MainWindow::StartDuplicateSelection, this);
        fileCommandHandlers.onSelectAll = [this]
        {
            if (browserPaneController_)
            {
                browserPaneController_->SelectAll();
            }
        };
        fileCommandHandlers.onProperties = std::bind_front(&MainWindow::ShowSelectedFileProperties, this);
        fileCommandHandlers.onEditTags = std::bind_front(&MainWindow::EditSelectionTags, this);
        fileCommandHandlers.onRotateJpeg = std::bind_front(&MainWindow::AdjustSelectedJpegOrientation, this);
        fileCommandHandlers.onBatchConvert = std::bind_front(&MainWindow::StartBatchConvert, this);
        fileCommandHandlers.onCancelBatchConvert = [this]
        {
            if (batchConvertService_ && batchConvertActive_ && !batchConvertCancelling_)
            {
                batchConvertService_->Cancel();
                batchConvertCancelling_ = true;
                UpdateStatusText();
                UpdateMenuState();
            }
        };
        fileCommandController_.Configure(std::move(fileCommandHandlers));

        ViewCommandController::Handlers viewCommandHandlers;
        viewCommandHandlers.onColorManagement = [this]
        {
            colorManagementEnabled_ = !colorManagementEnabled_;
            ApplyColorManagementSetting();
            UpdateMenuState();
            SaveWindowState();
        };
        viewCommandHandlers.onViewerMouseWheelBehavior = [this](UINT commandId)
        {
            viewerMouseWheelBehavior_ = ViewerMouseWheelBehaviorFromCommandId(commandId);
            ApplyViewerMouseWheelSetting();
            UpdateMenuState();
        };
        viewCommandHandlers.onAppTextSize = [this](UINT commandId)
        {
            appTextSize_ = AppTextSizeFromCommandId(commandId);
            ApplyAppTextSize();
            UpdateMenuState();
        };
        viewCommandHandlers.onViewerOverlayTextSize = [this](UINT commandId)
        {
            if (viewer::ViewerWindow* viewer = ActiveViewer())
            {
                viewer->SetOverlayTextSize(ViewerOverlayTextSizeFromCommandId(commandId));
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onRating = [this](UINT commandId)
        {
            SetSelectionRating(RatingFromCommandId(commandId));
        };
        viewCommandHandlers.onThumbnails = [this]
        {
            SetBrowserMode(BrowserMode::Thumbnails);
        };
        viewCommandHandlers.onDetails = [this]
        {
            SetBrowserMode(BrowserMode::Details);
        };
        viewCommandHandlers.onSortMenu = [this]
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                if (toolbarItems_[static_cast<std::size_t>(i)].commandId == ID_ACTION_SORT_MENU)
                {
                    ShowDropdownForItem(i);
                    break;
                }
            }
        };
        viewCommandHandlers.onThumbnailSizeMenu = [this]
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                if (toolbarItems_[static_cast<std::size_t>(i)].commandId == ID_ACTION_THUMBNAIL_SIZE_MENU)
                {
                    ShowDropdownForItem(i);
                    break;
                }
            }
        };
        viewCommandHandlers.onThemeMenu = []
        {
            // Theme is now toggled via menu; no dropdown button.
        };
        viewCommandHandlers.onRecursive = std::bind_front(&MainWindow::ToggleRecursiveBrowsing, this);
        viewCommandHandlers.onShowSubfolders = std::bind_front(&MainWindow::ToggleShowSubfoldersInBrowser, this);
        viewCommandHandlers.onGoToItemNumber = std::bind_front(&MainWindow::PromptForBrowserItemNumber, this);
        viewCommandHandlers.onSettings = std::bind_front(&MainWindow::ShowConsolidatedSettingsDialog, this);
        viewCommandHandlers.onAssociations = std::bind_front(&MainWindow::ShowFileAssociationsDialog, this);
        viewCommandHandlers.onNvJpeg = [this]
        {
            if (HasNvJpegCapability())
            {
                nvJpegEnabled_ = !nvJpegEnabled_;
                decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onLibRaw = [this]
        {
            if (decode::IsLibRawBuildEnabled())
            {
                libRawOutOfProcessEnabled_ = !libRawOutOfProcessEnabled_;
                decode::SetLibRawOutOfProcessEnabled(libRawOutOfProcessEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onPersistentThumbnailCache = [this]
        {
            persistentThumbnailCacheEnabled_ = !persistentThumbnailCacheEnabled_;
            ApplyPersistentThumbnailCacheSetting();
            UpdateMenuState();
        };
        viewCommandHandlers.onPersistentThumbnailCacheManager = std::bind_front(&MainWindow::ShowPersistentThumbnailCacheDialog, this);
        viewCommandHandlers.onSingleInstance = [this]
        {
            app::Application::SetSingleInstanceEnabled(!app::Application::IsSingleInstanceEnabled());
            UpdateMenuState();
        };
        viewCommandHandlers.onPreferJpeg = [this]
        {
            pairedRawJpegViewerPreference_ = browser::RawJpegDisplayPreference::Jpeg;
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
            }
            UpdateMenuState();
        };
        viewCommandHandlers.onPreferRaw = [this]
        {
            pairedRawJpegViewerPreference_ = browser::RawJpegDisplayPreference::Raw;
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
            }
            UpdateMenuState();
        };
        viewCommandHandlers.onDefaultViewerSecondaryMonitor = [this]
        {
            defaultViewerToSecondaryMonitor_ = !defaultViewerToSecondaryMonitor_;
            UpdateMenuState();
        };
        viewCommandHandlers.onUseSlideshowTransition = [this]
        {
            useSlideshowTransition_ = !useSlideshowTransition_;
            ApplyViewerTransitionSettings();
            UpdateMenuState();
        };
        viewCommandHandlers.onThumbnailSizePreset = [this](UINT commandId)
        {
            thumbnailSizePreset_ = ThumbnailSizePresetFromCommandId(commandId);
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
        };
        viewCommandHandlers.onThumbnailSizeIncrease = std::bind_front(&MainWindow::StepThumbnailSize, this, 1);
        viewCommandHandlers.onThumbnailSizeDecrease = std::bind_front(&MainWindow::StepThumbnailSize, this, -1);
        viewCommandHandlers.onThumbnailDetails = [this]
        {
            thumbnailDetailsVisible_ = !thumbnailDetailsVisible_;
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
        };
        viewCommandHandlers.onThumbnailLayoutCompact = [this]
        {
            compactThumbnailLayout_ = !compactThumbnailLayout_;
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
        };
        viewCommandHandlers.onDetailsStrip = std::bind_front(&MainWindow::ToggleDetailsPanelVisibility, this);
        viewCommandHandlers.onSortMode = [this](UINT commandId)
        {
            if (browserPaneController_)
            {
                sortMode_ = SortModeFromCommandId(commandId);
                browserPaneController_->SetSortMode(sortMode_);
                UpdateStatusText();
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onSortDirection = [this]
        {
            if (browserPaneController_)
            {
                sortAscending_ = !browserPaneController_->IsSortAscending();
                browserPaneController_->SetSortAscending(sortAscending_);
                UpdateStatusText();
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onThemeLight = [this]
        {
            SetThemeMode(ThemeMode::Light);
        };
        viewCommandHandlers.onThemeDark = [this]
        {
            SetThemeMode(ThemeMode::Dark);
        };
        viewCommandHandlers.onViewerDetailOverlays = [this]
        {
            if (viewer::ViewerWindow* viewer = ActiveViewer())
            {
                viewer->SetInfoOverlaysVisible(!viewer->AreInfoOverlaysVisible());
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onViewerFullMetadata = [this]
        {
            if (viewer::ViewerWindow* viewer = ActiveViewer())
            {
                viewer->SetFullMetadataVisible(!viewer->IsFullMetadataVisible());
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onPressureStateStatus = [this]
        {
            showPressureStateInStatusBar_ = !showPressureStateInStatusBar_;
            UpdateStatusText();
            UpdateMenuState();
        };
        viewCommandHandlers.onPerformanceHud = [this]
        {
            SetPerformanceHudEnabled(!performanceHudEnabled_);
        };
        viewCommandHandlers.onSlideshowSelection = std::bind_front(&MainWindow::StartSlideshow, this, true);
        viewCommandHandlers.onSlideshowFolder = std::bind_front(&MainWindow::StartSlideshow, this, false);
        viewCommandHandlers.onUserGuide = std::bind_front(&MainWindow::ShowUserGuide, this);
        viewCommandHandlers.onKeyboardShortcuts = std::bind_front(&MainWindow::ShowShortcutReference, this);
        viewCommandHandlers.onAbout = std::bind_front(&MainWindow::ShowAboutDialog, this);
        viewCommandHandlers.onPerformanceSettings = std::bind_front(&MainWindow::ShowPerformanceSettingsDialog, this);
        viewCommandHandlers.onPerformanceProfile = [this](UINT commandId)
        {
            const util::ResourceProfile requestedProfile = ResourceProfileFromCommandId(commandId);
            if (resourceProfile_ != requestedProfile)
            {
                resourceProfile_ = requestedProfile;
                ApplyResourceProfileSetting();
                UpdateDetailsPanel();
                UpdateStatusText();
                UpdateMenuState();
            }
        };
        viewCommandHandlers.onDiagnosticsSnapshot = std::bind_front(&MainWindow::ShowDiagnosticsSnapshot, this);
        viewCommandHandlers.onDiagnosticsExport = std::bind_front(&MainWindow::ExportRedactedDiagnosticsSnapshot, this);
        viewCommandHandlers.onDiagnosticsReset = std::bind_front(&MainWindow::ResetDiagnosticsState, this);
        viewCommandController_.Configure(std::move(viewCommandHandlers));
    }

    MainWindow::~MainWindow()
    {
        if (performanceHudTimerId_ != 0 && hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            KillTimer(hwnd_, performanceHudTimerId_);
        }
        performanceHudTimerId_ = 0;
        StopDetailsPanelPerformanceUpdates();
        accessibility_.reset();
        savedSearchController_.reset();

        if (userMetadataStore_)
        {
            userMetadataStore_->SetNotificationWindow(nullptr);
            userMetadataStore_->Shutdown();
        }

        if (imageCommandService_)
        {
            imageCommandService_->Shutdown();
        }

        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_))
        {
            DestroyWindow(shortcutReferenceWindow_);
            shortcutReferenceWindow_ = nullptr;
        }

        if (externalDropTarget_)
        {
            if (hwnd_)
            {
                RevokeDragDrop(hwnd_);
            }
            externalDropTarget_->Release();
            externalDropTarget_ = nullptr;
        }

        if (folderLoadCoordinator_)
        {
            folderLoadCoordinator_->Cancel();
        }

        if (folderTreeController_)
        {
            folderTreeController_->Cancel();
        }

        if (batchConvertService_)
        {
            batchConvertService_->Shutdown();
        }

        if (fileOperationService_)
        {
            fileOperationService_->Shutdown();
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (actionFieldBrush_)
        {
            DeleteObject(actionFieldBrush_);
        }

        if (detailsPanelBrush_)
        {
            DeleteObject(detailsPanelBrush_);
        }

        if (menuBackgroundBrush_)
        {
            DeleteObject(menuBackgroundBrush_);
        }

        DeleteFontIfOwned(appTextUiFont_);
        DeleteFontIfOwned(detailsPanelTitleFont_);
        DeleteFontIfOwned(detailsPanelSummaryFont_);
        DeleteFontIfOwned(detailsPanelBodyFont_);

        if (detailsPanelRichEditModule_)
        {
            FreeLibrary(detailsPanelRichEditModule_);
            detailsPanelRichEditModule_ = nullptr;
        }

        if (hwnd_)
        {
            RevokeDragDrop(hwnd_);
        }
        RemoveTrayIcon();
        if (externalDropTarget_)
        {
            externalDropTarget_->Release();
            externalDropTarget_ = nullptr;
        }
        if (taskbarList_)
        {
            taskbarList_->Release();
            taskbarList_ = nullptr;
        }

        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
        }
    }

    bool MainWindow::Create()
    {
        util::ScopedTimer timer{L"MainWindow::Create"};

        if (!RegisterWindowClass())
        {
            return false;
        }

        LoadWindowState();
        RebuildThemeBrushes();
        ApplyStartupLaunchPathOverride();
        ApplyResourceProfileSetting();
        ApplyCacheCapacityOverrideSettings();
        ApplyViewerMouseWheelSetting();
        ApplyViewerTransitionSettings();
        ApplyColorManagementSetting();

        int initialWindowX = CW_USEDEFAULT;
        int initialWindowY = CW_USEDEFAULT;
        int initialWindowWidth = CW_USEDEFAULT;
        int initialWindowHeight = CW_USEDEFAULT;
        if (hasPersistedWindowBounds_)
        {
            RECT initialWindowBounds = persistedWindowBounds_;
            CascadeWindowBoundsForAdditionalInstance(&initialWindowBounds);
            initialWindowX = initialWindowBounds.left;
            initialWindowY = initialWindowBounds.top;
            initialWindowWidth = initialWindowBounds.right - initialWindowBounds.left;
            initialWindowHeight = initialWindowBounds.bottom - initialWindowBounds.top;
        }

        hwnd_ = CreateWindowExW(
            0,
            kWindowClassName,
            L"HyperBrowse",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            initialWindowX,
            initialWindowY,
            initialWindowWidth,
            initialWindowHeight,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_)
        {
            util::LogLastError(L"CreateWindowExW(MainWindow)");
            return false;
        }

        if (userMetadataStore_)
        {
            userMetadataStore_->SetNotificationWindow(hwnd_);
        }

        sessionNotificationRegistered_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
        consoleDisplayNotify_ = RegisterPowerSettingNotification(
            hwnd_,
            &kConsoleDisplayStateGuid,
            DEVICE_NOTIFY_WINDOW_HANDLE);
        monitorPowerNotify_ = RegisterPowerSettingNotification(
            hwnd_,
            &kMonitorPowerOnGuid,
            DEVICE_NOTIFY_WINDOW_HANDLE);

        if (!CreateAccelerators() || !CreateMenuBar() || !CreateChildWindows())
        {
            return false;
        }

        accessibility_ = std::make_unique<MainWindowAccessibility>(
            hwnd_,
            [this]
            {
                return BuildAccessibilityItems();
            },
            [this]
            {
                return AccessibilityFocusedChildId();
            },
            [this](long childId)
            {
                return FocusAccessibilityChild(childId);
            },
            [this](long childId)
            {
                return ActivateAccessibilityChild(childId);
            });

        ApplyAppTextSize();

        externalDropTarget_ = new (std::nothrow) ExternalDropTarget(
            hwnd_,
            [this](IDataObject* dataObject, DWORD keyState, POINT clientPoint)
            {
                return UpdateExternalDropFeedback(dataObject, keyState, clientPoint);
            },
            [this](IDataObject* dataObject, DWORD keyState, POINT clientPoint)
            {
                return HandleExternalDrop(dataObject, keyState, clientPoint);
            },
            [this]()
            {
                ClearExternalDropVisuals();
            });
        if (externalDropTarget_ && SUCCEEDED(RegisterDragDrop(hwnd_, externalDropTarget_)))
        {
            // OLE drop target now owns feedback for file drags; the legacy
            // WM_DROPFILES path stays as a fallback for non-OLE sources.
        }
        else
        {
            if (externalDropTarget_)
            {
                externalDropTarget_->Release();
                externalDropTarget_ = nullptr;
            }
            DragAcceptFiles(hwnd_, TRUE);
        }

        savedSearchController_ = std::make_unique<SavedSearchController>(hwnd_);
        savedSearchController_->Load();
        memoryPressureExecutor_ = std::make_unique<util::BackgroundExecutor>(1);
        if (memoryPressureExecutor_)
        {
            memoryPressureTimerId_ = SetTimer(hwnd_, kMemoryPressureTimerId, kMemoryPressureIntervalMs, nullptr);
            QueueMemoryPressureSample();
        }
        gamepadTimerId_ = SetTimer(hwnd_, kGamepadTimerId, kGamepadPollIntervalMs, nullptr);
        if (gamepadTimerId_ == 0)
        {
            util::LogLastError(L"SetTimer(XInput polling)");
        }
        cacheMaintenanceState_ = std::make_shared<PersistentThumbnailCacheMaintenanceState>();

        ApplyPersistentThumbnailCacheSetting();
        ApplyTheme();
        UpdateMenuState();
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        return true;
    }

    void MainWindow::Show(int nCmdShow) const
    {
        ShowWindow(hwnd_, nCmdShow);
        UpdateWindow(hwnd_);
    }

    void MainWindow::SetStartupLaunchPath(std::wstring path)
    {
        startupLaunchPathOverride_ = std::move(path);
    }

    void MainWindow::OpenViewerAtPath(const std::wstring& filePath)
    {
        if (filePath.empty())
        {
            return;
        }

        std::error_code error;
        const fs::path resolvedPath(filePath);
        if (!fs::is_regular_file(resolvedPath, error) || error)
        {
            return;
        }

        if (!browser::IsSupportedImageExtension(resolvedPath.extension().wstring()))
        {
            return;
        }

        const fs::path containingFolder = resolvedPath.parent_path();
        if (containingFolder.empty())
        {
            return;
        }

        const std::wstring folderPath = NormalizeFolderPath(containingFolder.wstring());
        const std::wstring targetPath = NormalizeFolderPath(resolvedPath.wstring());

        // Navigate to the containing folder and mark the dropped file as the pending
        // viewer target; TryOpenPendingStartupViewerPath opens it once enumerated.
        if (folderLoadCoordinator_)
        {
            folderLoadCoordinator_->SetPendingStartupViewerPath(targetPath);
        }
        if (browserModel_ && FolderPathsEqual(browserModel_->FolderPath(), folderPath))
        {
            // Already browsing this folder: open immediately if the item exists.
            const int modelIndex = browserModel_->FindItemIndexByPath(targetPath);
            if (modelIndex >= 0)
            {
                if (folderLoadCoordinator_)
                {
                    folderLoadCoordinator_->ClearPendingStartupViewerPath();
                }
                browserPaneController_->RestoreSelectionByFilePaths({targetPath}, targetPath);
                OpenItemInViewer(modelIndex, ShouldDefaultViewerToSecondaryMonitor());
            }
            return;
        }

        LoadFolderAsync(folderPath);
    }

    void MainWindow::HandleExternalLaunchPath(const std::wstring& path)
    {
        if (!hwnd_)
        {
            return;
        }

        // Bring the existing window forward.
        if (IsIconic(hwnd_))
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }
        else if (!IsWindowVisible(hwnd_))
        {
            ShowWindow(hwnd_, SW_SHOW);
        }
        SetForegroundWindow(hwnd_);

        if (path.empty())
        {
            return;
        }

        std::error_code error;
        const fs::path resolvedPath(path);
        if (fs::is_directory(resolvedPath, error) && !error)
        {
            LoadFolderAsync(NormalizeFolderPath(resolvedPath.wstring()));
            return;
        }

        // A file path opens in the viewer.
        OpenViewerAtPath(path);
    }

    bool MainWindow::TranslateAcceleratorMessage(MSG* message)
    {
        if (!hwnd_ || !message)
        {
            return false;
        }

        // Do not translate accelerators when the message is destined for the viewer
        // window or one of its descendants. The viewer has its own keyboard handling
        // (notably for VK_DELETE) and must not be pre-empted by the main window's
        // browser-pane accelerators.
        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            const HWND viewerHwnd = viewer->Hwnd();
            if (viewerHwnd && message->hwnd
                && (message->hwnd == viewerHwnd || IsChild(viewerHwnd, message->hwnd)))
            {
                return false;
            }
        }

        if (message->message == WM_XBUTTONDOWN
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd)))
        {
            const UINT commandId = command_ids::CommandIdFromXButton(GET_XBUTTON_WPARAM(message->wParam));
            if (commandId != 0)
            {
                HandleCommand(commandId);
                return true;
            }
        }

        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_)
            && message->hwnd
            && (message->hwnd == shortcutReferenceWindow_ || IsChild(shortcutReferenceWindow_, message->hwnd))
            && IsDialogMessageW(shortcutReferenceWindow_, message))
        {
            return true;
        }

        if ((message->message == WM_SYSKEYDOWN || message->message == WM_SYSCHAR)
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
            && HandleCommandBarKeyboardInput(message->message, message->wParam, message->lParam))
        {
            return true;
        }

        if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
            && HandleKeyboardFocusInput(message->message, message->wParam, message->lParam))
        {
            return true;
        }

        if (message->message == WM_KEYDOWN
            && message->wParam == static_cast<WPARAM>('F')
            && (GetKeyState(VK_CONTROL) & 0x8000) != 0
            && (GetKeyState(VK_SHIFT) & 0x8000) == 0
            && (GetKeyState(VK_MENU) & 0x8000) == 0
            && (message->lParam & (1LL << 30)) == 0
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd)))
        {
            const HWND focus = GetFocus();
            if (!IsTextInputControlWindow(focus) || focus == filterEdit_)
            {
                if (filterEdit_)
                {
                    SetFocus(filterEdit_);
                    SendMessageW(filterEdit_, EM_SETSEL, 0, -1);
                    return true;
                }
            }
        }

        if (message->message == WM_KEYDOWN
            && IsTextInputControlWindow(message->hwnd)
            && (message->wParam == VK_BACK || message->wParam == VK_DELETE
                || message->wParam == VK_LEFT || message->wParam == VK_RIGHT
                || message->wParam == VK_UP || message->wParam == VK_DOWN
                || message->wParam == VK_PRIOR || message->wParam == VK_NEXT
                || message->wParam == VK_HOME || message->wParam == VK_END
                || message->wParam == VK_OEM_MINUS
                || message->wParam == VK_OEM_PLUS
                || ((message->wParam == static_cast<WPARAM>('A')
                     || message->wParam == static_cast<WPARAM>('C')
                     || message->wParam == static_cast<WPARAM>('G')
                     || message->wParam == static_cast<WPARAM>('V')
                     || message->wParam == static_cast<WPARAM>('X')
                     || message->wParam == static_cast<WPARAM>('Y')
                     || message->wParam == static_cast<WPARAM>('Z'))
                    && (GetKeyState(VK_CONTROL) & 0x8000) != 0)))
        {
            return false;
        }

        if (message->message == WM_SYSKEYDOWN
            && message->wParam == VK_UP
            && IsTextInputControlWindow(message->hwnd))
        {
            return false;
        }

        const HWND browserPaneHwnd = browserPaneController_ ? browserPaneController_->Hwnd() : nullptr;
        if (message->message == WM_KEYDOWN
            && browserPaneHwnd
            && message->hwnd == browserPaneHwnd
            && GetFocus() == browserPaneHwnd
            && browserPaneController_->GetViewMode() == browser::BrowserViewMode::Thumbnails)
        {
            const auto rating = browser::ThumbnailRatingKeyPolicy::RatingFromVirtualKey(
                static_cast<UINT>(message->wParam),
                GetKeyState(VK_CONTROL) < 0,
                GetKeyState(VK_SHIFT) < 0,
                GetKeyState(VK_MENU) < 0);
            if (rating)
            {
                const bool keyRepeat = (message->lParam & (1LL << 30)) != 0;
                if (!keyRepeat
                    && !fileOperationActive_
                    && !browserPaneController_->SelectedFilePathsSnapshot().empty())
                {
                    SetSelectionRating(*rating);
                }
                return true;
            }
        }

        if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
            && browserPaneController_
            && (message->wParam != VK_RETURN
                || CurrentKeyboardFocusTarget().kind == KeyboardFocusTargetKind::BrowserPane)
            && browserPaneController_->HandleNavigationKey(message->message,
                                                            message->wParam,
                                                            message->lParam))
        {
            return true;
        }

        if (!accelerators_)
        {
            return false;
        }

        // Do not translate the Escape accelerator when a folder drag is active.
        // The drag operation should have exclusive control over the Escape key to cancel itself.
        if (folderTreeDragController_.IsActive() && message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE)
        {
            return false;
        }

        if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
            && message->wParam == VK_ESCAPE
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
            && FindViewerByHwnd(activeViewerWindow_))
        {
            const HWND viewerHwnd = activeViewerWindow_;
            if (viewerHwnd && IsWindow(viewerHwnd) != FALSE
                && SendMessageW(viewerHwnd, WM_KEYDOWN, VK_ESCAPE, message->lParam) == 0)
            {
                return true;
            }
        }

        return TranslateAcceleratorW(hwnd_, accelerators_, message) != 0;
    }

    bool MainWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MainWindow::WindowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        wc.hIconSm = static_cast<HICON>(
            LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE),
                       IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

        const bool mainClassRegistered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

        WNDCLASSEXW toastClass = wc;
        toastClass.lpfnWndProc = &MainWindow::QuickSendConfirmationToastWindowProc;
        toastClass.lpszClassName = kQuickSendConfirmationToastWindowClassName;
        toastClass.hIcon = nullptr;
        toastClass.hIconSm = nullptr;
        toastClass.style = CS_HREDRAW | CS_VREDRAW;
        const bool toastClassRegistered = RegisterClassExW(&toastClass) != 0
            || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

        return mainClassRegistered && toastClassRegistered;
    }

    bool MainWindow::CreateAccelerators()
    {
        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
            accelerators_ = nullptr;
        }

        if (!kShortcutCatalogValid)
        {
            return false;
        }

        std::vector<ACCEL> accelerators;
        accelerators.reserve(MainWindowShortcuts().size());
        for (const ShortcutDefinition& shortcut : MainWindowShortcuts())
        {
            if (shortcut.commandId == 0)
            {
                continue;
            }
            accelerators.push_back(ACCEL{
                static_cast<BYTE>(FVIRTKEY | shortcut.modifiers),
                shortcut.virtualKey,
                static_cast<WORD>(shortcut.commandId)});
        }

        accelerators_ = CreateAcceleratorTableW(accelerators.data(), static_cast<int>(accelerators.size()));
        return accelerators_ != nullptr;
    }

    bool MainWindow::CreateMenuBar()
    {
        menu_ = CreateMenu();
        fileMenu_ = CreatePopupMenu();
        editMenu_ = CreatePopupMenu();
        viewMenu_ = CreatePopupMenu();
        toolsMenu_ = CreatePopupMenu();
        helpMenu_ = CreatePopupMenu();
        openRecentFolderMenu_ = CreatePopupMenu();
        savedSearchMenu_ = CreatePopupMenu();
        copySelectionToMenu_ = CreatePopupMenu();
        moveSelectionToMenu_ = CreatePopupMenu();
        HMENU fileMetadataMenu = CreatePopupMenu();
        HMENU fileOrganizeMenu = CreatePopupMenu();
        HMENU fileConvertMenu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU batchConvertFolderMenu = CreatePopupMenu();
        HMENU ratingMenu = CreatePopupMenu();
        HMENU viewMenu = viewMenu_;
        HMENU editMenu = editMenu_;
        HMENU sortMenu = CreatePopupMenu();
        HMENU thumbnailSizeMenu = CreatePopupMenu();
        HMENU slideshowMenu = CreatePopupMenu();
        HMENU diagnosticsMenu = CreatePopupMenu();
        HMENU helpMenu = helpMenu_;

        if (!menu_ || !fileMenu_ || !editMenu_ || !viewMenu_ || !toolsMenu_ || !helpMenu_ || !openRecentFolderMenu_ || !savedSearchMenu_ || !copySelectionToMenu_ || !moveSelectionToMenu_ || !fileMetadataMenu || !fileOrganizeMenu || !fileConvertMenu || !batchConvertSelectionMenu || !batchConvertFolderMenu || !ratingMenu || !sortMenu || !thumbnailSizeMenu || !slideshowMenu || !diagnosticsMenu)
        {
            return false;
        }

        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_FOLDER, L"Open &Folder...\tCtrl+O");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_NEW_FOLDER, L"New &Folder...\tCtrl+Shift+N");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(openRecentFolderMenu_), L"Open &Recent Folder");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(savedSearchMenu_), L"Open Saved &Search");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_SAVE_CURRENT_FILTER, L"Save Current &Filter...");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree\tF5");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION, L"Add Current Folder to Quick &Actions");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_CLEAR_FAVORITE_DESTINATIONS, L"&Clear All Quick Actions");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_IN_NEW_VIEWER_WINDOW, L"Open in New Viewer &Window\tCtrl+Shift+Enter");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_COMPARE_SELECTED, L"Compare &Selected");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_VIEW_ON_SECONDARY_MONITOR, L"View on Secondary &Monitor");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information\tCtrl+I");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_PROPERTIES, L"&Properties\tAlt+Enter");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer\tCtrl+E");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing Fol&der");
        AppendMenuW(editMenu, MF_STRING, ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
        AppendMenuW(editMenu, MF_STRING, ID_EDIT_REDO, L"&Redo\tCtrl+Y");
        AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(editMenu, MF_STRING, ID_EDIT_CUT, L"Cu&t\tCtrl+X");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_COPY_FILES_TO_CLIPBOARD, L"&Copy\tCtrl+C");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_COPY_IMAGE_PIXELS, L"Copy &Image\tCtrl+Shift+I");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_PASTE_FILES, L"&Paste\tCtrl+V");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h\tCtrl+Shift+C");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_SELECT_ALL, L"Select &All\tCtrl+A");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_QUICK_SEND_MOVE, L"Quick Actions &Move\tF7");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_QUICK_SEND_COPY, L"Quick Actions &Copy\tF8");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_RESUME_FILING, L"&Resume Filing Position\tF4");
        AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_0, L"&Clear Rating");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_1, L"&1 Star");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_2, L"&2 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_3, L"&3 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_4, L"&4 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_5, L"&5 Stars");
        AppendMenuW(fileMetadataMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set &Rating");
        AppendMenuW(fileMetadataMenu, MF_STRING, ID_FILE_EDIT_TAGS, L"Edit &Tags...");
        AppendMenuW(editMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMetadataMenu), L"&Metadata");

        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_RENAME_SELECTED, L"Re&name...\tF2");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_BATCH_RENAME_SELECTION, L"Batch R&ename...\tCtrl+Shift+R");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_DUPLICATE_SELECTION, L"Dup&licate\tCtrl+D");
        AppendMenuW(fileOrganizeMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileOrganizeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(copySelectionToMenu_), L"Cop&y Selection To\tF8");
        AppendMenuW(fileOrganizeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(moveSelectionToMenu_), L"Mo&ve Selection To\tF7");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS, L"Include Paired &RAW+JPEG");
        AppendMenuW(fileOrganizeMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete\tDel");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently\tShift+Del");
        AppendMenuW(editMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileOrganizeMenu), L"&Organize");

        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_JPEG, L"Folder to JPE&G");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_PNG, L"Folder to P&NG");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_TIFF, L"Folder to TIF&F");
        AppendMenuW(fileConvertMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
        AppendMenuW(fileConvertMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertFolderMenu), L"Batch Convert &Folder");
        AppendMenuW(fileConvertMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileConvertMenu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
        AppendMenuW(fileConvertMenu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");
        AppendMenuW(fileConvertMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileConvertMenu, MF_STRING, ID_FILE_BATCH_CONVERT_CANCEL, L"&Cancel Batch Convert");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileConvertMenu), L"Con&vert");

        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_ESCAPE, closeMainWindowOnEscape_ ? L"C&lose\tEsc" : L"I&gnore\tEsc");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_EXIT, L"E&xit");

        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode\tCtrl+1");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode\tCtrl+2");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NAVIGATE_BACK_FOLDER, L"Navigate &Back\tBackspace / Alt+Left");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NAVIGATE_FORWARD_FOLDER, L"Navigate &Forward\tAlt+Right");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NAVIGATE_PARENT_FOLDER, L"Navigate to &Parent\tAlt+Up");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_GO_TO_ITEM_NUMBER, kGoToItemNumberMenuLabel.data());
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_RECURSIVE, kRecursiveBrowsingMenuLabel.data());
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_SHOW_SUBFOLDERS, L"Show &Subfolders");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAIL_DETAILS, kThumbnailDetailsMenuLabel.data());
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS_STRIP, kDetailsPanelMenuLabel.data());
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_PERFORMANCE_HUD, L"Performance &HUD\tCtrl+Shift+P");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_COLOR_MANAGEMENT, L"Color &Management");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAIL_LAYOUT_COMPACT, L"&Compact Thumbnail Layout");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
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
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_INCREASE, L"Increase Size\t+ / =");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_DECREASE, L"Decrease Size\t- / _");
        AppendMenuW(thumbnailSizeMenu, MF_SEPARATOR, 0, nullptr);
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
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(thumbnailSizeMenu), L"Thumbnail Si&ze");
        AppendMenuW(slideshowMenu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"From &Selection\tCtrl+Shift+S");
        AppendMenuW(slideshowMenu, MF_STRING, ID_VIEW_SLIDESHOW_FOLDER, L"From &Folder\tCtrl+Shift+F");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(slideshowMenu), L"S&lideshow");

        AppendMenuW(helpMenu, MF_STRING, ID_HELP_USER_GUIDE, L"&User Guide\tF1");
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_KEYBOARD_SHORTCUTS, L"&Keyboard Shortcuts...");
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_ABOUT, L"&About");
        AppendMenuW(diagnosticsMenu, MF_STRING, ID_HELP_DIAGNOSTICS_SNAPSHOT, L"&Snapshot\tCtrl+Shift+D");
        AppendMenuW(diagnosticsMenu, MF_STRING, ID_HELP_DIAGNOSTICS_EXPORT, L"Export &Redacted Snapshot...");
        AppendMenuW(diagnosticsMenu, MF_STRING, ID_HELP_DIAGNOSTICS_RESET, L"&Reset\tCtrl+Shift+X");
        AppendMenuW(diagnosticsMenu, MF_STRING, ID_VIEW_PERSISTENT_THUMBNAIL_CACHE_MANAGER, L"Persistent Cache S&tats and Cleanup...");

        AppendMenuW(toolsMenu_, MF_STRING, ID_VIEW_SETTINGS, L"&Settings...\tCtrl+Shift+T");
        AppendMenuW(toolsMenu_, MF_STRING, ID_FILE_ASSOCIATIONS, L"File &Associations...");
        AppendMenuW(toolsMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(toolsMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(diagnosticsMenu), L"&Diagnostics");

        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu_), L"&File");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"&Edit");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(toolsMenu_), L"&Tools");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

        RefreshPersistentMenuOwnerDraw();
        commandBarController_.SetMenuButton(0, L"File", L'F', fileMenu_);
        commandBarController_.SetMenuButton(1, L"Edit", L'E', editMenu_);
        commandBarController_.SetMenuButton(2, L"View", L'V', viewMenu_);
        commandBarController_.SetMenuButton(3, L"Tools", L'T', toolsMenu_);
        commandBarController_.SetMenuButton(4, L"Help", L'H', helpMenu_);

        SetMenu(hwnd_, nullptr);
        DrawMenuBar(hwnd_);
        return true;
    }

    bool MainWindow::CreateChildWindows()
    {
        RebuildAppTextFonts();
        const HFONT defaultGuiFont = appTextUiFont_
            ? appTextUiFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        toolbarIconLibrary_ = std::make_unique<ToolbarIconLibrary>();
        if (toolbarIconLibrary_ && !toolbarIconLibrary_->Initialize())
        {
            util::LogError(L"Toolbar SVG icon library failed to initialize.");
        }

        InitToolbarItems();

        filterEdit_ = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 120, kToolbarFilterEditHeight,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION_FILTER_EDIT)),
            instance_,
            nullptr);

        if (filterEdit_)
        {
            SendMessageW(filterEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);
            SendMessageW(filterEdit_, EM_LIMITTEXT, services::SavedSearchStore::kMaximumExpressionCharacters, 0);
            SendMessageW(filterEdit_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Filter"));
        }

        tooltipControl_ = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd_,
            nullptr,
            instance_,
            nullptr);
        if (tooltipControl_)
        {
            SendMessageW(tooltipControl_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);
            SendMessageW(tooltipControl_, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELPARAM(400, 0));
            SendMessageW(tooltipControl_, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELPARAM(100, 0));
            SendMessageW(tooltipControl_,
                         TTM_SETMAXTIPWIDTH,
                         0,
                         hyperbrowse::util::ScaleAppTextDimension(kToolbarTooltipMaxWidth, appTextSize_));

            if (filterEdit_)
            {
                TTTOOLINFOW filterToolInfo{};
                filterToolInfo.cbSize = sizeof(filterToolInfo);
                filterToolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                filterToolInfo.hwnd = hwnd_;
                filterToolInfo.uId = reinterpret_cast<UINT_PTR>(filterEdit_);
                filterToolInfo.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&filterToolInfo));
            }

            for (int index = 0; index < static_cast<int>(toolbarItems_.size()); ++index)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.uFlags = TTF_SUBCLASS;
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = static_cast<UINT_PTR>(index);
                toolInfo.rect = toolbarItems_[static_cast<std::size_t>(index)].rect;
                toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }

            TTTOOLINFOW histogramToolInfo{};
            histogramToolInfo.cbSize = sizeof(histogramToolInfo);
            histogramToolInfo.uFlags = TTF_SUBCLASS;
            histogramToolInfo.hwnd = hwnd_;
            histogramToolInfo.uId = kDetailsPanelHistogramTooltipId;
            histogramToolInfo.rect = detailsPanelHistogramRect_;
            histogramToolInfo.lpszText = LPSTR_TEXTCALLBACKW;
            detailsPanelHistogramTooltipAdded_ = SendMessageW(tooltipControl_,
                                                               TTM_ADDTOOLW,
                                                               0,
                                                               reinterpret_cast<LPARAM>(&histogramToolInfo)) != FALSE;
        }

        treePane_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_TREEVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (tooltipControl_ && treePane_)
        {
            TTTOOLINFOW toolInfo{};
            toolInfo.cbSize = sizeof(toolInfo);
            toolInfo.uFlags = TTF_IDISHWND;
            toolInfo.hwnd = hwnd_;
            toolInfo.uId = reinterpret_cast<UINT_PTR>(treePane_);
            toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        }
        if (treePane_)
        {
            SetWindowSubclass(treePane_, &MainWindow::FolderTreeTooltipSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }

        statusBar_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusStripControlId)),
            instance_,
            nullptr);

        if (!detailsPanelRichEditModule_)
        {
            detailsPanelRichEditModule_ = LoadLibraryW(L"Msftedit.dll");
        }

        const DWORD detailsPanelTextStyle = WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | (detailsStripVisible_ ? WS_VISIBLE : 0) | WS_VSCROLL
            | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL;
        detailsPanelText_ = CreateWindowExW(
            0,
            detailsPanelRichEditModule_ ? MSFTEDIT_CLASS : L"EDIT",
            L"",
            detailsPanelTextStyle,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!detailsPanelText_ && detailsPanelRichEditModule_)
        {
            FreeLibrary(detailsPanelRichEditModule_);
            detailsPanelRichEditModule_ = nullptr;
            detailsPanelText_ = CreateWindowExW(
                0,
                L"EDIT",
                L"",
                detailsPanelTextStyle,
                0, 0, 100, 100,
                hwnd_,
                nullptr,
                instance_,
                nullptr);
        }

        if (detailsPanelText_)
        {
            SetWindowSubclass(detailsPanelText_,
                              &MainWindow::DetailsPanelTextSubclassProc,
                              1,
                              reinterpret_cast<DWORD_PTR>(this));
        }

            quickAccessScrollBar_ = CreateWindowExW(
                0,
                L"SCROLLBAR",
                nullptr,
                WS_CHILD | WS_TABSTOP | SBS_VERT,
                0, 0, 0, 0,
                hwnd_,
                nullptr,
                instance_,
                nullptr);

            if (!filterEdit_ || !treePane_ || !statusBar_ || !detailsPanelText_ || !quickAccessScrollBar_)
        {
            util::LogLastError(L"CreateChildWindows");
            return false;
        }

        SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);

        SendMessageW(detailsPanelText_, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
        RefreshDetailsPanelBodyPresentation();

        detailsPanelThumbnailScheduler_ = std::make_unique<services::ThumbnailScheduler>(
            thumbnailCacheCapacityOverrideBytes_,
            0,
            resourceProfile_);
        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->BindTargetWindow(hwnd_);
        }

        if (!browserPaneController_ || !browserPaneController_->Create(hwnd_))
        {
            util::LogError(L"Failed to create the browser pane control");
            return false;
        }

        browserPaneController_->SetUserMetadataStore(userMetadataStore_.get());

        browserPane_ = browserPaneController_->Hwnd();

        SendMessageW(treePane_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);

        SHFILEINFOW shellInfo{};
        treeImageList_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            L"C:\\",
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
        if (treeImageList_)
        {
            ImageList_SetBkColor(treeImageList_, CLR_NONE);
            TreeView_SetImageList(treePane_, treeImageList_, TVSIL_NORMAL);
        }
        ApplyFolderTreeMetrics();

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        ApplyRawJpegPairingSettings();
        ApplyThumbnailDisplaySettings();
        decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
        decode::SetLibRawOutOfProcessEnabled(libRawOutOfProcessEnabled_);

        if (folderLoadCoordinator_)
        {
            FolderLoadCoordinator::Handlers handlers;
            handlers.onLoadStarted = [this](const std::wstring& folderPath)
            {
                util::LogInfo(L"Queueing folder enumeration for " + folderPath);
                if (browserModel_)
                {
                    browserModel_->Reset(folderPath, recursiveBrowsingEnabled_);
                }
                if (browserPaneController_)
                {
                    browserPaneController_->BeginFolderLoad();
                    browserPaneController_->ClearSelection();
                    browserPaneController_->SetFilterQuery({});
                }
                if (filterEdit_)
                {
                    SetWindowTextW(filterEdit_, L"");
                }
                RefreshBrowserPane();
                UpdateStatusText();
                UpdateWindowTitle();
                UpdateWindow(browserPane_);
            };
            handlers.onLoadQueued = [this](const std::wstring&)
            {
                UpdateStatusText();
                if (folderTreeController_ && browserModel_)
                {
                    folderTreeController_->SelectFolder(browserModel_->FolderPath());
                }
            };
            handlers.onBatch = [this](std::vector<browser::BrowserItem> items,
                                      std::uint64_t totalCount,
                                      std::uint64_t totalBytes)
            {
                if (browserModel_)
                {
                    browserModel_->AppendItems(std::move(items), totalCount, totalBytes);
                }
            };
            handlers.onCompleted = [this](std::wstring folderPath,
                                          std::uint64_t totalCount,
                                          std::uint64_t)
            {
                if (!browserModel_)
                {
                    return;
                }

                browserModel_->Complete();
                util::LogInfo(L"Completed folder enumeration for " + folderPath);
                if (totalCount > 0)
                {
                    RecordRecentFolder(folderPath);
                }
            };
            handlers.onFailed = [this](std::wstring, std::wstring message)
            {
                if (browserModel_)
                {
                    browserModel_->Fail(message);
                }
                util::LogError(message);
            };
            handlers.onPresentation = [this](bool clearStartupPathsIfNotFound)
            {
                return FlushFolderEnumerationPresentation(clearStartupPathsIfNotFound);
            };
            handlers.onRestoreSelection = [this](const std::vector<std::wstring>& selectionPaths,
                                                 const std::wstring& focusedPath,
                                                 bool clearIfNotFound,
                                                 bool startupSelection)
            {
                if (selectionPaths.empty() || !browserModel_ || !browserPaneController_)
                {
                    return false;
                }

                const bool anyPathFound = std::any_of(
                    selectionPaths.begin(),
                    selectionPaths.end(),
                    [&](const std::wstring& path)
                    {
                        return browserModel_->FindItemIndexByPath(path) >= 0;
                    });
                if (!anyPathFound)
                {
                    if (clearIfNotFound && startupSelection)
                    {
                        util::LogInfo(L"Startup selected image was not found in the enumerated folder: " + selectionPaths.front());
                    }
                    return clearIfNotFound;
                }

                browserPaneController_->RestoreSelectionByFilePaths(selectionPaths, focusedPath);
                browserPaneController_->EnsureFocusedItemVisible();
                return true;
            };
            handlers.onOpenViewer = [this](const std::wstring& path, bool clearIfNotFound)
            {
                FolderLoadCoordinator::PendingViewerResult result;
                if (!browserModel_ || !browserPaneController_)
                {
                    return result;
                }

                const int modelIndex = browserModel_->FindItemIndexByPath(path);
                if (modelIndex < 0)
                {
                    if (clearIfNotFound)
                    {
                        util::LogInfo(L"Startup launch image was not found in the enumerated folder: " + path);
                        result.consumed = true;
                    }
                    return result;
                }

                browserPaneController_->RestoreSelectionByFilePaths({path}, path);
                OpenItemInViewer(modelIndex, ShouldDefaultViewerToSecondaryMonitor());
                result.consumed = true;
                result.opened = viewerWindow_
                    && viewerWindow_->IsOpen()
                    && browser::FilePathsEqual(viewerWindow_->CurrentFilePath(), path);
                return result;
            };
            handlers.onViewerSettled = [this]()
            {
                if (viewerWindow_ && viewerWindow_->IsOpen())
                {
                    SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
                }
            };
            handlers.onWatchUpdate = [this](const services::FolderWatchUpdate& update)
            {
                ApplyFolderWatchChanges(update);
            };
            folderLoadCoordinator_->Configure(hwnd_, std::move(handlers));
        }

        if (folderWatchChangeCoordinator_)
        {
            FolderWatchChangeCoordinator::Handlers handlers;
            handlers.onClearTreePresence = [this]()
            {
                if (folderTreeController_)
                {
                    folderTreeController_->ClearChildPresenceCache();
                }
            };
            handlers.onInvalidateTreePresence = [this](std::wstring_view path)
            {
                InvalidateFolderTreeChildPresence(path);
            };
            handlers.onHasTreeItem = [this](std::wstring_view path)
            {
                return FindFolderTreeItemByPath(std::wstring(path)) != nullptr;
            };
            handlers.onRefreshTree = [this]()
            {
                RefreshFolderTree();
            };
            handlers.onInsertTreeFolder = [this](std::wstring_view path)
            {
                InsertFolderTreeFolderIfParentLoaded(std::wstring(path));
            };
            handlers.onReloadFolder = [this](std::wstring folderPath)
            {
                if (!browserPaneController_)
                {
                    return;
                }

                const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
                const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();
                LoadFolderAsync(std::move(folderPath));
                if (folderLoadCoordinator_)
                {
                    folderLoadCoordinator_->SetPendingFolderReloadSelection(
                        selectedPaths,
                        focusedPath);
                }
            };
            handlers.onQueueWatchReload = [this](std::wstring folderPath)
            {
                if (folderLoadCoordinator_)
                {
                    folderLoadCoordinator_->QueueWatchReload(std::move(folderPath));
                }
            };
            handlers.onMarkWatchTreeRefresh = [this]()
            {
                if (folderLoadCoordinator_)
                {
                    folderLoadCoordinator_->MarkWatchTreeRefresh();
                }
            };
            handlers.onRefreshBrowser = [this]()
            {
                RefreshBrowserPane();
            };
            handlers.onRestoreSelection = [this](const std::vector<std::wstring>& selectionPaths,
                                                 const std::wstring& focusedPath)
            {
                if (browserPaneController_)
                {
                    browserPaneController_->RestoreSelectionByFilePaths(selectionPaths, focusedPath);
                }
            };
            handlers.onUpdatePresentation = [this]()
            {
                UpdateStatusText();
                UpdateWindowTitle();
            };
            folderWatchChangeCoordinator_->Configure(std::move(handlers));
        }

        if (folderTreeController_)
        {
            FolderTreeController::Handlers handlers;
            handlers.onStatusChanged = [this]()
            {
                UpdateStatusText();
            };
            handlers.onSelectionSuppressionChanged = [this](bool suppressed)
            {
                suppressTreeSelectionChange_ = suppressed;
            };
            folderTreeController_->Configure(hwnd_, treePane_, std::move(handlers));
            folderTreeController_->Initialize(browserModel_ ? browserModel_->FolderPath() : std::wstring{});
        }
        FolderTreeDragController::Handlers treeDragHandlers;
        treeDragHandlers.isFileOperationActive = [this]
        {
            return fileOperationActive_;
        };
        treeDragHandlers.nodePath = [this](HTREEITEM item)
        {
            const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
            return nodeData ? nodeData->path : std::wstring{};
        };
        treeDragHandlers.normalizePath = [](std::wstring path)
        {
            return NormalizeFolderPath(std::move(path));
        };
        treeDragHandlers.isExistingDirectory = [](std::wstring_view path)
        {
            return IsExistingDirectory(path);
        };
        treeDragHandlers.areFoldersOnSameDrive = [](std::wstring_view sourcePath, std::wstring_view destinationPath)
        {
            return AreFoldersOnSameDrive(sourcePath, destinationPath);
        };
        treeDragHandlers.onMove = [this](std::wstring sourcePath, std::wstring destinationPath)
        {
            StartFolderTreeMoveToDestination(std::move(sourcePath), std::move(destinationPath));
        };
        folderTreeDragController_.Configure(hwnd_, treePane_, std::move(treeDragHandlers));

        bool startupFolderLoadQueued = false;
        if (!startupFolderPath_.empty())
        {
            std::error_code error;
            if (fs::is_directory(fs::path(startupFolderPath_), error) && !error)
            {
                if (folderLoadCoordinator_)
                {
                    folderLoadCoordinator_->SetPendingStartupSelectionPath({});
                }
                if (folderLoadCoordinator_
                    && !folderLoadCoordinator_->HasPendingStartupViewerPath()
                    && !startupSelectedImagePath_.empty())
                {
                    const std::wstring startupImageParentPath = NormalizeFolderPath(fs::path(startupSelectedImagePath_).parent_path().wstring());
                    if (!startupImageParentPath.empty() && FolderPathsEqual(startupImageParentPath, startupFolderPath_))
                    {
                        folderLoadCoordinator_->SetPendingStartupSelectionPath(startupSelectedImagePath_);
                    }
                }

                LoadFolderAsync(startupFolderPath_);
                startupFolderLoadQueued = true;
            }
        }
        if (!startupFolderLoadQueued)
        {
            RefreshBrowserPane();
        }
        UpdateStatusText();
        UpdateToolbarItemStates();
        UpdateDetailsPanel();
        LayoutChildren();
        return true;
    }

    void MainWindow::ApplyThumbnailDisplaySettings()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetThumbnailSizePreset(thumbnailSizePreset_);
        browserPaneController_->SetCompactThumbnailLayout(compactThumbnailLayout_);
        browserPaneController_->SetThumbnailDetailsVisible(thumbnailDetailsVisible_);
        browserPaneController_->SetAppTextSize(appTextSize_);
    }

    LRESULT MainWindow::HandleNotifyMessage(LPARAM lParam)
    {
        const auto* nmh = reinterpret_cast<NMHDR*>(lParam);
        if (nmh->hwndFrom == tooltipControl_ && nmh->code == TTN_GETDISPINFOW)
        {
            auto* di = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            if (filterEdit_ && di->hdr.idFrom == reinterpret_cast<UINT_PTR>(filterEdit_))
            {
                di->lpszText = const_cast<LPWSTR>(kFilterEditTooltip);
                return 0;
            }

            if (treePane_ && di->hdr.idFrom == reinterpret_cast<UINT_PTR>(treePane_))
            {
                treeFolderTooltipText_.clear();
                if (!treeTooltipPath_.empty())
                {
                    if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(treeTooltipPath_))
                    {
                        const wchar_t shortcutCharacter = QuickSendModel::ShortcutCharacter(*assignedShortcut);
                        if (shortcutCharacter != L'\0')
                        {
                            treeFolderTooltipText_ = L"Move: F7,";
                            treeFolderTooltipText_.push_back(shortcutCharacter);
                            treeFolderTooltipText_.append(L"  Copy: F8,");
                            treeFolderTooltipText_.push_back(shortcutCharacter);
                        }
                    }
                }

                di->lpszText = const_cast<wchar_t*>(treeFolderTooltipText_.c_str());
                return 0;
            }

            if (di->hdr.idFrom == kQuickAccessSortTooltipId)
            {
                di->lpszText = const_cast<LPWSTR>(L"Sort Quick Actions destinations by hotkey");
                return 0;
            }

            if (di->hdr.idFrom == kDetailsPanelHistogramTooltipId)
            {
                di->lpszText = const_cast<LPWSTR>(L"RGB histogram: shows the distribution of pixel brightness across the red, green, and blue channels.");
                return 0;
            }

            const auto idx = static_cast<std::size_t>(di->hdr.idFrom);
            if (idx < toolbarItems_.size()
                && toolbarItems_[idx].kind == ToolbarItemKind::FilterClear
                && !toolbarItems_[idx].enabled)
            {
                di->lpszText = const_cast<LPWSTR>(L"");
                return 0;
            }

            if (idx < toolbarItems_.size() && !toolbarItems_[idx].tooltip.empty())
            {
                di->lpszText = const_cast<wchar_t*>(toolbarItems_[idx].tooltip.c_str());
            }
            return 0;
        }

        return OnFolderTreeNotify(lParam);
    }

    std::optional<LRESULT> MainWindow::HandleMouseInputMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_LBUTTONDOWN:
            OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_XBUTTONDOWN:
        {
            const UINT commandId = command_ids::CommandIdFromXButton(GET_XBUTTON_WPARAM(wParam));
            if (commandId != 0)
            {
                HandleCommand(commandId);
                return 0;
            }
            return std::nullopt;
        }
        case WM_PARENTNOTIFY:
            if (commandBarKeyboardActive_)
            {
                switch (LOWORD(wParam))
                {
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN:
                case WM_XBUTTONDOWN:
                    DeactivateCommandBarKeyboardMode(false);
                    break;
                default:
                    break;
                }
            }
            return std::nullopt;
        case WM_LBUTTONDBLCLK:
            OnLButtonDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            OnLButtonUp();
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEWHEEL:
            if (OnQuickAccessMouseWheel(wParam, lParam))
            {
                return 0;
            }
            return std::nullopt;
        case WM_VSCROLL:
            if (reinterpret_cast<HWND>(lParam) == quickAccessScrollBar_)
            {
                OnQuickAccessScroll(wParam);
                return 0;
            }
            return std::nullopt;
        case WM_CAPTURECHANGED:
            if (folderTreeDragController_.IsActive())
            {
                FinishFolderTreeDrag(false);
                return 0;
            }
            if (dragMode_ == DragMode::QuickAccessInternal)
            {
                FinishInternalSelectionDrag(false);
                return 0;
            }
            return std::nullopt;
        case WM_CANCELMODE:
            if (folderTreeDragController_.IsActive())
            {
                FinishFolderTreeDrag(false);
                return 0;
            }
            if (dragMode_ == DragMode::QuickAccessInternal)
            {
                FinishInternalSelectionDrag(false);
                return 0;
            }
            return std::nullopt;
        case WM_DROPFILES:
            return OnDropFiles(reinterpret_cast<HDROP>(wParam));
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (folderTreeDragController_.IsActive())
            {
                SetCursor(LoadCursorW(nullptr,
                                      folderTreeDragController_.IsDropAllowed() ? IDC_HAND : IDC_NO));
                return TRUE;
            }
            if (dragMode_ == DragMode::QuickAccessInternal)
            {
                SetCursor(LoadCursorW(nullptr,
                                      (quickAccessHotRowIndex_ >= 0 || internalSelectionTreeDropItem_ != nullptr)
                                          ? IDC_HAND
                                          : IDC_NO));
                return TRUE;
            }
            if (dragMode_ == DragMode::LeftSplitter || dragMode_ == DragMode::DetailsSplitter || IsOverSplitter(point.x, point.y))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (HitTestQuickAccessDestinationRow(point.x, point.y) >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (HitTestQuickAccessSortButton(point.x, point.y) >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (HitTestQuickAccessDestinationButton(point.x, point.y) >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if ((folderLoadCoordinator_ && folderLoadCoordinator_->IsEnumerationActive())
                || (folderTreeController_ && folderTreeController_->IsBusy()))
            {
                SetCursor(LoadCursorW(nullptr, IDC_APPSTARTING));
                return TRUE;
            }
            return std::nullopt;
        }
        case WM_MOUSELEAVE:
            toolbarMouseTracking_ = false;
            if (toolbarHotIndex_ >= 0)
            {
                toolbarHotIndex_ = -1;
                InvalidateToolbarStrip();
            }
            if (!commandBarKeyboardActive_ && commandBarHotIndex_ >= 0)
            {
                commandBarHotIndex_ = -1;
                InvalidateToolbarStrip();
            }
            if (detailsPanelHotTabIndex_ >= 0)
            {
                detailsPanelHotTabIndex_ = -1;
                if (!IsRectEmpty(&detailsPanelTabStripRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
                }
            }
            if (detailsPanelCloseButtonHot_ || detailsPanelCloseButtonPressed_)
            {
                detailsPanelCloseButtonHot_ = false;
                detailsPanelCloseButtonPressed_ = false;
                if (!IsRectEmpty(&detailsPanelRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
                }
            }
            if (quickAccessHotRowIndex_ >= 0
                || quickAccessHotButtonIndex_ >= 0
                || quickAccessSortButtonHot_)
            {
                quickAccessHotRowIndex_ = -1;
                quickAccessHotButtonIndex_ = -1;
                quickAccessSortButtonHot_ = false;
                if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
                {
                    InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
                }
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    std::optional<LRESULT> MainWindow::HandleCommandMessage(WPARAM wParam, LPARAM lParam)
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            const UINT commandId = LOWORD(wParam);
            if (commandId == kBreadcrumbMoreControlId)
            {
                ShowBreadcrumbOverflowMenu();
                return 0;
            }
            if (commandId >= kBreadcrumbSegmentControlIdBase)
            {
                const std::size_t segmentIndex = commandId - kBreadcrumbSegmentControlIdBase;
                if (segmentIndex < breadcrumbSegments_.size())
                {
                    const std::wstring& targetPath = breadcrumbSegments_[segmentIndex].targetFolderPath;
                    if (!browserModel_ || !FolderPathsEqual(browserModel_->FolderPath(), targetPath))
                    {
                        LoadFolderAsync(targetPath);
                    }
                    return 0;
                }
            }
        }

        if (LOWORD(wParam) >= kQuickAccessShortcutEditBaseId
            && LOWORD(wParam) < kQuickAccessShortcutEditBaseId + quickAccessShortcutEdits_.size())
        {
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                PostMessageW(reinterpret_cast<HWND>(lParam), EM_SETSEL, 0, static_cast<LPARAM>(-1));
                return 0;
            }

            if (HIWORD(wParam) == EN_CHANGE && !updatingQuickAccessShortcutEdits_)
            {
                const std::size_t rowIndex = static_cast<std::size_t>(LOWORD(wParam) - kQuickAccessShortcutEditBaseId);
                if (rowIndex < quickAccessDestinationRows_.size())
                {
                    HWND edit = quickAccessShortcutEdits_[rowIndex];
                    const int textLength = GetWindowTextLengthW(edit);
                    std::wstring shortcutText(static_cast<std::size_t>(textLength) + 1, L'\0');
                    GetWindowTextW(edit, shortcutText.data(), static_cast<int>(shortcutText.size()));
                    shortcutText.resize(wcslen(shortcutText.c_str()));

                    QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
                    QuickSendAssignmentResult result = QuickSendAssignmentResult::DestinationNotFavorite;
                    MutateQuickSendState([&]
                    {
                        result = quickSendModel_.SetShortcutForDestination(
                            row.destinationPath,
                            shortcutText);
                    });
                    const QuickAccessShortcutEditPolicy::Result editUpdate =
                        QuickAccessShortcutEditPolicy::Reconcile(
                            result,
                            quickSendModel_.ShortcutForDestination(row.destinationPath),
                            shortcutText);
                    row.assignedShortcut = editUpdate.assignedShortcut;
                    if (editUpdate.updateText)
                    {
                        updatingQuickAccessShortcutEdits_ = true;
                        SetWindowTextW(edit, editUpdate.canonicalText.c_str());
                        updatingQuickAccessShortcutEdits_ = false;
                    }

                    InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
                }
            }
            return 0;
        }

        if (LOWORD(wParam) == ID_ACTION_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE && browserPaneController_)
        {
            if (applyingSavedSearchFilter_) return 0;
            if (savedSearchController_) savedSearchController_->FilterEdited();
            const int textLength = GetWindowTextLengthW(filterEdit_);
            std::wstring filterText(static_cast<std::size_t>(textLength) + 1, L'\0');
            GetWindowTextW(filterEdit_, filterText.data(), static_cast<int>(filterText.size()));
            filterText.resize(wcslen(filterText.c_str()));
            browserPaneController_->SetFilterQuery(std::move(filterText));
            UpdateStatusText();
            UpdateMenuState();
            return 0;
        }

        if (HandleCommand(LOWORD(wParam)))
        {
            return 0;
        }

        return std::nullopt;
    }

    std::optional<LRESULT> MainWindow::HandleControlColorMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (message == WM_CTLCOLOREDIT && IsQuickAccessShortcutEdit(control))
        {
            const ThemePalette palette = GetThemePalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
            SetBkColor(reinterpret_cast<HDC>(wParam), palette.actionFieldBackground);
            return reinterpret_cast<INT_PTR>(actionFieldBrush_ ? actionFieldBrush_ : backgroundBrush_);
        }

        if (message == WM_CTLCOLOREDIT && control == filterEdit_)
        {
            const ThemePalette palette = GetThemePalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
            SetBkColor(reinterpret_cast<HDC>(wParam), palette.actionFieldBackground);
            return reinterpret_cast<INT_PTR>(actionFieldBrush_ ? actionFieldBrush_ : backgroundBrush_);
        }

        if (control == detailsPanelText_)
        {
            const ThemePalette palette = GetThemePalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
            SetBkColor(reinterpret_cast<HDC>(wParam), palette.paneBackground);
            return reinterpret_cast<INT_PTR>(detailsPanelBrush_ ? detailsPanelBrush_ : backgroundBrush_);
        }

        return std::nullopt;
    }

    LRESULT MainWindow::HandlePaintMessage()
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);

        bool paintedWithD2D = false;
        if (EnsureD2DResources())
        {
            const int clientWidth = std::max(1, static_cast<int>(client.right - client.left));
            const int clientHeight = std::max(1, static_cast<int>(client.bottom - client.top));
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = memDC ? CreateCompatibleBitmap(hdc, clientWidth, clientHeight) : nullptr;
            if (memDC && memBmp)
            {
                const HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
                auto bufferedRenderTarget = hyperbrowse::render::D2DRenderer::Instance().CreateDCRenderTarget();
                if (bufferedRenderTarget
                    && SUCCEEDED(bufferedRenderTarget->BindDC(memDC, &client)))
                {
                    bufferedRenderTarget->BeginDraw();
                    const ThemePalette palette = GetThemePalette();
                    const ShellPainterPalette shellPalette{
                        palette.windowBackground,
                        palette.splitter,
                        palette.actionStripBorder};
                    const ShellPainterGeometry shellGeometry{
                        leftPaneWidth_,
                                                 actionStripHeight_,
                        kSplitterWidth,
                        detailsStripVisible_,
                        detailsPanelRect_};
                    ShellPainter::PaintD2D(bufferedRenderTarget.Get(), client, shellPalette, shellGeometry);
                    const RECT stripRect{0, 0, client.right, actionStripHeight_};
                    PaintToolbarD2D(bufferedRenderTarget.Get(), stripRect);
                    const HRESULT drawResult = bufferedRenderTarget->EndDraw();
                    paintedWithD2D = SUCCEEDED(drawResult);
                }

                if (paintedWithD2D)
                {
                    PaintDetailsPanel(memDC, client);
                    BitBlt(hdc, 0, 0, clientWidth, clientHeight, memDC, 0, 0, SRCCOPY);
                }

                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);
            }
        }

        if (paintedWithD2D)
        {
            EndPaint(hwnd_, &ps);
            return 0;
        }

        const int clientWidth = std::max(1, static_cast<int>(client.right - client.left));
        const int clientHeight = std::max(1, static_cast<int>(client.bottom - client.top));
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientWidth, clientHeight);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        const ThemePalette palette = GetThemePalette();
        const ShellPainterPalette shellPalette{
            palette.windowBackground,
            palette.splitter,
            palette.actionStripBorder};
        const ShellPainterGeometry shellGeometry{
            leftPaneWidth_,
            actionStripHeight_,
            kSplitterWidth,
            detailsStripVisible_,
            detailsPanelRect_};
        ShellPainter::PaintGdi(memDC, client, shellPalette, shellGeometry);

        const RECT stripRect{0, 0, client.right, actionStripHeight_};
        PaintToolbar(memDC, stripRect);

        PaintDetailsPanel(memDC, client);

        BitBlt(hdc, 0, 0, clientWidth, clientHeight, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd_, &ps);
        return 0;
    }

    LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (trayIconMessageId_ != 0 && message == trayIconMessageId_)
        {
            return HandleTrayIconMessage(lParam);
        }

        switch (message)
        {
        case WM_PARENTNOTIFY:
        case WM_LBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_VSCROLL:
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
        case WM_DROPFILES:
        case WM_SETCURSOR:
        case WM_MOUSELEAVE:
            if (const std::optional<LRESULT> result = HandleMouseInputMessage(message, wParam, lParam))
            {
                return *result;
            }
            break;
        case WM_CLOSE:
            if (fileOperationActive_ || batchConvertActive_ || imageCommandActive_)
            {
                if (!closePending_)
                {
                    closePending_ = true;
                    closePendingSinceTick_ = GetTickCount64();
                    closeWaitNoticeShown_ = false;
                    SetTimer(hwnd_,
                             kFileOperationShutdownTimerId,
                             kFileOperationShutdownIntervalMs,
                             nullptr);
                }
                if (fileOperationActive_ && fileOperationService_)
                {
                    fileOperationService_->Cancel();
                }
                if (batchConvertActive_ && batchConvertService_)
                {
                    batchConvertService_->Cancel();
                    batchConvertCancelling_ = true;
                }
                if (imageCommandActive_ && imageCommandService_)
                {
                    imageCommandService_->Cancel();
                    imageCommandCancelling_ = true;
                    imageCommandActivityLabel_ = L"Cancelling image command";
                }
                activeFileOperationLabel_ = fileOperationActive_
                    ? (closeWaitNoticeShown_ ? L"Waiting for Windows to finish file operation"
                                             : L"Cancelling file operation")
                    : (batchConvertActive_ ? L"Cancelling batch conversion" : std::wstring{});
                UpdateStatusText();
                UpdateMenuState();
                return 0;
            }
            if (!exitRequested_ && ShouldKeepInNotificationArea())
            {
                closePending_ = false;
                closePendingSinceTick_ = 0;
                closeWaitNoticeShown_ = false;
                KillTimer(hwnd_, kFileOperationShutdownTimerId);
                HideToTray();
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_GETOBJECT:
            if (accessibility_)
            {
                const LRESULT result = accessibility_->HandleGetObject(wParam, lParam);
                if (result != 0)
                {
                    return result;
                }
            }
            break;
        case WM_GETMINMAXINFO:
            OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                OnSize();
                EnsureActiveFolderVisible();
            }
            return 0;
        case WM_APP + 1:
            LayoutChildren();
            return 0;
        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RebuildAppTextFonts();
            LayoutChildren();
            RefreshPersistentMenuOwnerDraw();
            ResetD2DResources();
            ApplyTheme();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            HandleDisplaySurfaceChange();
            RefreshColorProfiles(true);
            return 0;
        case WM_WINDOWPOSCHANGED:
            if (browserPaneController_) browserPaneController_->RefreshColorProfile();
            break;
        case WM_SETTINGCHANGE:
            RefreshColorProfiles(true);
            ApplyTheme();
            if (lParam == 0
                || _wcsicmp(reinterpret_cast<const wchar_t*>(lParam), L"ShellState") == 0)
            {
                RefreshFolderTree();
            }
            return 0;
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            ApplyTheme();
            return 0;
        case WM_WTSSESSION_CHANGE:
            switch (wParam)
            {
            case WTS_CONSOLE_CONNECT:
            case WTS_SESSION_UNLOCK:
            case WTS_SESSION_REMOTE_CONTROL:
                HandleDisplaySurfaceChange();
                break;
            default:
                break;
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
            {
                HandleDisplaySurfaceChange();
                return TRUE;
            }
            if (wParam == PBT_POWERSETTINGCHANGE)
            {
                const auto* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
                if (setting
                    && (IsEqualGUID(setting->PowerSetting, kConsoleDisplayStateGuid)
                        || IsEqualGUID(setting->PowerSetting, kMonitorPowerOnGuid))
                    && setting->DataLength >= sizeof(DWORD)
                    && *reinterpret_cast<const DWORD*>(setting->Data) != 0)
                {
                    HandleDisplaySurfaceChange();
                }
                return TRUE;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && commandBarKeyboardActive_)
            {
                DeactivateCommandBarKeyboardMode(false);
            }
            if (LOWORD(wParam) != WA_INACTIVE && !IsIconic(hwnd_))
            {
                LayoutChildren();
                EnsureActiveFolderVisible();
            }
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (accessibility_)
            {
                accessibility_->NotifyFocusChanged();
            }
            break;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MONITORPOWER && lParam == static_cast<LPARAM>(-1))
            {
                HandleDisplaySurfaceChange();
            }
            break;
        case WM_ENTERMENULOOP:
            menuLoopActive_ = true;
            break;
        case WM_EXITMENULOOP:
            menuLoopActive_ = false;
            if (menuStateRefreshPending_ && !menuStateRefreshPosted_)
            {
                menuStateRefreshPosted_ = PostMessageW(hwnd_, kDeferredMenuStateMessage, 0, 0) != FALSE;
            }
            break;
        case WM_INITMENUPOPUP:
            if (quickSendPopupActive_ && HIWORD(lParam) == FALSE)
            {
                const HWND popupWindow = FindPopupMenuWindow(reinterpret_cast<HMENU>(lParam));
                for (std::size_t index = 0; popupWindow && index < quickSendPopupInitialDownCount_; ++index)
                {
                    PostMessageW(popupWindow, WM_KEYDOWN, VK_DOWN, 0);
                    PostMessageW(popupWindow, WM_KEYUP, VK_DOWN, 0);
                }
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_SYSCHAR:
            if (folderTreeDragController_.IsActive()
                && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                && wParam == VK_ESCAPE)
            {
                FinishFolderTreeDrag(false);
                return 0;
            }
            if (dragMode_ == DragMode::QuickAccessInternal
                && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                && wParam == VK_ESCAPE)
            {
                FinishInternalSelectionDrag(false);
                return 0;
            }
            if (HandleKeyboardFocusInput(message, wParam, lParam))
            {
                return 0;
            }
            if (HandleCommandBarKeyboardInput(message, wParam, lParam))
            {
                return 0;
            }
            break;
        case WM_MEASUREITEM:
        {
            auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measureItem && measureItem->CtlType == ODT_MENU)
            {
                MeasureOwnerDrawMenuItem(measureItem);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (drawItem && drawItem->CtlType == ODT_MENU)
            {
                DrawOwnerDrawMenuItem(*drawItem);
                return TRUE;
            }
            if (drawItem && drawItem->CtlType == ODT_BUTTON
                && (drawItem->CtlID == kBreadcrumbMoreControlId
                    || (drawItem->CtlID >= kBreadcrumbSegmentControlIdBase
                        && drawItem->CtlID - kBreadcrumbSegmentControlIdBase < breadcrumbSegments_.size())))
            {
                DrawBreadcrumbButton(*drawItem);
                return TRUE;
            }
            if (drawItem && drawItem->CtlType == ODT_STATIC && drawItem->CtlID == kStatusStripControlId)
            {
                DrawStatusStrip(*drawItem);
                return TRUE;
            }
            break;
        }
        case WM_MENUCHAR:
            return HandleMenuCharMessage(wParam, lParam);
        case WM_TIMER:
            if (gamepadTimerId_ != 0 && static_cast<UINT_PTR>(wParam) == gamepadTimerId_)
            {
                ProcessGamepadInput();
                return 0;
            }
            if (performanceHudTimerId_ != 0
                && static_cast<UINT_PTR>(wParam) == performanceHudTimerId_)
            {
                UpdatePerformanceHud();
                return 0;
            }
            if (const std::optional<LRESULT> result = timerRouter_.Handle(static_cast<UINT_PTR>(wParam)))
            {
                return *result;
            }
            break;
        case WM_NOTIFY:
            return HandleNotifyMessage(lParam);
        case WM_CTLCOLOREDIT:
            if (const std::optional<LRESULT> result = HandleControlColorMessage(message, wParam, lParam))
            {
                return *result;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (const std::optional<LRESULT> result = HandleControlColorMessage(message, wParam, lParam))
            {
                return *result;
            }
            break;
        case WM_COMMAND:
            if (const std::optional<LRESULT> result = HandleCommandMessage(wParam, lParam))
            {
                return *result;
            }
            break;
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            HDC eraseDC = reinterpret_cast<HDC>(wParam);
            // Exclude the action strip — PaintToolbar handles it
            RECT below{0, actionStripHeight_, client.right, client.bottom};
            FillRect(
                eraseDC,
                &below,
                backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            return 1;
        }
        case WM_PAINT:
            return HandlePaintMessage();
        case WM_QUERYENDSESSION:
            // Persist state before the session ends. WM_DESTROY is not guaranteed during
            // a forced shutdown / Windows Update / sign-out.
            SaveWindowState();
            return TRUE;
        case WM_ENDSESSION:
            // Save again on a confirmed end-session in case any state changed between the
            // query and the actual termination. Cheap and idempotent.
            if (wParam)
            {
                SaveWindowState();
            }
            return 0;
        case WM_DESTROY:
            if (gamepadTimerId_ != 0)
            {
                KillTimer(hwnd_, gamepadTimerId_);
                gamepadTimerId_ = 0;
            }
            if (folderTreeDragController_.IsActive())
            {
                FinishFolderTreeDrag(false);
            }
            savedSearchController_.reset();
            StopDisplaySurfaceRecoveryRetries();
            if (sessionNotificationRegistered_)
            {
                WTSUnRegisterSessionNotification(hwnd_);
                sessionNotificationRegistered_ = false;
            }
            if (consoleDisplayNotify_)
            {
                UnregisterPowerSettingNotification(consoleDisplayNotify_);
                consoleDisplayNotify_ = nullptr;
            }
            if (monitorPowerNotify_)
            {
                UnregisterPowerSettingNotification(monitorPowerNotify_);
                monitorPowerNotify_ = nullptr;
            }
            KillTimer(hwnd_, kFileOperationShutdownTimerId);
            if (quickSendConfirmationTimerId_ != 0)
            {
                KillTimer(hwnd_, quickSendConfirmationTimerId_);
                quickSendConfirmationTimerId_ = 0;
            }
            if (quickSendConfirmationToastWindow_)
            {
                DestroyWindow(quickSendConfirmationToastWindow_);
                quickSendConfirmationToastWindow_ = nullptr;
            }
            if (memoryPressureTimerId_ != 0)
            {
                KillTimer(hwnd_, kMemoryPressureTimerId);
                memoryPressureTimerId_ = 0;
            }
            memoryPressureExecutor_.reset();
            if (folderLoadCoordinator_)
            {
                folderLoadCoordinator_->Cancel();
            }
            if (folderTreeController_)
            {
                folderTreeController_->Cancel();
            }
            if (batchConvertService_)
            {
                batchConvertService_->Shutdown();
            }
            if (imageCommandService_)
            {
                imageCommandService_->Shutdown();
            }
            if (fileOperationService_)
            {
                fileOperationService_->Shutdown();
            }
            if (userMetadataStore_)
            {
                userMetadataStore_->SetNotificationWindow(nullptr);
                userMetadataStore_->Shutdown();
            }
            SaveWindowState();
            PostQuitMessage(0);
            return 0;
        default:
            if (const std::optional<LRESULT> result = asyncMessageRouter_.Handle(message, wParam, lParam))
            {
                return *result;
            }
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::QuickSendConfirmationToastWindowProc(HWND hwnd,
                                                                       UINT message,
                                                                       WPARAM wParam,
                                                                       LPARAM lParam)
    {
        MainWindow* owner = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            owner = create ? static_cast<MainWindow*>(create->lpCreateParams) : nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        }

        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paintStruct{};
            HDC hdc = BeginPaint(hwnd, &paintStruct);
            if (owner)
            {
                owner->PaintQuickSendConfirmationToast(hdc);
            }
            EndPaint(hwnd, &paintStruct);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
