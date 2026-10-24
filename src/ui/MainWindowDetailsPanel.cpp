#include "ui/MainWindowInternal.h"

namespace
{
    constexpr UINT_PTR kPerformanceHudTimerId = 9108;
    constexpr UINT kPerformanceHudIntervalMs = 1000;
    constexpr int kCacheStatsVisualMinContentWidth = 520;
    constexpr int kCacheStatsVisualWidth = 220;
    constexpr int kCacheStatsVisualGap = 18;
    constexpr int kCacheStatsVisualHeight = 350;

    hyperbrowse::cache::ThumbnailCacheKey MakeThumbnailCacheKey(const hyperbrowse::browser::BrowserItem& item,
                                                                int targetWidth,
                                                                int targetHeight)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = item.filePath;
        key.modifiedTimestampUtc = item.modifiedTimestampUtc;
        key.targetWidth = targetWidth;
        key.targetHeight = targetHeight;
        return key;
    }

    COLORREF GetCacheStatsHighlightColor(bool darkMode)
    {
        return darkMode ? RGB(236, 174, 76) : RGB(170, 92, 22);
    }

    std::wstring BuildCameraSummaryLabel(const hyperbrowse::services::ImageMetadata& metadata)
    {
        if (!metadata.cameraMake.empty() && !metadata.cameraModel.empty())
        {
            return metadata.cameraMake + L" " + metadata.cameraModel;
        }

        return !metadata.cameraModel.empty() ? metadata.cameraModel : metadata.cameraMake;
    }

    bool IsCuratedDetailsProperty(std::wstring_view canonicalName)
    {
        return canonicalName == L"System.Image.Dimensions"
            || canonicalName == L"System.Image.HorizontalSize"
            || canonicalName == L"System.Image.VerticalSize"
            || canonicalName == L"System.Photo.CameraManufacturer"
            || canonicalName == L"System.Photo.CameraModel"
            || canonicalName == L"System.Photo.DateTaken"
            || canonicalName == L"System.Photo.ExposureTime"
            || canonicalName == L"System.Photo.FNumber"
            || canonicalName == L"System.Photo.ISOSpeed"
            || canonicalName == L"System.Photo.FocalLength"
            || canonicalName == L"System.Title"
            || canonicalName == L"System.Author"
            || canonicalName == L"System.Keywords"
            || canonicalName == L"System.Comment";
    }

    bool HasEquivalentDisplayedProperty(const std::vector<hyperbrowse::services::MetadataPropertyEntry>& properties,
                                       const hyperbrowse::services::MetadataPropertyEntry& candidate)
    {
        return std::any_of(properties.begin(),
                           properties.end(),
                           [&](const hyperbrowse::services::MetadataPropertyEntry& property)
                           {
                               return StringsEqualInsensitive(property.displayName, candidate.displayName)
                                   && StringsEqualInsensitive(property.value, candidate.value);
                           });
    }

    std::wstring BuildSingleSelectionSummary(const hyperbrowse::browser::BrowserItem& item)
    {
        if (item.isDirectory)
        {
            return L"Folder";
        }

        std::wstring summary = item.fileType;

        const std::wstring dimensions = hyperbrowse::browser::FormatDimensionsForItem(item);
        if (!dimensions.empty() && dimensions != L"...")
        {
            if (!summary.empty())
            {
                summary.append(L" | ");
            }
            summary.append(dimensions);
        }

        const std::wstring fileSize = hyperbrowse::browser::FormatByteSize(item.fileSizeBytes);
        if (!fileSize.empty())
        {
            if (!summary.empty())
            {
                summary.append(L" | ");
            }
            summary.append(fileSize);
        }

        return summary;
    }

    template <typename Getter>
    bool TryGetCommonItemString(const std::vector<hyperbrowse::browser::BrowserItem>& items,
                                Getter getter,
                                std::wstring* commonValue)
    {
        if (!commonValue || items.empty())
        {
            return false;
        }

        const std::wstring first = getter(items.front());
        if (first.empty() || first == L"...")
        {
            return false;
        }

        for (std::size_t index = 1; index < items.size(); ++index)
        {
            const std::wstring candidate = getter(items[index]);
            if (candidate.empty() || candidate == L"..." || !StringsEqualInsensitive(first, candidate))
            {
                return false;
            }
        }

        *commonValue = first;
        return true;
    }

    template <typename Getter>
    bool TryGetCommonMetadataString(const std::vector<std::shared_ptr<const hyperbrowse::services::ImageMetadata>>& metadataList,
                                    Getter getter,
                                    std::wstring* commonValue)
    {
        if (!commonValue || metadataList.empty() || !metadataList.front())
        {
            return false;
        }

        const std::wstring first = getter(*metadataList.front());
        if (first.empty())
        {
            return false;
        }

        for (std::size_t index = 1; index < metadataList.size(); ++index)
        {
            if (!metadataList[index])
            {
                return false;
            }

            const std::wstring candidate = getter(*metadataList[index]);
            if (candidate.empty() || !StringsEqualInsensitive(first, candidate))
            {
                return false;
            }
        }

        *commonValue = first;
        return true;
    }

    bool TryGetCommonDimensions(const std::vector<hyperbrowse::browser::BrowserItem>& items,
                                std::wstring* commonValue)
    {
        if (!commonValue || items.empty())
        {
            return false;
        }

        const int firstWidth = items.front().imageWidth;
        const int firstHeight = items.front().imageHeight;
        if (firstWidth <= 0 || firstHeight <= 0)
        {
            return false;
        }

        for (std::size_t index = 1; index < items.size(); ++index)
        {
            if (items[index].imageWidth != firstWidth || items[index].imageHeight != firstHeight)
            {
                return false;
            }
        }

        *commonValue = hyperbrowse::browser::FormatDimensions(firstWidth, firstHeight);
        return true;
    }

    std::vector<hyperbrowse::services::MetadataPropertyEntry> FindCommonMetadataProperties(
        const std::vector<std::shared_ptr<const hyperbrowse::services::ImageMetadata>>& metadataList)
    {
        if (metadataList.empty() || !metadataList.front())
        {
            return {};
        }

        std::vector<hyperbrowse::services::MetadataPropertyEntry> commonProperties;
        for (const hyperbrowse::services::MetadataPropertyEntry& property : metadataList.front()->properties)
        {
            if (property.value.empty())
            {
                continue;
            }

            bool isCommon = true;
            for (std::size_t index = 1; index < metadataList.size(); ++index)
            {
                if (!metadataList[index])
                {
                    isCommon = false;
                    break;
                }

                const auto propertyIt = std::find_if(metadataList[index]->properties.begin(),
                                                     metadataList[index]->properties.end(),
                                                     [&](const hyperbrowse::services::MetadataPropertyEntry& candidate)
                                                     {
                                                         return candidate.canonicalName == property.canonicalName
                                                             && StringsEqualInsensitive(candidate.value, property.value);
                                                     });
                if (propertyIt == metadataList[index]->properties.end())
                {
                    isCommon = false;
                    break;
                }
            }

            if (isCommon)
            {
                commonProperties.push_back(property);
            }
        }

        return commonProperties;
    }

    std::wstring FormatRatingForDisplay(int rating)
    {
        if (rating <= 0)
        {
            return L"Unrated";
        }

        return std::to_wstring(std::clamp(rating, 0, 5)) + L"/5";
    }
}

