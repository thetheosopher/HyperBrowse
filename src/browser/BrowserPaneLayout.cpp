#include "browser/BrowserPaneInternal.h"

namespace
{
    constexpr float kThumbnailPreviewAspectRatio = 0.72f;

    bool IsJpegFileType(std::wstring_view fileType)
    {
        const std::wstring normalized = ToLowercase(std::wstring(fileType));
        return normalized == L"jpg"
            || normalized == L"jpeg"
            || normalized == L".jpg"
            || normalized == L".jpeg";
    }

    enum class RawJpegPairKind : int
    {
        None = 0,
        Raw = 1,
        Jpeg = 2,
    };

    bool TryBuildRawJpegPairKey(const hyperbrowse::browser::BrowserItem& item,
                                std::wstring* key,
                                RawJpegPairKind* kind)
    {
        const bool isRaw = hyperbrowse::decode::IsRawFileType(item.fileType);
        const bool isJpeg = IsJpegFileType(item.fileType);
        if (!isRaw && !isJpeg)
        {
            if (key)
            {
                key->clear();
            }
            if (kind)
            {
                *kind = RawJpegPairKind::None;
            }
            return false;
        }

        const fs::path itemPath(item.filePath);
        if (key)
        {
            *key = ToLowercase(itemPath.parent_path().wstring());
            key->push_back(L'|');
            key->append(ToLowercase(itemPath.stem().wstring()));
        }
        if (kind)
        {
            *kind = isRaw ? RawJpegPairKind::Raw : RawJpegPairKind::Jpeg;
        }
        return true;
    }

    struct StructuredFilterQuery
    {
        std::vector<std::wstring> fileNameTerms;
        std::vector<std::wstring> tagTerms;
        std::vector<std::wstring> fileTypeTerms;
        bool ratingFilterEnabled{};
        int minimumRating{};
        int maximumRating{5};
        bool ratedOnly{};
        bool unratedOnly{};
    };

    std::vector<std::wstring> SplitWhitespaceTerms(std::wstring_view query)
    {
        std::vector<std::wstring> terms;
        std::wstring current;
        for (const wchar_t character : query)
        {
            if (iswspace(character) != 0)
            {
                if (!current.empty())
                {
                    terms.push_back(std::move(current));
                    current.clear();
                }
                continue;
            }

            current.push_back(character);
        }

        if (!current.empty())
        {
            terms.push_back(std::move(current));
        }
        return terms;
    }

    bool HasStructuredFilterTokens(std::wstring_view queryLower)
    {
        return queryLower.find(L"rating:") != std::wstring_view::npos
            || queryLower.find(L"tag:") != std::wstring_view::npos
            || queryLower.find(L"tags:") != std::wstring_view::npos
            || queryLower.find(L"type:") != std::wstring_view::npos;
    }

    bool TryApplyRatingConstraint(std::wstring_view rawValue, StructuredFilterQuery* filter)
    {
        if (!filter)
        {
            return false;
        }

        std::wstring value = TrimWhitespace(std::wstring(rawValue));
        if (value.empty())
        {
            return false;
        }

        if (value == L"rated")
        {
            filter->ratedOnly = true;
            return true;
        }
        if (value == L"unrated")
        {
            filter->unratedOnly = true;
            return true;
        }

        wchar_t comparator = L'=';
        bool inclusive = true;
        std::size_t numberOffset = 0;
        if (value.rfind(L">=", 0) == 0 || value.rfind(L"<=", 0) == 0)
        {
            comparator = value[0];
            inclusive = true;
            numberOffset = 2;
        }
        else if (value[0] == L'>' || value[0] == L'<' || value[0] == L'=')
        {
            comparator = value[0];
            inclusive = comparator == L'=';
            numberOffset = 1;
        }

        const wchar_t* numberText = value.c_str() + numberOffset;
        wchar_t* numberEnd = nullptr;
        const long parsedRating = wcstol(numberText, &numberEnd, 10);
        if (numberEnd == numberText || *numberEnd != L'\0')
        {
            return false;
        }

        const int clampedRating = std::clamp(static_cast<int>(parsedRating), 0, 5);
        filter->ratingFilterEnabled = true;
        switch (comparator)
        {
        case L'>':
            filter->minimumRating = std::max(filter->minimumRating, clampedRating + (inclusive ? 0 : 1));
            break;
        case L'<':
            filter->maximumRating = std::min(filter->maximumRating, clampedRating - (inclusive ? 0 : 1));
            break;
        case L'=':
        default:
            filter->minimumRating = std::max(filter->minimumRating, clampedRating);
            filter->maximumRating = std::min(filter->maximumRating, clampedRating);
            break;
        }

        if (clampedRating == 0 && comparator == L'=')
        {
            filter->unratedOnly = true;
        }
        return true;
    }

