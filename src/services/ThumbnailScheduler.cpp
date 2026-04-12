#include "services/ThumbnailScheduler.h"

#include <algorithm>

#include "decode/ImageDecoder.h"

namespace
{
    std::size_t ResolveWorkerCount(std::size_t requestedWorkerCount)
    {
        if (requestedWorkerCount != 0)
        {
            return requestedWorkerCount;
        }

        const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
        const std::size_t normalized = hardwareConcurrency == 0 ? 2U : hardwareConcurrency;
        return std::clamp<std::size_t>(normalized, 1U, 4U);
    }
}

namespace hyperbrowse::services
{
    ThumbnailScheduler::ThumbnailScheduler(std::size_t cacheCapacityBytes, std::size_t workerCount)
        : cache_(cacheCapacityBytes)
    {
        const std::size_t resolvedWorkerCount = ResolveWorkerCount(workerCount);
        workers_.reserve(resolvedWorkerCount);
        for (std::size_t index = 0; index < resolvedWorkerCount; ++index)
        {
            workers_.emplace_back([this]()
            {
                WorkerLoop();
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
        for (std::thread& worker : workers_)
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

    void ThumbnailScheduler::WorkerLoop()
    {
        while (true)
        {
            PendingJob job;
            {
                std::unique_lock lock(mutex_);
                workAvailable_.wait(lock, [this]()
                {
                    return shuttingDown_ || !pendingJobs_.empty();
                });

                if (shuttingDown_)
                {
                    return;
                }

                const auto jobIterator = std::min_element(
                    pendingJobs_.begin(),
                    pendingJobs_.end(),
                    [](const PendingJob& lhs, const PendingJob& rhs)
                    {
                        if (lhs.workItem.priority != rhs.workItem.priority)
                        {
                            return lhs.workItem.priority < rhs.workItem.priority;
                        }

                        return lhs.sequence < rhs.sequence;
                    });

                job = *jobIterator;
                queuedKeys_.erase(job.workItem.cacheKey);
                inflightKeys_.insert(job.workItem.cacheKey);
                pendingJobs_.erase(jobIterator);
            }

            std::shared_ptr<const cache::CachedThumbnail> thumbnail = cache_.Find(job.workItem.cacheKey);
            if (!thumbnail)
            {
                thumbnail = decode::DecodeThumbnail(job.workItem.cacheKey);
                if (thumbnail)
                {
                    cache_.Insert(job.workItem.cacheKey, thumbnail);
                }
            }

            bool shouldNotify = false;
            {
                std::scoped_lock lock(mutex_);
                inflightKeys_.erase(job.workItem.cacheKey);
                shouldNotify = targetWindow_ != nullptr
                    && thumbnail != nullptr
                    && job.sessionId == activeSessionId_
                    && (job.requestEpoch == activeRequestEpoch_ || requestedKeys_.contains(job.workItem.cacheKey));
            }

            if (shouldNotify)
            {
                PostReady(job.sessionId,
                          job.requestEpoch,
                          job.workItem.modelIndex,
                          job.workItem.cacheKey,
                          thumbnail->SourceWidth(),
                          thumbnail->SourceHeight(),
                          true);
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