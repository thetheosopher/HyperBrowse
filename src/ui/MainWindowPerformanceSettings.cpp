#include "ui/MainWindowInternal.h"

namespace
{
    using hyperbrowse::ui::dialog_detail::PerformanceSettingsDialogLayoutMetrics;
    using hyperbrowse::ui::dialog_detail::PerformanceSettingsDialogState;
    enum class PersistentThumbnailCacheMaintenanceOperation : unsigned int
    {
        Statistics = 0,
        Compact = 1,
        Purge = 2,
        Trim = 3,
    };
    constexpr unsigned int kPersistentThumbnailCacheMaintenanceSuccessFlag = 4;
    constexpr unsigned int kPersistentThumbnailCacheMaintenanceShowDialogFlag = 8;
    constexpr unsigned int kPersistentThumbnailCacheSourcePathsIncludedFlag = 16;
    constexpr UINT_PTR kPersistentThumbnailCacheDialogCursorSubclassId = 1;
    constexpr std::uint64_t kMemoryPressureAvailableBytesThreshold = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kMemoryPressureActivateUsedPercent = 85ULL;
    constexpr std::uint64_t kMemoryPressureRecoverUsedPercent = 70ULL;
    constexpr wchar_t kPerformanceSettingsDialogClassName[] = L"HyperBrowsePerformanceSettingsDialog";
    constexpr int kPerformanceSettingsDialogWidth = 640;
    constexpr int kPerformanceSettingsDialogHeight = 432;
    constexpr int kPerformanceSettingsDialogLabelWidth = 210;
    constexpr int kPerformanceSettingsDialogEditWidth = 118;
    constexpr int kPerformanceSettingsDialogUnitWidth = 72;
    constexpr int kPerformanceSettingsDialogCheckboxWidth = 124;
    constexpr int kPerformanceSettingsDialogValueTopGap = 14;
    constexpr int kPerformanceSettingsDialogControlGap = 10;
    constexpr int kPerformanceSettingsDialogSectionInset = 14;
    constexpr int kPerformanceSettingsDialogTitleControlId = 300;
    constexpr int kPerformanceSettingsInstructionControlId = 301;
    constexpr int kPerformanceSettingsSummaryGroupControlId = 302;
    constexpr int kPerformanceSettingsSummaryControlId = 303;
    constexpr int kPerformanceSettingsCacheGroupControlId = 304;
    constexpr int kPerformanceSettingsThumbnailLabelControlId = 305;
    constexpr int kPerformanceSettingsThumbnailEditControlId = 306;
    constexpr int kPerformanceSettingsThumbnailUnitControlId = 307;
    constexpr int kPerformanceSettingsThumbnailAutoControlId = 308;
    constexpr int kPerformanceSettingsMetadataLabelControlId = 309;
    constexpr int kPerformanceSettingsMetadataEditControlId = 310;
    constexpr int kPerformanceSettingsMetadataUnitControlId = 311;
    constexpr int kPerformanceSettingsMetadataAutoControlId = 312;
    constexpr int kPerformanceSettingsPressureStatusControlId = 313;
    constexpr int kPerformanceSettingsFootnoteControlId = 314;
    constexpr int kPerformanceSettingsDividerControlId = 315;

    bool SetWindowEnabledIfDifferent(HWND hwnd, bool enabled)
    {
        if (!hwnd)
        {
            return false;
        }

        const bool currentlyEnabled = IsWindowEnabled(hwnd) != FALSE;
        if (currentlyEnabled == enabled)
        {
            return false;
        }

        EnableWindow(hwnd, enabled ? TRUE : FALSE);
        return true;
    }

    std::wstring FormatMegabytesFromBytes(std::size_t bytes)
    {
        return std::to_wstring(bytes / (1024ULL * 1024ULL));
    }

    struct PersistentThumbnailCacheSourceScanResult
    {
        std::size_t checked{};
        std::size_t missing{};
        std::size_t inaccessible{};
        bool failed{};
    };

    struct PersistentThumbnailCacheSourceScanState
    {
        std::mutex mutex;
        std::atomic_bool cancelRequested{};
        std::atomic_bool completed{};
        PersistentThumbnailCacheSourceScanResult result;
    };

    struct PersistentThumbnailCacheDialogCallbackData
    {
        std::shared_ptr<PersistentThumbnailCacheSourceScanState> sourceScan;
        std::wstring content;
        std::wstring expandedInformation;
        bool cursorSubclassInstalled{};
        bool sourceScanTextUpdated{};
    };

    struct PersistentThumbnailCacheMaintenanceProgressCallbackData
    {
        std::shared_ptr<PersistentThumbnailCacheMaintenanceState> state;
    };

    void CompletePersistentThumbnailCacheSourceScan(
        const std::shared_ptr<PersistentThumbnailCacheSourceScanState>& scanState,
        PersistentThumbnailCacheSourceScanResult result)
    {
        {
            std::scoped_lock lock(scanState->mutex);
            scanState->result = result;
        }
        scanState->completed.store(true, std::memory_order_release);
    }

    void ScanPersistentThumbnailCacheSourceFiles(
        std::vector<std::wstring> sourceFilePaths,
        const std::shared_ptr<PersistentThumbnailCacheSourceScanState>& scanState)
    {
        PersistentThumbnailCacheSourceScanResult result;
        if (!SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN))
        {
            hyperbrowse::util::IncrementCounter(L"persistent_cache.source_scan.background_priority_failed");
        }

        try
        {
            for (const std::wstring& sourceFilePath : sourceFilePaths)
            {
                if (scanState->cancelRequested.load(std::memory_order_acquire))
                {
                    break;
                }

                std::error_code sourceError;
                const bool sourceExists = std::filesystem::exists(std::filesystem::path(sourceFilePath), sourceError);
                ++result.checked;
                if (!sourceExists
                    && (!sourceError || sourceError == std::make_error_code(std::errc::no_such_file_or_directory)))
                {
                    ++result.missing;
                }
                else if (sourceError)
                {
                    ++result.inaccessible;
                }
            }
        }
        catch (const std::exception&)
        {
            result.failed = true;
            hyperbrowse::util::IncrementCounter(L"persistent_cache.source_scan.exception");
        }
        catch (...)
        {
            result.failed = true;
            hyperbrowse::util::IncrementCounter(L"persistent_cache.source_scan.unknown_exception");
        }

