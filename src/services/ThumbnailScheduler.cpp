#include "services/ThumbnailScheduler.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

#include "decode/ImageDecoder.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::size_t kDedicatedRawWorkerCount = 1;
    constexpr std::size_t kMinNvJpegBatchSize = 4;
    constexpr std::size_t kMaxNvJpegBatchSize = 12;
    constexpr int kNvJpegBatchPriorityWindow = 1;

    std::size_t ResolveWorkerCount(std::size_t requestedWorkerCount)
    {
        if (requestedWorkerCount != 0)
        {
            return std::max<std::size_t>(requestedWorkerCount, 2U);
        }

        const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
        const std::size_t normalized = hardwareConcurrency == 0 ? 2U : hardwareConcurrency;
        return std::clamp<std::size_t>(normalized, 2U, 4U);
    }

    std::size_t ResolveGeneralWorkerCount(std::size_t requestedWorkerCount)
    {
        const std::size_t totalWorkerCount = ResolveWorkerCount(requestedWorkerCount);
        return std::max<std::size_t>(1U, totalWorkerCount - kDedicatedRawWorkerCount);
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
        std::wstring extension = fs::path(cacheKey.filePath).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(towlower(value));
        });

        return extension == L".nef" || extension == L".nrw";
    }
}

namespace hyperbrowse::services
{
    ThumbnailScheduler::ThumbnailScheduler(std::size_t cacheCapacityBytes, std::size_t workerCount)
        : cache_(cacheCapacityBytes)
    {
        const std::size_t generalWorkerCount = ResolveGeneralWorkerCount(workerCount);
        generalWorkers_.reserve(generalWorkerCount);
        for (std::size_t index = 0; index < generalWorkerCount; ++index)
        {
            generalWorkers_.emplace_back([this]()
            {
                WorkerLoop(WorkerKind::General);
            });
        }

        rawWorkers_.reserve(kDedicatedRawWorkerCount);
        for (std::size_t index = 0; index < kDedicatedRawWorkerCount; ++index)
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
                if (cache_.Find(workItem.cacheKey))
                {
                    continue;
                }

                if (inflightKeys_.contains(workItem.cacheKey))
                {
                    continue;
                }

                pendingJobs_.push_back(PendingJob{
                    sessionId,
                    requestEpoch,
                    nextSequence_++,
                    std::move(workItem),
                });
                queuedKeys_.insert(pendingJobs_.back().workItem.cacheKey);
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
    }

    std::shared_ptr<const cache::CachedThumbnail> ThumbnailScheduler::FindCachedThumbnail(const cache::ThumbnailCacheKey& key) const
    {
        return cache_.Find(key);
    }

    std::size_t ThumbnailScheduler::CacheBytes() const
    {
        return cache_.CurrentBytes();
    }

    std::size_t ThumbnailScheduler::CacheCapacityBytes() const
    {
        return cache_.CapacityBytes();
    }

