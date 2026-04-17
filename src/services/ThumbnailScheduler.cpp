#include "services/ThumbnailScheduler.h"

#include <algorithm>
#include <limits>
#include <cwctype>
#include <filesystem>

#include "decode/ImageDecoder.h"
#include "util/Diagnostics.h"
#include "util/PathUtils.h"
#include "util/ResourceSizing.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::size_t kMinWorkerCount = 2;
    constexpr std::size_t kRawWorkerDivisor = 4;
    constexpr std::size_t kMinNvJpegBatchSize = 4;
    constexpr std::size_t kMaxNvJpegBatchSize = 12;
    constexpr int kNvJpegBatchPriorityWindow = 1;
    constexpr std::uint64_t kDefaultThumbnailCacheCapacityBytes = 96ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kMinimumThumbnailCacheCapacityBytes = 128ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kMaximumThumbnailCacheCapacityBytes = 1024ULL * 1024ULL * 1024ULL;

    std::size_t ResolveThumbnailCacheCapacityBytes(std::size_t requestedCapacityBytes)
    {
        if (requestedCapacityBytes != 0)
        {
            return requestedCapacityBytes;
        }

        const auto memorySnapshot = hyperbrowse::util::QueryMemorySnapshot();
        if (!memorySnapshot.IsValid() || memorySnapshot.availablePhysicalBytes == 0)
        {
            return static_cast<std::size_t>(kDefaultThumbnailCacheCapacityBytes);
        }

        const std::uint64_t availabilityBudget = memorySnapshot.availablePhysicalBytes / 5ULL;
        const std::uint64_t totalBudget = memorySnapshot.totalPhysicalBytes / 8ULL;
        const std::uint64_t preferredBudget = std::min(availabilityBudget, totalBudget);
        const std::uint64_t clampedBudget = std::clamp(preferredBudget,
                                                       kMinimumThumbnailCacheCapacityBytes,
                                                       kMaximumThumbnailCacheCapacityBytes);
        return hyperbrowse::util::SaturatingCastToSizeT(clampedBudget);
    }

    std::size_t ResolveWorkerCount(std::size_t requestedWorkerCount)
    {
        if (requestedWorkerCount != 0)
        {
            return std::max<std::size_t>(requestedWorkerCount, kMinWorkerCount);
        }

        const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
        const std::size_t normalized = hardwareConcurrency == 0 ? kMinWorkerCount : hardwareConcurrency;
        return std::max<std::size_t>(normalized, kMinWorkerCount);
    }

    std::size_t ResolveRawWorkerCount(std::size_t totalWorkerCount)
    {
        if (totalWorkerCount <= kMinWorkerCount)
        {
            return 1U;
        }

        const std::size_t rawWorkerCount = std::max<std::size_t>(1U, totalWorkerCount / kRawWorkerDivisor);
        return std::min(rawWorkerCount, totalWorkerCount - 1U);
    }

    std::size_t ResolveGeneralWorkerCount(std::size_t totalWorkerCount)
    {
        return totalWorkerCount - ResolveRawWorkerCount(totalWorkerCount);
    }

    bool IsJpegCacheKey(const hyperbrowse::cache::ThumbnailCacheKey& cacheKey)
    {
        std::wstring extension = fs::path(cacheKey.filePath).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(towlower(value));
        });

        return extension == L".jpg" || extension == L".jpeg";
    }

    bool IsRawCacheKey(const hyperbrowse::cache::ThumbnailCacheKey& cacheKey)
    {
        return hyperbrowse::decode::IsRawFileType(fs::path(cacheKey.filePath).extension().wstring());
    }

}

