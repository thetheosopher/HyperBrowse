#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cache/ThumbnailCache.h"

namespace hyperbrowse::cache
{
    inline constexpr wchar_t kCacheDirectoryEnvironmentVariable[] = L"HYPERBROWSE_THUMBNAIL_CACHE_DIRECTORY";

    class DiskThumbnailCache
    {
    public:
        struct Statistics
        {
            struct ShardStatistics
            {
                std::wstring name;
                std::size_t indexedEntryCount{};
                std::size_t indexedBytes{};
                std::size_t fileCount{};
                std::size_t fileBytes{};
                std::size_t orphanFileCount{};
                std::size_t orphanFileBytes{};
                std::size_t missingFileCount{};
            };

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
            std::vector<std::wstring> sourceFilePaths;
            std::vector<ShardStatistics> shards;
        };

        explicit DiskThumbnailCache(std::size_t capacityBytes = 0, std::wstring cacheDirectory = {});
        ~DiskThumbnailCache();

        DiskThumbnailCache(const DiskThumbnailCache&) = delete;
        DiskThumbnailCache& operator=(const DiskThumbnailCache&) = delete;

        static std::uint64_t QueryDefaultCacheVolumeFreeBytes() noexcept;

        std::shared_ptr<const CachedThumbnail> TryLoad(const ThumbnailCacheKey& key);
        void Store(const ThumbnailCacheKey& key, std::shared_ptr<const CachedThumbnail> thumbnail);
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        void Clear();
        bool Compact();
        Statistics QueryStatistics(bool includeSourceFilePaths = false) const;
        std::size_t CurrentBytes() const;
        void SetCapacityBytes(std::size_t capacityBytes);
        bool FlushPendingAccessUpdates();
        bool NeedsCompaction() const;
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
        bool SaveIndexLocked() const;
        bool AppendJournalRecordLocked(std::wstring_view record);
        void ReplayJournalRecordLocked(const std::wstring& record);
        bool CompactIndexLocked();
        bool FlushPendingAccessUpdatesLocked();
        bool MigrateLegacyLayoutLocked();
        bool WriteFormatVersionLocked() const;
        void EvictIfNeededLocked();
        std::wstring EnsureCacheDirectoryLocked();

        std::atomic_size_t capacityBytes_{};
        mutable std::mutex mutex_;
        bool loaded_{};
        std::wstring cacheDirectory_;
        std::size_t currentBytes_{};
        std::size_t journalBytes_{};
        std::uint64_t nextAccessOrdinal_{1};
        mutable std::size_t pendingAccessUpdates_{};
        mutable std::vector<ThumbnailCacheKey> pendingAccessKeys_;
        std::unordered_map<ThumbnailCacheKey, Entry, ThumbnailCacheKeyHasher> entries_;
        bool compactionRequested_{};
    };
}
