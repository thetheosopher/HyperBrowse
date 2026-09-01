#include "services/ThumbnailScheduler.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <unordered_set>

#include "decode/ImageDecoder.h"
#include "util/Diagnostics.h"
#include "util/PathUtils.h"
#include "util/ResourceSizing.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::size_t kMinWorkerCount = 2;
    constexpr std::size_t kRawWorkerDivisor = 4;
    constexpr std::size_t kConservativeRawWorkerDivisor = 6;
    constexpr std::size_t kPerformanceRawWorkerDivisor = 2;
    constexpr std::size_t kMinNvJpegBatchSize = 4;
    constexpr std::size_t kMaxNvJpegBatchSize = 12;
    constexpr int kNvJpegBatchPriorityWindow = 1;
    constexpr std::uint64_t kDefaultThumbnailCacheCapacityBytes = 96ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kConservativeThumbnailCacheCapacityBytes = 128ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kPerformanceThumbnailCacheCapacityBytes = 256ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kAggressiveThumbnailCacheCapacityBytes = 512ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kMinimumThumbnailCacheCapacityBytes = 128ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kConservativeMaximumThumbnailCacheCapacityBytes = 256ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kMaximumThumbnailCacheCapacityBytes = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kPerformanceMaximumThumbnailCacheCapacityBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kAggressiveMaximumThumbnailCacheCapacityBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;

    std::size_t ResolveThumbnailCacheCapacityBytes(std::size_t requestedCapacityBytes,
                                                   hyperbrowse::util::ResourceProfile resourceProfile)
    {
        if (requestedCapacityBytes != 0)
        {
            return requestedCapacityBytes;
        }

        const auto memorySnapshot = hyperbrowse::util::QueryMemorySnapshot();
        if (!memorySnapshot.IsValid() || memorySnapshot.availablePhysicalBytes == 0)
        {
            switch (resourceProfile)
            {
            case hyperbrowse::util::ResourceProfile::Conservative:
                return static_cast<std::size_t>(kConservativeThumbnailCacheCapacityBytes);
            case hyperbrowse::util::ResourceProfile::Performance:
                return static_cast<std::size_t>(kPerformanceThumbnailCacheCapacityBytes);
            case hyperbrowse::util::ResourceProfile::Aggressive:
                return static_cast<std::size_t>(kAggressiveThumbnailCacheCapacityBytes);
            case hyperbrowse::util::ResourceProfile::Balanced:
            default:
                return static_cast<std::size_t>(kDefaultThumbnailCacheCapacityBytes);
            }
        }

        std::uint64_t availabilityBudget = memorySnapshot.availablePhysicalBytes / 5ULL;
        std::uint64_t totalBudget = memorySnapshot.totalPhysicalBytes / 8ULL;
        std::uint64_t minimumBudget = kMinimumThumbnailCacheCapacityBytes;
        std::uint64_t maximumBudget = kMaximumThumbnailCacheCapacityBytes;
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            availabilityBudget = memorySnapshot.availablePhysicalBytes / 10ULL;
            totalBudget = memorySnapshot.totalPhysicalBytes / 16ULL;
            maximumBudget = kConservativeMaximumThumbnailCacheCapacityBytes;
            break;
        case hyperbrowse::util::ResourceProfile::Performance:
            availabilityBudget = memorySnapshot.availablePhysicalBytes / 3ULL;
            totalBudget = memorySnapshot.totalPhysicalBytes / 4ULL;
            minimumBudget = kPerformanceThumbnailCacheCapacityBytes;
            maximumBudget = kPerformanceMaximumThumbnailCacheCapacityBytes;
            break;
        case hyperbrowse::util::ResourceProfile::Aggressive:
            availabilityBudget = memorySnapshot.availablePhysicalBytes / 2ULL;
            totalBudget = memorySnapshot.totalPhysicalBytes / 2ULL;
            minimumBudget = kAggressiveThumbnailCacheCapacityBytes;
            maximumBudget = kAggressiveMaximumThumbnailCacheCapacityBytes;
            break;
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            break;
        }

        const std::uint64_t preferredBudget = std::min(availabilityBudget, totalBudget);
        const std::uint64_t clampedBudget = std::clamp(preferredBudget, minimumBudget, maximumBudget);
        return hyperbrowse::util::SaturatingCastToSizeT(clampedBudget);
    }

    std::size_t ResolveWorkerCount(std::size_t requestedWorkerCount,
                                   hyperbrowse::util::ResourceProfile resourceProfile)
    {
        if (requestedWorkerCount != 0)
        {
            return std::max<std::size_t>(requestedWorkerCount, kMinWorkerCount);
        }

        const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
        const std::size_t normalized = hardwareConcurrency == 0 ? kMinWorkerCount : hardwareConcurrency;
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            return std::max<std::size_t>(normalized / 4U, kMinWorkerCount);
        case hyperbrowse::util::ResourceProfile::Performance:
            return std::max<std::size_t>(normalized, kMinWorkerCount);
        case hyperbrowse::util::ResourceProfile::Aggressive:
            return std::max<std::size_t>(normalized + (normalized / 2U), kMinWorkerCount);
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            return std::max<std::size_t>(normalized, kMinWorkerCount);
        }
    }

    std::size_t ResolveRawWorkerCount(std::size_t totalWorkerCount,
                                      hyperbrowse::util::ResourceProfile resourceProfile)
    {
        if (totalWorkerCount <= kMinWorkerCount)
        {
            return 1U;
        }

        std::size_t rawWorkerDivisor = kRawWorkerDivisor;
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            rawWorkerDivisor = kConservativeRawWorkerDivisor;
            break;
        case hyperbrowse::util::ResourceProfile::Performance:
        case hyperbrowse::util::ResourceProfile::Aggressive:
            rawWorkerDivisor = kPerformanceRawWorkerDivisor;
            break;
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            break;
        }

        const std::size_t rawWorkerCount = std::max<std::size_t>(1U, totalWorkerCount / rawWorkerDivisor);
        return std::min(rawWorkerCount, totalWorkerCount - 1U);
    }

    std::size_t ResolveGeneralWorkerCount(std::size_t totalWorkerCount,
                                          hyperbrowse::util::ResourceProfile resourceProfile)
    {
        return totalWorkerCount - ResolveRawWorkerCount(totalWorkerCount, resourceProfile);
    }

    // Allocation-free extension extraction (including the leading '.') for hot-path use.
    std::wstring_view ExtensionView(std::wstring_view path)
    {
        const std::size_t lastSeparator = path.find_last_of(L"\\/");
        const std::size_t searchStart = lastSeparator == std::wstring_view::npos ? 0 : lastSeparator + 1;
        const std::size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring_view::npos || dot < searchStart)
        {
            return {};
        }
        return path.substr(dot);
    }

    bool EqualsIgnoreCase(std::wstring_view lhs, std::wstring_view rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < lhs.size(); ++index)
        {
            if (towlower(lhs[index]) != towlower(rhs[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool IsJpegCacheKey(const hyperbrowse::cache::ThumbnailCacheKey& cacheKey)
    {
        const std::wstring_view extension = ExtensionView(cacheKey.filePath);
        return EqualsIgnoreCase(extension, L".jpg") || EqualsIgnoreCase(extension, L".jpeg");
    }

    bool IsRawCacheKey(const hyperbrowse::cache::ThumbnailCacheKey& cacheKey)
    {
        return hyperbrowse::decode::IsRawFileType(std::wstring(ExtensionView(cacheKey.filePath)));
    }

}

namespace hyperbrowse::services
{
    std::size_t ThumbnailScheduler::ResolveCacheCapacityBytes(std::size_t requestedCapacityBytes,
                                                               util::ResourceProfile resourceProfile)
    {
        return ResolveThumbnailCacheCapacityBytes(requestedCapacityBytes, resourceProfile);
    }

    ThumbnailScheduler::ThumbnailScheduler(std::size_t cacheCapacityBytes,
                                           std::size_t workerCount,
                                           util::ResourceProfile resourceProfile,
                                           std::function<void()> persistenceBeforeJobHook)
        : cache_(ResolveThumbnailCacheCapacityBytes(cacheCapacityBytes, resourceProfile))
        , diskCache_(cache_.CapacityBytes())
        , persistenceBeforeJobHook_(std::move(persistenceBeforeJobHook))
    {
        const std::size_t totalWorkerCount = ResolveWorkerCount(workerCount, resourceProfile);
        const std::size_t rawWorkerCount = ResolveRawWorkerCount(totalWorkerCount, resourceProfile);
        const std::size_t generalWorkerCount = ResolveGeneralWorkerCount(totalWorkerCount, resourceProfile);
        activeDecodeLimit_ = std::max<std::size_t>(1, totalWorkerCount);

        generalWorkers_.reserve(generalWorkerCount);
        for (std::size_t index = 0; index < generalWorkerCount; ++index)
        {
            const bool foregroundLane = index == 0;
            generalWorkers_.emplace_back([this, foregroundLane]()
            {
                WorkerLoop(WorkerKind::General, foregroundLane);
            });
        }

        rawWorkers_.reserve(rawWorkerCount);
        for (std::size_t index = 0; index < rawWorkerCount; ++index)
        {
            rawWorkers_.emplace_back([this]()
            {
                WorkerLoop(WorkerKind::Raw);
            });
        }

        diskPersistenceWorker_ = std::thread([this]()
        {
            DiskPersistenceLoop();
        });
    }

    ThumbnailScheduler::~ThumbnailScheduler()
    {
        {
            std::scoped_lock lock(mutex_);
            shuttingDown_ = true;
            pendingJobs_.clear();
            queuedKeys_.clear();
            inflightJobs_.clear();
            requestedKeys_.clear();
        }

        workAvailable_.notify_all();
        for (std::thread& worker : generalWorkers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        {
            std::scoped_lock lock(diskPersistenceMutex_);
            diskPersistenceShuttingDown_ = true;
        }
        diskPersistenceAvailable_.notify_all();
        if (diskPersistenceWorker_.joinable())
        {
            diskPersistenceWorker_.join();
        }

        for (std::thread& worker : rawWorkers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void ThumbnailScheduler::BindTargetWindow(HWND targetWindow)
    {
        std::scoped_lock lock(mutex_);
        targetWindow_ = targetWindow;
    }

    void ThumbnailScheduler::Schedule(std::uint64_t sessionId, std::uint64_t requestEpoch, std::vector<ThumbnailWorkItem> workItems)
    {
        {
            std::scoped_lock lock(mutex_);
            activeSessionId_ = sessionId;
            activeRequestEpoch_ = requestEpoch;
            foregroundLaneEnabled_ = false;
            requestedKeys_.clear();
            pendingJobs_.clear();
            queuedKeys_.clear();

            for (ThumbnailWorkItem& workItem : workItems)
            {
                requestedKeys_.insert(workItem.cacheKey);
                if (cache_.Find(workItem.cacheKey) || failedKeys_.contains(workItem.cacheKey))
                {
                    continue;
                }

                foregroundLaneEnabled_ = foregroundLaneEnabled_ || workItem.priority == 0;

                const auto inflight = inflightJobs_.find(workItem.cacheKey);
                if (inflight != inflightJobs_.end())
                {
                    int bestInflightPriority = std::numeric_limits<int>::max();
                    bool hasInflightCpuDecode = false;
                    for (const InflightDecode& inflightDecode : inflight->second)
                    {
                        bestInflightPriority = std::min(bestInflightPriority, inflightDecode.priority);
                        hasInflightCpuDecode = hasInflightCpuDecode || inflightDecode.preferCpu;
                    }

                    if (!(workItem.priority < bestInflightPriority
                        || (workItem.preferCpu && !hasInflightCpuDecode)))
                    {
                        continue;
                    }

                    hyperbrowse::util::IncrementCounter(L"thumbnail.promote.inflight_override");
                    if (workItem.preferCpu)
                    {
                        hyperbrowse::util::IncrementCounter(L"thumbnail.promote.inflight_cpu_override");
                    }
                }

                const bool jobIsRaw = IsRawCacheKey(workItem.cacheKey);
                const bool jobIsJpeg = IsJpegCacheKey(workItem.cacheKey);
                cache::ThumbnailCacheKey insertedKey = workItem.cacheKey;
                pendingJobs_.insert(PendingJob{
                    sessionId,
                    requestEpoch,
                    nextSequence_++,
                    static_cast<std::uint64_t>(GetTickCount64()),
                    std::move(workItem),
                    jobIsRaw,
                    jobIsJpeg,
                });
                queuedKeys_.insert(std::move(insertedKey));
            }
        }

        workAvailable_.notify_all();
    }

    void ThumbnailScheduler::CancelOutstanding()
    {
        std::scoped_lock lock(mutex_);
        ++activeRequestEpoch_;
        pendingJobs_.clear();
        queuedKeys_.clear();
        requestedKeys_.clear();
    }

    void ThumbnailScheduler::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
    {
        cache_.InvalidateFilePaths(filePaths);

        if (filePaths.empty())
        {
            return;
        }

        // Never invalidate the persistent cache inline: it holds a process-wide
        // filesystem mutex and rewrites the entire on-disk index, which routinely
        // blocks for seconds behind concurrent thumbnail stores. Callers include the
        // UI thread, which must not stall on disk I/O.
        {
            std::scoped_lock diskLock(diskPersistenceMutex_);
            if (!diskPersistenceShuttingDown_)
            {
                DiskPersistenceJob job;
                job.kind = DiskPersistenceJob::Kind::Invalidate;
                job.filePaths = filePaths;
                pendingDiskPersistence_.push_back(std::move(job));
            }
        }
        diskPersistenceAvailable_.notify_one();

        std::unordered_set<std::wstring> normalizedPaths;
        normalizedPaths.reserve(filePaths.size());
        for (const std::wstring& filePath : filePaths)
        {
            normalizedPaths.insert(util::NormalizePathForComparison(filePath));
        }

        std::scoped_lock lock(mutex_);
        for (auto iterator = failedKeys_.begin(); iterator != failedKeys_.end();)
        {
            if (!normalizedPaths.contains(util::NormalizePathForComparison(iterator->first.filePath)))
            {
                ++iterator;
                continue;
            }

            const auto key = iterator->first;
            iterator = failedKeys_.erase(iterator);
            failureMessages_.erase(key);
        }
    }

    void ThumbnailScheduler::EnqueueDiskStore(const cache::ThumbnailCacheKey& cacheKey,
                                               std::shared_ptr<const cache::CachedThumbnail> thumbnail)
    {
        if (!thumbnail)
        {
            return;
        }

        {
            std::scoped_lock lock(diskPersistenceMutex_);
            if (diskPersistenceShuttingDown_)
            {
                return;
            }

            DiskPersistenceJob job;
            job.kind = DiskPersistenceJob::Kind::Store;
            job.cacheKey = cacheKey;
            job.thumbnail = std::move(thumbnail);
            pendingDiskPersistence_.push_back(std::move(job));
        }
        diskPersistenceAvailable_.notify_one();
    }

    void ThumbnailScheduler::DiskPersistenceLoop()
    {
        for (;;)
        {
            DiskPersistenceJob job;
            {
                std::unique_lock lock(diskPersistenceMutex_);
                diskPersistenceAvailable_.wait(lock, [this]()
                {
                    return diskPersistenceShuttingDown_ || !pendingDiskPersistence_.empty();
                });

                if (diskPersistenceShuttingDown_ && pendingDiskPersistence_.empty())
                {
                    return;
                }

                job = std::move(pendingDiskPersistence_.front());
                pendingDiskPersistence_.pop_front();
            }

            try
            {
                if (persistenceBeforeJobHook_)
                {
                    persistenceBeforeJobHook_();
                }

                util::Stopwatch diskPersistenceTimer;
                if (job.kind == DiskPersistenceJob::Kind::Store)
                {
                    diskCache_.Store(job.cacheKey, std::move(job.thumbnail));
                    util::RecordTiming(L"thumbnail.disk.store", diskPersistenceTimer.ElapsedMilliseconds());
                }
                else
                {
                    diskCache_.InvalidateFilePaths(job.filePaths);
                    util::RecordTiming(L"thumbnail.disk.invalidate", diskPersistenceTimer.ElapsedMilliseconds());
                }
            }
            catch (const std::exception&)
            {
                util::IncrementCounter(L"thumbnail.disk_persistence.exception");
            }
            catch (...)
            {
                util::IncrementCounter(L"thumbnail.disk_persistence.unknown_exception");
            }
        }
    }

    std::shared_ptr<const cache::CachedThumbnail> ThumbnailScheduler::FindCachedThumbnail(const cache::ThumbnailCacheKey& key) const
    {
        return cache_.Find(key);
    }

    void ThumbnailScheduler::SetDiskCacheEnabled(bool enabled)
    {
        std::scoped_lock lock(mutex_);
        diskCacheEnabled_ = enabled;
    }

    void ThumbnailScheduler::SetPressureModeEnabled(bool enabled)
    {
        {
            std::scoped_lock lock(mutex_);
            pressureModeEnabled_ = enabled;
            const std::size_t totalWorkerCount = generalWorkers_.size() + rawWorkers_.size();
            activeDecodeLimit_ = enabled
                ? std::max<std::size_t>(1, totalWorkerCount / 2)
                : std::max<std::size_t>(1, totalWorkerCount);
        }

        workAvailable_.notify_all();
    }

    void ThumbnailScheduler::TrimCacheToBytes(std::size_t targetBytes)
    {
        cache_.TrimToBytes(targetBytes);
    }

    bool ThumbnailScheduler::IsDiskCacheEnabled() const
    {
        std::scoped_lock lock(mutex_);
        return diskCacheEnabled_;
    }

    bool ThumbnailScheduler::HasKnownFailure(const cache::ThumbnailCacheKey& key) const
    {
        std::scoped_lock lock(mutex_);
        return failedKeys_.contains(key);
    }

    decode::ThumbnailDecodeFailureKind ThumbnailScheduler::KnownFailureKind(const cache::ThumbnailCacheKey& key) const
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = failedKeys_.find(key);
        return iterator == failedKeys_.end()
            ? decode::ThumbnailDecodeFailureKind::None
            : iterator->second;
    }

    std::wstring ThumbnailScheduler::KnownFailureMessage(const cache::ThumbnailCacheKey& key) const
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = failureMessages_.find(key);
        return iterator == failureMessages_.end() ? std::wstring{} : iterator->second;
    }

    std::size_t ThumbnailScheduler::CacheBytes() const
    {
        return cache_.CurrentBytes();
    }

    std::size_t ThumbnailScheduler::CacheCapacityBytes() const
    {
        return cache_.CapacityBytes();
    }

    std::size_t ThumbnailScheduler::DiskCacheCapacityBytes() const noexcept
    {
        return diskCache_.CapacityBytes();
    }

    std::size_t ThumbnailScheduler::WorkerCount() const
    {
        return GeneralWorkerCount() + RawWorkerCount();
    }

    std::size_t ThumbnailScheduler::GeneralWorkerCount() const
    {
        return generalWorkers_.size();
    }

    std::size_t ThumbnailScheduler::RawWorkerCount() const
    {
        return rawWorkers_.size();
    }

    bool ThumbnailScheduler::HasDispatchableWorkLocked(WorkerKind kind) const
    {
        return HasDispatchableWorkLocked(kind, false);
    }

    bool ThumbnailScheduler::HasDispatchableWorkLocked(WorkerKind kind, bool foregroundLane) const
    {
        (void)kind;
        if (activeWorkerCount_ >= activeDecodeLimit_)
        {
            return false;
        }

        if (!foregroundLane)
        {
            return !pendingJobs_.empty();
        }

        if (!foregroundLaneEnabled_)
        {
            return false;
        }

        return std::any_of(pendingJobs_.begin(), pendingJobs_.end(), [](const PendingJob& job)
        {
            return job.workItem.priority == 0;
        });
    }

    void ThumbnailScheduler::WorkerLoop(WorkerKind kind, bool foregroundLane)
    {
        while (true)
        {
            std::vector<PendingJob> jobs;
            const bool canUseNvJpegBatch = decode::IsNvJpegAccelerationEnabled()
                && decode::IsNvJpegRuntimeAvailable();
            {
                std::unique_lock lock(mutex_);
                workAvailable_.wait(lock, [this, kind, foregroundLane]()
                {
                    return shuttingDown_ || HasDispatchableWorkLocked(kind, foregroundLane);
                });

                if (shuttingDown_)
                {
                    return;
                }

                // pendingJobs_ is a multiset ordered by (priority asc, sequence asc),
                // so begin() is always the highest-priority oldest job. Selection walks
                // the set in order and collects iterators that we can erase in O(log n).
                std::vector<std::multiset<PendingJob, PendingJobLess>::iterator> selectedIterators;

                const auto selectJobsForKind = [&](WorkerKind jobKind)
                {
                    if (foregroundLane)
                    {
                        for (auto iterator = pendingJobs_.begin(); iterator != pendingJobs_.end(); ++iterator)
                        {
                            if (iterator->workItem.priority == 0)
                            {
                                jobs.push_back(*iterator);
                                selectedIterators.push_back(iterator);
                                break;
                            }
                        }
                        return;
                    }

                    if (jobKind == WorkerKind::Raw)
                    {
                        for (auto iterator = pendingJobs_.begin(); iterator != pendingJobs_.end(); ++iterator)
                        {
                            if (iterator->isRaw)
                            {
                                jobs.push_back(*iterator);
                                selectedIterators.push_back(iterator);
                                break;
                            }
                        }
                        return;
                    }

                    auto headIterator = pendingJobs_.end();
                    for (auto iterator = pendingJobs_.begin(); iterator != pendingJobs_.end(); ++iterator)
                    {
                        if (!iterator->isRaw)
                        {
                            headIterator = iterator;
                            jobs.push_back(*iterator);
                            selectedIterators.push_back(iterator);
                            break;
                        }
                    }

                    if (headIterator != pendingJobs_.end()
                        && canUseNvJpegBatch
                        && !headIterator->workItem.preferCpu
                        && headIterator->isJpeg)
                    {
                        const int highestPriority = headIterator->workItem.priority;
                        auto iterator = std::next(headIterator);
                        while (iterator != pendingJobs_.end() && jobs.size() < kMaxNvJpegBatchSize)
                        {
                            if (iterator->workItem.priority > highestPriority + kNvJpegBatchPriorityWindow)
                            {
                                break;
                            }
                            if (!iterator->isRaw
                                && !iterator->workItem.preferCpu
                                && iterator->isJpeg)
                            {
                                jobs.push_back(*iterator);
                                selectedIterators.push_back(iterator);
                            }
                            ++iterator;
                        }

                        if (jobs.size() < kMinNvJpegBatchSize)
                        {
                            jobs.resize(1);
                            selectedIterators.resize(1);
                        }
                    }
                };

                selectJobsForKind(kind);
                if (jobs.empty() && !foregroundLane)
                {
                    const WorkerKind fallbackKind = kind == WorkerKind::Raw
                        ? WorkerKind::General
                        : WorkerKind::Raw;
                    selectJobsForKind(fallbackKind);
                }

                if (jobs.empty())
                {
                    continue;
                }

                ++activeWorkerCount_;

                for (const PendingJob& job : jobs)
                {
                    queuedKeys_.erase(job.workItem.cacheKey);
                    inflightJobs_[job.workItem.cacheKey].push_back(InflightDecode{
                        job.workItem.priority,
                        job.workItem.preferCpu,
                    });
                }

                for (auto iterator : selectedIterators)
                {
                    pendingJobs_.erase(iterator);
                }
            }

            const std::uint64_t dispatchTickCount = static_cast<std::uint64_t>(GetTickCount64());
            for (const PendingJob& job : jobs)
            {
                const double queueWaitMs = static_cast<double>(dispatchTickCount - job.enqueuedTickCount);
                if (job.isRaw)
                {
                    hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.raw", queueWaitMs);
                    if (job.workItem.priority == 0)
                    {
                        hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.raw.priority0", queueWaitMs);
                    }
                    else if (job.workItem.priority == 1)
                    {
                        hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.raw.priority1", queueWaitMs);
                    }
                    else if (job.workItem.priority == 2)
                    {
                        hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.raw.priority2", queueWaitMs);
                    }
                }
                else
                {
                    hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.general", queueWaitMs);
                    if (job.workItem.priority == 0)
                    {
                        hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.general.priority0", queueWaitMs);
                    }
                    else if (job.workItem.priority == 1)
                    {
                        hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.general.priority1", queueWaitMs);
                    }
                    else if (job.workItem.priority == 2)
                    {
                        hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.general.priority2", queueWaitMs);
                    }
                }
            }

            std::vector<std::shared_ptr<const cache::CachedThumbnail>> thumbnails(jobs.size());
            std::vector<std::size_t> missingIndices;
            std::vector<cache::ThumbnailCacheKey> missingKeys;
            std::vector<decode::ThumbnailDecodeFailureKind> failureKinds(jobs.size(), decode::ThumbnailDecodeFailureKind::None);
            std::vector<std::wstring> failureMessages(jobs.size());
            std::vector<bool> cancelled(jobs.size(), false);
            const bool useDiskCache = IsDiskCacheEnabled();
            bool allowDiskCacheStore = false;
            {
                std::scoped_lock lock(mutex_);
                allowDiskCacheStore = useDiskCache && !pressureModeEnabled_;
            }
            missingIndices.reserve(jobs.size());
            missingKeys.reserve(jobs.size());
            for (std::size_t index = 0; index < jobs.size(); ++index)
            {
                try
                {
                    thumbnails[index] = cache_.Find(jobs[index].workItem.cacheKey);
                    if (!thumbnails[index] && useDiskCache)
                    {
                        util::Stopwatch diskLoadTimer;
                        thumbnails[index] = diskCache_.TryLoad(jobs[index].workItem.cacheKey);
                        util::RecordTiming(L"thumbnail.disk.load", diskLoadTimer.ElapsedMilliseconds());
                        if (thumbnails[index])
                        {
                            cache_.Insert(jobs[index].workItem.cacheKey, thumbnails[index]);
                        }
                    }
                }
                catch (const std::exception&)
                {
                    failureKinds[index] = decode::ThumbnailDecodeFailureKind::DecodeFailed;
                    failureMessages[index] = L"Thumbnail cache lookup raised an unexpected exception.";
                }
                catch (...)
                {
                    failureKinds[index] = decode::ThumbnailDecodeFailureKind::DecodeFailed;
                    failureMessages[index] = L"Thumbnail cache lookup raised an unexpected exception.";
                }
                if (!thumbnails[index])
                {
                    missingIndices.push_back(index);
                    missingKeys.push_back(jobs[index].workItem.cacheKey);
                }
            }

            // Check whether the batch was cancelled while it sat in the worker's local
            // queue. If every job's request epoch is now stale we skip the decode entirely
            // (nothing is observing the result). This stops scroll-driven decode storms
            // from continuing after the user has already moved on.
            std::vector<bool> relevant(jobs.size(), false);
            bool batchStillRelevant = false;
            {
                std::scoped_lock lock(mutex_);
                if (shuttingDown_)
                {
                    if (activeWorkerCount_ > 0)
                    {
                        --activeWorkerCount_;
                    }
                    workAvailable_.notify_all();
                    return;
                }
                for (std::size_t index = 0; index < jobs.size(); ++index)
                {
                    relevant[index] = jobs[index].requestEpoch == activeRequestEpoch_
                        || requestedKeys_.contains(jobs[index].workItem.cacheKey);
                    if (relevant[index])
                    {
                        batchStillRelevant = true;
                    }
                }
            }

            if (!batchStillRelevant)
            {
                hyperbrowse::util::IncrementCounter(L"thumbnail.batch.cancelled_before_decode");
                std::fill(cancelled.begin(), cancelled.end(), true);
                missingKeys.clear();
                missingIndices.clear();
            }
            else
            {
                std::vector<std::size_t> relevantMissingIndices;
                std::vector<cache::ThumbnailCacheKey> relevantMissingKeys;
                relevantMissingIndices.reserve(missingIndices.size());
                relevantMissingKeys.reserve(missingKeys.size());
                for (std::size_t index = 0; index < missingIndices.size(); ++index)
                {
                    const std::size_t jobIndex = missingIndices[index];
                    if (!relevant[jobIndex])
                    {
                        cancelled[jobIndex] = true;
                        util::IncrementCounter(L"thumbnail.batch.cancelled_stale_items");
                        continue;
                    }

                    relevantMissingIndices.push_back(jobIndex);
                    relevantMissingKeys.push_back(std::move(missingKeys[index]));
                }
                missingIndices = std::move(relevantMissingIndices);
                missingKeys = std::move(relevantMissingKeys);
            }

            try
            {
                if (missingKeys.size() > 1)
                {
                    std::vector<std::wstring> decodedFailureMessages;
                    std::vector<decode::ThumbnailDecodeFailureKind> decodedFailureKinds;
                    std::vector<std::shared_ptr<const cache::CachedThumbnail>> decodedBatch = decode::DecodeThumbnailBatch(missingKeys, &decodedFailureMessages, &decodedFailureKinds);
                    for (std::size_t index = 0; index < missingIndices.size(); ++index)
                    {
                        thumbnails[missingIndices[index]] = std::move(decodedBatch[index]);
                        failureMessages[missingIndices[index]] = std::move(decodedFailureMessages[index]);
                        failureKinds[missingIndices[index]] = decodedFailureKinds[index];
                    }
                }
                else if (missingKeys.size() == 1)
                {
                    std::wstring failureMessage;
                    decode::ThumbnailDecodeFailureKind failureKind = decode::ThumbnailDecodeFailureKind::None;
                    if (jobs.front().workItem.preferCpu)
                    {
                        thumbnails[missingIndices.front()] = decode::DecodeThumbnailCpuOnly(missingKeys.front(), &failureMessage, &failureKind);
                    }
                    else
                    {
                        thumbnails[missingIndices.front()] = decode::DecodeThumbnail(missingKeys.front(), &failureMessage, &failureKind);
                    }
                    failureMessages[missingIndices.front()] = std::move(failureMessage);
                    failureKinds[missingIndices.front()] = failureKind;
                }
            }
            catch (const std::exception&)
            {
                util::IncrementCounter(L"thumbnail.decode.exception");
                for (const std::size_t index : missingIndices)
                {
                    failureKinds[index] = decode::ThumbnailDecodeFailureKind::DecodeFailed;
                    failureMessages[index] = L"Thumbnail decoding raised an unexpected exception.";
                }
            }
            catch (...)
            {
                util::IncrementCounter(L"thumbnail.decode.unknown_exception");
                for (const std::size_t index : missingIndices)
                {
                    failureKinds[index] = decode::ThumbnailDecodeFailureKind::DecodeFailed;
                    failureMessages[index] = L"Thumbnail decoding raised an unexpected exception.";
                }
            }

            for (std::size_t index = 0; index < jobs.size(); ++index)
            {
                util::Stopwatch readyNotificationTimer;
                const std::shared_ptr<const cache::CachedThumbnail>& thumbnail = thumbnails[index];
                if (thumbnail)
                {
                    try
                    {
                        cache_.Insert(jobs[index].workItem.cacheKey, thumbnail);
                    }
                    catch (const std::exception&)
                    {
                        util::IncrementCounter(L"thumbnail.cache_store.exception");
                    }
                    catch (...)
                    {
                        util::IncrementCounter(L"thumbnail.cache_store.unknown_exception");
                    }
                }

                bool shouldNotify = false;
                {
                    std::scoped_lock lock(mutex_);
                    if (cancelled[index])
                    {
                        // Cancelled jobs leave failedKeys_ untouched: a cancelled decode
                        // is not a failed decode, and a future Schedule() must be free to
                        // retry without first having to invalidate a poisoned entry.
                    }
                    else if (thumbnail)
                    {
                        failedKeys_.erase(jobs[index].workItem.cacheKey);
                        failureMessages_.erase(jobs[index].workItem.cacheKey);
                    }
                    else
                    {
                        failedKeys_[jobs[index].workItem.cacheKey] = failureKinds[index] == decode::ThumbnailDecodeFailureKind::None
                            ? decode::ThumbnailDecodeFailureKind::DecodeFailed
                            : failureKinds[index];
                        failureMessages_[jobs[index].workItem.cacheKey] = failureMessages[index].empty()
                            ? L"The thumbnail decoder did not provide an error description."
                            : failureMessages[index];
                    }

                    const auto inflight = inflightJobs_.find(jobs[index].workItem.cacheKey);
                    if (inflight != inflightJobs_.end())
                    {
                        auto& activeDecodes = inflight->second;
                        const auto decode = std::find_if(activeDecodes.begin(), activeDecodes.end(), [&](const InflightDecode& inflightDecode)
                        {
                            return inflightDecode.priority == jobs[index].workItem.priority
                                && inflightDecode.preferCpu == jobs[index].workItem.preferCpu;
                        });
                        if (decode != activeDecodes.end())
                        {
                            activeDecodes.erase(decode);
                        }
                        if (activeDecodes.empty())
                        {
                            inflightJobs_.erase(inflight);
                        }
                    }

                    shouldNotify = !cancelled[index]
                        && targetWindow_ != nullptr
                        && jobs[index].sessionId == activeSessionId_
                        && (jobs[index].requestEpoch == activeRequestEpoch_ || requestedKeys_.contains(jobs[index].workItem.cacheKey));
                }

                if (shouldNotify)
                {
                    const bool readyPosted = PostReady(jobs[index].sessionId,
                                                        jobs[index].requestEpoch,
                                                        jobs[index].workItem.modelIndex,
                                                        jobs[index].workItem.cacheKey,
                                                        thumbnail ? thumbnail->SourceWidth() : 0,
                                                        thumbnail ? thumbnail->SourceHeight() : 0,
                                                        thumbnail != nullptr);
                    if (readyPosted)
                    {
                        util::RecordTiming(L"thumbnail.ready.post", readyNotificationTimer.ElapsedMilliseconds());
                    }
                }

                if (thumbnail && allowDiskCacheStore)
                {
                    EnqueueDiskStore(jobs[index].workItem.cacheKey, thumbnail);
                }
            }

            {
                std::scoped_lock lock(mutex_);
                if (activeWorkerCount_ > 0)
                {
                    --activeWorkerCount_;
                }
            }
            workAvailable_.notify_all();
        }
    }

    bool ThumbnailScheduler::PostReady(std::uint64_t sessionId,
                                       std::uint64_t requestEpoch,
                                       int modelIndex,
                                       const cache::ThumbnailCacheKey& cacheKey,
                                       int imageWidth,
                                       int imageHeight,
                                       bool success) const
    {
        HWND targetWindow = nullptr;
        {
            std::scoped_lock lock(mutex_);
            targetWindow = targetWindow_;
        }

        if (!targetWindow)
        {
            return false;
        }

        auto update = std::make_unique<ThumbnailReadyUpdate>();
        update->sessionId = sessionId;
        update->requestEpoch = requestEpoch;
        update->modelIndex = modelIndex;
        update->cacheKey = cacheKey;
        update->imageWidth = imageWidth;
        update->imageHeight = imageHeight;
        update->success = success;

        if (!PostMessageW(targetWindow, kMessageId, 0, reinterpret_cast<LPARAM>(update.get())))
        {
            return false;
        }

        update.release();
        return true;
    }
}