        CompletePersistentThumbnailCacheSourceScan(scanState, result);
    }

    std::wstring BuildPersistentThumbnailCacheSourceScanStatus(
        const PersistentThumbnailCacheSourceScanResult* result)
    {
        if (!result)
        {
            return L"Scanning...";
        }
        if (result->failed)
        {
            return L"Scan failed.";
        }

        std::wstring status = std::to_wstring(result->checked);
        status.append(L" checked, ");
        status.append(std::to_wstring(result->missing));
        status.append(L" missing, ");
        status.append(std::to_wstring(result->inaccessible));
        status.append(L" inaccessible");
        return status;
    }

    bool ReplacePersistentThumbnailCacheSourceScanStatus(
        std::wstring* text,
        std::wstring_view label,
        std::wstring_view status)
    {
        if (!text)
        {
            return false;
        }

        std::wstring scanningText(label);
        scanningText.append(L"Scanning...");
        const std::size_t position = text->find(scanningText);
        if (position == std::wstring::npos)
        {
            return false;
        }

        std::wstring replacement(label);
        replacement.append(status);
        text->replace(position, scanningText.size(), replacement);
        return true;
    }

    std::wstring BuildPersistentThumbnailCacheSummary(const hyperbrowse::cache::DiskThumbnailCache::Statistics& statistics,
                                                      bool persistentCacheEnabled,
                                                      std::wstring_view sourceScanStatus)
    {
        std::wstring summary = persistentCacheEnabled
            ? L"Persistent thumbnail caching is currently enabled.\r\n"
            : L"Persistent thumbnail caching is currently disabled. Saved thumbnails remain on disk until they are purged.\r\n";
        summary.append(L"Indexed thumbnails: ");
        summary.append(std::to_wstring(statistics.indexedEntryCount));
        summary.append(L"\r\nDisk usage: ");
        summary.append(hyperbrowse::browser::FormatByteSize(statistics.cacheFileBytes));
        summary.append(L" on disk, ");
        summary.append(hyperbrowse::browser::FormatByteSize(statistics.indexedBytes));
        summary.append(L" tracked in the cache index.\r\nConfigured budget: ");
        summary.append(hyperbrowse::browser::FormatByteSize(statistics.capacityBytes));
        summary.append(L"\r\nOriginal source files: ");
        summary.append(sourceScanStatus);
        return summary;
    }

    std::wstring BuildPersistentThumbnailCacheDetails(
        const hyperbrowse::cache::DiskThumbnailCache::Statistics& statistics,
        std::wstring_view sourceScanStatus)
    {
        std::wstring details;
        AppendLabeledLine(&details, L"Cache Folder: ", statistics.cacheDirectory.empty() ? std::wstring(L"(unavailable)") : statistics.cacheDirectory);
        AppendLabeledLine(&details, L"Configured Budget: ", hyperbrowse::browser::FormatByteSize(statistics.capacityBytes));
        AppendLabeledLine(&details, L"Indexed Entries: ", std::to_wstring(statistics.indexedEntryCount));
        AppendLabeledLine(&details, L"Indexed Bytes: ", hyperbrowse::browser::FormatByteSize(statistics.indexedBytes));
        AppendLabeledLine(&details, L"Thumbnail Files On Disk: ", std::to_wstring(statistics.cacheFileCount));
        AppendLabeledLine(&details, L"Thumbnail File Bytes: ", hyperbrowse::browser::FormatByteSize(statistics.cacheFileBytes));
        AppendLabeledLine(&details, L"Index File Size: ", hyperbrowse::browser::FormatByteSize(statistics.indexFileBytes));
        AppendLabeledLine(&details, L"Missing Indexed Files: ", std::to_wstring(statistics.missingFileCount));
        AppendLabeledLine(&details, L"Original Source Files: ", sourceScanStatus);
        AppendLabeledLine(&details, L"Orphaned Files: ", std::to_wstring(statistics.orphanFileCount));
        AppendLabeledLine(&details, L"Orphaned File Bytes: ", hyperbrowse::browser::FormatByteSize(statistics.orphanFileBytes));
        return details;
    }

    LRESULT CALLBACK PersistentThumbnailCacheDialogCursorSubclass(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData)
    {
        auto* scanState = reinterpret_cast<PersistentThumbnailCacheSourceScanState*>(referenceData);
        if (message == WM_SETCURSOR
            && scanState
            && !scanState->completed.load(std::memory_order_acquire)
            && !scanState->cancelRequested.load(std::memory_order_acquire))
        {
            if (HCURSOR cursor = LoadCursorW(nullptr, IDC_APPSTARTING))
            {
                SetCursor(cursor);
                return TRUE;
            }
        }
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hwnd, PersistentThumbnailCacheDialogCursorSubclass, subclassId);
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    HRESULT CALLBACK PersistentThumbnailCacheDialogCallback(
        HWND hwnd,
        UINT notification,
        WPARAM,
        LPARAM,
        LONG_PTR callbackDataValue)
    {
        auto* callbackData = reinterpret_cast<PersistentThumbnailCacheDialogCallbackData*>(callbackDataValue);
        if (!callbackData || !callbackData->sourceScan)
        {
            return S_OK;
        }

        if (notification == TDN_CREATED)
        {
            callbackData->cursorSubclassInstalled = SetWindowSubclass(
                hwnd,
                PersistentThumbnailCacheDialogCursorSubclass,
                kPersistentThumbnailCacheDialogCursorSubclassId,
                reinterpret_cast<DWORD_PTR>(callbackData->sourceScan.get())) != FALSE;
            if (!callbackData->cursorSubclassInstalled)
            {
                hyperbrowse::util::IncrementCounter(L"persistent_cache.source_scan.cursor_subclass_failed");
            }
            return S_OK;
        }

        if (notification == TDN_TIMER)
        {
            if (callbackData->sourceScan->completed.load(std::memory_order_acquire))
            {
                if (!callbackData->sourceScanTextUpdated)
                {
                    PersistentThumbnailCacheSourceScanResult result;
                    {
                        std::scoped_lock lock(callbackData->sourceScan->mutex);
                        result = callbackData->sourceScan->result;
                    }
                    const std::wstring status = BuildPersistentThumbnailCacheSourceScanStatus(&result);
                    const bool contentUpdated = ReplacePersistentThumbnailCacheSourceScanStatus(
                        &callbackData->content,
                        L"Original source files: ",
                        status);
                    const bool detailsUpdated = ReplacePersistentThumbnailCacheSourceScanStatus(
                        &callbackData->expandedInformation,
                        L"Original Source Files: ",
                        status);
                    if (!contentUpdated || !detailsUpdated)
                    {
                        hyperbrowse::util::IncrementCounter(L"persistent_cache.source_scan.text_update_failed");
                    }
                    SendMessageW(hwnd,
                                 TDM_SET_ELEMENT_TEXT,
                                 TDE_CONTENT,
                                 reinterpret_cast<LPARAM>(callbackData->content.c_str()));
                    SendMessageW(hwnd,
                                 TDM_SET_ELEMENT_TEXT,
                                 TDE_EXPANDED_INFORMATION,
                                 reinterpret_cast<LPARAM>(callbackData->expandedInformation.c_str()));
                    callbackData->sourceScanTextUpdated = true;
                    if (HCURSOR cursor = LoadCursorW(nullptr, IDC_ARROW))
                    {
                        SetCursor(cursor);
                    }
                }
                return S_OK;
            }

            if (!callbackData->sourceScan->cancelRequested.load(std::memory_order_acquire))
            {
                if (HCURSOR cursor = LoadCursorW(nullptr, IDC_APPSTARTING))
                {
                    SetCursor(cursor);
                }
            }
            return S_FALSE;
        }

        if (notification == TDN_DESTROYED)
        {
            callbackData->sourceScan->cancelRequested.store(true, std::memory_order_release);
            if (callbackData->cursorSubclassInstalled)
            {
                RemoveWindowSubclass(hwnd,
                                      PersistentThumbnailCacheDialogCursorSubclass,
                                      kPersistentThumbnailCacheDialogCursorSubclassId);
                callbackData->cursorSubclassInstalled = false;
            }
        }
        return S_OK;
    }

    HRESULT CALLBACK PersistentThumbnailCacheMaintenanceProgressDialogCallback(
        HWND hwnd,
        UINT notification,
        WPARAM,
        LPARAM,
        LONG_PTR callbackDataValue)
    {
        auto* callbackData = reinterpret_cast<PersistentThumbnailCacheMaintenanceProgressCallbackData*>(callbackDataValue);
        if (!callbackData || !callbackData->state)
        {
            return S_OK;
        }

        if (notification == TDN_CREATED)
        {
            SendMessageW(hwnd, TDM_ENABLE_BUTTON, IDCLOSE, FALSE);
            return S_OK;
        }

        const unsigned int completionMessage = callbackData->state->completionMessage.load(std::memory_order_acquire);
        if (notification == TDN_BUTTON_CLICKED && completionMessage == 0)
        {
            return S_FALSE;
        }

        if (notification == TDN_TIMER && completionMessage != 0)
        {
            const wchar_t* completionText = (completionMessage & kPersistentThumbnailCacheMaintenanceSuccessFlag) != 0
                ? L"Cache maintenance is complete."
                : L"Cache maintenance did not complete.";
            SendMessageW(hwnd,
                         TDM_SET_ELEMENT_TEXT,
                         TDE_CONTENT,
                         reinterpret_cast<LPARAM>(completionText));
            SendMessageW(hwnd, TDM_ENABLE_BUTTON, IDCLOSE, TRUE);
            SendMessageW(hwnd, TDM_CLICK_BUTTON, IDCLOSE, 0);
        }
        return S_OK;
    }

    void RefreshPerformanceSettingsDialogControls(const PerformanceSettingsDialogState& state)
    {
        const bool thumbnailAutomatic = state.thumbnailAutoCheckWindow
            && SendMessageW(state.thumbnailAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool metadataAutomatic = state.metadataAutoCheckWindow
            && SendMessageW(state.metadataAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;

        SetWindowEnabledIfDifferent(state.thumbnailEditWindow, !thumbnailAutomatic);
        SetWindowEnabledIfDifferent(state.metadataEditWindow, !metadataAutomatic);
    }

    PerformanceSettingsDialogLayoutMetrics BuildPerformanceSettingsDialogLayoutMetrics(int clientWidth,
                                                                                      const PerformanceSettingsDialogState& state)
    {
        PerformanceSettingsDialogLayoutMetrics metrics;
        metrics.margin = ScaleDialogAppTextDimension(kTextInputDialogMargin, state.appTextSize, state.dpi);
        metrics.contentLeft = metrics.margin + 2;
        metrics.contentWidth = std::max(0, clientWidth - (metrics.contentLeft * 2));
        metrics.sectionInset = ScaleDialogAppTextDimension(kPerformanceSettingsDialogSectionInset, state.appTextSize, state.dpi);
        metrics.controlGap = ScaleDialogAppTextDimension(kPerformanceSettingsDialogControlGap, state.appTextSize, state.dpi);

        const HFONT titleFont = state.titleFont ? state.titleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HFONT bodyFont = state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int lineHeight = MeasureSingleLineTextHeight(bodyFont, 20);

        metrics.titleTop = metrics.margin + ScaleDialogAppTextDimension(4, state.appTextSize, state.dpi);
        metrics.titleHeight = MeasureTextBlockHeight(titleFont,
                                                     state.title,
                                                     metrics.contentWidth,
                                                     DT_LEFT | DT_NOPREFIX | DT_SINGLELINE,
                                                     ScaleDialogAppTextDimension(28, state.appTextSize, state.dpi));
        metrics.instructionTop = metrics.titleTop + metrics.titleHeight
            + ScaleDialogAppTextDimension(6, state.appTextSize, state.dpi);
        metrics.instructionHeight = MeasureTextBlockHeight(bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           ScaleDialogAppTextDimension(42, state.appTextSize, state.dpi));
        metrics.summaryGroupTop = metrics.instructionTop + metrics.instructionHeight
            + ScaleDialogAppTextDimension(14, state.appTextSize, state.dpi);
        metrics.summaryInnerWidth = std::max(0, metrics.contentWidth - (metrics.sectionInset * 2));
        metrics.summaryHeight = MeasureTextBlockHeight(bodyFont,
                                                       state.summary,
                                                       metrics.summaryInnerWidth,
                                                       DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                       ScaleDialogAppTextDimension(52, state.appTextSize, state.dpi));
        metrics.summaryGroupHeight = std::max(
            ScaleDialogAppTextDimension(78, state.appTextSize, state.dpi),
            metrics.summaryHeight + ScaleDialogAppTextDimension(30, state.appTextSize, state.dpi));
        metrics.cacheGroupTop = metrics.summaryGroupTop + metrics.summaryGroupHeight
            + ScaleDialogAppTextDimension(12, state.appTextSize, state.dpi);
        metrics.labelWidth = std::max(
            ScaleDialogAppTextDimension(kPerformanceSettingsDialogLabelWidth, state.appTextSize, state.dpi),
            std::max(MeasureDialogButtonWidth(bodyFont, L"Thumbnail memory cache:", 0),
                     MeasureDialogButtonWidth(bodyFont, L"Metadata cache:", 0)));
        metrics.editWidth = ScaleDialogAppTextDimension(kPerformanceSettingsDialogEditWidth, state.appTextSize, state.dpi);
        metrics.unitWidth = std::max(
            ScaleDialogAppTextDimension(kPerformanceSettingsDialogUnitWidth, state.appTextSize, state.dpi),
            MeasureDialogButtonWidth(bodyFont, L"entries", 0));
        metrics.checkboxWidth = MeasureDialogButtonWidth(
            bodyFont,
            L"Follow profile",
            ScaleDialogAppTextDimension(kPerformanceSettingsDialogCheckboxWidth, state.appTextSize, state.dpi));
        metrics.rowHeight = std::max(
            ScaleDialogAppTextDimension(kTextInputEditHeight, state.appTextSize, state.dpi),
            lineHeight + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi));
        metrics.checkboxHeight = std::max(
            ScaleDialogAppTextDimension(22, state.appTextSize, state.dpi),
            lineHeight + ScaleDialogAppTextDimension(2, state.appTextSize, state.dpi));
        metrics.rowLabelLeft = metrics.contentLeft + metrics.sectionInset;
        metrics.rowValueLeft = metrics.rowLabelLeft + metrics.labelWidth + metrics.controlGap;
        metrics.rowUnitLeft = metrics.rowValueLeft + metrics.editWidth + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi);
        metrics.rowCheckboxLeft = metrics.contentLeft + metrics.contentWidth - metrics.sectionInset - metrics.checkboxWidth;
        metrics.firstRowTop = metrics.cacheGroupTop + ScaleDialogAppTextDimension(30, state.appTextSize, state.dpi);
        metrics.secondRowTop = metrics.firstRowTop + metrics.rowHeight
            + ScaleDialogAppTextDimension(kPerformanceSettingsDialogValueTopGap + 12, state.appTextSize, state.dpi);
        metrics.pressureStatusTop = metrics.secondRowTop + metrics.rowHeight
            + ScaleDialogAppTextDimension(18, state.appTextSize, state.dpi);
        const int pressureStatusBottom = metrics.pressureStatusTop + metrics.checkboxHeight;
        metrics.cacheGroupHeight = std::max(
            ScaleDialogAppTextDimension(136, state.appTextSize, state.dpi),
            pressureStatusBottom - metrics.cacheGroupTop + ScaleDialogAppTextDimension(12, state.appTextSize, state.dpi));
        metrics.footnoteTop = metrics.cacheGroupTop + metrics.cacheGroupHeight
            + ScaleDialogAppTextDimension(12, state.appTextSize, state.dpi);
        metrics.minimumFootnoteHeight = MeasureTextBlockHeight(bodyFont,
                                                               state.footnote,
                                                               metrics.contentWidth,
                                                               DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                               ScaleDialogAppTextDimension(42, state.appTextSize, state.dpi));
        metrics.buttonHeight = std::max(
            ScaleDialogAppTextDimension(kTextInputButtonHeight, state.appTextSize, state.dpi),
            lineHeight + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi));
        metrics.applyButtonWidth = MeasureDialogButtonWidth(
            bodyFont,
            L"Apply",
            ScaleDialogAppTextDimension(kTextInputButtonWidth, state.appTextSize, state.dpi));
        metrics.cancelButtonWidth = MeasureDialogButtonWidth(
            bodyFont,
            L"Cancel",
            ScaleDialogAppTextDimension(kTextInputButtonWidth, state.appTextSize, state.dpi));
        const int leftRowWidth = metrics.labelWidth
            + metrics.controlGap
            + metrics.editWidth
            + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi)
            + metrics.unitWidth;
        metrics.minimumClientWidth = std::max(
            ScaleDialogAppTextDimension(kPerformanceSettingsDialogWidth, state.appTextSize, state.dpi),
            metrics.contentLeft * 2
                + metrics.sectionInset * 2
                + leftRowWidth
                + ScaleDialogAppTextDimension(24, state.appTextSize, state.dpi)
                + metrics.checkboxWidth);
        metrics.minimumClientHeight = metrics.footnoteTop
            + metrics.minimumFootnoteHeight
            + ScaleDialogAppTextDimension(8, state.appTextSize, state.dpi)
            + ScaleDialogAppTextDimension(16, state.appTextSize, state.dpi)
            + metrics.buttonHeight
            + metrics.margin;
        return metrics;
    }

    void LayoutPerformanceSettingsDialogControls(HWND hwnd, const PerformanceSettingsDialogState& state)
    {
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);

        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const PerformanceSettingsDialogLayoutMetrics metrics = BuildPerformanceSettingsDialogLayoutMetrics(clientWidth, state);
        const HFONT bodyFont = state.bodyFont
            ? state.bodyFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int lineHeight = MeasureSingleLineTextHeight(bodyFont, 20);
        const int buttonTop = clientHeight - metrics.margin - metrics.buttonHeight;
        const int dividerTop = buttonTop - ScaleDialogAppTextDimension(16, state.appTextSize, state.dpi);
        const int cancelLeft = clientWidth - metrics.margin - metrics.cancelButtonWidth;
        const int okLeft = cancelLeft - metrics.controlGap - metrics.applyButtonWidth;

        const HWND titleWindow = GetDlgItem(hwnd, kPerformanceSettingsDialogTitleControlId);
        if (titleWindow)
        {
            MoveWindow(titleWindow, metrics.contentLeft, metrics.titleTop, metrics.contentWidth, metrics.titleHeight, TRUE);
        }

        const HWND instructionWindow = GetDlgItem(hwnd, kPerformanceSettingsInstructionControlId);
        if (instructionWindow)
        {
            MoveWindow(instructionWindow, metrics.contentLeft, metrics.instructionTop, metrics.contentWidth, metrics.instructionHeight, TRUE);
        }

        const HWND summaryGroupWindow = GetDlgItem(hwnd, kPerformanceSettingsSummaryGroupControlId);
        if (summaryGroupWindow)
        {
            MoveWindow(summaryGroupWindow, metrics.contentLeft, metrics.summaryGroupTop, metrics.contentWidth, metrics.summaryGroupHeight, TRUE);
        }

        const HWND summaryWindow = GetDlgItem(hwnd, kPerformanceSettingsSummaryControlId);
        if (summaryWindow)
        {
            MoveWindow(summaryWindow,
                       metrics.contentLeft + ScaleDialogAppTextDimension(kPerformanceSettingsDialogSectionInset, state.appTextSize, state.dpi),
                       metrics.summaryGroupTop + ScaleDialogAppTextDimension(22, state.appTextSize, state.dpi),
                       metrics.summaryInnerWidth,
                       metrics.summaryHeight,
                       TRUE);
        }

        const HWND cacheGroupWindow = GetDlgItem(hwnd, kPerformanceSettingsCacheGroupControlId);
        if (cacheGroupWindow)
        {
            MoveWindow(cacheGroupWindow, metrics.contentLeft, metrics.cacheGroupTop, metrics.contentWidth, metrics.cacheGroupHeight, TRUE);
        }

        const HWND thumbnailLabel = GetDlgItem(hwnd, kPerformanceSettingsThumbnailLabelControlId);
        if (thumbnailLabel)
        {
            MoveWindow(thumbnailLabel,
                       metrics.rowLabelLeft,
                       metrics.firstRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.labelWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.thumbnailEditWindow)
        {
            MoveWindow(state.thumbnailEditWindow,
                       metrics.rowValueLeft,
                       metrics.firstRowTop,
                       metrics.editWidth,
                       metrics.rowHeight,
                       TRUE);
        }
        const HWND thumbnailUnit = GetDlgItem(hwnd, kPerformanceSettingsThumbnailUnitControlId);
        if (thumbnailUnit)
        {
            MoveWindow(thumbnailUnit,
                       metrics.rowUnitLeft,
                       metrics.firstRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.unitWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.thumbnailAutoCheckWindow)
        {
            MoveWindow(state.thumbnailAutoCheckWindow,
                       metrics.rowCheckboxLeft,
                       metrics.firstRowTop + (metrics.rowHeight - metrics.checkboxHeight) / 2,
                       metrics.checkboxWidth,
                       metrics.checkboxHeight,
                       TRUE);
        }

        const HWND metadataLabel = GetDlgItem(hwnd, kPerformanceSettingsMetadataLabelControlId);
        if (metadataLabel)
        {
            MoveWindow(metadataLabel,
                       metrics.rowLabelLeft,
                       metrics.secondRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.labelWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.metadataEditWindow)
        {
            MoveWindow(state.metadataEditWindow,
                       metrics.rowValueLeft,
                       metrics.secondRowTop,
                       metrics.editWidth,
                       metrics.rowHeight,
                       TRUE);
        }
        const HWND metadataUnit = GetDlgItem(hwnd, kPerformanceSettingsMetadataUnitControlId);
        if (metadataUnit)
        {
            MoveWindow(metadataUnit,
                       metrics.rowUnitLeft,
                       metrics.secondRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.unitWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.metadataAutoCheckWindow)
        {
            MoveWindow(state.metadataAutoCheckWindow,
                       metrics.rowCheckboxLeft,
                       metrics.secondRowTop + (metrics.rowHeight - metrics.checkboxHeight) / 2,
                       metrics.checkboxWidth,
                       metrics.checkboxHeight,
                       TRUE);
        }

        if (state.pressureStatusCheckWindow)
        {
            MoveWindow(state.pressureStatusCheckWindow,
                       metrics.rowLabelLeft,
                       metrics.pressureStatusTop,
                       metrics.contentWidth - ScaleDialogAppTextDimension(kPerformanceSettingsDialogSectionInset * 2, state.appTextSize, state.dpi),
                       metrics.checkboxHeight,
                       TRUE);
        }

        const HWND footnoteWindow = GetDlgItem(hwnd, kPerformanceSettingsFootnoteControlId);
        if (footnoteWindow)
        {
            MoveWindow(footnoteWindow,
                       metrics.contentLeft,
                       metrics.footnoteTop,
                       metrics.contentWidth,
                       std::max(metrics.minimumFootnoteHeight, dividerTop - metrics.footnoteTop - 8),
                       TRUE);
        }

        const HWND dividerWindow = GetDlgItem(hwnd, kPerformanceSettingsDividerControlId);
        if (dividerWindow)
        {
            MoveWindow(dividerWindow, metrics.contentLeft, dividerTop, metrics.contentWidth, 2, TRUE);
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton, okLeft, buttonTop, metrics.applyButtonWidth, metrics.buttonHeight, TRUE);
        }

        const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
        if (cancelButton)
        {
            MoveWindow(cancelButton, cancelLeft, buttonTop, metrics.cancelButtonWidth, metrics.buttonHeight, TRUE);
        }
    }

    bool CollectPerformanceSettingsDialogResult(HWND hwnd, PerformanceSettingsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        const bool thumbnailAutomatic = state->thumbnailAutoCheckWindow
            && SendMessageW(state->thumbnailAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool metadataAutomatic = state->metadataAutoCheckWindow
            && SendMessageW(state->metadataAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;

        std::size_t thumbnailMegabytes = 0;
        if (!thumbnailAutomatic && !TryParsePositiveSizeValue(ReadWindowText(state->thumbnailEditWindow), &thumbnailMegabytes))
        {
            MessageBoxW(hwnd,
                        L"Enter a positive thumbnail cache size in megabytes, or keep Follow profile enabled.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->thumbnailEditWindow)
            {
                SetFocus(state->thumbnailEditWindow);
            }
            return false;
        }

        std::size_t metadataEntries = 0;
        if (!metadataAutomatic && !TryParsePositiveSizeValue(ReadWindowText(state->metadataEditWindow), &metadataEntries))
        {
            MessageBoxW(hwnd,
                        L"Enter a positive metadata cache capacity in entries, or keep Follow profile enabled.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->metadataEditWindow)
            {
                SetFocus(state->metadataEditWindow);
            }
            return false;
        }

        state->thumbnailCacheAutomatic = thumbnailAutomatic;
        state->metadataCacheAutomatic = metadataAutomatic;
        state->thumbnailCacheCapacityOverrideBytes = thumbnailAutomatic
            ? 0
            : hyperbrowse::util::SaturatingCastToSizeT(static_cast<std::uint64_t>(thumbnailMegabytes) * 1024ULL * 1024ULL);
        state->metadataCacheCapacityOverrideEntries = metadataAutomatic ? 0 : metadataEntries;
        state->showPressureStateInStatusBar = state->pressureStatusCheckWindow
            && SendMessageW(state->pressureStatusCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    }

    LRESULT CALLBACK PerformanceSettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<PerformanceSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<PerformanceSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            state->backgroundBrush = CreateSolidBrush(state->theme.windowBackground);
            state->fieldBrush = CreateSolidBrush(state->theme.fieldBackground);
            const HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

            const HWND titleWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->title.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE,
                0,
                0,
                100,
                24,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsDialogTitleControlId)),
                hInstance,
                nullptr);
            state->instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                42,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsInstructionControlId)),
                hInstance,
                nullptr);
            const HWND summaryGroupWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Current profile",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0,
                0,
                100,
                70,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsSummaryGroupControlId)),
                hInstance,
                nullptr);
            state->summaryWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->summary.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                46,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsSummaryControlId)),
                hInstance,
                nullptr);
            const HWND cacheGroupWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cache limits",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0,
                0,
                100,
                120,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsCacheGroupControlId)),
                hInstance,
                nullptr);
            const HWND thumbnailLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Thumbnail memory cache:",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailLabelControlId)),
                hInstance,
                nullptr);
            state->thumbnailEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->thumbnailCacheText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                0,
                0,
                100,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailEditControlId)),
                hInstance,
                nullptr);
            const HWND thumbnailUnitLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"MB",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                48,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailUnitControlId)),
                hInstance,
                nullptr);
            state->thumbnailAutoCheckWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Follow profile",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailAutoControlId)),
                hInstance,
                nullptr);
            const HWND metadataLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Metadata cache:",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataLabelControlId)),
                hInstance,
                nullptr);
            state->metadataEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->metadataCacheText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                0,
                0,
                100,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataEditControlId)),
                hInstance,
                nullptr);
            const HWND metadataUnitLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"entries",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                60,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataUnitControlId)),
                hInstance,
                nullptr);
            state->metadataAutoCheckWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Follow profile",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataAutoControlId)),
                hInstance,
                nullptr);
            state->pressureStatusCheckWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Show memory pressure state in the status bar",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsPressureStatusControlId)),
                hInstance,
                nullptr);
            const HWND footnoteWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->footnote.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                42,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsFootnoteControlId)),
                hInstance,
                nullptr);
            const HWND dividerWindow = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                0,
                0,
                100,
                2,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsDividerControlId)),
                hInstance,
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                hInstance,
                nullptr);
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                hInstance,
                nullptr);

            const HWND windows[] = {
                titleWindow,
                state->instructionWindow,
                summaryGroupWindow,
                state->summaryWindow,
                cacheGroupWindow,
                thumbnailLabel,
                state->thumbnailEditWindow,
                thumbnailUnitLabel,
                state->thumbnailAutoCheckWindow,
                metadataLabel,
                state->metadataEditWindow,
                metadataUnitLabel,
                state->metadataAutoCheckWindow,
                state->pressureStatusCheckWindow,
                footnoteWindow,
                dividerWindow,
                state->okButton,
                cancelButton,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            if (titleWindow && state->titleFont)
            {
                SendMessageW(titleWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
            }
            if (state->instructionWindow && state->bodyFont)
            {
                SendMessageW(state->instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }
            if (state->summaryWindow && state->bodyFont)
            {
                SendMessageW(state->summaryWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }
            if (footnoteWindow && state->bodyFont)
            {
                SendMessageW(footnoteWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }

            if (state->thumbnailAutoCheckWindow)
            {
                SendMessageW(state->thumbnailAutoCheckWindow, BM_SETCHECK, state->thumbnailCacheAutomatic ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (state->metadataAutoCheckWindow)
            {
                SendMessageW(state->metadataAutoCheckWindow, BM_SETCHECK, state->metadataCacheAutomatic ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (state->pressureStatusCheckWindow)
            {
                SendMessageW(state->pressureStatusCheckWindow, BM_SETCHECK, state->showPressureStateInStatusBar ? BST_CHECKED : BST_UNCHECKED, 0);
            }

            LayoutPerformanceSettingsDialogControls(hwnd, *state);
            RefreshPerformanceSettingsDialogControls(*state);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutPerformanceSettingsDialogControls(hwnd, *state);
            }
            return 0;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = std::max<UINT>(96, HIWORD(wParam));
                const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
                if (suggestedRect)
                {
                    const RECT adjustedRect = ClampDialogFrameToWorkArea(
                        *suggestedRect,
                        MeasureDialogShellMetrics(hwnd, state->appTextSize).workArea);
                    SetWindowPos(hwnd, nullptr,
                                 adjustedRect.left,
                                 adjustedRect.top,
                                 adjustedRect.right - adjustedRect.left,
                                 adjustedRect.bottom - adjustedRect.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                DeleteFontIfOwned(state->titleFont);
                DeleteFontIfOwned(state->bodyFont);
                state->titleFont = CreateDialogUiFont(16, FW_BOLD, state->appTextSize, state->dpi);
                state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
                const HFONT font = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                EnumChildWindows(hwnd,
                                 [](HWND child, LPARAM parameter) -> BOOL
                                 {
                                     SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(parameter), TRUE);
                                     return TRUE;
                                 },
                                 reinterpret_cast<LPARAM>(font));
                const HWND titleWindow = GetDlgItem(hwnd, kPerformanceSettingsDialogTitleControlId);
                if (titleWindow && state->titleFont)
                {
                    SendMessageW(titleWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
                }
                LayoutPerformanceSettingsDialogControls(hwnd, *state);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state)
            {
                SetFocus(state->thumbnailAutoCheckWindow ? state->thumbnailAutoCheckWindow : state->okButton);
                return FALSE;
            }
            break;
        case WM_CTLCOLORDLG:
            return state && state->backgroundBrush
                ? reinterpret_cast<INT_PTR>(state->backgroundBrush)
                : 0;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLOREDIT:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.fieldBackground);
                return reinterpret_cast<INT_PTR>(state->fieldBrush);
            }
            break;
        case WM_CTLCOLORBTN:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->theme.text);
                SetBkColor(dc, state->theme.windowBackground);
                return reinterpret_cast<INT_PTR>(state->backgroundBrush);
            }
            break;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush)
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == kPerformanceSettingsThumbnailAutoControlId
                || LOWORD(wParam) == kPerformanceSettingsMetadataAutoControlId)
            {
                RefreshPerformanceSettingsDialogControls(*state);
                return 0;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (CollectPerformanceSettingsDialogResult(hwnd, state))
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                if (state->backgroundBrush)
                {
                    DeleteObject(state->backgroundBrush);
                    state->backgroundBrush = nullptr;
                }
                if (state->fieldBrush)
                {
                    DeleteObject(state->fieldBrush);
                    state->fieldBrush = nullptr;
                }
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForPerformanceSettings(HWND ownerWindow,
                                      HINSTANCE instance,
                                      hyperbrowse::util::AppTextSize appTextSize,
                                      bool darkTheme,
                                      hyperbrowse::util::ResourceProfile resourceProfile,
                                      std::size_t currentThumbnailCacheCapacityBytes,
                                      std::size_t currentMetadataCacheCapacityEntries,
                                      std::size_t initialThumbnailCacheCapacityOverrideBytes,
                                      std::size_t initialMetadataCacheCapacityOverrideEntries,
                                      bool initialShowPressureStateInStatusBar,
                                      std::size_t* thumbnailCacheCapacityOverrideBytes,
                                      std::size_t* metadataCacheCapacityOverrideEntries,
                                      bool* showPressureStateInStatusBar)
    {
        if (!thumbnailCacheCapacityOverrideBytes || !metadataCacheCapacityOverrideEntries || !showPressureStateInStatusBar)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kPerformanceSettingsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &PerformanceSettingsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kPerformanceSettingsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        PerformanceSettingsDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.theme = hyperbrowse::ui::MakeDialogTheme(darkTheme);
        state.titleFont = CreateDialogUiFont(16, FW_BOLD, state.appTextSize, state.dpi);
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize, state.dpi);
        state.title = L"Performance Settings";
        state.instruction = L"Choose whether HyperBrowse should follow adaptive cache sizing or use explicit cache caps for the active profile.";
        state.summary = L"Profile: ";
        state.summary.append(hyperbrowse::util::ResourceProfileToDisplayName(resourceProfile));
        state.summary.append(L"\r\nEffective thumbnail cache: ");
        state.summary.append(FormatMegabytesFromBytes(currentThumbnailCacheCapacityBytes));
        state.summary.append(L" MB\r\nEffective metadata cache: ");
        state.summary.append(std::to_wstring(currentMetadataCacheCapacityEntries));
        state.summary.append(L" entries");
        state.footnote = L"Changes apply immediately to browser and file-details services. Thumbnail values are entered in megabytes; metadata values are entry counts.";
        state.thumbnailCacheAutomatic = initialThumbnailCacheCapacityOverrideBytes == 0;
        state.metadataCacheAutomatic = initialMetadataCacheCapacityOverrideEntries == 0;
        state.showPressureStateInStatusBar = initialShowPressureStateInStatusBar;
        state.thumbnailCacheText = state.thumbnailCacheAutomatic
            ? FormatMegabytesFromBytes(currentThumbnailCacheCapacityBytes)
            : FormatMegabytesFromBytes(initialThumbnailCacheCapacityOverrideBytes);
        state.metadataCacheText = state.metadataCacheAutomatic
            ? std::to_wstring(currentMetadataCacheCapacityEntries)
            : std::to_wstring(initialMetadataCacheCapacityOverrideEntries);

        const int baseDialogWidth = ScaleDialogAppTextDimension(
            kPerformanceSettingsDialogWidth,
            state.appTextSize,
            state.dpi);
        const PerformanceSettingsDialogLayoutMetrics initialLayoutMetrics =
            BuildPerformanceSettingsDialogLayoutMetrics(baseDialogWidth, state);
        const int dialogWidth = std::max(baseDialogWidth, initialLayoutMetrics.minimumClientWidth);
        const PerformanceSettingsDialogLayoutMetrics layoutMetrics =
            BuildPerformanceSettingsDialogLayoutMetrics(dialogWidth, state);
        RECT windowRect{0, 0, dialogWidth, std::max(
            ScaleDialogAppTextDimension(kPerformanceSettingsDialogHeight, state.appTextSize, state.dpi),
            layoutMetrics.minimumClientHeight)};
        AdjustDialogWindowRectForDpi(&windowRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);
        windowRect = ClampDialogFrameToWorkArea(
            windowRect,
            MeasureDialogShellMetrics(ownerWindow, state.appTextSize).workArea);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kPerformanceSettingsDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.titleFont);
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *thumbnailCacheCapacityOverrideBytes = state.thumbnailCacheCapacityOverrideBytes;
        *metadataCacheCapacityOverrideEntries = state.metadataCacheCapacityOverrideEntries;
        *showPressureStateInStatusBar = state.showPressureStateInStatusBar;
        return true;
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::ApplyResourceProfileSetting(bool recreateDetailsPanelScheduler, bool applyBrowserPaneSettings)
    {
        if (applyBrowserPaneSettings && browserPaneController_)
        {
            browserPaneController_->SetResourceProfile(resourceProfile_);
            browserPaneController_->SetPrefetchDepthOverride(prefetchDepthOverride_);
        }

        for (viewer::ViewerWindow* viewer : OpenViewerWindows())
        {
            viewer->SetResourceProfile(resourceProfile_);
            viewer->SetPrefetchDepthOverride(prefetchDepthOverride_);
        }

        if (recreateDetailsPanelScheduler)
        {
            RecreateDetailsPanelThumbnailScheduler();
        }
    }

    void MainWindow::QueueMemoryPressureSample()
    {
        if (!hwnd_ || !memoryPressureExecutor_ || memoryPressureSampleQueued_)
        {
            return;
        }

        memoryPressureSampleQueued_ = true;
        const HWND targetWindow = hwnd_;
        if (!memoryPressureExecutor_->Post([targetWindow]()
            {
                auto update = std::make_unique<MemoryPressureSampleResult>();
                const util::MemorySnapshot memorySnapshot = util::QueryMemorySnapshot();
                if (memorySnapshot.IsValid() && memorySnapshot.totalPhysicalBytes != 0)
                {
                    const std::uint64_t usedPercent = 100ULL
                        - ((memorySnapshot.availablePhysicalBytes * 100ULL) / memorySnapshot.totalPhysicalBytes);
                    update->pressureDetected = memorySnapshot.availablePhysicalBytes < kMemoryPressureAvailableBytesThreshold
                        || usedPercent >= kMemoryPressureActivateUsedPercent;
                    update->recoveryCandidate = memorySnapshot.availablePhysicalBytes >= kMemoryPressureAvailableBytesThreshold
                        && usedPercent < kMemoryPressureRecoverUsedPercent;
                }

                if (!PostMessageW(targetWindow, kMemoryPressureSampledMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            }))
        {
            memoryPressureSampleQueued_ = false;
        }
    }

    void MainWindow::RecreateDetailsPanelThumbnailScheduler()
    {
        if (!hwnd_)
        {
            return;
        }

        if (detailsPanelThumbnailScheduler_)
        {
            ++detailsPanelThumbnailSessionId_;
            ++detailsPanelThumbnailRequestEpoch_;

            auto scheduler = std::make_unique<services::ThumbnailScheduler>(
                thumbnailCacheCapacityOverrideBytes_,
                0,
                resourceProfile_,
                std::function<void()>{},
                std::function<void()>{},
                persistentThumbnailCacheCapacityOverrideBytes_);
            if (scheduler)
            {
                if (hwnd_)
                {
                    scheduler->BindTargetWindow(hwnd_);
                }
                scheduler->SetDiskCacheEnabled(persistentThumbnailCacheEnabled_);
                scheduler->SetPressureModeEnabled(thumbnailMemoryPressureActive_);
                if (thumbnailMemoryPressureActive_)
                {
                    scheduler->TrimCacheToBytes(std::max<std::size_t>(1, scheduler->CacheCapacityBytes() / 2));
                }
            }
            detailsPanelThumbnailScheduler_->CancelOutstanding();
            detailsPanelThumbnailScheduler_->BindTargetWindow(nullptr);
            if (!serviceRetirementExecutor_)
            {
                serviceRetirementExecutor_ = std::make_unique<util::BackgroundExecutor>(1, 0, true);
            }
            if (!serviceRetirementExecutor_->PostDestruction(std::move(detailsPanelThumbnailScheduler_)))
            {
                util::LogError(L"Could not queue the old details-panel thumbnail scheduler for background retirement.");
            }
            detailsPanelThumbnailScheduler_ = std::move(scheduler);
        }
    }

    void MainWindow::ApplyCacheCapacityOverrideSettings(bool recreateDetailsPanelScheduler, bool applyBrowserPaneSettings)
    {
        if (applyBrowserPaneSettings && browserPaneController_)
        {
            browserPaneController_->SetCacheCapacityOverrides(
                thumbnailCacheCapacityOverrideBytes_,
                metadataCacheCapacityOverrideEntries_,
                persistentThumbnailCacheCapacityOverrideBytes_);
        }

        if (recreateDetailsPanelScheduler)
        {
            RecreateDetailsPanelThumbnailScheduler();
        }
    }

    void MainWindow::ApplyPersistentThumbnailCacheSetting()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetPersistentThumbnailCacheEnabled(persistentThumbnailCacheEnabled_);
        }

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->SetDiskCacheEnabled(persistentThumbnailCacheEnabled_);
        }
    }

    void MainWindow::ShowPersistentThumbnailCacheDialog()
    {
        if (!cacheMaintenanceState_)
        {
            return;
        }

        if (cacheMaintenanceActive_)
        {
            cacheMaintenanceDialogPending_ = true;
            return;
        }

        StartPersistentThumbnailCacheStatistics();
    }

    void MainWindow::ShowPersistentThumbnailCacheDialogContents(
        std::wstring content,
        std::wstring expandedInformation,
        std::vector<std::wstring> sourceFilePaths)
    {
        constexpr int kCompactPersistentCacheButtonId = 1001;
        constexpr int kPurgePersistentCacheButtonId = 1002;

        auto sourceScan = std::make_shared<PersistentThumbnailCacheSourceScanState>();
        if (sourceFilePaths.empty())
        {
            CompletePersistentThumbnailCacheSourceScan(sourceScan, {});
        }
        else
        {
            try
            {
                if (!persistentCacheSourceScanExecutor_)
                {
                    persistentCacheSourceScanExecutor_ = std::make_unique<util::BackgroundExecutor>(1, 1);
                }
                if (!persistentCacheSourceScanExecutor_->Post(
                        [sourceFilePaths = std::move(sourceFilePaths), sourceScan]() mutable
                        {
                            ScanPersistentThumbnailCacheSourceFiles(std::move(sourceFilePaths), sourceScan);
                        }))
                {
                    PersistentThumbnailCacheSourceScanResult result;
                    result.failed = true;
                    CompletePersistentThumbnailCacheSourceScan(sourceScan, result);
                    util::IncrementCounter(L"persistent_cache.source_scan.queue_failed");
                }
            }
            catch (const std::exception&)
            {
                PersistentThumbnailCacheSourceScanResult result;
                result.failed = true;
                CompletePersistentThumbnailCacheSourceScan(sourceScan, result);
                util::IncrementCounter(L"persistent_cache.source_scan.queue_exception");
            }
            catch (...)
            {
                PersistentThumbnailCacheSourceScanResult result;
                result.failed = true;
                CompletePersistentThumbnailCacheSourceScan(sourceScan, result);
                util::IncrementCounter(L"persistent_cache.source_scan.queue_unknown_exception");
            }
        }

        PersistentThumbnailCacheDialogCallbackData callbackData;
        callbackData.sourceScan = sourceScan;
        callbackData.content = content;
        callbackData.expandedInformation = expandedInformation;

        TASKDIALOG_BUTTON buttons[] = {
            {kCompactPersistentCacheButtonId, L"Compact cache\nRepair the saved index, remove orphaned thumbnails, and trim the cache to its storage budget."},
            {kPurgePersistentCacheButtonId, L"Purge cache\nDelete every persistent thumbnail and clear the saved cache index."},
        };

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = hwnd_;
        config.hInstance = instance_;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CALLBACK_TIMER;
        config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        config.pszWindowTitle = L"Persistent Thumbnail Cache";
        config.pszMainIcon = MAKEINTRESOURCEW(IDI_HYPERBROWSE);
        config.pszMainInstruction = L"Inspect or clean the persistent thumbnail cache.";
        config.pszContent = content.c_str();
        config.pszExpandedInformation = expandedInformation.c_str();
        config.pszCollapsedControlText = L"Show Cache Details";
        config.pszExpandedControlText = L"Hide Cache Details";
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = kCompactPersistentCacheButtonId;
        config.pfCallback = PersistentThumbnailCacheDialogCallback;
        config.lpCallbackData = reinterpret_cast<LONG_PTR>(&callbackData);

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        sourceScan->cancelRequested.store(true, std::memory_order_release);
        if (FAILED(dialogResult))
        {
            MessageBoxW(hwnd_,
                        L"Failed to open the persistent thumbnail cache dialog.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
            return;
        }

        if (clickedButton == kCompactPersistentCacheButtonId)
        {
            StartPersistentThumbnailCacheMaintenance(false);
            if (cacheMaintenanceActive_)
            {
                ShowPersistentThumbnailCacheMaintenanceProgressDialog(false);
            }
            return;
        }

        if (clickedButton == kPurgePersistentCacheButtonId)
        {
            const int confirmResult = MessageBoxW(hwnd_,
                                                  L"Delete all thumbnails saved in the persistent cache? This does not delete your images.",
                                                  L"Purge Persistent Thumbnail Cache",
                                                  MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (confirmResult == IDYES)
            {
                StartPersistentThumbnailCacheMaintenance(true);
                if (cacheMaintenanceActive_)
                {
                    ShowPersistentThumbnailCacheMaintenanceProgressDialog(true);
                }
            }
        }
    }

    void MainWindow::ShowPersistentThumbnailCacheMaintenanceProgressDialog(bool purge)
    {
        if (!cacheMaintenanceState_ || !hwnd_)
        {
            return;
        }

        PersistentThumbnailCacheMaintenanceProgressCallbackData callbackData;
        callbackData.state = cacheMaintenanceState_;

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = hwnd_;
        config.hInstance = instance_;
        config.dwFlags = TDF_CALLBACK_TIMER | TDF_SHOW_MARQUEE_PROGRESS_BAR;
        config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        config.pszWindowTitle = L"Persistent Thumbnail Cache";
        config.pszMainIcon = MAKEINTRESOURCEW(IDI_HYPERBROWSE);
        config.pszMainInstruction = purge
            ? L"Purging persistent thumbnail cache..."
            : L"Compacting persistent thumbnail cache...";
        config.pszContent = purge
            ? L"Deleting saved thumbnails and clearing the cache index. Please wait."
            : L"Removing orphaned thumbnails and enforcing the configured storage budget. Please wait.";
        config.pfCallback = PersistentThumbnailCacheMaintenanceProgressDialogCallback;
        config.lpCallbackData = reinterpret_cast<LONG_PTR>(&callbackData);

        cacheMaintenanceProgressDialogActive_ = true;
        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        cacheMaintenanceProgressDialogActive_ = false;
        if (FAILED(dialogResult))
        {
            util::IncrementCounter(L"persistent_cache.maintenance.progress_dialog_failed");
            MessageBoxW(hwnd_,
                        L"Cache maintenance is still running in the background.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONWARNING);
        }

        if (cacheMaintenanceCompletionMessagePending_)
        {
            const WPARAM completionMessage = cacheMaintenancePendingCompletionMessage_;
            cacheMaintenanceCompletionMessagePending_ = false;
            cacheMaintenancePendingCompletionMessage_ = 0;
            OnPersistentThumbnailCacheMaintenanceMessage(completionMessage);
        }
    }

    void MainWindow::StartPersistentThumbnailCacheStatistics(bool showDialog)
    {
        if (cacheMaintenanceActive_ || !cacheMaintenanceState_ || !browserPaneController_ || !hwnd_)
        {
            return;
        }

        cacheMaintenanceActive_ = true;
        const HWND targetWindow = hwnd_;
        const auto state = cacheMaintenanceState_;
        const bool queued = browserPaneController_->QueuePersistentThumbnailCacheStatistics(
            [targetWindow, state, showDialog](bool succeeded, cache::DiskThumbnailCache::Statistics statistics)
        {
            std::vector<std::wstring> sourceFilePaths = std::move(statistics.sourceFilePaths);
            statistics.sourceFilePaths.clear();
            if (succeeded)
            {
                {
                    std::scoped_lock lock(state->mutex);
                    state->statistics = std::move(statistics);
                    state->sourceFilePaths = std::move(sourceFilePaths);
                }
            }
            else
            {
                std::scoped_lock lock(state->mutex);
                state->sourceFilePaths.clear();
            }

            if (!PostMessageW(targetWindow,
                              kPersistentThumbnailCacheMaintenanceMessage,
                              static_cast<WPARAM>((succeeded ? kPersistentThumbnailCacheMaintenanceSuccessFlag : 0u)
                                                  | (showDialog
                                                         ? kPersistentThumbnailCacheMaintenanceShowDialogFlag
                                                             | kPersistentThumbnailCacheSourcePathsIncludedFlag
                                                         : 0u)),
                              0))
            {
                return;
            }
        },
            showDialog);

        if (!queued)
        {
            cacheMaintenanceActive_ = false;
            MessageBoxW(hwnd_,
                        L"The persistent thumbnail cache operation could not be started.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::StartPersistentThumbnailCacheMaintenance(bool purge, bool showDialog)
    {
        if (cacheMaintenanceActive_ || !cacheMaintenanceState_ || !browserPaneController_ || !hwnd_)
        {
            return;
        }

        cacheMaintenanceActive_ = true;
        cacheMaintenanceState_->completionMessage.store(0, std::memory_order_release);
        const HWND targetWindow = hwnd_;
        const auto state = cacheMaintenanceState_;
        const bool queued = browserPaneController_->QueuePersistentThumbnailCacheMaintenance(
            purge,
            [targetWindow, state, purge, showDialog](bool succeeded)
        {
            const unsigned int completionMessage = static_cast<unsigned int>(purge
                                                                                  ? PersistentThumbnailCacheMaintenanceOperation::Purge
                                                                                  : showDialog
                                                                                      ? PersistentThumbnailCacheMaintenanceOperation::Compact
                                                                                      : PersistentThumbnailCacheMaintenanceOperation::Trim)
                | (succeeded ? kPersistentThumbnailCacheMaintenanceSuccessFlag : 0u);
            state->completionMessage.store(completionMessage, std::memory_order_release);
            if (!PostMessageW(targetWindow,
                              kPersistentThumbnailCacheMaintenanceMessage,
                              static_cast<WPARAM>(completionMessage),
                              0))
            {
                util::IncrementCounter(L"persistent_cache.maintenance.completion_post_failed");
            }
        });

        if (!queued)
        {
            cacheMaintenanceActive_ = false;
            MessageBoxW(hwnd_,
                        L"The persistent thumbnail cache operation could not be started.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
        }
    }

    LRESULT MainWindow::OnPersistentThumbnailCacheMaintenanceMessage(WPARAM wParam)
    {
        if (cacheMaintenanceProgressDialogActive_)
        {
            cacheMaintenanceCompletionMessagePending_ = true;
            cacheMaintenancePendingCompletionMessage_ = wParam;
            return 0;
        }

        cacheMaintenanceActive_ = false;
        const auto operation = static_cast<PersistentThumbnailCacheMaintenanceOperation>(wParam & 3u);
        const bool succeeded = (wParam & kPersistentThumbnailCacheMaintenanceSuccessFlag) != 0;
        const bool sourceFilePathsIncluded = (wParam & kPersistentThumbnailCacheSourcePathsIncludedFlag) != 0;
        const bool showDialog = (wParam & kPersistentThumbnailCacheMaintenanceShowDialogFlag) != 0
            || cacheMaintenanceDialogPending_;

        if (operation == PersistentThumbnailCacheMaintenanceOperation::Statistics)
        {
            cacheMaintenanceDialogPending_ = false;
            if (succeeded && showDialog && !sourceFilePathsIncluded)
            {
                StartPersistentThumbnailCacheStatistics(true);
                return 0;
            }

            if (!succeeded || !cacheMaintenanceState_)
            {
                if (showDialog)
                {
                    MessageBoxW(hwnd_,
                                L"Failed to inspect the persistent thumbnail cache.",
                                L"Persistent Thumbnail Cache",
                                MB_OK | MB_ICONERROR);
                }
                return 0;
            }

            cache::DiskThumbnailCache::Statistics statistics;
            std::vector<std::wstring> sourceFilePaths;
            {
                std::scoped_lock lock(cacheMaintenanceState_->mutex);
                const auto& cachedStatistics = cacheMaintenanceState_->statistics;
                statistics.cacheDirectory = cachedStatistics.cacheDirectory;
                statistics.capacityBytes = cachedStatistics.capacityBytes;
                statistics.indexedEntryCount = cachedStatistics.indexedEntryCount;
                statistics.indexedBytes = cachedStatistics.indexedBytes;
                statistics.indexFileBytes = cachedStatistics.indexFileBytes;
                statistics.cacheFileCount = cachedStatistics.cacheFileCount;
                statistics.cacheFileBytes = cachedStatistics.cacheFileBytes;
                statistics.orphanFileCount = cachedStatistics.orphanFileCount;
                statistics.orphanFileBytes = cachedStatistics.orphanFileBytes;
                statistics.missingFileCount = cachedStatistics.missingFileCount;
                sourceFilePaths = std::move(cacheMaintenanceState_->sourceFilePaths);
            }
            if (statistics.cacheDirectory.empty())
            {
                if (showDialog)
                {
                    MessageBoxW(hwnd_,
                                L"The persistent thumbnail cache folder could not be resolved.",
                                L"Persistent Thumbnail Cache",
                                MB_OK | MB_ICONERROR);
                }
                return 0;
            }

            if (showDialog)
            {
                const std::wstring initialSourceScanStatus = BuildPersistentThumbnailCacheSourceScanStatus(nullptr);
                ShowPersistentThumbnailCacheDialogContents(
                    BuildPersistentThumbnailCacheSummary(
                        statistics,
                        persistentThumbnailCacheEnabled_,
                        initialSourceScanStatus),
                    BuildPersistentThumbnailCacheDetails(statistics, initialSourceScanStatus),
                    std::move(sourceFilePaths));
            }
            else if (activeRightPaneTab_ == RightPaneTab::CacheStats && detailsStripVisible_)
            {
                UpdateDetailsPanelPerformanceStats();
                if (!IsRectEmpty(&detailsPanelRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
                }
            }
            return 0;
        }

        const bool purge = operation == PersistentThumbnailCacheMaintenanceOperation::Purge;
        const bool trim = operation == PersistentThumbnailCacheMaintenanceOperation::Trim;
        const bool dialogRequested = cacheMaintenanceDialogPending_;
        cacheMaintenanceDialogPending_ = false;

        if (!succeeded)
        {
            MessageBoxW(hwnd_,
                        purge
                            ? L"Failed to purge the persistent thumbnail cache."
                            : trim
                                ? L"Failed to trim the persistent thumbnail cache."
                                : L"Failed to compact the persistent thumbnail cache.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
            if (!trim)
            {
                ShowPersistentThumbnailCacheDialog();
            }
            return 0;
        }

        NotifyLongOperationComplete(
            purge ? L"Persistent Thumbnail Cache Purged" : trim ? L"Persistent Thumbnail Cache Trimmed" : L"Persistent Thumbnail Cache Compacted",
            purge
                ? L"All saved thumbnails were removed from the persistent cache."
                : trim
                    ? L"The persistent thumbnail cache was trimmed to its configured budget."
                    : L"The persistent thumbnail cache index and storage were repaired.");
        if (!trim || dialogRequested)
        {
            ShowPersistentThumbnailCacheDialog();
        }
        return 0;
    }

    void MainWindow::ApplyRawJpegPairingSettings()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetRawJpegDisplayPreference(pairedRawJpegViewerPreference_);
        browserPaneController_->SetRawJpegStackingEnabled(rawJpegPairedOperationsEnabled_);
    }

    void MainWindow::ShowPerformanceSettingsDialog()
    {
        const std::size_t currentThumbnailCacheCapacityBytes = browserPaneController_
            ? browserPaneController_->ThumbnailCacheCapacityBytes()
            : thumbnailCacheCapacityOverrideBytes_;
        const std::size_t currentMetadataCacheCapacityEntries = browserPaneController_
            ? browserPaneController_->MetadataCacheCapacityEntries()
            : metadataCacheCapacityOverrideEntries_;

        std::size_t thumbnailCacheCapacityOverrideBytes = thumbnailCacheCapacityOverrideBytes_;
        std::size_t metadataCacheCapacityOverrideEntries = metadataCacheCapacityOverrideEntries_;
        bool showPressureStateInStatusBar = showPressureStateInStatusBar_;
        if (!PromptForPerformanceSettings(hwnd_,
                                          instance_,
                                          appTextSize_,
                                          themeMode_ == ThemeMode::Dark,
                                          resourceProfile_,
                                          currentThumbnailCacheCapacityBytes,
                                          currentMetadataCacheCapacityEntries,
                                          thumbnailCacheCapacityOverrideBytes_,
                                          metadataCacheCapacityOverrideEntries_,
                                          showPressureStateInStatusBar_,
                                          &thumbnailCacheCapacityOverrideBytes,
                                          &metadataCacheCapacityOverrideEntries,
                                          &showPressureStateInStatusBar))
        {
            return;
        }

        if (thumbnailCacheCapacityOverrideBytes_ == thumbnailCacheCapacityOverrideBytes
            && metadataCacheCapacityOverrideEntries_ == metadataCacheCapacityOverrideEntries
            && showPressureStateInStatusBar_ == showPressureStateInStatusBar)
        {
            return;
        }

        thumbnailCacheCapacityOverrideBytes_ = thumbnailCacheCapacityOverrideBytes;
        metadataCacheCapacityOverrideEntries_ = metadataCacheCapacityOverrideEntries;
        showPressureStateInStatusBar_ = showPressureStateInStatusBar;
        ApplyCacheCapacityOverrideSettings();
        UpdateStatusText();
    }
}
