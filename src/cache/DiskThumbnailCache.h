#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cache/ThumbnailCache.h"

namespace hyperbrowse::cache
{
    class DiskThumbnailCache
    {
    public:
        struct Statistics
        {
            std::wstring cacheDirectory;
            std::size_t capacityBytes{};
            std::size_t indexedEntryCount{};
            std::size_t indexedBytes{};
            std::size_t indexFileBytes{};
            std::size_t cacheFileCount{};
            std::size_t cacheFileBytes{};
            std::size_t orphanFileCount{};
            std::size_t orphanFileBytes{};
            std::size_t missingFileCount{};
        };

        explicit DiskThumbnailCache(std::size_t capacityBytes = 0, std::wstring cacheDirectory = {});

        std::shared_ptr<const CachedThumbnail> TryLoad(const ThumbnailCacheKey& key);
        void Store(const ThumbnailCacheKey& key, std::shared_ptr<const CachedThumbnail> thumbnail);
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        void Clear();
        bool Compact();
        Statistics QueryStatistics() const;
        std::size_t CurrentBytes() const;
        std::size_t CapacityBytes() const noexcept;

    private:
        struct Entry
        {
            std::wstring cacheFileName;
            std::size_t fileBytes{};
            std::uint64_t lastAccessOrdinal{};
        };

        void EnsureLoadedLocked();
        void ReloadIndexLocked();
        bool LoadIndexLocked();
        void SaveIndexLocked() const;
        void EvictIfNeededLocked();
        std::wstring EnsureCacheDirectoryLocked();

        const std::size_t capacityBytes_{};
        mutable std::mutex mutex_;
        bool loaded_{};
        std::wstring cacheDirectory_;
        std::size_t currentBytes_{};
        std::uint64_t nextAccessOrdinal_{1};
        std::unordered_map<ThumbnailCacheKey, Entry, ThumbnailCacheKeyHasher> entries_;
    };
}