namespace hyperbrowse::services
{
    ThumbnailScheduler::ThumbnailScheduler(std::size_t cacheCapacityBytes, std::size_t workerCount)
        : cache_(ResolveThumbnailCacheCapacityBytes(cacheCapacityBytes))
    {
        const std::size_t totalWorkerCount = ResolveWorkerCount(workerCount);
        const std::size_t rawWorkerCount = ResolveRawWorkerCount(totalWorkerCount);
        const std::size_t generalWorkerCount = ResolveGeneralWorkerCount(totalWorkerCount);

        generalWorkers_.reserve(generalWorkerCount);
        for (std::size_t index = 0; index < generalWorkerCount; ++index)
        {
            generalWorkers_.emplace_back([this]()
            {
                WorkerLoop(WorkerKind::General);
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

        std::vector<std::wstring> normalizedPaths;
        normalizedPaths.reserve(filePaths.size());
        for (const std::wstring& filePath : filePaths)
        {
            normalizedPaths.push_back(util::NormalizePathForComparison(filePath));
        }

        std::scoped_lock lock(mutex_);
        for (auto iterator = failedKeys_.begin(); iterator != failedKeys_.end();)
        {
            if (std::find(normalizedPaths.begin(), normalizedPaths.end(), util::NormalizePathForComparison(iterator->first.filePath))
                == normalizedPaths.end())
            {
                ++iterator;
                continue;
            }

            iterator = failedKeys_.erase(iterator);
        }
    }

    std::shared_ptr<const cache::CachedThumbnail> ThumbnailScheduler::FindCachedThumbnail(const cache::ThumbnailCacheKey& key) const
    {
        return cache_.Find(key);
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

    std::size_t ThumbnailScheduler::CacheBytes() const
    {
        return cache_.CurrentBytes();
    }

    std::size_t ThumbnailScheduler::CacheCapacityBytes() const
    {
        return cache_.CapacityBytes();
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
        (void)kind;
        return !pendingJobs_.empty();
    }

    void ThumbnailScheduler::WorkerLoop(WorkerKind kind)
    {
        while (true)
        {
            std::vector<PendingJob> jobs;
            const bool canUseNvJpegBatch = decode::IsNvJpegAccelerationEnabled()
                && decode::IsNvJpegRuntimeAvailable();
            {
                std::unique_lock lock(mutex_);
                workAvailable_.wait(lock, [this, kind]()
                {
                    return shuttingDown_ || HasDispatchableWorkLocked(kind);
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
                if (jobs.empty())
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
                }
                else
                {
                    hyperbrowse::util::RecordTiming(L"thumbnail.queue.wait.general", queueWaitMs);
                }
            }

            std::vector<std::shared_ptr<const cache::CachedThumbnail>> thumbnails(jobs.size());
            std::vector<std::size_t> missingIndices;
            std::vector<cache::ThumbnailCacheKey> missingKeys;
            std::vector<decode::ThumbnailDecodeFailureKind> failureKinds(jobs.size(), decode::ThumbnailDecodeFailureKind::None);
            std::vector<bool> cancelled(jobs.size(), false);
            missingIndices.reserve(jobs.size());
            missingKeys.reserve(jobs.size());
            for (std::size_t index = 0; index < jobs.size(); ++index)
            {
                thumbnails[index] = cache_.Find(jobs[index].workItem.cacheKey);
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
            bool batchStillRelevant = false;
            {
                std::scoped_lock lock(mutex_);
                if (shuttingDown_)
                {
                    return;
                }
                for (const PendingJob& job : jobs)
                {
                    if (job.requestEpoch == activeRequestEpoch_
                        || requestedKeys_.contains(job.workItem.cacheKey))
                    {
                        batchStillRelevant = true;
                        break;
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

            if (missingKeys.size() > 1)
            {
                std::vector<decode::ThumbnailDecodeFailureKind> decodedFailureKinds;
                std::vector<std::shared_ptr<const cache::CachedThumbnail>> decodedBatch = decode::DecodeThumbnailBatch(missingKeys, nullptr, &decodedFailureKinds);
                for (std::size_t index = 0; index < missingIndices.size(); ++index)
                {
                    thumbnails[missingIndices[index]] = std::move(decodedBatch[index]);
                    failureKinds[missingIndices[index]] = decodedFailureKinds[index];
                }
            }
            else if (missingKeys.size() == 1)
            {
                decode::ThumbnailDecodeFailureKind failureKind = decode::ThumbnailDecodeFailureKind::None;
                if (jobs.front().workItem.preferCpu)
                {
                    thumbnails[missingIndices.front()] = decode::DecodeThumbnailCpuOnly(missingKeys.front(), nullptr, &failureKind);
                }
                else
                {
                    thumbnails[missingIndices.front()] = decode::DecodeThumbnail(missingKeys.front(), nullptr, &failureKind);
                }
                failureKinds[missingIndices.front()] = failureKind;
            }

            for (std::size_t index = 0; index < jobs.size(); ++index)
            {
                const std::shared_ptr<const cache::CachedThumbnail>& thumbnail = thumbnails[index];
                if (thumbnail)
                {
                    cache_.Insert(jobs[index].workItem.cacheKey, thumbnail);
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
                    }
                    else
                    {
                        failedKeys_[jobs[index].workItem.cacheKey] = failureKinds[index] == decode::ThumbnailDecodeFailureKind::None
                            ? decode::ThumbnailDecodeFailureKind::DecodeFailed
                            : failureKinds[index];
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
                    PostReady(jobs[index].sessionId,
                              jobs[index].requestEpoch,
                              jobs[index].workItem.modelIndex,
                              jobs[index].workItem.cacheKey,
                              thumbnail ? thumbnail->SourceWidth() : 0,
                              thumbnail ? thumbnail->SourceHeight() : 0,
                              thumbnail != nullptr);
                }
            }
        }
    }

    void ThumbnailScheduler::PostReady(std::uint64_t sessionId,
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
            return;
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
            return;
        }

        update.release();
    }
}