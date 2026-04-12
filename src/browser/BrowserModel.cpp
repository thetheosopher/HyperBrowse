#include "browser/BrowserModel.h"

#include <cwchar>
#include <iterator>

namespace hyperbrowse::browser
{
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