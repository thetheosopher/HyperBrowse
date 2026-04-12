#pragma once

#include <cstdint>
#include <string>
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
    };

    class BrowserModel
    {
    public:
        void Reset(std::wstring folderPath, bool recursive);
        void AppendItems(std::vector<BrowserItem>&& items, std::uint64_t totalCount, std::uint64_t totalBytes);
        void Complete();
        void Fail(std::wstring errorMessage);
        bool UpdateDecodedDimensions(int modelIndex, int width, int height);

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
    int EffectiveImageWidth(const BrowserItem& item) noexcept;
    int EffectiveImageHeight(const BrowserItem& item) noexcept;
    std::wstring FormatDimensionsForItem(const BrowserItem& item);
}