namespace hyperbrowse::ui
{

    void MainWindow::ApplyDetailsPanelText(std::wstring title, std::wstring summary, std::wstring body)
    {
        detailsPanelTitleText_ = std::move(title);
        detailsPanelSummaryText_ = std::move(summary);
        detailsPanelBodyText_ = std::move(body);

        if (detailsPanelText_)
        {
            const bool changed = SetWindowTextIfDifferent(detailsPanelText_, detailsPanelBodyText_);
            RefreshDetailsPanelBodyPresentation();
            if (changed)
            {
                SendMessageW(detailsPanelText_, EM_SETSEL, 0, 0);
                SendMessageW(detailsPanelText_, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
            }
        }


        if (hwnd_ && !IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
    }

    void MainWindow::RefreshDetailsPanelBodyPresentation()
    {
        if (!detailsPanelText_ || !detailsPanelRichEditModule_)
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        SendMessageW(detailsPanelText_, EM_SETBKGNDCOLOR, FALSE, palette.paneBackground);
        SendMessageW(detailsPanelText_, WM_SETREDRAW, FALSE, 0);

        CHARRANGE allText{};
        allText.cpMin = 0;
        allText.cpMax = -1;
        SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allText));

        CHARFORMAT2W baseFormat{};
        baseFormat.cbSize = sizeof(baseFormat);
        baseFormat.dwMask = CFM_BOLD | CFM_COLOR;
        baseFormat.dwEffects = 0;
        baseFormat.crTextColor = palette.mutedText;
        SendMessageW(detailsPanelText_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&baseFormat));

