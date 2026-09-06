#include "ui/PairedRawJpegResolver.h"

#include <filesystem>

#include "decode/ImageDecoder.h"
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
        for (browser::BrowserItem& item : items)
        {
            item = Resolve(item, candidates, preference, folderPathEquals);
        }

        return items;
    }
}
