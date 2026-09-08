#include "ui/PairedRawJpegResolver.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

#include "decode/ImageDecoder.h"
#include "util/PathUtils.h"
#include "util/StringConvert.h"

namespace hyperbrowse::ui
{
    namespace
    {
        namespace fs = std::filesystem;

        bool IsJpegFileType(std::wstring_view fileType)
        {
            return util::EqualsIgnoreCaseOrdinal(fileType, L"jpg")
                || util::EqualsIgnoreCaseOrdinal(fileType, L"jpeg")
                || util::EqualsIgnoreCaseOrdinal(fileType, L".jpg")
                || util::EqualsIgnoreCaseOrdinal(fileType, L".jpeg");
        }

        std::wstring PairKey(const browser::BrowserItem& item)
        {
            const fs::path itemPath(item.filePath);
            return util::NormalizePathForComparison(itemPath.parent_path().wstring())
                + L'\0'
                + util::NormalizePathForComparison(itemPath.stem().wstring());
        }
    }

    browser::BrowserItem PairedRawJpegResolver::Resolve(
        const browser::BrowserItem& item,
        const std::vector<browser::BrowserItem>& candidates,
        browser::RawJpegDisplayPreference preference,
        const FolderPathEquals& folderPathEquals)
    {
        const bool itemIsRaw = decode::IsRawFileType(item.fileType);
        const bool itemIsJpeg = IsJpegFileType(item.fileType);
        if (!itemIsRaw && !itemIsJpeg)
        {
            return item;
        }

        const bool preferRaw = preference == browser::RawJpegDisplayPreference::Raw;
        if ((preferRaw && itemIsRaw) || (!preferRaw && itemIsJpeg))
        {
            return item;
        }

        const fs::path itemPath(item.filePath);
        const std::wstring itemParent = itemPath.parent_path().wstring();
        const std::wstring itemStem = itemPath.stem().wstring();
        for (const browser::BrowserItem& candidate : candidates)
        {
            if (browser::FilePathsEqual(candidate.filePath, item.filePath))
            {
                continue;
            }

            const fs::path candidatePath(candidate.filePath);
            if (!folderPathEquals(candidatePath.parent_path().wstring(), itemParent)
                || !util::EqualsIgnoreCaseOrdinal(candidatePath.stem().wstring(), itemStem))
            {
                continue;
            }

            if (preferRaw && decode::IsRawFileType(candidate.fileType))
            {
                return candidate;
            }
            if (!preferRaw && IsJpegFileType(candidate.fileType))
            {
                return candidate;
            }
        }

        return item;
    }

    std::vector<browser::BrowserItem> PairedRawJpegResolver::ResolveItems(
        std::vector<browser::BrowserItem> items,
        const std::vector<browser::BrowserItem>& candidates,
        browser::RawJpegDisplayPreference preference,
        const FolderPathEquals& folderPathEquals)
    {
        std::unordered_map<std::wstring, const browser::BrowserItem*> rawCandidates;
        std::unordered_map<std::wstring, const browser::BrowserItem*> jpegCandidates;
        rawCandidates.reserve(candidates.size());
        jpegCandidates.reserve(candidates.size());
        for (const browser::BrowserItem& candidate : candidates)
        {
            if (decode::IsRawFileType(candidate.fileType))
            {
                rawCandidates.emplace(PairKey(candidate), &candidate);
            }
            else if (IsJpegFileType(candidate.fileType))
            {
                jpegCandidates.emplace(PairKey(candidate), &candidate);
            }
        }

        const auto& preferredCandidates = preference == browser::RawJpegDisplayPreference::Raw
            ? rawCandidates
            : jpegCandidates;
        for (browser::BrowserItem& item : items)
        {
            const bool itemIsRaw = decode::IsRawFileType(item.fileType);
            const bool itemIsJpeg = IsJpegFileType(item.fileType);
            if ((preference == browser::RawJpegDisplayPreference::Raw && !itemIsJpeg)
                || (preference == browser::RawJpegDisplayPreference::Jpeg && !itemIsRaw))
            {
                continue;
            }

            const auto candidate = preferredCandidates.find(PairKey(item));
            if (candidate != preferredCandidates.end()
                && !browser::FilePathsEqual(candidate->second->filePath, item.filePath))
            {
                const fs::path itemPath(item.filePath);
                const fs::path candidatePath(candidate->second->filePath);
                if (folderPathEquals(itemPath.parent_path().wstring(), candidatePath.parent_path().wstring()))
                {
                    item = *candidate->second;
                }
            }
        }

        return items;
    }

    std::vector<std::wstring> PairedRawJpegResolver::ExpandPaths(
        const std::vector<std::wstring>& paths,
        const std::vector<browser::BrowserItem>& candidates,
        const FolderPathEquals& folderPathEquals)
    {
        std::vector<std::wstring> expandedPaths = paths;
        for (const std::wstring& selectedPath : paths)
        {
            const auto selectedItem = std::find_if(candidates.begin(), candidates.end(), [&](const browser::BrowserItem& candidate)
            {
                return browser::FilePathsEqual(candidate.filePath, selectedPath);
            });
            if (selectedItem == candidates.end())
            {
                continue;
            }

            const bool selectedIsRaw = decode::IsRawFileType(selectedItem->fileType);
            const bool selectedIsJpeg = IsJpegFileType(selectedItem->fileType);
            if (!selectedIsRaw && !selectedIsJpeg)
            {
                continue;
            }

            const fs::path selectedFsPath(selectedItem->filePath);
            const std::wstring selectedParent = selectedFsPath.parent_path().wstring();
            const std::wstring selectedStem = selectedFsPath.stem().wstring();
            for (const browser::BrowserItem& candidate : candidates)
            {
                if (browser::FilePathsEqual(candidate.filePath, selectedItem->filePath))
                {
                    continue;
                }

                const fs::path candidatePath(candidate.filePath);
                if (!folderPathEquals(candidatePath.parent_path().wstring(), selectedParent)
                    || !util::EqualsIgnoreCaseOrdinal(candidatePath.stem().wstring(), selectedStem))
                {
                    continue;
                }

                const bool candidateIsRaw = decode::IsRawFileType(candidate.fileType);
                const bool candidateIsJpeg = IsJpegFileType(candidate.fileType);
                if (!((selectedIsRaw && candidateIsJpeg) || (selectedIsJpeg && candidateIsRaw)))
                {
                    continue;
                }

                const auto existing = std::find_if(expandedPaths.begin(), expandedPaths.end(), [&](const std::wstring& existingPath)
                {
                    return browser::FilePathsEqual(existingPath, candidate.filePath);
                });
                if (existing == expandedPaths.end())
                {
                    expandedPaths.push_back(candidate.filePath);
                }
            }
        }

        return expandedPaths;
    }
}
