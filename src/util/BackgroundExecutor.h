#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace hyperbrowse::util
{
    // Fixed-size FIFO worker pool for short-lived background work. Used in place of
    // per-task std::async calls, which spawn a fresh OS thread for every invocation
    // and have no upper bound on concurrent threads under burst load.
    //
    // Tasks are std::function<void()>. The executor takes no opinion on cancellation;
    // callers should embed their own atomic-flag / generation checks inside the task.
    // On destruction, all queued (not-yet-running) tasks are dropped, in-flight tasks
    // run to completion, and worker threads are joined.
    class BackgroundExecutor
    {
    public:
        explicit BackgroundExecutor(std::size_t workerCount, std::size_t maxPendingTaskCount = 0)
            : maxPendingTaskCount_(maxPendingTaskCount)
        {
            const std::size_t resolvedWorkerCount = workerCount == 0 ? std::size_t{1} : workerCount;
            workers_.reserve(resolvedWorkerCount);
            for (std::size_t index = 0; index < resolvedWorkerCount; ++index)
            {
                workers_.emplace_back([this]() { WorkerLoop(); });
            }
        }

        ~BackgroundExecutor()
        {
            Shutdown();
        }

        void Shutdown() noexcept
        {
            std::call_once(shutdownOnce_, [this]()
            {
                {
                    std::scoped_lock lock(mutex_);
                    shuttingDown_ = true;
                    tasks_.clear();
                }
                condition_.notify_all();
                for (std::thread& worker : workers_)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }
            });
        }

        BackgroundExecutor(const BackgroundExecutor&) = delete;
        BackgroundExecutor& operator=(const BackgroundExecutor&) = delete;

        // Enqueue a task. Returns false if the executor is shutting down and the task
        // was not accepted.
        bool Post(std::function<void()> task)
        {
            if (!task)
            {
                return false;
            }

            {
                std::scoped_lock lock(mutex_);
                if (shuttingDown_
                    || (maxPendingTaskCount_ != 0 && tasks_.size() >= maxPendingTaskCount_))
                {
                    rejectedTaskCount_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                tasks_.push_back(std::move(task));
                if (tasks_.size() > peakPendingTaskCount_)
                {
                    peakPendingTaskCount_ = tasks_.size();
                }
            }
            condition_.notify_one();
            return true;
        }

        void SetMaxPendingTaskCount(std::size_t maxPendingTaskCount)
        {
            std::scoped_lock lock(mutex_);
            maxPendingTaskCount_ = maxPendingTaskCount;
        }

        std::size_t WorkerCount() const noexcept
        {
            return workers_.size();
        }

        std::size_t PendingTaskCount() const noexcept
        {
            std::scoped_lock lock(mutex_);
            return tasks_.size();
        }

        std::size_t ActiveTaskCount() const noexcept
        {
            return activeTaskCount_.load(std::memory_order_acquire);
        }

        std::size_t RejectedTaskCount() const noexcept
        {
            return rejectedTaskCount_.load(std::memory_order_acquire);
        }

        std::size_t PeakPendingTaskCount() const noexcept
        {
            std::scoped_lock lock(mutex_);
            return peakPendingTaskCount_;
        }

    private:
        void WorkerLoop()
        {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, [this]() { return shuttingDown_ || !tasks_.empty(); });
                    if (shuttingDown_ && tasks_.empty())
                    {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }

                if (task)
                {
                    activeTaskCount_.fetch_add(1, std::memory_order_acq_rel);
                    try
                    {
                        task();
                    }
                    catch (const std::exception&)
                    {
                        OutputDebugStringW(L"HyperBrowse background task threw an exception.\n");
                    }
                    catch (...)
                    {
                        OutputDebugStringW(L"HyperBrowse background task threw an unknown exception.\n");
                    }
                    activeTaskCount_.fetch_sub(1, std::memory_order_acq_rel);
                }
            }
        }

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<std::function<void()>> tasks_;
        std::vector<std::thread> workers_;
        std::once_flag shutdownOnce_;
        std::size_t maxPendingTaskCount_{};
        std::size_t peakPendingTaskCount_{};
        bool shuttingDown_{false};
        std::atomic<std::size_t> activeTaskCount_{};
        std::atomic<std::size_t> rejectedTaskCount_{};
    };
}
