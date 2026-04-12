#include "browser/BrowserModel.h"

#include "decode/ImageDecoder.h"

#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <iterator>

#include <windows.h>

namespace hyperbrowse::browser
{
    namespace
    {
        constexpr int kPlaceholderWidth = 256;
        constexpr int kPlaceholderHeight = 256;

        std::wstring ToLowercase(std::wstring_view value)
        {
            std::wstring lowercased;
            lowercased.reserve(value.size());
            for (const wchar_t character : value)
            {
                lowercased.push_back(static_cast<wchar_t>(towlower(character)));
            }
            return lowercased;
        }

        std::wstring NormalizePathForComparison(std::wstring_view value)
        {
            std::wstring normalized(value);
            std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
            while (normalized.size() > 3 && !normalized.empty() && normalized.back() == L'\\')
            {
                normalized.pop_back();
            }
            return ToLowercase(normalized);
        }

        std::wstring FormatFileTime(const FILETIME& fileTime)
        {
            FILETIME localFileTime{};
            SYSTEMTIME systemTime{};
            if (!FileTimeToLocalFileTime(&fileTime, &localFileTime)
                || !FileTimeToSystemTime(&localFileTime, &systemTime))
            {
                return L"Unavailable";
            }

            wchar_t buffer[64]{};
            swprintf_s(buffer,
                       L"%04u-%02u-%02u %02u:%02u",
                       systemTime.wYear,
                       systemTime.wMonth,
                       systemTime.wDay,
                       systemTime.wHour,
                       systemTime.wMinute);
            return buffer;
        }
    }

    void BrowserModel::Reset(std::wstring folderPath, bool recursive)
    {
        folderPath_ = std::move(folderPath);
        items_.clear();
        errorMessage_.clear();
        totalCount_ = 0;
        totalBytes_ = 0;
        recursive_ = recursive;
        enumerating_ = true;
    }

    void BrowserModel::AppendItems(std::vector<BrowserItem>&& items, std::uint64_t totalCount, std::uint64_t totalBytes)
    {
        items_.insert(items_.end(),
                      std::make_move_iterator(items.begin()),
                      std::make_move_iterator(items.end()));
        totalCount_ = totalCount;
        totalBytes_ = totalBytes;
    }

    void BrowserModel::Complete()
    {
        enumerating_ = false;
        errorMessage_.clear();
    }

    void BrowserModel::Fail(std::wstring errorMessage)
    {
        enumerating_ = false;
        errorMessage_ = std::move(errorMessage);
    }

    bool BrowserModel::UpdateDecodedDimensions(int modelIndex, int width, int height)
    {
        if (modelIndex < 0 || modelIndex >= static_cast<int>(items_.size()) || width <= 0 || height <= 0)
        {
            return false;
        }

        BrowserItem& item = items_[static_cast<std::size_t>(modelIndex)];
        if (item.imageWidth == width && item.imageHeight == height)
        {
            return false;
        }

        item.imageWidth = width;
        item.imageHeight = height;
        return true;
    }

