#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::browser
{
    struct BrowserItem
    {
        std::wstring fileName;
        std::wstring filePath;
        std::wstring fileType;
        std::wstring modifiedDate;
        std::uint64_t modifiedTimestampUtc{};
        std::uint64_t fileSizeBytes{};
        int placeholderWidth{};
        int placeholderHeight{};
        int imageWidth{};
        int imageHeight{};
        std::int64_t dateTakenTimestampUtc{};
    };

    class BrowserModel
    {
    public:
        void Reset(std::wstring folderPath, bool recursive);
        void AppendItems(std::vector<BrowserItem>&& items, std::uint64_t totalCount, std::uint64_t totalBytes);
        void Complete();
        void Fail(std::wstring errorMessage);
        bool UpdateDecodedDimensions(int modelIndex, int width, int height);
        bool UpdateDateTakenTimestamp(int modelIndex, std::int64_t timestampUtc);
        int FindItemIndexByPath(std::wstring_view filePath) const noexcept;
        bool UpsertItem(BrowserItem item);
        bool RemoveItemByPath(std::wstring_view filePath);
        bool RemoveItemsByPathPrefix(std::wstring_view pathPrefix);
        bool ReplacePathPrefix(std::wstring_view oldPrefix, std::wstring_view newPrefix);

        const std::wstring& FolderPath() const noexcept;
        const std::vector<BrowserItem>& Items() const noexcept;
        const std::wstring& ErrorMessage() const noexcept;

        std::uint64_t TotalCount() const noexcept;
        std::uint64_t TotalBytes() const noexcept;

        bool IsRecursive() const noexcept;
        bool IsEnumerating() const noexcept;
        bool HasError() const noexcept;

    private:
        std::wstring folderPath_;
        std::vector<BrowserItem> items_;
        std::wstring errorMessage_;
        std::uint64_t totalCount_{};
        std::uint64_t totalBytes_{};
        bool recursive_{};
        bool enumerating_{};
    };

    std::wstring FormatByteSize(std::uint64_t byteCount);
    std::wstring FormatDimensions(int width, int height);
    bool IsSupportedImageExtension(std::wstring_view extension);
    bool FilePathsEqual(std::wstring_view lhs, std::wstring_view rhs);
    bool PathHasPrefix(std::wstring_view path, std::wstring_view prefix);
    BrowserItem BuildBrowserItemFromPath(const std::filesystem::path& path);
    int EffectiveImageWidth(const BrowserItem& item) noexcept;
    int EffectiveImageHeight(const BrowserItem& item) noexcept;
    std::wstring FormatDimensionsForItem(const BrowserItem& item);
}