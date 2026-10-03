#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
        struct RuntimeStatistics
        {
            std::size_t pendingJobCount{};
            std::size_t inflightDecodeCount{};
            std::size_t activeWorkerCount{};
            std::size_t activeDecodeLimit{};
        };

        using PersistentCacheStatisticsCallback = std::function<void(bool, cache::DiskThumbnailCache::Statistics)>;
        using PersistentCacheOperationCallback = std::function<void(bool)>;

        static constexpr UINT kMessageId = WM_APP + 43;

        explicit ThumbnailScheduler(std::size_t cacheCapacityBytes = 0,
                                    std::size_t workerCount = 0,
                                    util::ResourceProfile resourceProfile = util::ResourceProfile::Balanced,
                                    std::function<void()> persistenceBeforeJobHook = {},
                                    std::function<void()> decodeBeforeJobHook = {},
                                    std::size_t persistentCacheCapacityBytes = 0,
                                    std::wstring persistentCacheDirectory = {});
        ~ThumbnailScheduler();

        static std::size_t ResolveCacheCapacityBytes(std::size_t requestedCapacityBytes,
                                  util::ResourceProfile resourceProfile);
        static std::size_t ResolvePersistentCacheCapacityBytes(std::size_t requestedCapacityBytes,
                                    util::ResourceProfile resourceProfile);

        void BindTargetWindow(HWND targetWindow);
        void Schedule(std::uint64_t sessionId, std::uint64_t requestEpoch, std::vector<ThumbnailWorkItem> workItems);
        void CancelOutstanding();
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        void SetDiskCacheEnabled(bool enabled);
        void SetPressureModeEnabled(bool enabled);
        void TrimCacheToBytes(std::size_t targetBytes);
        bool IsDiskCacheEnabled() const;
        bool QueuePersistentCacheStatistics(PersistentCacheStatisticsCallback callback,
                                            bool includeSourceFilePaths = false);
        bool QueuePersistentCacheMaintenance(bool purge, PersistentCacheOperationCallback callback);

        std::shared_ptr<const cache::CachedThumbnail> FindCachedThumbnail(const cache::ThumbnailCacheKey& key) const;
        bool HasKnownFailure(const cache::ThumbnailCacheKey& key) const;
        decode::ThumbnailDecodeFailureKind KnownFailureKind(const cache::ThumbnailCacheKey& key) const;
        std::wstring KnownFailureMessage(const cache::ThumbnailCacheKey& key) const;
        std::size_t CacheBytes() const;
        std::size_t CacheCapacityBytes() const;
        cache::ThumbnailCache::Statistics GetCacheStatistics() const;
        RuntimeStatistics GetRuntimeStatistics() const;
        std::size_t DiskCacheCapacityBytes() const noexcept;
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
            bool diskLookupCompleted{};
            std::shared_ptr<const cache::CachedThumbnail> cachedThumbnail;
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

        struct DiskPersistenceJob
        {
            enum class Kind
            {
                Lookup,
                RefreshCapacity,
                Store,
                Invalidate,
                Compact,
                Purge,
                Statistics,
            };

            Kind kind{Kind::Store};
            std::uint64_t enqueuedTickCount{};
            PendingJob lookupJob;
            cache::ThumbnailCacheKey cacheKey;
            std::shared_ptr<const cache::CachedThumbnail> thumbnail;
            std::vector<std::wstring> filePaths;
            bool includeSourceFilePaths{};
            PersistentCacheStatisticsCallback statisticsCallback;
            PersistentCacheOperationCallback operationCallback;
        };

        bool HasDispatchableWorkLocked(WorkerKind kind) const;
        bool HasDispatchableWorkLocked(WorkerKind kind, bool foregroundLane) const;
        void WorkerLoop(WorkerKind kind, bool foregroundLane = false);
        void DiskPersistenceLoop();
        bool HasVisibleWorkPending() const;
        void EnqueueDiskLookup(PendingJob lookupJob);
        void EnqueueDiskStore(const cache::ThumbnailCacheKey& cacheKey,
                              std::shared_ptr<const cache::CachedThumbnail> thumbnail);
        bool PostReady(std::uint64_t sessionId,
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
        std::unordered_map<cache::ThumbnailCacheKey, ThumbnailWorkItem, cache::ThumbnailCacheKeyHasher> requestedWorkItems_;
        std::unordered_map<cache::ThumbnailCacheKey, decode::ThumbnailDecodeFailureKind, cache::ThumbnailCacheKeyHasher> failedKeys_;
        std::unordered_map<cache::ThumbnailCacheKey, std::wstring, cache::ThumbnailCacheKeyHasher> failureMessages_;
        std::size_t activeWorkerCount_{};
        std::size_t activeDecodeLimit_{1};
        bool foregroundLaneEnabled_{};
        std::vector<std::thread> generalWorkers_;
        std::vector<std::thread> rawWorkers_;
        cache::ThumbnailCache cache_;
        cache::DiskThumbnailCache diskCache_;
        bool diskCacheEnabled_{true};
        bool pressureModeEnabled_{};
        std::function<void()> persistenceBeforeJobHook_;
        std::function<void()> decodeBeforeJobHook_;

        mutable std::mutex diskPersistenceMutex_;
        std::condition_variable diskPersistenceAvailable_;
        std::deque<DiskPersistenceJob> pendingDiskPersistence_;
        bool diskPersistenceShuttingDown_{};
        std::thread diskPersistenceWorker_;
        util::ResourceProfile resourceProfile_{util::ResourceProfile::Balanced};
    };
}
