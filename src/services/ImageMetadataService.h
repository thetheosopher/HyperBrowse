#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "browser/BrowserModel.h"
#include "util/PathUtils.h"

namespace hyperbrowse::services
{
    struct ImageMetadata
    {
        bool hasExif{};
        bool hasIptc{};
        bool hasXmp{};
        std::wstring cameraMake;
        std::wstring cameraModel;
        std::wstring dateTaken;
        std::wstring exposureTime;
        std::wstring fNumber;
        std::wstring isoSpeed;
        std::wstring focalLength;
        std::wstring title;
        std::wstring author;
        std::wstring keywords;
        std::wstring comment;
    };

    struct MetadataWorkItem
    {
        int modelIndex{};
        browser::BrowserItem item;
        int priority{};
    };

    struct MetadataReadyUpdate
    {
        std::uint64_t sessionId{};
        int modelIndex{};
        browser::BrowserItem item;
        bool success{};
    };

    std::shared_ptr<const ImageMetadata> ExtractImageMetadata(const browser::BrowserItem& item,
                                                              std::wstring* errorMessage = nullptr);
    std::wstring FormatImageMetadataReport(const browser::BrowserItem& item, const ImageMetadata& metadata);

    class ImageMetadataService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 45;

        using MetadataExtractor = std::function<std::shared_ptr<const ImageMetadata>(const browser::BrowserItem&, std::wstring*)>;

        explicit ImageMetadataService(std::size_t workerCount = 1,
                                      std::size_t cacheCapacityEntries = 512,
                                      MetadataExtractor extractor = ExtractImageMetadata);
        ~ImageMetadataService();

        void BindTargetWindow(HWND targetWindow);
        void Schedule(std::uint64_t sessionId, MetadataWorkItem workItem);
        void CancelOutstanding();
        std::shared_ptr<const ImageMetadata> FindCachedMetadata(const browser::BrowserItem& item) const;
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        std::size_t CacheEntryCount() const;
        std::size_t CacheCapacityEntries() const noexcept;

    private:
        struct MetadataCacheKey
        {
            std::wstring filePath;
            std::uint64_t modifiedTimestampUtc{};

            bool operator==(const MetadataCacheKey& other) const noexcept
            {
                return modifiedTimestampUtc == other.modifiedTimestampUtc
                    && util::NormalizePathForComparison(filePath) == util::NormalizePathForComparison(other.filePath);
            }
        };

        struct MetadataCacheKeyHasher
        {
            std::size_t operator()(const MetadataCacheKey& key) const noexcept;
        };

        struct PendingJob
        {
            std::uint64_t sessionId{};
            int sequence{};
            std::uint64_t pathGeneration{};
            MetadataCacheKey cacheKey;
            MetadataWorkItem workItem;
        };

        struct CacheEntry
        {
            std::shared_ptr<const ImageMetadata> metadata;
            std::list<MetadataCacheKey>::iterator lruIterator;
        };

        void WorkerLoop();
        void PostReady(std::uint64_t sessionId, int modelIndex, const browser::BrowserItem& item, bool success) const;
        void InsertCacheEntryLocked(MetadataCacheKey key, std::shared_ptr<const ImageMetadata> metadata);

        mutable std::mutex mutex_;
        HWND targetWindow_{};
        bool shuttingDown_{};
        std::uint64_t activeSessionId_{};
        int nextSequence_{};
        const std::size_t cacheCapacityEntries_{};
        MetadataExtractor extractor_;
        std::vector<PendingJob> pendingJobs_;
        std::unordered_set<MetadataCacheKey, MetadataCacheKeyHasher> queuedKeys_;
        std::unordered_set<MetadataCacheKey, MetadataCacheKeyHasher> inflightKeys_;
        mutable std::list<MetadataCacheKey> cacheLruOrder_;
        mutable std::unordered_map<MetadataCacheKey, CacheEntry, MetadataCacheKeyHasher> cache_;
        std::unordered_map<std::wstring, std::uint64_t> pathGenerations_;
        std::uint64_t nextPathGeneration_{1};
        std::vector<std::thread> workers_;
        std::condition_variable workAvailable_;
    };
}