    bool ThumbnailScheduler::HasDispatchableWorkLocked(WorkerKind kind) const
    {
        for (const PendingJob& job : pendingJobs_)
        {
            const bool isRaw = IsRawCacheKey(job.workItem.cacheKey);
            if ((kind == WorkerKind::Raw && isRaw)
                || (kind == WorkerKind::General && !isRaw))
            {
                return true;
            }
        }

        return false;
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

                std::vector<PendingJob> orderedJobs = pendingJobs_;
                std::stable_sort(
                    orderedJobs.begin(),
                    orderedJobs.end(),
                    [](const PendingJob& lhs, const PendingJob& rhs)
                    {
                        if (lhs.workItem.priority != rhs.workItem.priority)
                        {
                            return lhs.workItem.priority < rhs.workItem.priority;
                        }

                        return lhs.sequence < rhs.sequence;
                    });

                if (kind == WorkerKind::Raw)
                {
                    for (const PendingJob& candidate : orderedJobs)
                    {
                        if (IsRawCacheKey(candidate.workItem.cacheKey))
                        {
                            jobs.push_back(candidate);
                            break;
                        }
                    }
                }
                else
                {
                    for (const PendingJob& candidate : orderedJobs)
                    {
                        if (!IsRawCacheKey(candidate.workItem.cacheKey))
                        {
                            jobs.push_back(candidate);
                            break;
                        }
                    }

                    if (!jobs.empty()
                        && canUseNvJpegBatch
                        && !jobs.front().workItem.preferCpu
                        && IsJpegCacheKey(jobs.front().workItem.cacheKey))
                    {
                        const int highestPriority = jobs.front().workItem.priority;
                        for (std::size_t index = 1; index < orderedJobs.size() && jobs.size() < kMaxNvJpegBatchSize; ++index)
                        {
                            const PendingJob& candidate = orderedJobs[index];
                            if (candidate.workItem.priority > highestPriority + kNvJpegBatchPriorityWindow)
                            {
                                break;
                            }

                            if (IsRawCacheKey(candidate.workItem.cacheKey)
                                || candidate.workItem.preferCpu
                                || !IsJpegCacheKey(candidate.workItem.cacheKey))
                            {
                                continue;
                            }

                            jobs.push_back(candidate);
                        }

                        if (jobs.size() < kMinNvJpegBatchSize)
                        {
                            jobs.resize(1);
                        }
                    }
                }

                if (jobs.empty())
                {
                    continue;
                }

                std::vector<int> selectedSequences;
                selectedSequences.reserve(jobs.size());
                for (const PendingJob& job : jobs)
                {
                    selectedSequences.push_back(job.sequence);
                    queuedKeys_.erase(job.workItem.cacheKey);
                    inflightKeys_.insert(job.workItem.cacheKey);
                }

                pendingJobs_.erase(
                    std::remove_if(
                        pendingJobs_.begin(),
                        pendingJobs_.end(),
                        [&selectedSequences](const PendingJob& pendingJob)
                        {
                            return std::find(selectedSequences.begin(),
                                             selectedSequences.end(),
                                             pendingJob.sequence) != selectedSequences.end();
                        }),
                    pendingJobs_.end());
            }

            std::vector<std::shared_ptr<const cache::CachedThumbnail>> thumbnails(jobs.size());
            std::vector<std::size_t> missingIndices;
            std::vector<cache::ThumbnailCacheKey> missingKeys;
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

            if (missingKeys.size() > 1)
            {
                std::vector<std::shared_ptr<const cache::CachedThumbnail>> decodedBatch = decode::DecodeThumbnailBatch(missingKeys);
                for (std::size_t index = 0; index < missingIndices.size(); ++index)
                {
                    thumbnails[missingIndices[index]] = std::move(decodedBatch[index]);
                }
            }
            else if (missingKeys.size() == 1)
            {
                if (jobs.front().workItem.preferCpu)
                {
                    thumbnails[missingIndices.front()] = decode::DecodeThumbnailCpuOnly(missingKeys.front());
                }
                else
                {
                    thumbnails[missingIndices.front()] = decode::DecodeThumbnail(missingKeys.front());
                }
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
                    inflightKeys_.erase(jobs[index].workItem.cacheKey);
                    shouldNotify = targetWindow_ != nullptr
                        && thumbnail != nullptr
                        && jobs[index].sessionId == activeSessionId_
                        && (jobs[index].requestEpoch == activeRequestEpoch_ || requestedKeys_.contains(jobs[index].workItem.cacheKey));
                }

                if (shouldNotify)
                {
                    PostReady(jobs[index].sessionId,
                              jobs[index].requestEpoch,
                              jobs[index].workItem.modelIndex,
                              jobs[index].workItem.cacheKey,
                              thumbnail->SourceWidth(),
                              thumbnail->SourceHeight(),
                              true);
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