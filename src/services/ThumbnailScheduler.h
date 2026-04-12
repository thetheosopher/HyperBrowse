#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "cache/ThumbnailCache.h"

namespace hyperbrowse::services
{
    struct ThumbnailWorkItem
    {
        int modelIndex{};
        cache::ThumbnailCacheKey cacheKey;
        int priority{};
    };

    struct ThumbnailReadyUpdate
    {
        std::uint64_t sessionId{};
        std::uint64_t requestEpoch{};
        int modelIndex{};
        cache::ThumbnailCacheKey cacheKey;
        int imageWidth{};
        int imageHeight{};
        bool success{};
    };

    class ThumbnailScheduler
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 43;

        explicit ThumbnailScheduler(std::size_t cacheCapacityBytes = 96ULL * 1024ULL * 1024ULL,
                                    std::size_t workerCount = 0);
        ~ThumbnailScheduler();

        void BindTargetWindow(HWND targetWindow);
        void Schedule(std::uint64_t sessionId, std::uint64_t requestEpoch, std::vector<ThumbnailWorkItem> workItems);
        void CancelOutstanding();
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);

        std::shared_ptr<const cache::CachedThumbnail> FindCachedThumbnail(const cache::ThumbnailCacheKey& key) const;
        std::size_t CacheBytes() const;
        std::size_t CacheCapacityBytes() const;

    private:
        struct PendingJob
        {
            std::uint64_t sessionId{};
            std::uint64_t requestEpoch{};
            int sequence{};
            ThumbnailWorkItem workItem;
        };

        void WorkerLoop();
        void PostReady(std::uint64_t sessionId,
                       std::uint64_t requestEpoch,
                       int modelIndex,
                       const cache::ThumbnailCacheKey& cacheKey,
                       int imageWidth,
                       int imageHeight,
                       bool success) const;

        mutable std::mutex mutex_;
        std::condition_variable workAvailable_;
        bool shuttingDown_{};
        HWND targetWindow_{};
        std::uint64_t activeSessionId_{};
        std::uint64_t activeRequestEpoch_{};
        int nextSequence_{};
        std::vector<PendingJob> pendingJobs_;
        std::unordered_set<cache::ThumbnailCacheKey, cache::ThumbnailCacheKeyHasher> queuedKeys_;
        std::unordered_set<cache::ThumbnailCacheKey, cache::ThumbnailCacheKeyHasher> inflightKeys_;
        std::unordered_set<cache::ThumbnailCacheKey, cache::ThumbnailCacheKeyHasher> requestedKeys_;
        std::vector<std::thread> workers_;
        cache::ThumbnailCache cache_;
    };
}