#include "ui/MainWindowInternal.h"

namespace
{
    constexpr wchar_t kRegistryValueRecentFolders[] = L"RecentFolders";
    constexpr wchar_t kRegistryValueRecentDestinationFolders[] = L"RecentDestinationFolders";

    bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    bool TryReadQwordValue(HKEY key, const wchar_t* valueName, std::uint64_t* value)
    {
        if (!value)
        {
            return false;
        }

        DWORD size = sizeof(*value);
        DWORD type = REG_QWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_QWORD;
    }

    void WriteQwordValue(HKEY key, const wchar_t* valueName, std::uint64_t value)
    {
        RegSetValueExW(key, valueName, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    std::wstring ResolveStartupPath(std::wstring_view path)
    {
        if (path.empty())
        {
            return {};
        }

        std::error_code error;
        fs::path resolvedPath = fs::absolute(fs::path(path), error);
        if (error)
        {
            resolvedPath = fs::path(path);
        }

        return NormalizeFolderPath(resolvedPath.lexically_normal().wstring());
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::LoadWindowState()
    {
        // Always start non-recursive so restoring the last folder cannot trigger an expensive drive-wide scan.
        recursiveBrowsingEnabled_ = false;
        showSubfoldersInBrowser_ = false;
        hasPersistedWindowBounds_ = false;
        persistedWindowBounds_ = {};

        HKEY key{};
        if (hyperbrowse::util::OpenSettingsRegistryKey(KEY_READ, &key) == ERROR_SUCCESS)
        {
            const BrowserPresentationState presentationState = BrowserPresentationPersistence::Load(
                [&](std::wstring_view valueName, DWORD* persistedValue)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadDwordValue(key, registryValueName.c_str(), persistedValue);
                },
                BrowserPresentationState{
                    leftPaneWidth_,
                    static_cast<DWORD>(browserMode_),
                    static_cast<DWORD>(themeMode_),
                    appTextSize_,
                    thumbnailSizePreset_,
                    compactThumbnailLayout_,
                    thumbnailDetailsVisible_,
                    showSubfoldersInBrowser_,
                    sortMode_,
                    sortAscending_,
                    detailsStripVisible_,
                    detailsPanelWidth_});
            leftPaneWidth_ = presentationState.leftPaneWidth;
            browserMode_ = static_cast<BrowserMode>(presentationState.browserMode);
            themeMode_ = static_cast<ThemeMode>(presentationState.themeMode);
            appTextSize_ = presentationState.appTextSize;
            thumbnailSizePreset_ = presentationState.thumbnailSizePreset;
            compactThumbnailLayout_ = presentationState.compactThumbnailLayout;
            thumbnailDetailsVisible_ = presentationState.thumbnailDetailsVisible;
            showSubfoldersInBrowser_ = presentationState.showSubfoldersInBrowser;
            sortMode_ = presentationState.sortMode;
            sortAscending_ = presentationState.sortAscending;
            detailsStripVisible_ = presentationState.detailsStripVisible;
            detailsPanelWidth_ = presentationState.detailsPanelWidth;

            const ImageWorkflowState imageWorkflowState = ImageWorkflowPersistence::Load(
                [&](std::wstring_view valueName, DWORD* persistedValue)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadDwordValue(key, registryValueName.c_str(), persistedValue);
                },
                ImageWorkflowState{
                    nvJpegEnabled_,
                    libRawOutOfProcessEnabled_,
                    rawJpegPairedOperationsEnabled_,
                    pairedRawJpegViewerPreference_,
                    defaultViewerToSecondaryMonitor_});
            nvJpegEnabled_ = imageWorkflowState.nvJpegEnabled;
            libRawOutOfProcessEnabled_ = imageWorkflowState.libRawOutOfProcessEnabled;
            rawJpegPairedOperationsEnabled_ = imageWorkflowState.rawJpegPairedOperationsEnabled;
            pairedRawJpegViewerPreference_ = imageWorkflowState.pairedRawJpegViewerPreference;
            defaultViewerToSecondaryMonitor_ = imageWorkflowState.defaultViewerToSecondaryMonitor;

            const SelectedPathState selectedPathState = SelectedPathPersistence::Load(
                [&](std::wstring_view valueName, std::wstring* value)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadStringValue(key, registryValueName.c_str(), value);
                });
            startupFolderPath_ = selectedPathState.folderPath;
            startupSelectedImagePath_ = selectedPathState.imagePath;

            const std::optional<RECT> persistedWindowBounds = WindowBoundsPersistence::Load(
                [&](std::wstring_view valueName, DWORD* persistedValue)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadDwordValue(key, registryValueName.c_str(), persistedValue);
                });
            if (persistedWindowBounds)
            {
                const HMONITOR monitor = MonitorFromRect(&*persistedWindowBounds, MONITOR_DEFAULTTONULL);
                MONITORINFO monitorInfo{};
                monitorInfo.cbSize = sizeof(monitorInfo);
                if (monitor
                    && GetMonitorInfoW(monitor, &monitorInfo)
                    && WindowBoundsPersistence::IsWithinWorkArea(
                        *persistedWindowBounds,
                        kMinWindowWidth,
                        kMinWindowHeight,
                        monitorInfo.rcWork))
                {
                    persistedWindowBounds_ = *persistedWindowBounds;
                    hasPersistedWindowBounds_ = true;
                }
            }

            std::wstring serializedPaths;
            if (TryReadStringValue(key, kRegistryValueRecentFolders, &serializedPaths))
            {
                recentFolders_ = QuickAccessPathList::Deserialize(serializedPaths, kQuickAccessFolderLimit);
            }

            if (TryReadStringValue(key, kRegistryValueRecentDestinationFolders, &serializedPaths))
            {
                recentDestinationFolders_ = QuickAccessPathList::Deserialize(serializedPaths, kQuickAccessFolderLimit);
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
            SyncQuickSendModel();
            quickSendModel_.SetShortcutAssignments(persistedState.shortcutAssignments);
            SortFavoriteDestinationsByShortcutInMemory();

            filingResumeState_ = FilingResumePersistence::Load(
                [&](std::wstring_view valueName, std::wstring* value)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadStringValue(key, registryValueName.c_str(), value);
                });

            ViewerSettingsState viewerSettings = ViewerSettingsPersistence::Load(
                [&](std::wstring_view valueName, DWORD* persistedValue)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadDwordValue(key, registryValueName.c_str(), persistedValue);
                },
                ViewerSettingsState{
                    slideshowIntervalMs_,
                    slideshowTransitionStyle_,
                    slideshowTransitionDurationMs_,
                    useSlideshowTransition_,
                    viewerMouseWheelBehavior_,
                    invertKeyboardPanning_,
                    viewerEscapeKeyBehavior_,
                    colorManagementEnabled_});
            slideshowIntervalMs_ = viewerSettings.slideshowIntervalMs;
            slideshowTransitionStyle_ = viewerSettings.slideshowTransitionStyle;
            slideshowTransitionDurationMs_ = viewerSettings.slideshowTransitionDurationMs;
            useSlideshowTransition_ = viewerSettings.useSlideshowTransition;
            viewerMouseWheelBehavior_ = viewerSettings.mouseWheelBehavior;
            invertKeyboardPanning_ = viewerSettings.invertKeyboardPanning;
            viewerEscapeKeyBehavior_ = viewerSettings.escapeKeyBehavior;
            colorManagementEnabled_ = viewerSettings.colorManagementEnabled;
            const PerformanceSettingsState performanceState = PerformanceSettingsPersistence::Load(
                [&](std::wstring_view valueName, DWORD* persistedValue)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadDwordValue(key, registryValueName.c_str(), persistedValue);
                },
                [&](std::wstring_view valueName, std::uint64_t* persistedValue)
                {
                    const std::wstring registryValueName(valueName);
                    return TryReadQwordValue(key, registryValueName.c_str(), persistedValue);
                },
                PerformanceSettingsState{
                    persistentThumbnailCacheEnabled_,
                    resourceProfile_,
                    prefetchDepthOverride_,
                    thumbnailCacheCapacityOverrideBytes_,
                    metadataCacheCapacityOverrideEntries_,
                    persistentThumbnailCacheCapacityOverrideBytes_,
                    showPressureStateInStatusBar_,
                    closeMainWindowOnEscape_});
            persistentThumbnailCacheEnabled_ = performanceState.persistentThumbnailCacheEnabled;
            resourceProfile_ = performanceState.resourceProfile;
            prefetchDepthOverride_ = performanceState.prefetchDepthOverride;
            thumbnailCacheCapacityOverrideBytes_ = performanceState.thumbnailCacheCapacityOverrideBytes;
            metadataCacheCapacityOverrideEntries_ = performanceState.metadataCacheCapacityOverrideEntries;
            persistentThumbnailCacheCapacityOverrideBytes_ = performanceState.persistentThumbnailCacheCapacityOverrideBytes;
            showPressureStateInStatusBar_ = performanceState.showPressureStateInStatusBar;
            closeMainWindowOnEscape_ = performanceState.closeMainWindowOnEscape;

            RegCloseKey(key);
        }

        leftPaneWidth_ = std::max(leftPaneWidth_, kMinLeftPaneWidth);
        detailsPanelWidth_ = std::max(detailsPanelWidth_, kDetailsPanelMinWidth);
        startupFolderPath_ = NormalizeFolderPath(std::move(startupFolderPath_));
        startupSelectedImagePath_ = NormalizeFolderPath(std::move(startupSelectedImagePath_));
        lastQuickSendDestination_ = NormalizeFolderPath(std::move(lastQuickSendDestination_));
    }

    void MainWindow::ApplyStartupLaunchPathOverride()
    {
        if (folderLoadCoordinator_)
        {
            folderLoadCoordinator_->ClearPendingStartupPaths();
        }
        if (startupLaunchPathOverride_.empty())
        {
            return;
        }

        const std::wstring launchPath = ResolveStartupPath(startupLaunchPathOverride_);
        startupLaunchPathOverride_.clear();
        if (launchPath.empty())
        {
            return;
        }

        std::error_code error;
        const fs::path resolvedPath(launchPath);
        if (fs::is_directory(resolvedPath, error) && !error)
        {
            startupFolderPath_ = NormalizeFolderPath(resolvedPath.wstring());
            return;
        }

        error.clear();
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

        startupFolderPath_ = NormalizeFolderPath(containingFolder.wstring());
        if (folderLoadCoordinator_)
        {
            folderLoadCoordinator_->SetPendingStartupViewerPath(
                NormalizeFolderPath(resolvedPath.wstring()));
        }
    }

    void MainWindow::SaveWindowState() const
    {
        HKEY key{};
        if (hyperbrowse::util::CreateSettingsRegistryKey(KEY_WRITE, &key) == ERROR_SUCCESS)
        {
            const std::wstring selectedFolderPath = browserModel_ && !browserModel_->FolderPath().empty()
                ? NormalizeFolderPath(browserModel_->FolderPath())
                : GetSelectedFolderTreePath();
            std::wstring selectedImagePath;
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                selectedImagePath = NormalizeFolderPath(viewerWindow_->CurrentFilePath());
            }
            else if (browserPaneController_)
            {
                selectedImagePath = NormalizeFolderPath(browserPaneController_->FocusedFilePathSnapshot());
            }

            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            if (hwnd_ && GetWindowPlacement(hwnd_, &placement))
            {
                const RECT& normalBounds = placement.rcNormalPosition;
                WindowBoundsPersistence::Save(
                    normalBounds,
                    kMinWindowWidth,
                    kMinWindowHeight,
                    [&](std::wstring_view valueName, DWORD value)
                    {
                        const std::wstring registryValueName(valueName);
                        WriteDwordValue(key, registryValueName.c_str(), value);
                    });
            }

            BrowserPresentationPersistence::Save(
                BrowserPresentationState{
                    leftPaneWidth_,
                    static_cast<DWORD>(browserMode_),
                    static_cast<DWORD>(themeMode_),
                    appTextSize_,
                    thumbnailSizePreset_,
                    compactThumbnailLayout_,
                    thumbnailDetailsVisible_,
                    showSubfoldersInBrowser_,
                    browserPaneController_ ? browserPaneController_->GetSortMode() : sortMode_,
                    browserPaneController_ ? browserPaneController_->IsSortAscending() : sortAscending_,
                    detailsStripVisible_,
                    detailsPanelWidth_},
                [&](std::wstring_view valueName, DWORD value)
                {
                    const std::wstring registryValueName(valueName);
                    WriteDwordValue(key, registryValueName.c_str(), value);
                });
            RegDeleteValueW(key, L"RecursiveBrowsing");
            ImageWorkflowPersistence::Save(
                ImageWorkflowState{
                    nvJpegEnabled_,
                    libRawOutOfProcessEnabled_,
                    rawJpegPairedOperationsEnabled_,
                    pairedRawJpegViewerPreference_,
                    defaultViewerToSecondaryMonitor_},
                [&](std::wstring_view valueName, DWORD value)
                {
                    const std::wstring registryValueName(valueName);
                    WriteDwordValue(key, registryValueName.c_str(), value);
                });
            SelectedPathPersistence::Save(
                SelectedPathState{selectedFolderPath, selectedImagePath},
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
            WriteStringValue(key, kRegistryValueRecentFolders, QuickAccessPathList::Serialize(recentFolders_));
            WriteStringValue(key, kRegistryValueRecentDestinationFolders, QuickAccessPathList::Serialize(recentDestinationFolders_));
            ViewerSettingsPersistence::Save(
                ViewerSettingsState{
                    slideshowIntervalMs_,
                    slideshowTransitionStyle_,
                    slideshowTransitionDurationMs_,
                    useSlideshowTransition_,
                    viewerMouseWheelBehavior_,
                    invertKeyboardPanning_,
                    viewerEscapeKeyBehavior_,
                    colorManagementEnabled_},
                [&](std::wstring_view valueName, DWORD value)
                {
                    const std::wstring registryValueName(valueName);
                    WriteDwordValue(key, registryValueName.c_str(), value);
                });
            PerformanceSettingsPersistence::Save(
                PerformanceSettingsState{
                    persistentThumbnailCacheEnabled_,
                    resourceProfile_,
                    prefetchDepthOverride_,
                    thumbnailCacheCapacityOverrideBytes_,
                    metadataCacheCapacityOverrideEntries_,
                    persistentThumbnailCacheCapacityOverrideBytes_,
                    showPressureStateInStatusBar_,
                    closeMainWindowOnEscape_},
                [&](std::wstring_view valueName, DWORD value)
                {
                    const std::wstring registryValueName(valueName);
                    WriteDwordValue(key, registryValueName.c_str(), value);
                },
                [&](std::wstring_view valueName, std::uint64_t value)
                {
                    const std::wstring registryValueName(valueName);
                    WriteQwordValue(key, registryValueName.c_str(), value);
                });
            FilingResumePersistence::Save(
                filingResumeState_,
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
    }

    void MainWindow::SaveFilingResumeStateToRegistry() const
    {
        HKEY key{};
        if (hyperbrowse::util::CreateSettingsRegistryKey(KEY_WRITE, &key) != ERROR_SUCCESS)
        {
            return;
        }

        FilingResumePersistence::Save(
            filingResumeState_,
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
}