    StructuredFilterQuery ParseStructuredFilterQuery(std::wstring_view queryLower)
    {
        StructuredFilterQuery filter;
        for (const std::wstring& term : SplitWhitespaceTerms(queryLower))
        {
            if (term.rfind(L"rating:", 0) == 0)
            {
                if (!TryApplyRatingConstraint(term.substr(7), &filter))
                {
                    filter.fileNameTerms.push_back(term);
                }
                continue;
            }

            if (term.rfind(L"tag:", 0) == 0 || term.rfind(L"tags:", 0) == 0)
            {
                const std::size_t valueOffset = term.rfind(L':') + 1;
                std::wstring value = TrimWhitespace(term.substr(valueOffset));
                if (!value.empty())
                {
                    filter.tagTerms.push_back(std::move(value));
                }
                continue;
            }

            if (term.rfind(L"type:", 0) == 0)
            {
                std::wstring value = term.substr(5);
                if (!value.empty() && value.front() == L'.') value.erase(0, 1);
                if (value == L"raw" || hyperbrowse::browser::IsSupportedImageExtension(L"." + value))
                {
                    filter.fileTypeTerms.push_back(std::move(value));
                }
                else
                {
                    filter.fileNameTerms.push_back(term);
                }
                continue;
            }

            filter.fileNameTerms.push_back(term);
        }

        return filter;
    }

    bool MatchesStructuredFilter(const hyperbrowse::browser::BrowserItem& item,
                                 const hyperbrowse::services::UserMetadataEntry& userMetadata,
                                 const StructuredFilterQuery& filter)
    {
        const std::wstring fileNameLower = ToLowercase(item.fileName);
        for (const std::wstring& term : filter.fileNameTerms)
        {
            if (fileNameLower.find(term) == std::wstring::npos)
            {
                return false;
            }
        }

        if (!filter.fileTypeTerms.empty())
        {
            std::wstring fileType = ToLowercase(item.fileType);
            if (!fileType.empty() && fileType.front() == L'.') fileType.erase(0, 1);
            for (const std::wstring& term : filter.fileTypeTerms)
            {
                if (term == L"raw" ? !hyperbrowse::decode::IsRawFileType(fileType) : fileType != term)
                {
                    return false;
                }
            }
        }

        const int rating = std::clamp(userMetadata.rating, 0, 5);
        if (filter.ratedOnly && rating <= 0)
        {
            return false;
        }
        if (filter.unratedOnly && rating > 0)
        {
            return false;
        }
        if (filter.ratingFilterEnabled && (rating < filter.minimumRating || rating > filter.maximumRating))
        {
            return false;
        }

        if (!filter.tagTerms.empty())
        {
            const std::wstring tagsLower = ToLowercase(userMetadata.tags);
            for (const std::wstring& term : filter.tagTerms)
            {
                if (tagsLower.find(term) == std::wstring::npos)
                {
                    return false;
                }
            }
        }

        return true;
    }

    int CompareCaseInsensitive(const std::wstring& lhs, const std::wstring& rhs)
    {
        return _wcsicmp(lhs.c_str(), rhs.c_str());
    }
}

namespace hyperbrowse::browser
{

