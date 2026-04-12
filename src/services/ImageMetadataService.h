#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
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

        explicit ImageMetadataService(std::size_t workerCount = 1);
        ~ImageMetadataService();

        void BindTargetWindow(HWND targetWindow);
        void Schedule(std::uint64_t sessionId, MetadataWorkItem workItem);
        void CancelOutstanding();
        std::shared_ptr<const ImageMetadata> FindCachedMetadata(const browser::BrowserItem& item) const;
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);

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
            MetadataWorkItem workItem;
        };

        void WorkerLoop();
        void PostReady(std::uint64_t sessionId, int modelIndex, const browser::BrowserItem& item, bool success) const;

        mutable std::mutex mutex_;
        HWND targetWindow_{};
        bool shuttingDown_{};
        std::uint64_t activeSessionId_{};
        int nextSequence_{};
        std::vector<PendingJob> pendingJobs_;
        std::unordered_set<MetadataCacheKey, MetadataCacheKeyHasher> queuedKeys_;
        std::unordered_set<MetadataCacheKey, MetadataCacheKeyHasher> inflightKeys_;
        std::unordered_map<MetadataCacheKey, std::shared_ptr<const ImageMetadata>, MetadataCacheKeyHasher> cache_;
        std::vector<std::thread> workers_;
        std::condition_variable workAvailable_;
    };
}