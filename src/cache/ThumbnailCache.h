#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/PathUtils.h"

namespace hyperbrowse::cache
{
    struct ThumbnailCacheKey
    {
        std::wstring filePath;
        std::uint64_t modifiedTimestampUtc{};
        int targetWidth{};
        int targetHeight{};

        bool operator==(const ThumbnailCacheKey& other) const noexcept
        {
            return modifiedTimestampUtc == other.modifiedTimestampUtc
                && targetWidth == other.targetWidth
                && targetHeight == other.targetHeight
                && util::NormalizePathForComparison(filePath) == util::NormalizePathForComparison(other.filePath);
        }
    };

    struct ThumbnailCacheKeyHasher
    {
        std::size_t operator()(const ThumbnailCacheKey& key) const noexcept;
    };

    class CachedThumbnail
    {
    public:
        CachedThumbnail(HBITMAP bitmap,
                        int width,
                        int height,
                        std::size_t byteCount,
                        int sourceWidth,
                        int sourceHeight);
        ~CachedThumbnail();

        CachedThumbnail(const CachedThumbnail&) = delete;
        CachedThumbnail& operator=(const CachedThumbnail&) = delete;

        HBITMAP Bitmap() const noexcept;
        int Width() const noexcept;
        int Height() const noexcept;
        std::size_t ByteCount() const noexcept;
        int SourceWidth() const noexcept;
        int SourceHeight() const noexcept;

    private:
        HBITMAP bitmap_{};
        int width_{};
        int height_{};
        std::size_t byteCount_{};
        int sourceWidth_{};
        int sourceHeight_{};
    };

    class ThumbnailCache
    {
    public:
        explicit ThumbnailCache(std::size_t capacityBytes);

        std::shared_ptr<const CachedThumbnail> Find(const ThumbnailCacheKey& key) const;
        void Insert(ThumbnailCacheKey key, std::shared_ptr<const CachedThumbnail> thumbnail);
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        void Clear();
        std::size_t CurrentBytes() const;
        std::size_t CapacityBytes() const noexcept;

    private:
        struct Entry
        {
            std::shared_ptr<const CachedThumbnail> thumbnail;
            std::list<ThumbnailCacheKey>::iterator lruIterator;
            std::size_t byteCount{};
        };

        void EvictIfNeeded();

        const std::size_t capacityBytes_{};
        mutable std::mutex mutex_;
        std::size_t currentBytes_{};
        mutable std::list<ThumbnailCacheKey> lruOrder_;
        mutable std::unordered_map<ThumbnailCacheKey, Entry, ThumbnailCacheKeyHasher> entries_;
    };
}