    bool BrowserPane::CreateDetailsListView()
    {
        detailsList_ = CreateWindowExW(
            0,
            WC_LISTVIEWW,
            nullptr,
            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
            0,
            0,
            100,
            100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!detailsList_)
        {
            return false;
        }

        ListView_SetUnicodeFormat(detailsList_, TRUE);
        ListView_SetExtendedListViewStyle(detailsList_, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
        if (shellImageList_)
        {
            ListView_SetImageList(detailsList_, shellImageList_, LVSIL_SMALL);
        }

        RebuildDetailsListFont();

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        std::wstring name = L"Name";
        column.pszText = name.data();
        column.cx = 280;
        ListView_InsertColumn(detailsList_, 0, &column);

        std::wstring type = L"Type";
        column.pszText = type.data();
        column.cx = 90;
        column.iSubItem = 1;
        ListView_InsertColumn(detailsList_, 1, &column);

        std::wstring size = L"Size";
        column.pszText = size.data();
        column.cx = 110;
        column.iSubItem = 2;
        ListView_InsertColumn(detailsList_, 2, &column);

        std::wstring modified = L"Modified";
        column.pszText = modified.data();
        column.cx = 150;
        column.iSubItem = 3;
        ListView_InsertColumn(detailsList_, 3, &column);

        std::wstring dimensions = L"Dimensions";
        column.pszText = dimensions.data();
        column.cx = 110;
        column.iSubItem = 4;
        ListView_InsertColumn(detailsList_, 4, &column);

        std::wstring dateTaken = L"Date Taken";
        column.pszText = dateTaken.data();
        column.cx = 160;
        column.iSubItem = 5;
        ListView_InsertColumn(detailsList_, 5, &column);

        std::wstring camera = L"Camera";
        column.pszText = camera.data();
        column.cx = 180;
        column.iSubItem = 6;
        ListView_InsertColumn(detailsList_, 6, &column);

        std::wstring title = L"Title";
        column.pszText = title.data();
        column.cx = 220;
        column.iSubItem = 7;
        ListView_InsertColumn(detailsList_, 7, &column);

        ApplyThemeToDetailsList();
        return true;
    }

    void BrowserPane::CreateThumbnailTooltip()
    {
        if (thumbnailTooltip_ || !hwnd_)
        {
            return;
        }

        thumbnailTooltip_ = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            hwnd_,
            nullptr,
            instance_,
            nullptr);
        if (!thumbnailTooltip_)
        {
            return;
        }

        SetWindowPos(thumbnailTooltip_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(thumbnailTooltip_, TTM_SETMAXTIPWIDTH, 0, 420);

        TOOLINFOW toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.uFlags = TTF_SUBCLASS;
        toolInfo.hwnd = hwnd_;
        toolInfo.uId = 1;
        SetRectEmpty(&toolInfo.rect);
        toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(thumbnailTooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
    }

    BrowserPane::ThumbnailLayoutMetrics BrowserPane::CurrentThumbnailLayout() const
    {
        const int previewWidth = ThumbnailSizePresetToPixels(thumbnailSizePreset_);
        const int previewHeight = std::max(72, static_cast<int>(std::lround(static_cast<double>(previewWidth) * kThumbnailPreviewAspectRatio)));
        const bool compact = compactThumbnailLayout_;
        const bool showDetails = thumbnailDetailsVisible_;

        ThumbnailLayoutMetrics layout;
        layout.textDpi = BrowserTextDpi(hwnd_);
        const auto scaleTextDimension = [this, &layout](int value)
        {
            return MulDiv(
                hyperbrowse::util::ScaleAppTextDimension(value, appTextSize_),
                layout.textDpi,
                96);
        };
        const int referenceLineHeight = MulDiv(
            hyperbrowse::util::AppTextTreeRowHeight(appTextSize_),
            layout.textDpi,
            96);
        layout.cellPadding = compact ? std::max(6, previewWidth / 20) : std::max(10, previewWidth / 14);
        layout.previewInset = compact ? std::max(4, previewWidth / 32) : std::max(8, previewWidth / 20);
        layout.previewWidth = previewWidth;
        layout.previewHeight = previewHeight;
        layout.textInset = compact ? std::max(8, previewWidth / 22) : std::max(12, previewWidth / 16);
        layout.titleTopGap = showDetails
            ? scaleTextDimension(2)
            : 0;
        layout.titleHeight = showDetails
            ? referenceLineHeight
            : 0;
        layout.metaTopGap = 0;
        layout.metaHeight = 0;
        layout.infoBottomInset = showDetails
            ? scaleTextDimension(2)
            : 0;
        layout.infoHeight = showDetails
            ? referenceLineHeight
            : 0;
        layout.badgeHorizontalPadding = compact ? 6 : 8;
        layout.badgeGap = compact ? 6 : 10;
        layout.badgeCornerRadius = compact ? 6 : 8;
        layout.cellCornerRadius = compact ? 12 : 16;
        layout.previewCornerRadius = compact ? 10 : 12;
        layout.loadingIconSize = std::clamp(previewWidth / 5, 18, 40);
        layout.titlePointSize = hyperbrowse::util::ScaleAppTextDimension(
            std::clamp(previewWidth / 24, 8, 9),
            appTextSize_);
        layout.metaPointSize = hyperbrowse::util::ScaleAppTextDimension(
            std::clamp(previewWidth / 28, 7, 8),
            appTextSize_);
        layout.statusPointSize = hyperbrowse::util::ScaleAppTextDimension(
            std::clamp(previewWidth / 28, 7, 8),
            appTextSize_);
        const int horizontalInset = std::max(layout.previewInset, layout.textInset);
        layout.itemWidth = layout.previewWidth + (horizontalInset * 2);
        layout.itemHeight = layout.previewHeight + (layout.previewInset * (showDetails ? 1 : 2));
        if (showDetails)
        {
            layout.itemHeight += layout.titleTopGap
                + layout.titleHeight
                + layout.metaTopGap
                + layout.metaHeight
                + layout.infoBottomInset
                + layout.infoHeight;
        }

        return layout;
    }

    void BrowserPane::LayoutChildren()
    {
        if (!hwnd_)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);

        const bool showDetailsList = viewMode_ == BrowserViewMode::Details && !orderedModelIndices_.empty();
        if (detailsList_)
        {
            ShowWindow(detailsList_, showDetailsList ? SW_SHOW : SW_HIDE);
            if (showDetailsList)
            {
                MoveWindow(detailsList_, 0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
                SyncDetailsListSelectionFromModel();
            }
        }

        UpdateVerticalScrollBar();
        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            ScheduleVisibleThumbnailWork();
        }
    }

    void BrowserPane::RebuildOrder()
    {
        orderedModelIndices_.clear();
        pairedCompanionByRepresentativeModelIndex_.clear();
        pairedRepresentativeByHiddenModelIndex_.clear();
        if (!model_)
        {
            return;
        }

        const auto& items = model_->Items();
        const bool useStructuredFilter = HasStructuredFilterTokens(filterQueryLower_);
        const StructuredFilterQuery structuredFilter = useStructuredFilter
            ? ParseStructuredFilterQuery(filterQueryLower_)
            : StructuredFilterQuery{};
        std::vector<int> filteredModelIndices;
        filteredModelIndices.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (!filterQueryLower_.empty())
            {
                const BrowserItem& item = items[index];
                if (!useStructuredFilter)
                {
                    if (ToLowercase(item.fileName).find(filterQueryLower_) == std::wstring::npos)
                    {
                        continue;
                    }
                }
                else
                {
                    const services::UserMetadataEntry userMetadata = userMetadataStore_
                        ? userMetadataStore_->EntryForPath(item.filePath)
                        : services::UserMetadataEntry{};
                    if (!MatchesStructuredFilter(item, userMetadata, structuredFilter))
                    {
                        continue;
                    }
                }
            }

            filteredModelIndices.push_back(static_cast<int>(index));
        }

        if (rawJpegStackingEnabled_)
        {
            struct PairGroup
            {
                int rawIndex{-1};
                int jpegIndex{-1};
                int rawCount{};
                int jpegCount{};
            };

            std::unordered_map<std::wstring, PairGroup> pairGroups;
            pairGroups.reserve(filteredModelIndices.size());
            for (const int modelIndex : filteredModelIndices)
            {
                std::wstring pairKey;
                RawJpegPairKind pairKind = RawJpegPairKind::None;
                if (!TryBuildRawJpegPairKey(items[static_cast<std::size_t>(modelIndex)], &pairKey, &pairKind))
                {
                    continue;
                }

                PairGroup& group = pairGroups[pairKey];
                if (pairKind == RawJpegPairKind::Raw)
                {
                    group.rawIndex = modelIndex;
                    ++group.rawCount;
                }
                else if (pairKind == RawJpegPairKind::Jpeg)
                {
                    group.jpegIndex = modelIndex;
                    ++group.jpegCount;
                }
            }

            std::unordered_set<std::wstring> emittedPairKeys;
            emittedPairKeys.reserve(pairGroups.size());
            orderedModelIndices_.reserve(filteredModelIndices.size());
            for (const int modelIndex : filteredModelIndices)
            {
                std::wstring pairKey;
                RawJpegPairKind pairKind = RawJpegPairKind::None;
                if (!TryBuildRawJpegPairKey(items[static_cast<std::size_t>(modelIndex)], &pairKey, &pairKind))
                {
                    orderedModelIndices_.push_back(modelIndex);
                    continue;
                }

                const auto groupIterator = pairGroups.find(pairKey);
                if (groupIterator == pairGroups.end())
                {
                    orderedModelIndices_.push_back(modelIndex);
                    continue;
                }

                const PairGroup& group = groupIterator->second;
                const bool isExactPair = group.rawCount == 1
                    && group.jpegCount == 1
                    && group.rawIndex >= 0
                    && group.jpegIndex >= 0;
                if (!isExactPair)
                {
                    orderedModelIndices_.push_back(modelIndex);
                    continue;
                }

                if (emittedPairKeys.contains(pairKey))
                {
                    continue;
                }

                emittedPairKeys.insert(pairKey);
                const bool preferRaw = rawJpegDisplayPreference_ == RawJpegDisplayPreference::Raw;
                const int representativeModelIndex = preferRaw ? group.rawIndex : group.jpegIndex;
                const int hiddenCompanionModelIndex = preferRaw ? group.jpegIndex : group.rawIndex;
                orderedModelIndices_.push_back(representativeModelIndex);
                pairedCompanionByRepresentativeModelIndex_.emplace(representativeModelIndex, hiddenCompanionModelIndex);
                pairedRepresentativeByHiddenModelIndex_.emplace(hiddenCompanionModelIndex, representativeModelIndex);
            }
        }
        else
        {
            orderedModelIndices_ = std::move(filteredModelIndices);
        }

        const auto comparator = [this, &items](int lhsIndex, int rhsIndex)
        {
            const BrowserItem& lhs = items[static_cast<std::size_t>(lhsIndex)];
            const BrowserItem& rhs = items[static_cast<std::size_t>(rhsIndex)];
            const services::UserMetadataEntry lhsUserMetadata = userMetadataStore_
                ? userMetadataStore_->EntryForPath(lhs.filePath)
                : services::UserMetadataEntry{};
            const services::UserMetadataEntry rhsUserMetadata = userMetadataStore_
                ? userMetadataStore_->EntryForPath(rhs.filePath)
                : services::UserMetadataEntry{};

            auto tieBreakByName = [&]() -> bool
            {
                const int fileNameCompare = CompareCaseInsensitive(lhs.fileName, rhs.fileName);
                if (fileNameCompare != 0)
                {
                    return fileNameCompare < 0;
                }

                return lhs.filePath < rhs.filePath;
            };

            auto compareAscending = [&]() -> bool
            {
                switch (sortMode_)
                {
                case BrowserSortMode::FileName:
                    return tieBreakByName();
                case BrowserSortMode::ModifiedDate:
                    if (lhs.modifiedTimestampUtc != rhs.modifiedTimestampUtc)
                    {
                        return lhs.modifiedTimestampUtc < rhs.modifiedTimestampUtc;
                    }
                    return tieBreakByName();
                case BrowserSortMode::FileSize:
                    if (lhs.fileSizeBytes != rhs.fileSizeBytes)
                    {
                        return lhs.fileSizeBytes < rhs.fileSizeBytes;
                    }
                    return tieBreakByName();
                case BrowserSortMode::Dimensions:
                {
                    const int lhsWidth = EffectiveImageWidth(lhs);
                    const int lhsHeight = EffectiveImageHeight(lhs);
                    const int rhsWidth = EffectiveImageWidth(rhs);
                    const int rhsHeight = EffectiveImageHeight(rhs);
                    const auto lhsArea = static_cast<long long>(lhsWidth) * static_cast<long long>(lhsHeight);
                    const auto rhsArea = static_cast<long long>(rhsWidth) * static_cast<long long>(rhsHeight);
                    if (lhsArea != rhsArea)
                    {
                        return lhsArea < rhsArea;
                    }
                    if (lhsWidth != rhsWidth)
                    {
                        return lhsWidth < rhsWidth;
                    }
                    if (lhsHeight != rhsHeight)
                    {
                        return lhsHeight < rhsHeight;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::FileType:
                {
                    const int typeCompare = CompareCaseInsensitive(lhs.fileType, rhs.fileType);
                    if (typeCompare != 0)
                    {
                        return typeCompare < 0;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::DateTaken:
                {
                    const bool lhsHas = lhs.dateTakenTimestampUtc != 0;
                    const bool rhsHas = rhs.dateTakenTimestampUtc != 0;
                    if (lhsHas != rhsHas)
                    {
                        return lhsHas;
                    }
                    if (lhsHas && lhs.dateTakenTimestampUtc != rhs.dateTakenTimestampUtc)
                    {
                        return lhs.dateTakenTimestampUtc < rhs.dateTakenTimestampUtc;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::Rating:
                {
                    const int lhsRating = std::clamp(lhsUserMetadata.rating, 0, 5);
                    const int rhsRating = std::clamp(rhsUserMetadata.rating, 0, 5);
                    const bool lhsHas = lhsRating > 0;
                    const bool rhsHas = rhsRating > 0;
                    if (lhsHas != rhsHas)
                    {
                        return lhsHas;
                    }
                    if (lhsRating != rhsRating)
                    {
                        return sortAscending_ ? lhsRating < rhsRating : lhsRating > rhsRating;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::Tags:
                {
                    const bool lhsHas = !lhsUserMetadata.tags.empty();
                    const bool rhsHas = !rhsUserMetadata.tags.empty();
                    if (lhsHas != rhsHas)
                    {
                        return lhsHas;
                    }

                    const int tagCompare = CompareCaseInsensitive(lhsUserMetadata.tags, rhsUserMetadata.tags);
                    if (tagCompare != 0)
                    {
                        return sortAscending_ ? tagCompare < 0 : tagCompare > 0;
                    }
                    return tieBreakByName();
                }
                case BrowserSortMode::Random:
                {
                    const auto lhsHash = std::hash<std::wstring>{}(lhs.filePath);
                    const auto rhsHash = std::hash<std::wstring>{}(rhs.filePath);
                    if (lhsHash != rhsHash)
                    {
                        return lhsHash < rhsHash;
                    }
                    return tieBreakByName();
                }
                default:
                    return tieBreakByName();
                }
            };

            if (lhs.isDirectory != rhs.isDirectory)
            {
                return lhs.isDirectory;
            }

            const bool ascending = compareAscending();
            if (!sortAscending_
                && sortMode_ != BrowserSortMode::Random
                && sortMode_ != BrowserSortMode::Rating
                && sortMode_ != BrowserSortMode::Tags)
            {
                return !ascending && !(lhs.filePath == rhs.filePath);
            }
            return ascending;
        };

        std::sort(orderedModelIndices_.begin(), orderedModelIndices_.end(), comparator);
        PruneSelectionToVisibleItems();
    }

    void BrowserPane::PruneSelectionToVisibleItems()
    {
        if (selectedModelIndices_.empty() && focusedModelIndex_ < 0 && anchorModelIndex_ < 0)
        {
            return;
        }

        std::unordered_set<int> visibleModelIndices;
        visibleModelIndices.reserve(orderedModelIndices_.size());
        for (const int modelIndex : orderedModelIndices_)
        {
            visibleModelIndices.insert(modelIndex);
        }

        std::unordered_set<int> remappedSelection;
        remappedSelection.reserve(selectedModelIndices_.size());
        for (const int selectedModelIndex : selectedModelIndices_)
        {
            const int visibleModelIndex = VisibleRepresentativeForModelIndex(selectedModelIndex);
            if (visibleModelIndices.contains(visibleModelIndex))
            {
                remappedSelection.insert(visibleModelIndex);
            }
        }
        selectedModelIndices_ = std::move(remappedSelection);

        if (focusedModelIndex_ >= 0)
        {
            focusedModelIndex_ = VisibleRepresentativeForModelIndex(focusedModelIndex_);
            if (!visibleModelIndices.contains(focusedModelIndex_))
            {
                focusedModelIndex_ = selectedModelIndices_.empty() ? -1 : *selectedModelIndices_.begin();
            }
        }

        if (anchorModelIndex_ >= 0)
        {
            anchorModelIndex_ = VisibleRepresentativeForModelIndex(anchorModelIndex_);
            if (!visibleModelIndices.contains(anchorModelIndex_))
            {
                anchorModelIndex_ = focusedModelIndex_;
            }
        }
    }

    int BrowserPane::VisibleRepresentativeForModelIndex(int modelIndex) const noexcept
    {
        const auto iterator = pairedRepresentativeByHiddenModelIndex_.find(modelIndex);
        return iterator != pairedRepresentativeByHiddenModelIndex_.end()
            ? iterator->second
            : modelIndex;
    }

    int BrowserPane::PairedCompanionModelIndex(int modelIndex) const noexcept
    {
        const auto iterator = pairedCompanionByRepresentativeModelIndex_.find(modelIndex);
        return iterator != pairedCompanionByRepresentativeModelIndex_.end()
            ? iterator->second
            : -1;
    }

    void BrowserPane::UpdateDetailsListView()
    {
        if (!detailsList_)
        {
            return;
        }

        const int itemCount = static_cast<int>(orderedModelIndices_.size());
        ListView_SetItemCountEx(detailsList_, itemCount, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        LayoutChildren();
        InvalidateRect(detailsList_, nullptr, TRUE);
    }

    void BrowserPane::UpdateVerticalScrollBar()
    {
        if (!hwnd_)
        {
            return;
        }

        if (viewMode_ != BrowserViewMode::Thumbnails || orderedModelIndices_.empty())
        {
            scrollOffsetY_ = 0;
            ShowScrollBar(hwnd_, SB_VERT, FALSE);
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
    const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientWidth);
        const int rows = (static_cast<int>(orderedModelIndices_.size()) + columns - 1) / columns;
    const int contentHeight = layout.cellPadding + rows * (layout.itemHeight + layout.cellPadding);
        const int maxOffset = std::max(0, contentHeight - clientHeight);
        scrollOffsetY_ = std::clamp(scrollOffsetY_, 0, maxOffset);

        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scrollInfo.nMin = 0;
        scrollInfo.nMax = std::max(0, contentHeight - 1);
        scrollInfo.nPage = static_cast<UINT>(std::max(0, clientHeight));
        scrollInfo.nPos = scrollOffsetY_;
        SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
        ShowScrollBar(hwnd_, SB_VERT, maxOffset > 0);
    }

    void BrowserPane::CancelSmoothScrollAnimation()
    {
        if (smoothScrollTimerId_)
        {
            KillTimer(hwnd_, kSmoothScrollTimerId);
            smoothScrollTimerId_ = 0;
        }

        smoothScrollCurrent_ = static_cast<double>(scrollOffsetY_);
        smoothScrollTarget_ = smoothScrollCurrent_;
    }

    void BrowserPane::SetScrollOffset(int value, bool scheduleVisibleWork, bool forceVisibleWork)
    {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int columns = ColumnsForClientWidth(clientWidth);
        const int rows = columns > 0 ? (static_cast<int>(orderedModelIndices_.size()) + columns - 1) / columns : 0;
        const int contentHeight = layout.cellPadding + rows * (layout.itemHeight + layout.cellPadding);
        const int maxOffset = std::max(0, contentHeight - clientHeight);
        const int clampedValue = std::clamp(value, 0, maxOffset);
        if (clampedValue == scrollOffsetY_)
        {
            if (scheduleVisibleWork && forceVisibleWork)
            {
                ScheduleVisibleThumbnailWork();
            }
            return;
        }

        HideThumbnailTooltip();
        thumbnailScrollDirection_ = clampedValue > scrollOffsetY_ ? 1 : -1;
        scrollOffsetY_ = clampedValue;
        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_POS;
        scrollInfo.nPos = scrollOffsetY_;
        SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (scheduleVisibleWork)
        {
            ScheduleVisibleThumbnailWork();
        }
    }

    int BrowserPane::PrimarySelectedViewIndex() const
    {
        const int primaryModelIndex = PrimarySelectedModelIndex();
        if (primaryModelIndex >= 0)
        {
            return ViewIndexFromModelIndex(primaryModelIndex);
        }

        return orderedModelIndices_.empty() ? -1 : 0;
    }

    int BrowserPane::ComputeThumbnailNavigationTarget(int currentViewIndex, WPARAM keyCode) const
    {
        const int itemCount = static_cast<int>(orderedModelIndices_.size());
        if (itemCount <= 0)
        {
            return -1;
        }

        if (currentViewIndex < 0)
        {
            if (keyCode == VK_END)
            {
                return itemCount - 1;
            }
            return 0;
        }

        switch (keyCode)
        {
        case VK_LEFT:
            return currentViewIndex > 0 ? currentViewIndex - 1 : currentViewIndex;
        case VK_RIGHT:
            return currentViewIndex + 1 < itemCount ? currentViewIndex + 1 : currentViewIndex;
        case VK_HOME:
            return 0;
        case VK_END:
            return itemCount - 1;
        case VK_UP:
        case VK_DOWN:
        {
            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
            const int currentRow = currentViewIndex / columns;
            const int currentColumn = currentViewIndex % columns;
            const int lastRow = (itemCount - 1) / columns;
            const int targetRow = keyCode == VK_UP ? currentRow - 1 : currentRow + 1;
            if (targetRow < 0 || targetRow > lastRow)
            {
                return currentViewIndex;
            }

            const int targetRowStart = targetRow * columns;
            const int targetRowLast = std::min(itemCount - 1, targetRowStart + columns - 1);
            return std::min(targetRowStart + currentColumn, targetRowLast);
        }
        case VK_PRIOR:
        case VK_NEXT:
        {
            RECT clientRect{};
            GetClientRect(hwnd_, &clientRect);
            const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
            const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
            const int clientHeight = clientRect.bottom - clientRect.top;
            const int rowStride = layout.itemHeight + layout.cellPadding;
            const int pageRows = std::max(1, clientHeight / rowStride);
            const int currentRow = currentViewIndex / columns;
            const int currentColumn = currentViewIndex % columns;
            const int lastRow = (itemCount - 1) / columns;
            const int targetRow = keyCode == VK_PRIOR
                ? std::max(0, currentRow - pageRows)
                : std::min(lastRow, currentRow + pageRows);
            const int targetRowStart = targetRow * columns;
            const int targetRowLast = std::min(itemCount - 1, targetRowStart + columns - 1);
            return std::min(targetRowStart + currentColumn, targetRowLast);
        }
        default:
            break;
        }

        return -1;
    }

    void BrowserPane::EnsureThumbnailViewIndexVisible(int viewIndex)
    {
        if (!hwnd_
            || viewMode_ != BrowserViewMode::Thumbnails
            || viewIndex < 0
            || viewIndex >= static_cast<int>(orderedModelIndices_.size()))
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int clientHeight = clientRect.bottom - clientRect.top;
        if (clientHeight <= 0)
        {
            return;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int row = viewIndex / columns;
        const int cellTop = layout.cellPadding + row * (layout.itemHeight + layout.cellPadding);
        const int cellBottom = cellTop + layout.itemHeight;

        int nextOffset = scrollOffsetY_;
        if (cellTop < scrollOffsetY_)
        {
            nextOffset = cellTop;
        }
        else if (cellBottom > scrollOffsetY_ + clientHeight)
        {
            nextOffset = cellBottom - clientHeight;
        }

        SetScrollOffset(nextOffset);
    }

    bool BrowserPane::HandleThumbnailNavigationKey(WPARAM keyCode, bool extendSelection)
    {
        const int currentViewIndex = PrimarySelectedViewIndex();
        const int targetViewIndex = ComputeThumbnailNavigationTarget(currentViewIndex, keyCode);
        if (targetViewIndex < 0)
        {
            return false;
        }

        if (extendSelection)
        {
            ExtendSelectionToViewIndex(targetViewIndex);
        }
        else
        {
            SelectSingleViewIndex(targetViewIndex);
        }

        EnsureThumbnailViewIndexVisible(targetViewIndex);
        return true;
    }

    bool BrowserPane::HandleNavigationKey(UINT message, WPARAM keyCode, LPARAM keyData)
    {
        if (message == WM_SYSKEYDOWN)
        {
            return false;
        }

        switch (keyCode)
        {
        case VK_RETURN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_HOME:
        case VK_END:
            break;
        default:
            return false;
        }

        if (viewMode_ == BrowserViewMode::Thumbnails)
        {
            if (keyCode == VK_RETURN)
            {
                RequestOpenPrimarySelection();
                if (hwnd_ && GetFocus() != hwnd_)
                {
                    SetFocus(hwnd_);
                }
                return true;
            }

            HandleThumbnailNavigationKey(keyCode, GetKeyState(VK_SHIFT) < 0);
            if (hwnd_ && GetFocus() != hwnd_)
            {
                SetFocus(hwnd_);
            }
            return true;
        }

        if (viewMode_ == BrowserViewMode::Details && detailsList_)
        {
            if (GetFocus() != detailsList_)
            {
                SetFocus(detailsList_);
            }
            SendMessageW(detailsList_, message, keyCode, keyData);
            return true;
        }

        return false;
    }

    int BrowserPane::ColumnsForClientWidth(int width) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int stride = layout.itemWidth + layout.cellPadding;
        return std::max(1, (std::max(width, stride) - layout.cellPadding) / stride);
    }

    RECT BrowserPane::GetThumbnailCellRect(int viewIndex) const
    {
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int row = viewIndex / columns;
        const int column = viewIndex % columns;

        RECT rect{};
        rect.left = layout.cellPadding + column * (layout.itemWidth + layout.cellPadding);
        rect.top = layout.cellPadding + row * (layout.itemHeight + layout.cellPadding) - scrollOffsetY_;
        rect.right = rect.left + layout.itemWidth;
        rect.bottom = rect.top + layout.itemHeight;
        return rect;
    }

    RECT BrowserPane::GetThumbnailPreviewRect(const RECT& cellRect) const
    {
        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        const int cellWidth = cellRect.right - cellRect.left;
        RECT previewRect{};
        previewRect.left = cellRect.left + std::max(layout.previewInset, (cellWidth - layout.previewWidth) / 2);
        previewRect.top = cellRect.top + layout.previewInset;
        previewRect.right = previewRect.left + layout.previewWidth;
        previewRect.bottom = previewRect.top + layout.previewHeight;
        return previewRect;
    }

    int BrowserPane::HitTestThumbnailItem(POINT point) const
    {
        if (!hwnd_ || orderedModelIndices_.empty())
        {
            return -1;
        }

        const ThumbnailLayoutMetrics layout = CurrentThumbnailLayout();
        if (point.x < layout.cellPadding || point.y < 0)
        {
            return -1;
        }

        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        const int columns = ColumnsForClientWidth(clientRect.right - clientRect.left);
        const int horizontalStride = layout.itemWidth + layout.cellPadding;
        const int verticalStride = layout.itemHeight + layout.cellPadding;
        const int adjustedY = point.y + scrollOffsetY_;
        if (adjustedY < layout.cellPadding)
        {
            return -1;
        }

        const int column = (point.x - layout.cellPadding) / horizontalStride;
        const int row = (adjustedY - layout.cellPadding) / verticalStride;
        if (column < 0 || column >= columns || row < 0)
        {
            return -1;
        }

        const int xWithinStride = (point.x - layout.cellPadding) % horizontalStride;
        const int yWithinStride = (adjustedY - layout.cellPadding) % verticalStride;
        if (xWithinStride > layout.itemWidth || yWithinStride > layout.itemHeight)
        {
            return -1;
        }

        const int viewIndex = row * columns + column;
        return viewIndex >= 0 && viewIndex < static_cast<int>(orderedModelIndices_.size()) ? viewIndex : -1;
    }

    int BrowserPane::ViewIndexFromModelIndex(int modelIndex) const
    {
        const auto iterator = std::find(orderedModelIndices_.begin(), orderedModelIndices_.end(), modelIndex);
        return iterator == orderedModelIndices_.end() ? -1 : static_cast<int>(std::distance(orderedModelIndices_.begin(), iterator));
    }

    int BrowserPane::ModelIndexFromViewIndex(int viewIndex) const
    {
        return viewIndex >= 0 && viewIndex < static_cast<int>(orderedModelIndices_.size())
            ? orderedModelIndices_[static_cast<std::size_t>(viewIndex)]
            : -1;
    }

    const BrowserItem* BrowserPane::ItemFromViewIndex(int viewIndex) const
    {
        const int modelIndex = ModelIndexFromViewIndex(viewIndex);
        if (!model_ || modelIndex < 0)
        {
            return nullptr;
        }

        const auto& items = model_->Items();
        return modelIndex < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(modelIndex)] : nullptr;
    }
}