    int BrowserModel::FindItemIndexByPath(std::wstring_view filePath) const noexcept
    {
        for (std::size_t index = 0; index < items_.size(); ++index)
        {
            if (FilePathsEqual(items_[index].filePath, filePath))
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    bool BrowserModel::UpsertItem(BrowserItem item)
    {
        const int existingIndex = FindItemIndexByPath(item.filePath);
        if (existingIndex >= 0)
        {
            BrowserItem& existingItem = items_[static_cast<std::size_t>(existingIndex)];
            item.imageWidth = existingItem.imageWidth;
            item.imageHeight = existingItem.imageHeight;
            totalBytes_ = totalBytes_ - existingItem.fileSizeBytes + item.fileSizeBytes;
            const bool changed = existingItem.fileSizeBytes != item.fileSizeBytes
                || existingItem.modifiedTimestampUtc != item.modifiedTimestampUtc
                || existingItem.modifiedDate != item.modifiedDate
                || existingItem.fileName != item.fileName
                || existingItem.fileType != item.fileType;
            existingItem = std::move(item);
            totalCount_ = static_cast<std::uint64_t>(items_.size());
            return changed;
        }

        totalBytes_ += item.fileSizeBytes;
        items_.push_back(std::move(item));
        totalCount_ = static_cast<std::uint64_t>(items_.size());
        return true;
    }

    bool BrowserModel::RemoveItemByPath(std::wstring_view filePath)
    {
        const int existingIndex = FindItemIndexByPath(filePath);
        if (existingIndex < 0)
        {
            return false;
        }

        totalBytes_ -= items_[static_cast<std::size_t>(existingIndex)].fileSizeBytes;
        items_.erase(items_.begin() + existingIndex);
        totalCount_ = static_cast<std::uint64_t>(items_.size());
        return true;
    }

    bool BrowserModel::RemoveItemsByPathPrefix(std::wstring_view pathPrefix)
    {
        const auto iterator = std::remove_if(items_.begin(), items_.end(), [&](const BrowserItem& item)
        {
            return PathHasPrefix(item.filePath, pathPrefix);
        });

        if (iterator == items_.end())
        {
            return false;
        }

        for (auto current = iterator; current != items_.end(); ++current)
        {
            totalBytes_ -= current->fileSizeBytes;
        }

        items_.erase(iterator, items_.end());
        totalCount_ = static_cast<std::uint64_t>(items_.size());
        return true;
    }

    bool BrowserModel::ReplacePathPrefix(std::wstring_view oldPrefix, std::wstring_view newPrefix)
    {
        bool changed = false;
        for (BrowserItem& item : items_)
        {
            if (!PathHasPrefix(item.filePath, oldPrefix))
            {
                continue;
            }

            std::wstring rewrittenPath(newPrefix);
            std::wstring suffix(item.filePath.substr(oldPrefix.size()));
            if (!rewrittenPath.empty() && !suffix.empty() && rewrittenPath.back() == L'\\' && suffix.front() == L'\\')
            {
                suffix.erase(suffix.begin());
            }
            rewrittenPath.append(suffix);

            BrowserItem rebuiltItem = BuildBrowserItemFromPath(std::filesystem::path(rewrittenPath));
            rebuiltItem.imageWidth = item.imageWidth;
            rebuiltItem.imageHeight = item.imageHeight;
            totalBytes_ = totalBytes_ - item.fileSizeBytes + rebuiltItem.fileSizeBytes;
            item = std::move(rebuiltItem);
            changed = true;
        }

        totalCount_ = static_cast<std::uint64_t>(items_.size());
        return changed;
    }

    const std::wstring& BrowserModel::FolderPath() const noexcept
    {
        return folderPath_;
    }

    const std::vector<BrowserItem>& BrowserModel::Items() const noexcept
    {
        return items_;
    }

    const std::wstring& BrowserModel::ErrorMessage() const noexcept
    {
        return errorMessage_;
    }

    std::uint64_t BrowserModel::TotalCount() const noexcept
    {
        return totalCount_;
    }

    std::uint64_t BrowserModel::TotalBytes() const noexcept
    {
        return totalBytes_;
    }

    bool BrowserModel::IsRecursive() const noexcept
    {
        return recursive_;
    }

    bool BrowserModel::IsEnumerating() const noexcept
    {
        return enumerating_;
    }

    bool BrowserModel::HasError() const noexcept
    {
        return !errorMessage_.empty();
    }

    std::wstring FormatByteSize(std::uint64_t byteCount)
    {
        static constexpr const wchar_t* kSuffixes[] = {L"B", L"KB", L"MB", L"GB", L"TB"};

        double scaledValue = static_cast<double>(byteCount);
        std::size_t suffixIndex = 0;
        while (scaledValue >= 1024.0 && suffixIndex + 1 < std::size(kSuffixes))
        {
            scaledValue /= 1024.0;
            ++suffixIndex;
        }

        wchar_t buffer[64]{};
        if (suffixIndex == 0)
        {
            swprintf_s(buffer, L"%llu %ls", static_cast<unsigned long long>(byteCount), kSuffixes[suffixIndex]);
        }
        else
        {
            swprintf_s(buffer, L"%.1f %ls", scaledValue, kSuffixes[suffixIndex]);
        }

        return buffer;
    }

    std::wstring FormatDimensions(int width, int height)
    {
        return std::to_wstring(width) + L"x" + std::to_wstring(height);
    }

    bool IsSupportedImageExtension(std::wstring_view extension)
    {
        const std::wstring normalized = ToLowercase(extension);
            return hyperbrowse::decode::IsWicFileType(normalized)
                || hyperbrowse::decode::IsRawFileType(normalized);
    }

    bool FilePathsEqual(std::wstring_view lhs, std::wstring_view rhs)
    {
        return NormalizePathForComparison(lhs) == NormalizePathForComparison(rhs);
    }

    bool PathHasPrefix(std::wstring_view path, std::wstring_view prefix)
    {
        const std::wstring normalizedPath = NormalizePathForComparison(path);
        std::wstring normalizedPrefix = NormalizePathForComparison(prefix);
        if (normalizedPrefix.empty())
        {
            return false;
        }

        if (normalizedPath == normalizedPrefix)
        {
            return true;
        }

        if (!normalizedPrefix.empty() && normalizedPrefix.back() != L'\\')
        {
            normalizedPrefix.push_back(L'\\');
        }

        return normalizedPath.rfind(normalizedPrefix, 0) == 0;
    }

    BrowserItem BuildBrowserItemFromPath(const std::filesystem::path& path)
    {
        BrowserItem item;
        item.fileName = path.filename().wstring();
        item.filePath = path.wstring();
        item.fileType = ToLowercase(path.extension().wstring());
        if (!item.fileType.empty() && item.fileType.front() == L'.')
        {
            item.fileType.erase(item.fileType.begin());
        }
        if (item.fileType.empty())
        {
            item.fileType = L"unknown";
        }

        std::transform(item.fileType.begin(), item.fileType.end(), item.fileType.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });

        item.placeholderWidth = kPlaceholderWidth;
        item.placeholderHeight = kPlaceholderHeight;

        WIN32_FILE_ATTRIBUTE_DATA attributeData{};
        if (GetFileAttributesExW(item.filePath.c_str(), GetFileExInfoStandard, &attributeData) != FALSE)
        {
            ULARGE_INTEGER fileSize{};
            fileSize.HighPart = attributeData.nFileSizeHigh;
            fileSize.LowPart = attributeData.nFileSizeLow;
            item.fileSizeBytes = fileSize.QuadPart;
            item.modifiedDate = FormatFileTime(attributeData.ftLastWriteTime);

            ULARGE_INTEGER modifiedTimestamp{};
            modifiedTimestamp.HighPart = attributeData.ftLastWriteTime.dwHighDateTime;
            modifiedTimestamp.LowPart = attributeData.ftLastWriteTime.dwLowDateTime;
            item.modifiedTimestampUtc = modifiedTimestamp.QuadPart;
        }
        else
        {
            item.modifiedDate = L"Unavailable";
        }

        return item;
    }

    int EffectiveImageWidth(const BrowserItem& item) noexcept
    {
        return item.imageWidth > 0 ? item.imageWidth : item.placeholderWidth;
    }

    int EffectiveImageHeight(const BrowserItem& item) noexcept
    {
        return item.imageHeight > 0 ? item.imageHeight : item.placeholderHeight;
    }

    std::wstring FormatDimensionsForItem(const BrowserItem& item)
    {
        return FormatDimensions(EffectiveImageWidth(item), EffectiveImageHeight(item));
    }
}