        const LRESULT lineCount = SendMessageW(detailsPanelText_, EM_GETLINECOUNT, 0, 0);
        for (LONG lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            const LONG lineStart = static_cast<LONG>(SendMessageW(detailsPanelText_, EM_LINEINDEX, lineIndex, 0));
            if (lineStart < 0)
            {
                continue;
            }

            const LONG lineLength = static_cast<LONG>(SendMessageW(detailsPanelText_, EM_LINELENGTH, lineStart, 0));
            if (lineLength <= 0)
            {
                continue;
            }

            std::vector<wchar_t> lineBuffer(static_cast<std::size_t>(lineLength) + 2, L'\0');
            *reinterpret_cast<WORD*>(lineBuffer.data()) = static_cast<WORD>(lineBuffer.size() - 1);
            const LRESULT copiedChars = SendMessageW(detailsPanelText_,
                                                     EM_GETLINE,
                                                     static_cast<WPARAM>(lineIndex),
                                                     reinterpret_cast<LPARAM>(lineBuffer.data()));
            if (copiedChars <= 0)
            {
                continue;
            }

            std::wstring_view line(lineBuffer.data(), static_cast<std::size_t>(copiedChars));
            LONG emphasisStart = lineStart;
            LONG emphasisEnd = lineStart + static_cast<LONG>(line.size());

            const std::size_t colon = line.find(L':');
            if (colon != std::wstring::npos)
            {
                std::size_t valueOffset = colon + 1;
                while (valueOffset < line.size() && iswspace(line[valueOffset]) != 0)
                {
                    ++valueOffset;
                }

                if (valueOffset >= line.size())
                {
                    continue;
                }

                emphasisStart = lineStart + static_cast<LONG>(valueOffset);
            }

            CHARRANGE emphasisRange{};
            emphasisRange.cpMin = emphasisStart;
            emphasisRange.cpMax = emphasisEnd;
            SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&emphasisRange));

            CHARFORMAT2W emphasisFormat{};
            emphasisFormat.cbSize = sizeof(emphasisFormat);
            emphasisFormat.dwMask = CFM_BOLD | CFM_COLOR;
            emphasisFormat.dwEffects = CFE_BOLD;
            emphasisFormat.crTextColor = palette.text;
            SendMessageW(detailsPanelText_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&emphasisFormat));
        }

        CHARRANGE resetSelection{};
        resetSelection.cpMin = 0;
        resetSelection.cpMax = 0;
        SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&resetSelection));
        SendMessageW(detailsPanelText_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(detailsPanelText_, nullptr, TRUE);
    }

    void MainWindow::ResetDetailsPanelHistogram()
    {
        detailsPanelHistogramPath_.clear();
        detailsPanelHistogramModifiedTimestampUtc_ = 0;
        detailsPanelHistogramModelIndex_ = -1;
        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = false;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->CancelOutstanding();
        }
    }

    void MainWindow::RequestDetailsPanelHistogram(const browser::BrowserItem& item, int modelIndex)
    {
        if (!detailsPanelThumbnailScheduler_ || !decode::CanDecodeThumbnail(item) || modelIndex < 0)
        {
            ResetDetailsPanelHistogram();
            return;
        }

        const int targetWidth = 192;
        const int targetHeight = 128;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);

        if (detailsPanelHistogramModelIndex_ == modelIndex
            && detailsPanelHistogramModifiedTimestampUtc_ == item.modifiedTimestampUtc
            && StringsEqualInsensitive(detailsPanelHistogramPath_, item.filePath)
            && (detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_))
        {
            return;
        }

        detailsPanelHistogramPath_ = item.filePath;
        detailsPanelHistogramModifiedTimestampUtc_ = item.modifiedTimestampUtc;
        detailsPanelHistogramModelIndex_ = modelIndex;

        if (const auto cachedThumbnail = detailsPanelThumbnailScheduler_->FindCachedThumbnail(cacheKey))
        {
            ApplyDetailsPanelHistogram(*cachedThumbnail);
            LayoutChildren();
            return;
        }

        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = true;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        ++detailsPanelThumbnailRequestEpoch_;
        detailsPanelThumbnailScheduler_->Schedule(detailsPanelThumbnailSessionId_,
                                                 detailsPanelThumbnailRequestEpoch_,
                                                 {services::ThumbnailWorkItem{modelIndex, cacheKey, 0, true}});
        LayoutChildren();
    }

    void MainWindow::ApplyDetailsPanelHistogram(const cache::CachedThumbnail& thumbnail)
    {
        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = false;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        DetailsPanelHistogram::Result result;
        if (!DetailsPanelHistogram::Compute(thumbnail.Bitmap(), &result))
        {
            return;
        }

        detailsPanelHistogramRed_ = result.red;
        detailsPanelHistogramGreen_ = result.green;
        detailsPanelHistogramBlue_ = result.blue;
        detailsPanelHistogramPeak_ = result.peak;
        detailsPanelHistogramVisible_ = result.visible;
    }

    void MainWindow::UpdateDetailsPanelPerformanceStats()
    {
        DetailsPanelPerformanceStats stats;
        if (browserPaneController_)
        {
            const auto thumbnailStatistics = browserPaneController_->ThumbnailCacheStatistics();
            const auto metadataStatistics = browserPaneController_->MetadataCacheStatistics();
            stats.thumbnailBytes = browserPaneController_->ThumbnailCacheBytes();
            stats.thumbnailCapacityBytes = browserPaneController_->ThumbnailCacheCapacityBytes();
            stats.thumbnailHits = thumbnailStatistics.hitCount;
            stats.thumbnailMisses = thumbnailStatistics.missCount;
            stats.thumbnailEvictions = thumbnailStatistics.evictionCount;
            const auto thumbnailRuntime = browserPaneController_->ThumbnailRuntimeStatistics();
            stats.thumbnailPendingJobs = thumbnailRuntime.pendingJobCount;
            stats.thumbnailInflightDecodes = thumbnailRuntime.inflightDecodeCount;
            stats.thumbnailActiveWorkers = thumbnailRuntime.activeWorkerCount;
            stats.thumbnailDecodeLimit = thumbnailRuntime.activeDecodeLimit;
            stats.metadataEntries = browserPaneController_->MetadataCacheEntryCount();
            stats.metadataCapacityEntries = browserPaneController_->MetadataCacheCapacityEntries();
            stats.metadataHits = metadataStatistics.hitCount;
            stats.metadataMisses = metadataStatistics.missCount;
            stats.metadataEvictions = metadataStatistics.evictionCount;
        }

        stats.persistentCapacityBytes = persistentThumbnailCacheCapacityOverrideBytes_ != 0
            ? persistentThumbnailCacheCapacityOverrideBytes_
            : services::ThumbnailScheduler::ResolvePersistentCacheCapacityBytes(0, resourceProfile_);
        if (cacheMaintenanceState_)
        {
            std::scoped_lock lock(cacheMaintenanceState_->mutex);
            stats.persistentBytes = cacheMaintenanceState_->statistics.cacheFileBytes;
            stats.persistentIndexedEntries = cacheMaintenanceState_->statistics.indexedEntryCount;
            stats.persistentFileCount = cacheMaintenanceState_->statistics.cacheFileCount;
            stats.persistentStatisticsAvailable = !cacheMaintenanceState_->statistics.cacheDirectory.empty();
        }

        const auto diagnostics = util::CaptureDiagnosticsSnapshot();
        const auto scaleTiming = std::find_if(diagnostics.timings.begin(), diagnostics.timings.end(), [](const auto& timing)
        {
            return timing.name == L"thumbnail.scale";
        });
        if (scaleTiming != diagnostics.timings.end())
        {
            stats.scaleSampleCount = scaleTiming->count;
            stats.scaleAverageMs = scaleTiming->averageMs;
        }

        const auto hitRate = [](std::uint64_t hits, std::uint64_t misses)
        {
            const std::uint64_t total = hits + misses;
            return total == 0 ? 0ULL : (hits * 100ULL) / total;
        };
        std::wstring text;
        text.reserve(1024);
        text.append(L"Thumbnail Cache\r\n");
        text.append(L"Usage: ");
        text.append(browser::FormatByteSize(stats.thumbnailBytes));
        text.append(L" / ");
        text.append(browser::FormatByteSize(stats.thumbnailCapacityBytes));
        text.append(L"\r\nHit rate: ");
        text.append(std::to_wstring(hitRate(stats.thumbnailHits, stats.thumbnailMisses)));
        text.append(L"%   Hits: ");
        text.append(std::to_wstring(stats.thumbnailHits));
        text.append(L"\r\nMisses: ");
        text.append(std::to_wstring(stats.thumbnailMisses));
        text.append(L"   Evictions: ");
        text.append(std::to_wstring(stats.thumbnailEvictions));
        text.append(L"\r\n\r\nMetadata Cache\r\nEntries: ");
        text.append(std::to_wstring(stats.metadataEntries));
        text.append(L" / ");
        text.append(std::to_wstring(stats.metadataCapacityEntries));
        text.append(L"\r\nHit rate: ");
        text.append(std::to_wstring(hitRate(stats.metadataHits, stats.metadataMisses)));
        text.append(L"%   Hits: ");
        text.append(std::to_wstring(stats.metadataHits));
        text.append(L"\r\nMisses: ");
        text.append(std::to_wstring(stats.metadataMisses));
        text.append(L"   Evictions: ");
        text.append(std::to_wstring(stats.metadataEvictions));
        text.append(L"\r\n\r\nDecode Pipeline\r\nQueue: ");
        text.append(std::to_wstring(stats.thumbnailPendingJobs));
        text.append(L"   Active: ");
        text.append(std::to_wstring(stats.thumbnailInflightDecodes));
        text.append(L" (workers ");
        text.append(std::to_wstring(stats.thumbnailActiveWorkers));
        text.append(L" / ");
        text.append(std::to_wstring(stats.thumbnailDecodeLimit));
        text.append(L")\r\nScale average: ");
        if (stats.scaleSampleCount == 0)
        {
            text.append(L"No samples");
        }
        else
        {
            text.append(std::to_wstring(stats.scaleAverageMs));
            text.append(L" ms");
        }
        text.append(L"\r\n\r\nPersistent Thumbnail Cache\r\n");
        if (stats.persistentStatisticsAvailable)
        {
            text.append(L"Disk: ");
            text.append(browser::FormatByteSize(stats.persistentBytes));
            text.append(L" / ");
            text.append(browser::FormatByteSize(stats.persistentCapacityBytes));
            text.append(L"\r\nIndexed: ");
            text.append(std::to_wstring(stats.persistentIndexedEntries));
            text.append(L"   Files: ");
            text.append(std::to_wstring(stats.persistentFileCount));
        }
        else
        {
            text.append(L"Checking asynchronously...");
        }
        text.append(L"\r\n\r\nMemory pressure: ");
        text.append(thumbnailMemoryPressureActive_ ? L"Active" : L"Normal");

        detailsPanelPerformanceStats_ = stats;
        detailsPanelPerformanceText_ = std::move(text);
    }

    void MainWindow::SetPerformanceHudEnabled(bool enabled)
    {
        if (performanceHudEnabled_ == enabled)
        {
            return;
        }

        if (enabled)
        {
            if (!hwnd_)
            {
                return;
            }

            performanceHudTimerId_ = SetTimer(hwnd_,
                                              kPerformanceHudTimerId,
                                              kPerformanceHudIntervalMs,
                                              nullptr);
            if (performanceHudTimerId_ == 0)
            {
                util::LogError(L"Unable to start the performance HUD refresh timer.");
                UpdateMenuState();
                return;
            }

            performanceHudEnabled_ = true;
            UpdatePerformanceHud();
        }
        else
        {
            performanceHudEnabled_ = false;
            if (performanceHudTimerId_ != 0 && hwnd_ && IsWindow(hwnd_) != FALSE)
            {
                KillTimer(hwnd_, performanceHudTimerId_);
            }
            performanceHudTimerId_ = 0;
            if (browserPaneController_)
            {
                browserPaneController_->SetPerformanceHudText({});
            }
        }

        UpdateMenuState();
    }

    void MainWindow::UpdatePerformanceHud()
    {
        if (!performanceHudEnabled_ || !browserPaneController_)
        {
            return;
        }

        UpdateDetailsPanelPerformanceStats();
        const DetailsPanelPerformanceStats& stats = detailsPanelPerformanceStats_;
        PerformanceHudSnapshot snapshot;
        snapshot.activeDecodes = stats.thumbnailInflightDecodes;
        if (stats.scaleSampleCount > 0)
        {
            snapshot.scaleAverageMs = stats.scaleAverageMs;
        }

        const std::uint64_t thumbnailCacheSamples = stats.thumbnailHits + stats.thumbnailMisses;
        if (thumbnailCacheSamples > 0)
        {
            snapshot.thumbnailCacheHitRatePercent =
                (static_cast<double>(stats.thumbnailHits) * 100.0)
                / static_cast<double>(thumbnailCacheSamples);
        }
        snapshot.memoryPressureActive = thumbnailMemoryPressureActive_;
        snapshot.pendingThumbnailJobs = stats.thumbnailPendingJobs;
        browserPaneController_->SetPerformanceHudText(FormatPerformanceHudText(snapshot));
    }

    void MainWindow::UpdateDetailsPanel()
    {
        detailsPanelPromptText_.clear();
        UpdateDetailsPanelPerformanceStats();

        if (!detailsStripVisible_)
        {
            ResetDetailsPanelHistogram();
            return;
        }

        if (!browserPaneController_ || !browserModel_)
        {
            ApplyDetailsPanelText(L"File Details", L"Select one or more images to inspect metadata.", L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        const std::vector<int> selectedModelIndices = browserPaneController_->OrderedSelectedModelIndicesSnapshot();
        if (selectedModelIndices.empty())
        {
            ApplyDetailsPanelText(L"File Details",
                                  L"Select one or more images to inspect camera, EXIF, and other image metadata.",
                                  L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        const auto& items = browserModel_->Items();
        std::vector<browser::BrowserItem> selectedItems;
        selectedItems.reserve(selectedModelIndices.size());
        constexpr std::size_t kMaxSharedMetadataItems = 100;
        std::vector<std::shared_ptr<const services::ImageMetadata>> metadataList;
        metadataList.reserve(std::min(selectedModelIndices.size(), kMaxSharedMetadataItems));
        bool allMetadataLoaded = true;
        std::uint64_t selectedBytes = 0;

        for (const int modelIndex : selectedModelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
            {
                continue;
            }

            selectedItems.push_back(items[static_cast<std::size_t>(modelIndex)]);
            selectedBytes += items[static_cast<std::size_t>(modelIndex)].fileSizeBytes;
            if (metadataList.size() < kMaxSharedMetadataItems)
            {
                if (items[static_cast<std::size_t>(modelIndex)].isDirectory)
                {
                    metadataList.push_back(nullptr);
                    continue;
                }

                const auto metadata = browserPaneController_->FindCachedMetadataForModelIndex(modelIndex);
                metadataList.push_back(metadata);
                allMetadataLoaded = allMetadataLoaded && static_cast<bool>(metadata);
            }
        }

        browserPaneController_->RequestMetadataForModelIndices(selectedModelIndices);

        if (selectedItems.empty())
        {
            ApplyDetailsPanelText(L"File Details", L"Select one or more images to inspect metadata.", L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        if (selectedItems.size() == 1)
        {
            const browser::BrowserItem& item = selectedItems.front();
            const auto metadata = metadataList.front();

            if (metadata && !item.isDirectory)
            {
                detailsPanelPromptText_ = services::ExtractImagePrompt(*metadata);
            }

            const std::wstring summary = BuildSingleSelectionSummary(item);

            std::wstring body = item.isDirectory
                ? L"Double-click this folder to open it."
                : (metadata ? services::FormatImageMetadataReport(item, *metadata)
                            : L"Loading detailed metadata...");

            if (userMetadataStore_)
            {
                const services::UserMetadataEntry metadataEntry = userMetadataStore_->EntryForPath(item.filePath);
                if (metadataEntry.rating > 0 || !metadataEntry.tags.empty())
                {
                    if (!body.empty())
                    {
                        body.append(L"\r\n\r\n");
                    }
                    body.append(L"User Metadata\r\n");
                    if (metadataEntry.rating > 0)
                    {
                        AppendLabeledLine(&body, L"Rating: ", FormatRatingForDisplay(metadataEntry.rating));
                    }
                    if (!metadataEntry.tags.empty())
                    {
                        AppendLabeledLine(&body, L"Tags: ", metadataEntry.tags);
                    }
                }
            }

            ApplyDetailsPanelText(item.fileName, std::move(summary), std::move(body));
            RequestDetailsPanelHistogram(item, selectedModelIndices.front());
            LayoutChildren();
            return;
        }

        const bool analysisTruncated = selectedItems.size() > metadataList.size();
        const std::vector<browser::BrowserItem> analysisItems(
            selectedItems.begin(),
            selectedItems.begin() + static_cast<std::ptrdiff_t>(metadataList.size()));

        std::wstring body;
        body.reserve(2048);
        body.append(L"Common Attributes\r\n");
        bool hasCommonAttributes = false;

        std::wstring commonValue;
        if (TryGetCommonItemString(analysisItems, [](const browser::BrowserItem& item) { return item.fileType; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Type: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonDimensions(analysisItems, &commonValue))
        {
            AppendLabeledLine(&body, L"Dimensions: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return BuildCameraSummaryLabel(metadata); }, &commonValue))
        {
            AppendLabeledLine(&body, L"Camera: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.dateTaken; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Date Taken: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.exposureTime; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Exposure: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.fNumber; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Aperture: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.isoSpeed; }, &commonValue))
        {
            AppendLabeledLine(&body, L"ISO: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.focalLength; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Focal Length: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.title; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Title: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.author; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Author: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.keywords; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Keywords: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.comment; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Comment: ", commonValue);
            hasCommonAttributes = true;
        }

        bool wroteSelectionMetadataHeader = false;
        if (userMetadataStore_)
        {
            bool firstRating = true;
            int selectionRating = 0;
            bool mixedRating = false;
            bool firstTags = true;
            std::wstring selectionTags;
            bool mixedTags = false;

            for (const browser::BrowserItem& item : selectedItems)
            {
                const services::UserMetadataEntry metadataEntry = userMetadataStore_->EntryForPath(item.filePath);
                const int candidateRating = std::clamp(metadataEntry.rating, 0, 5);
                if (firstRating)
                {
                    selectionRating = candidateRating;
                    firstRating = false;
                }
                else if (candidateRating != selectionRating)
                {
                    mixedRating = true;
                }

                if (firstTags)
                {
                    selectionTags = metadataEntry.tags;
                    firstTags = false;
                }
                else if (!util::EqualsIgnoreCaseOrdinal(selectionTags, metadataEntry.tags))
                {
                    mixedTags = true;
                }
            }

            const bool showRating = mixedRating || selectionRating > 0;
            const bool showTags = mixedTags || !selectionTags.empty();
            if (showRating || showTags)
            {
                body.append(L"\r\nSelection Metadata\r\n");
                wroteSelectionMetadataHeader = true;
            }
            if (showRating)
            {
                AppendLabeledLine(&body, L"Rating: ", mixedRating ? std::wstring(L"Mixed") : FormatRatingForDisplay(selectionRating));
            }
            if (showTags)
            {
                AppendLabeledLine(&body, L"Tags: ", mixedTags ? std::wstring(L"Mixed") : selectionTags);
            }
        }

        if (allMetadataLoaded)
        {
            bool wroteAdditionalHeader = false;
            std::vector<services::MetadataPropertyEntry> renderedCommonProperties;
            for (const services::MetadataPropertyEntry& property : FindCommonMetadataProperties(metadataList))
            {
                if (property.value.empty() || IsCuratedDetailsProperty(property.canonicalName))
                {
                    continue;
                }

                if (HasEquivalentDisplayedProperty(renderedCommonProperties, property))
                {
                    continue;
                }

                if (!wroteAdditionalHeader)
                {
                    body.append(L"\r\nCommon Additional Metadata\r\n");
                    wroteAdditionalHeader = true;
                }
                AppendLabeledLine(&body, property.displayName + L": ", property.value);
                renderedCommonProperties.push_back(property);
                hasCommonAttributes = true;
            }
        }
        else
        {
            body.append(L"\r\nLoading detailed metadata for the current selection...\r\n");
        }

        if (!hasCommonAttributes && !wroteSelectionMetadataHeader)
        {
            body.append(L"No common file or metadata attributes are shared by the current selection.");
        }

        if (analysisTruncated)
        {
            body.append(L"\r\nNote: Shared attributes are based on the first ");
            body.append(std::to_wstring(kMaxSharedMetadataItems));
            body.append(L" of ");
            body.append(std::to_wstring(selectedItems.size()));
            body.append(L" selected files.");
        }

        ApplyDetailsPanelText(std::to_wstring(selectedItems.size()) + L" Files Selected",
                              std::to_wstring(selectedItems.size()) + L" items | " + browser::FormatByteSize(selectedBytes),
                              std::move(body));
        ResetDetailsPanelHistogram();
        LayoutChildren();
    }

    DetailsPanelChromePainter::State MainWindow::BuildDetailsPanelChromePainterState() const
    {
        return DetailsPanelChromePainter::State{
            detailsPanelTabStripRect_,
            detailsPanelTabRects_,
            static_cast<int>(activeRightPaneTab_),
            detailsPanelHotTabIndex_,
            detailsPanelPressedTabIndex_,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::DetailsTab && GetFocus() == hwnd_
                ? keyboardFocusTarget_.index
                : -1,
            detailsPanelCloseButtonRect_,
            detailsPanelCloseButtonHot_,
            detailsPanelCloseButtonPressed_,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::DetailsCloseButton && GetFocus() == hwnd_};
    }

    DetailsPanelChromePainter::Palette MainWindow::BuildDetailsPanelChromePainterPalette(const ThemePalette& palette) const
    {
        return DetailsPanelChromePainter::Palette{
            palette.actionFieldBackground,
            palette.paneBackground,
            palette.actionStripBorder,
            palette.accent,
            palette.accentFill,
            palette.accentText,
            palette.text,
            palette.mutedText,
            themeMode_ == ThemeMode::Dark};
    }

    DetailsPanelHistogramPainter::State MainWindow::BuildDetailsPanelHistogramPainterState() const
    {
        return DetailsPanelHistogramPainter::State{
            detailsPanelHistogramRect_,
            detailsPanelHistogramRed_,
            detailsPanelHistogramGreen_,
            detailsPanelHistogramBlue_,
            detailsPanelHistogramPeak_,
            detailsPanelHistogramVisible_,
            detailsPanelHistogramLoading_};
    }

    DetailsPanelHistogramPainter::Palette MainWindow::BuildDetailsPanelHistogramPainterPalette(const ThemePalette& palette) const
    {
        return DetailsPanelHistogramPainter::Palette{
            BlendColor(palette.actionFieldBackground,
                       palette.paneBackground,
                       themeMode_ == ThemeMode::Dark ? 24 : 12),
            palette.actionStripBorder,
            palette.mutedText};
    }

    DetailsPanelTextPainter::Palette MainWindow::BuildDetailsPanelTextPainterPalette(const ThemePalette& palette) const
    {
        return DetailsPanelTextPainter::Palette{
            palette.text,
            palette.mutedText,
            palette.paneBackground};
    }

    CacheStatsPainter::VisualState MainWindow::BuildCacheStatsPainterVisualState() const
    {
        const auto ratio = [](std::uint64_t value, std::uint64_t capacity)
        {
            return capacity == 0
                ? 0.0f
                : static_cast<float>((std::min)(1.0, static_cast<double>(value) / static_cast<double>(capacity)));
        };
        const auto hitRate = [](std::uint64_t hits, std::uint64_t misses)
        {
            const std::uint64_t total = hits + misses;
            return total == 0
                ? 0.0f
                : static_cast<float>(static_cast<double>(hits) / static_cast<double>(total));
        };

        CacheStatsPainter::VisualState visual;
        visual.gauges = {
            CacheStatsPainter::Gauge{
                L"THUMBS",
                browser::FormatByteSize(detailsPanelPerformanceStats_.thumbnailBytes),
                ratio(detailsPanelPerformanceStats_.thumbnailBytes,
                      detailsPanelPerformanceStats_.thumbnailCapacityBytes)},
            CacheStatsPainter::Gauge{
                L"DISK",
                detailsPanelPerformanceStats_.persistentStatisticsAvailable
                    ? browser::FormatByteSize(detailsPanelPerformanceStats_.persistentBytes)
                    : std::wstring(L"Checking"),
                ratio(detailsPanelPerformanceStats_.persistentBytes,
                      detailsPanelPerformanceStats_.persistentCapacityBytes)},
            CacheStatsPainter::Gauge{
                L"META",
                std::to_wstring(detailsPanelPerformanceStats_.metadataEntries),
                ratio(detailsPanelPerformanceStats_.metadataEntries,
                      detailsPanelPerformanceStats_.metadataCapacityEntries)},
            CacheStatsPainter::Gauge{
                L"WORKERS",
                std::to_wstring(detailsPanelPerformanceStats_.thumbnailActiveWorkers)
                    + L" / "
                    + std::to_wstring(detailsPanelPerformanceStats_.thumbnailDecodeLimit),
                ratio(detailsPanelPerformanceStats_.thumbnailActiveWorkers,
                      detailsPanelPerformanceStats_.thumbnailDecodeLimit)},
            CacheStatsPainter::Gauge{
                L"QUEUE",
                std::to_wstring(detailsPanelPerformanceStats_.thumbnailPendingJobs),
                ratio(detailsPanelPerformanceStats_.thumbnailPendingJobs,
                      (std::max)(std::uint64_t{1}, detailsPanelPerformanceStats_.thumbnailDecodeLimit * 4))}};
        visual.hitRate = hitRate(detailsPanelPerformanceStats_.thumbnailHits,
                                 detailsPanelPerformanceStats_.thumbnailMisses);
        visual.pressureActive = thumbnailMemoryPressureActive_;
        return visual;
    }

    QuickAccessPainter::State MainWindow::BuildQuickAccessPainterState(
        const QuickAccessLayout::Metrics& metrics,
        std::vector<QuickAccessPainter::RowState>& rowStates) const
    {
        RECT headerRect{quickAccessDestinationPanelRect_.left,
                        quickAccessDestinationPanelRect_.top,
                        quickAccessDestinationPanelRect_.right,
                        quickAccessDestinationPanelRect_.top + metrics.headerHeight};
        if (!IsRectEmpty(&quickAccessSortButtonRect_))
        {
            headerRect.right = quickAccessSortButtonRect_.left - kQuickAccessPanelSortButtonGap;
        }

        rowStates.clear();
        rowStates.reserve(quickAccessDestinationRows_.size());
        for (const QuickAccessDestinationRow& row : quickAccessDestinationRows_)
        {
            rowStates.push_back(QuickAccessPainter::RowState{
                &row,
                CanNavigateToQuickAccessDestination(row.destinationPath),
                CanUseQuickAccessDestinationActions(row.destinationPath)});
        }

        return QuickAccessPainter::State{
            headerRect,
            quickAccessDestinationViewportRect_,
            quickAccessSortButtonRect_,
            rowStates,
            metrics,
            quickAccessSortButtonHot_,
            quickAccessSortButtonPressed_,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessSortButton && GetFocus() == hwnd_,
            quickAccessHotRowIndex_,
            quickAccessHotButtonIndex_,
            quickAccessPressedRowIndex_,
            quickAccessPressedButtonIndex_,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessRow && GetFocus() == hwnd_
                ? keyboardFocusTarget_.index
                : -1,
            keyboardFocusTarget_.kind == KeyboardFocusTargetKind::QuickAccessButton && GetFocus() == hwnd_
                ? keyboardFocusTarget_.index
                : -1};
    }

    QuickAccessPainter::Palette MainWindow::BuildQuickAccessPainterPalette(const ThemePalette& palette) const
    {
        return QuickAccessPainter::Palette{
            palette.actionFieldBackground,
            palette.paneBackground,
            palette.actionStripBorder,
            palette.accent,
            palette.accentFill,
            palette.accentText,
            palette.text,
            palette.mutedText,
            themeMode_ == ThemeMode::Dark};
    }

    bool MainWindow::PaintDetailsPanelD2D(HDC hdc, const RECT& clientRect) const
    {
        if (!hdc || !detailsStripVisible_ || IsRectEmpty(&detailsPanelRect_))
        {
            return false;
        }

        auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
        if (!renderer.IsAvailable())
        {
            return false;
        }

        const auto renderTarget = renderer.CreateDCRenderTarget();
        if (!renderTarget)
        {
            return false;
        }

        const auto titleFormat = renderer.CreateTextFormatFromFont(detailsPanelTitleFont_);
        const auto summaryFormat = renderer.CreateTextFormatFromFont(detailsPanelSummaryFont_);
        const auto bodyFormat = renderer.CreateTextFormatFromFont(detailsPanelBodyFont_);
        const auto cacheHeadingFormat = renderer.CreateTextFormatFromFont(
            detailsPanelBodyFont_,
            DWRITE_FONT_WEIGHT_SEMI_BOLD);
        const auto tabFormat = renderer.CreateTextFormatFromFont(detailsPanelSummaryFont_, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if (!titleFormat || !summaryFormat || !bodyFormat || !cacheHeadingFormat || !tabFormat)
        {
            return false;
        }

        titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        summaryFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        bodyFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        tabFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        tabFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        tabFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        const ThemePalette palette = GetThemePalette();
        const auto createBrush = [renderTarget](COLORREF color)
        {
            hyperbrowse::render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(
                hyperbrowse::render::ToD2DColor(color),
                brush.GetAddressOf());
            return brush;
        };
        const auto panelBrush = createBrush(palette.paneBackground);
        const auto borderBrush = createBrush(palette.actionStripBorder);
        const auto mutedBrush = createBrush(palette.mutedText);
        if (!panelBrush || !borderBrush || !mutedBrush)
        {
            return false;
        }

        const int clientWidth = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
        const int clientHeight = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
        HDC bufferedDc = CreateCompatibleDC(hdc);
        HBITMAP bufferedBitmap = bufferedDc
            ? CreateCompatibleBitmap(hdc, clientWidth, clientHeight)
            : nullptr;
        if (!bufferedDc || !bufferedBitmap)
        {
            if (bufferedBitmap)
            {
                DeleteObject(bufferedBitmap);
            }
            if (bufferedDc)
            {
                DeleteDC(bufferedDc);
            }
            return false;
        }

        const HGDIOBJ previousBitmap = SelectObject(bufferedDc, bufferedBitmap);
        if (FAILED(renderTarget->BindDC(bufferedDc, &clientRect)))
        {
            SelectObject(bufferedDc, previousBitmap);
            DeleteObject(bufferedBitmap);
            DeleteDC(bufferedDc);
            return false;
        }

        renderTarget->BeginDraw();
        DetailsPanelSurfacePainter::PaintD2D(renderTarget.Get(),
                                             detailsPanelRect_,
                                             panelBrush.Get(),
                                             borderBrush.Get());

        const DetailsPanelChromePainter::State chromeState = BuildDetailsPanelChromePainterState();
        const DetailsPanelChromePainter::Palette chromePalette = BuildDetailsPanelChromePainterPalette(palette);
        DetailsPanelChromePainter::PaintD2D(renderTarget.Get(), chromeState, chromePalette, tabFormat.Get());

        if (activeRightPaneTab_ == RightPaneTab::FileDetails && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const int innerWidth = std::max(0, static_cast<int>(detailsPanelContentRect_.right - detailsPanelContentRect_.left));
            const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
            const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                           title,
                                                           innerWidth,
                                                           DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                           22);
            const RECT titleRect{detailsPanelContentRect_.left,
                                 detailsPanelContentRect_.top,
                                 detailsPanelContentRect_.right,
                                 detailsPanelContentRect_.top + titleHeight};
            const int summaryTop = detailsPanelContentRect_.top + titleHeight + 6;
            const int summaryHeight = detailsPanelSummaryText_.empty()
                ? 0
                : MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                         detailsPanelSummaryText_,
                                         innerWidth,
                                         DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                         18);
            const DetailsPanelTextPainter::State textState{
                titleRect,
                RECT{detailsPanelContentRect_.left,
                     summaryTop,
                     detailsPanelContentRect_.right,
                     summaryTop + summaryHeight},
                RECT{},
                title,
                detailsPanelSummaryText_,
                {}};
            const DetailsPanelTextPainter::Palette textPalette = BuildDetailsPanelTextPainterPalette(palette);
            DetailsPanelTextPainter::PaintD2D(renderTarget.Get(),
                                              textState,
                                              textPalette,
                                              titleFormat.Get(),
                                              summaryFormat.Get());

            if ((detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_)
                && !IsRectEmpty(&detailsPanelHistogramRect_))
            {
                const DetailsPanelHistogramPainter::State histogramState = BuildDetailsPanelHistogramPainterState();
                const DetailsPanelHistogramPainter::Palette histogramPalette = BuildDetailsPanelHistogramPainterPalette(palette);
                DetailsPanelHistogramPainter::PaintD2D(renderTarget.Get(),
                                                        histogramState,
                                                        histogramPalette,
                                                        borderBrush.Get(),
                                                        mutedBrush.Get(),
                                                        summaryFormat.Get());
            }
        }

        if (activeRightPaneTab_ == RightPaneTab::QuickSend && !quickAccessDestinationRows_.empty()
            && !IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
        const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(
            detailsPanelSummaryFont_,
            detailsPanelBodyFont_,
            MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u));
            std::vector<QuickAccessPainter::RowState> rowStates;
            const QuickAccessPainter::State quickAccessState = BuildQuickAccessPainterState(metrics, rowStates);
            const QuickAccessPainter::Palette quickAccessPalette = BuildQuickAccessPainterPalette(palette);
            QuickAccessPainter::PaintD2D(renderTarget.Get(),
                                         bufferedDc,
                                         quickAccessState,
                                         quickAccessPalette,
                                         summaryFormat.Get(),
                                         bodyFormat.Get());
        }
        else if (activeRightPaneTab_ == RightPaneTab::QuickSend && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const DetailsPanelTextPainter::State textState{
                RECT{},
                RECT{},
                detailsPanelContentRect_,
                {},
                {},
                L"Quick action destinations will appear here."};
            const DetailsPanelTextPainter::Palette textPalette = BuildDetailsPanelTextPainterPalette(palette);
            DetailsPanelTextPainter::PaintD2D(renderTarget.Get(),
                                              textState,
                                              textPalette,
                                              titleFormat.Get(),
                                              summaryFormat.Get());
        }
        else if (activeRightPaneTab_ == RightPaneTab::CacheStats && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const int contentWidth = std::max(0, static_cast<int>(detailsPanelContentRect_.right - detailsPanelContentRect_.left));
            const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                           L"Cache Performance",
                                                           contentWidth,
                                                           DT_LEFT | DT_NOPREFIX | DT_SINGLELINE,
                                                           28);
            const RECT titleRect{detailsPanelContentRect_.left,
                                 detailsPanelContentRect_.top,
                                 detailsPanelContentRect_.right,
                                 detailsPanelContentRect_.top + titleHeight};
            const RECT bodyRect{detailsPanelContentRect_.left,
                                titleRect.bottom + 12,
                                detailsPanelContentRect_.right,
                                detailsPanelContentRect_.bottom};
            RECT textRect = bodyRect;
            RECT visualRect{};
            if (contentWidth >= kCacheStatsVisualMinContentWidth)
            {
                visualRect = RECT{bodyRect.right - kCacheStatsVisualWidth,
                                  bodyRect.top,
                                  bodyRect.right,
                                  (std::min)(static_cast<int>(bodyRect.bottom),
                                             static_cast<int>(bodyRect.top) + kCacheStatsVisualHeight)};
                textRect.right = visualRect.left - kCacheStatsVisualGap;
            }
            const DetailsPanelTextPainter::State textState{
                titleRect,
                RECT{},
                RECT{},
                L"Cache Performance",
                {},
                {}};
            const DetailsPanelTextPainter::Palette textPalette = BuildDetailsPanelTextPainterPalette(palette);
            DetailsPanelTextPainter::PaintD2D(renderTarget.Get(),
                                              textState,
                                              textPalette,
                                              titleFormat.Get(),
                                              summaryFormat.Get());
            const CacheStatsPainter::Palette cachePalette{
                palette.mutedText,
                palette.accent,
                palette.paneBackground,
                BlendColor(palette.actionFieldBackground, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 42 : 18),
                BlendColor(palette.actionStripBorder, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 96 : 150),
                palette.accent,
                GetCacheStatsHighlightColor(themeMode_ == ThemeMode::Dark)};
            CacheStatsPainter::VisualState visualState = BuildCacheStatsPainterVisualState();
            visualState.rect = visualRect;
            CacheStatsPainter::PaintD2D(renderTarget.Get(),
                                        CacheStatsPainter::State{textRect, detailsPanelPerformanceText_, 22, 28, visualState},
                                        cachePalette,
                                        bodyFormat.Get(),
                                        cacheHeadingFormat.Get());
        }

        const HRESULT drawResult = renderTarget->EndDraw();
        if (SUCCEEDED(drawResult))
        {
            const int panelWidth = std::max(0, static_cast<int>(detailsPanelRect_.right - detailsPanelRect_.left));
            const int panelHeight = std::max(0, static_cast<int>(detailsPanelRect_.bottom - detailsPanelRect_.top));
            if (panelWidth > 0 && panelHeight > 0)
            {
                BitBlt(hdc,
                       detailsPanelRect_.left,
                       detailsPanelRect_.top,
                       panelWidth,
                       panelHeight,
                       bufferedDc,
                       detailsPanelRect_.left,
                       detailsPanelRect_.top,
                       SRCCOPY);
            }
        }

        SelectObject(bufferedDc, previousBitmap);
        DeleteObject(bufferedBitmap);
        DeleteDC(bufferedDc);
        return SUCCEEDED(drawResult);
    }

    void MainWindow::PaintDetailsPanel(HDC hdc, const RECT& clientRect) const
    {
        if (PaintDetailsPanelD2D(hdc, clientRect))
        {
            return;
        }

        (void)clientRect;
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelRect_))
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        DetailsPanelSurfacePainter::PaintGdi(hdc,
                             detailsPanelRect_,
                             detailsPanelBrush_,
                             backgroundBrush_,
                             palette.actionStripBorder);

        const DetailsPanelChromePainter::State chromeState = BuildDetailsPanelChromePainterState();
        const DetailsPanelChromePainter::Palette chromePalette = BuildDetailsPanelChromePainterPalette(palette);
        DetailsPanelChromePainter::PaintGdi(hdc,
                                            chromeState,
                                            chromePalette,
                                            detailsPanelSummaryFont_);

        SelectObject(hdc, detailsPanelTitleFont_ ? detailsPanelTitleFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        if (activeRightPaneTab_ == RightPaneTab::FileDetails && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const int innerLeft = detailsPanelContentRect_.left;
            const int innerRight = detailsPanelContentRect_.right;
            const int innerWidth = std::max(0, innerRight - innerLeft);

            const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
            const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                           title,
                                                           innerWidth,
                                                           DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                           22);
            RECT titleRect{innerLeft, detailsPanelContentRect_.top, innerRight, detailsPanelContentRect_.top + titleHeight};
            const int summaryTop = titleRect.bottom + 6;
            const int summaryHeight = detailsPanelSummaryText_.empty()
                ? 0
                : MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                         detailsPanelSummaryText_,
                                         innerWidth,
                                         DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                         18);
            const DetailsPanelTextPainter::State textState{
                titleRect,
                RECT{innerLeft, summaryTop, innerRight, summaryTop + summaryHeight},
                RECT{},
                title,
                detailsPanelSummaryText_,
                {}};
            const DetailsPanelTextPainter::Palette textPalette = BuildDetailsPanelTextPainterPalette(palette);
            DetailsPanelTextPainter::PaintGdi(hdc,
                                              textState,
                                              textPalette,
                                              detailsPanelTitleFont_,
                                              detailsPanelSummaryFont_);

            if ((detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_) && !IsRectEmpty(&detailsPanelHistogramRect_))
            {
                const DetailsPanelHistogramPainter::State histogramState = BuildDetailsPanelHistogramPainterState();
                const DetailsPanelHistogramPainter::Palette histogramPalette = BuildDetailsPanelHistogramPainterPalette(palette);
                DetailsPanelHistogramPainter::PaintGdi(
                    hdc,
                    histogramState,
                    histogramPalette,
                    detailsPanelSummaryFont_);
            }
        }

        if (activeRightPaneTab_ == RightPaneTab::QuickSend && !quickAccessDestinationRows_.empty() && !IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(
                detailsPanelSummaryFont_,
                detailsPanelBodyFont_,
                MakeMenuMetrics(appTextSize_, hwnd_ ? (std::max)(96u, GetDpiForWindow(hwnd_)) : 96u));
            std::vector<QuickAccessPainter::RowState> rowStates;
            const QuickAccessPainter::State quickAccessState = BuildQuickAccessPainterState(metrics, rowStates);
            const QuickAccessPainter::Palette quickAccessPalette = BuildQuickAccessPainterPalette(palette);
            QuickAccessPainter::PaintGdi(hdc,
                                         quickAccessState,
                                         quickAccessPalette,
                                         detailsPanelSummaryFont_,
                                         detailsPanelBodyFont_,
                                         toolbarIconLibrary_.get());
        }
        else if (activeRightPaneTab_ == RightPaneTab::QuickSend && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const DetailsPanelTextPainter::State textState{
                RECT{},
                RECT{},
                detailsPanelContentRect_,
                {},
                {},
                L"Quick action destinations will appear here."};
            const DetailsPanelTextPainter::Palette textPalette = BuildDetailsPanelTextPainterPalette(palette);
            DetailsPanelTextPainter::PaintGdi(hdc,
                                              textState,
                                              textPalette,
                                              detailsPanelTitleFont_,
                                              detailsPanelSummaryFont_);
        }
        else if (activeRightPaneTab_ == RightPaneTab::CacheStats && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const int contentWidth = std::max(0, static_cast<int>(detailsPanelContentRect_.right - detailsPanelContentRect_.left));
            const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                           L"Cache Performance",
                                                           contentWidth,
                                                           DT_LEFT | DT_NOPREFIX | DT_SINGLELINE,
                                                           28);
            const RECT titleRect{detailsPanelContentRect_.left,
                                 detailsPanelContentRect_.top,
                                 detailsPanelContentRect_.right,
                                 detailsPanelContentRect_.top + titleHeight};
            const RECT bodyRect{detailsPanelContentRect_.left,
                                titleRect.bottom + 12,
                                detailsPanelContentRect_.right,
                                detailsPanelContentRect_.bottom};
            RECT textRect = bodyRect;
            RECT visualRect{};
            if (contentWidth >= kCacheStatsVisualMinContentWidth)
            {
                visualRect = RECT{bodyRect.right - kCacheStatsVisualWidth,
                                  bodyRect.top,
                                  bodyRect.right,
                                  (std::min)(static_cast<int>(bodyRect.bottom),
                                             static_cast<int>(bodyRect.top) + kCacheStatsVisualHeight)};
                textRect.right = visualRect.left - kCacheStatsVisualGap;
            }
            const DetailsPanelTextPainter::State textState{
                titleRect,
                RECT{},
                RECT{},
                L"Cache Performance",
                {},
                {}};
            const DetailsPanelTextPainter::Palette textPalette = BuildDetailsPanelTextPainterPalette(palette);
            DetailsPanelTextPainter::PaintGdi(hdc,
                                              textState,
                                              textPalette,
                                              detailsPanelTitleFont_,
                                              detailsPanelSummaryFont_);
            const CacheStatsPainter::Palette cachePalette{
                palette.mutedText,
                palette.accent,
                palette.paneBackground,
                BlendColor(palette.actionFieldBackground, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 42 : 18),
                BlendColor(palette.actionStripBorder, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 96 : 150),
                palette.accent,
                GetCacheStatsHighlightColor(themeMode_ == ThemeMode::Dark)};
            CacheStatsPainter::VisualState visualState = BuildCacheStatsPainterVisualState();
            visualState.rect = visualRect;
            CacheStatsPainter::PaintGdi(hdc,
                                        CacheStatsPainter::State{textRect, detailsPanelPerformanceText_, 22, 28, visualState},
                                        cachePalette,
                                        detailsPanelBodyFont_,
                                        detailsPanelTitleFont_);
        }
    }
}
