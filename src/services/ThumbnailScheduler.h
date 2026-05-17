#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cache/DiskThumbnailCache.h"
#include "cache/ThumbnailCache.h"
#include "util/ResourceSizing.h"

namespace hyperbrowse::decode
{
    enum class ThumbnailDecodeFailureKind : int;
}

namespace hyperbrowse::services
{
    struct ThumbnailWorkItem
    {
        int modelIndex{};
        cache::ThumbnailCacheKey cacheKey;
        int priority{};
        bool preferCpu{};
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

        explicit ThumbnailScheduler(std::size_t cacheCapacityBytes = 0,
                                    std::size_t workerCount = 0,
                                    util::ResourceProfile resourceProfile = util::ResourceProfile::Balanced);
        ~ThumbnailScheduler();

        void BindTargetWindow(HWND targetWindow);
        void Schedule(std::uint64_t sessionId, std::uint64_t requestEpoch, std::vector<ThumbnailWorkItem> workItems);
        void CancelOutstanding();
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        void SetDiskCacheEnabled(bool enabled);
        void SetPressureModeEnabled(bool enabled);
        void TrimCacheToBytes(std::size_t targetBytes);
        bool IsDiskCacheEnabled() const;

        std::shared_ptr<const cache::CachedThumbnail> FindCachedThumbnail(const cache::ThumbnailCacheKey& key) const;
        bool HasKnownFailure(const cache::ThumbnailCacheKey& key) const;
        decode::ThumbnailDecodeFailureKind KnownFailureKind(const cache::ThumbnailCacheKey& key) const;
        std::size_t CacheBytes() const;
        std::size_t CacheCapacityBytes() const;
        std::size_t WorkerCount() const;
        std::size_t GeneralWorkerCount() const;
        std::size_t RawWorkerCount() const;

    private:
        enum class WorkerKind
        {
            General,
            Raw,
        };

        struct PendingJob
        {
            std::uint64_t sessionId{};
            std::uint64_t requestEpoch{};
            int sequence{};
            std::uint64_t enqueuedTickCount{};
            ThumbnailWorkItem workItem;
            bool isRaw{};
            bool isJpeg{};
        };

        // Order pending jobs by (priority asc, sequence asc) so begin() is always the
        // highest-priority oldest job. Sequence is unique so this is effectively strict.
        struct PendingJobLess
        {
            bool operator()(const PendingJob& lhs, const PendingJob& rhs) const noexcept
            {
                if (lhs.workItem.priority != rhs.workItem.priority)
                {
                    return lhs.workItem.priority < rhs.workItem.priority;
                }
                return lhs.sequence < rhs.sequence;
            }
        };

        struct InflightDecode
        {
            int priority{};
            bool preferCpu{};
        };

        bool HasDispatchableWorkLocked(WorkerKind kind) const;
        void WorkerLoop(WorkerKind kind);
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
        std::multiset<PendingJob, PendingJobLess> pendingJobs_;
        std::unordered_set<cache::ThumbnailCacheKey, cache::ThumbnailCacheKeyHasher> queuedKeys_;
        std::unordered_map<cache::ThumbnailCacheKey, std::vector<InflightDecode>, cache::ThumbnailCacheKeyHasher> inflightJobs_;
        std::unordered_set<cache::ThumbnailCacheKey, cache::ThumbnailCacheKeyHasher> requestedKeys_;
        std::unordered_map<cache::ThumbnailCacheKey, decode::ThumbnailDecodeFailureKind, cache::ThumbnailCacheKeyHasher> failedKeys_;
        std::size_t activeWorkerCount_{};
        std::size_t activeDecodeLimit_{1};
        std::vector<std::thread> generalWorkers_;
        std::vector<std::thread> rawWorkers_;
        cache::ThumbnailCache cache_;
        cache::DiskThumbnailCache diskCache_;
        bool diskCacheEnabled_{true};
    };
}