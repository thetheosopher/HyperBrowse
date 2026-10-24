#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunThumbnailSchedulerScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowsePrompt5Scheduler");
        const fs::path offscreenPngPath = root.Root() / L"offscreen.png";
        const fs::path visibleJpegPath = root.Root() / L"visible.jpg";
        const fs::path visibleGifPath = root.Root() / L"visible.gif";
        const fs::path offscreenTiffPath = root.Root() / L"offscreen.tif";

        WriteTestImage(offscreenPngPath, TestImageFormat::Png, 96, 48);
        WriteTestImage(visibleJpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(visibleGifPath, TestImageFormat::Gif, 36, 18);
        WriteTestImage(offscreenTiffPath, TestImageFormat::Tiff, 18, 54);

        const auto offscreenPngKey = MakeCacheKey(offscreenPngPath, 10);
        const auto visibleJpegKey = MakeCacheKey(visibleJpegPath, 11);
        const auto visibleGifKey = MakeCacheKey(visibleGifPath, 12);
        const auto offscreenTiffKey = MakeCacheKey(offscreenTiffPath, 13);

        hyperbrowse::services::ThumbnailScheduler scheduler(8ULL * 1024ULL * 1024ULL, 1);
        scheduler.BindTargetWindow(hwnd);
        scheduler.SetDiskCacheEnabled(false);

        ResetThumbnailResult(state, 7);
        const std::vector<hyperbrowse::services::ThumbnailWorkItem> requests{
            {0, offscreenPngKey, 1},
            {1, visibleJpegKey, 0},
            {2, visibleGifKey, 0},
            {3, offscreenTiffKey, 1},
        };
        scheduler.Schedule(7, 1, requests);

        Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.readyCount >= 4; }, 5000),
               "Thumbnail scheduler decode work timed out");
        Expect(!state->thumbnailResult.readyPaths.empty(), "Thumbnail scheduler did not post any ready messages");
        const std::wstring& firstReadyPath = state->thumbnailResult.readyPaths.front();
        Expect(firstReadyPath == visibleJpegPath.wstring() || firstReadyPath == visibleGifPath.wstring(),
               "Thumbnail scheduler did not prioritize visible work ahead of offscreen work");
        Expect(scheduler.FindCachedThumbnail(offscreenPngKey) != nullptr, "Offscreen PNG thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(visibleJpegKey) != nullptr, "Visible JPEG thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(visibleGifKey) != nullptr, "Visible GIF thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(offscreenTiffKey) != nullptr, "Offscreen TIFF thumbnail was not cached");

        const auto visibleJpegThumbnail = scheduler.FindCachedThumbnail(visibleJpegKey);
        Expect(visibleJpegThumbnail->SourceWidth() == 48 && visibleJpegThumbnail->SourceHeight() == 24,
               "Thumbnail scheduler did not retain the JPEG source dimensions in cache");
        const auto offscreenPngThumbnail = scheduler.FindCachedThumbnail(offscreenPngKey);
        Expect(offscreenPngThumbnail->SourceWidth() == 96 && offscreenPngThumbnail->SourceHeight() == 48,
               "Thumbnail scheduler did not retain the PNG source dimensions in cache");

        ResetThumbnailResult(state, 7);
        scheduler.Schedule(7, 2, requests);
        PumpMessagesFor(300);
        Expect(state->thumbnailResult.readyCount == 0, "Cached thumbnails should not be re-decoded on the next schedule pass");

        const auto diagnostics = hyperbrowse::util::CaptureDiagnosticsSnapshot();
        const auto readyTiming = std::find_if(diagnostics.timings.begin(), diagnostics.timings.end(), [](const auto& timing)
        {
            return timing.name == L"thumbnail.ready.post";
        });
        Expect(readyTiming != diagnostics.timings.end() && readyTiming->count > 0,
               "Thumbnail scheduler did not record ready-message posting latency");

        const auto visibleQueueTiming = std::find_if(diagnostics.timings.begin(), diagnostics.timings.end(), [](const auto& timing)
        {
            return timing.name == L"thumbnail.queue.wait.general.priority0";
        });
        Expect(visibleQueueTiming != diagnostics.timings.end() && visibleQueueTiming->count > 0,
               "Thumbnail scheduler did not record visible general-worker queue latency");

        const auto scaleTiming = std::find_if(diagnostics.timings.begin(), diagnostics.timings.end(), [](const auto& timing)
        {
            return timing.name == L"thumbnail.scale";
        });
        Expect(scaleTiming != diagnostics.timings.end() && scaleTiming->count > 0,
               "Thumbnail decode did not record unified scale timing");
    }

    void RunThumbnailReadyBeforePersistenceScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseThumbnailReadyBeforePersistence");
        const fs::path imagePath = root.Root() / L"visible.jpg";
        WriteTestImage(imagePath, TestImageFormat::Jpeg, 24, 48, 6);
        const auto key = MakeCacheKey(imagePath, static_cast<std::uint64_t>(GetTickCount64()));

        const auto diagnosticsCounterValue = [](std::wstring_view name)
        {
            const hyperbrowse::util::DiagnosticsSnapshot snapshot = hyperbrowse::util::CaptureDiagnosticsSnapshot();
            for (const hyperbrowse::util::DiagnosticCounterRow& row : snapshot.counters)
            {
                if (row.name == name)
                {
                    return row.value;
                }
            }
            return std::uint64_t{};
        };
        const std::uint64_t completedStoreCountBefore = diagnosticsCounterValue(L"thumbnail.disk.store.completed");

        std::atomic<bool> persistenceStarted{false};
        std::atomic<bool> releasePersistence{false};
        {
            hyperbrowse::services::ThumbnailScheduler scheduler(
                8ULL * 1024ULL * 1024ULL,
                1,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&]()
                {
                    persistenceStarted.store(true, std::memory_order_release);
                    while (!releasePersistence.load(std::memory_order_acquire))
                    {
                        Sleep(1);
                    }
                });
            scheduler.BindTargetWindow(hwnd);

            ResetThumbnailResult(state, 9);
            scheduler.Schedule(9, 1, {{0, key, 0, true}});
            const bool readyPostedWhilePersistenceWasBlocked = PumpMessagesUntil([&]()
            {
                return state->thumbnailResult.readyCount >= 1
                    && persistenceStarted.load(std::memory_order_acquire);
            }, 15000);
            releasePersistence.store(true, std::memory_order_release);

            Expect(readyPostedWhilePersistenceWasBlocked,
                   "Thumbnail ready update did not arrive while persistent storage was deliberately blocked (ready="
                       + std::to_string(state->thumbnailResult.readyCount)
                       + ", failed=" + std::to_string(state->thumbnailResult.failedCount)
                       + ", persistenceStarted=" + (persistenceStarted.load(std::memory_order_acquire) ? "true" : "false")
                       + ")");
        }

        Expect(diagnosticsCounterValue(L"thumbnail.disk.store.completed") > completedStoreCountBefore,
               "Thumbnail scheduler shutdown did not drain the queued persistent store");
        {
            hyperbrowse::cache::DiskThumbnailCache verificationCache(8ULL * 1024ULL * 1024ULL);
            Expect(verificationCache.TryLoad(key) != nullptr,
                 "Thumbnail scheduler shutdown did not persist the requested cache key");
        }

        std::atomic<int> decodeCount{0};
        {
            hyperbrowse::services::ThumbnailScheduler scheduler(
                8ULL * 1024ULL * 1024ULL,
                1,
                hyperbrowse::util::ResourceProfile::Balanced,
                {},
                [&]()
                {
                    decodeCount.fetch_add(1, std::memory_order_relaxed);
                });
            scheduler.BindTargetWindow(hwnd);
            ResetThumbnailResult(state, 9);
                        const std::uint64_t lookupHitsBefore = diagnosticsCounterValue(L"thumbnail.disk.lookup.hit");
            scheduler.Schedule(9, 1, {{0, key, 0, true}});
            Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.readyCount >= 1; }, 5000),
                   "Thumbnail scheduler did not return a persistent-cache lookup result");
                  Expect(diagnosticsCounterValue(L"thumbnail.disk.lookup.hit") > lookupHitsBefore,
                        "Thumbnail scheduler did not find the persisted key through its cache worker");
            Expect(decodeCount.load(std::memory_order_relaxed) == 0,
                        "Persistent-cache hit incorrectly reached the decode worker");
        }
    }

    void RunThumbnailStaleCompletionScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseThumbnailStaleCompletion");
        const fs::path imagePath = root.Root() / L"stale.jpg";
        WriteTestImage(imagePath, TestImageFormat::Jpeg, 24, 48, 6);
        const std::uint64_t cacheGeneration = static_cast<std::uint64_t>(GetTickCount64());
        const auto key = MakeCacheKey(imagePath, cacheGeneration);

        std::mutex decodeMutex;
        std::condition_variable decodeStartedCondition;
        std::condition_variable decodeReleasedCondition;
        bool decodeStarted = false;
        bool releaseDecode = false;

        {
            hyperbrowse::services::ThumbnailScheduler scheduler(
                8ULL * 1024ULL * 1024ULL,
                1,
                hyperbrowse::util::ResourceProfile::Balanced,
                {},
                [&]()
                {
                    std::unique_lock lock(decodeMutex);
                    decodeStarted = true;
                    decodeStartedCondition.notify_all();
                    decodeReleasedCondition.wait(lock, [&]()
                    {
                        return releaseDecode;
                    });
                });
            scheduler.BindTargetWindow(hwnd);

            ResetThumbnailResult(state, 60);
            scheduler.Schedule(60, 1, {{0, key, 0, true}});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(decodeMutex);
                return decodeStarted;
            }, 5000), "Thumbnail stale-completion scenario never entered the decode barrier");

             const auto runtimeStatistics = scheduler.GetRuntimeStatistics();
             Expect(runtimeStatistics.inflightDecodeCount == 1,
                 "Thumbnail scheduler did not report the blocked decode as in flight");
             Expect(runtimeStatistics.activeWorkerCount == 1,
                 "Thumbnail scheduler did not report its active worker");
             Expect(runtimeStatistics.activeDecodeLimit >= runtimeStatistics.activeWorkerCount,
                 "Thumbnail scheduler reported an invalid active decode limit");

            scheduler.Schedule(60, 2, {});
            {
                std::scoped_lock lock(decodeMutex);
                releaseDecode = true;
            }
            decodeReleasedCondition.notify_one();

            Expect(PumpMessagesUntil([&]()
            {
                return scheduler.FindCachedThumbnail(key) != nullptr;
            }, 5000), "A stale successful thumbnail was not retained in the memory cache");
            PumpMessagesFor(100);
            Expect(state->thumbnailResult.readyCount == 0 && state->thumbnailResult.failedCount == 0,
                   "A stale thumbnail completion incorrectly posted a ready update");
        }

        hyperbrowse::cache::DiskThumbnailCache diskCache(8ULL * 1024ULL * 1024ULL);
        Expect(diskCache.TryLoad(key) != nullptr,
               "A stale successful thumbnail was not retained in the persistent cache");

        {
            const fs::path lookupImagePath = root.Root() / L"stale-lookup.png";
            WriteTestImage(lookupImagePath, TestImageFormat::Png, 48, 24);
            const auto lookupKey = MakeCacheKey(lookupImagePath, cacheGeneration + 1);

            std::mutex persistenceMutex;
            std::condition_variable persistenceCondition;
            bool persistenceEntered = false;
            bool releasePersistence = false;
            const auto diagnosticsCounterValue = [](std::wstring_view name)
            {
                const hyperbrowse::util::DiagnosticsSnapshot snapshot = hyperbrowse::util::CaptureDiagnosticsSnapshot();
                for (const hyperbrowse::util::DiagnosticCounterRow& row : snapshot.counters)
                {
                    if (row.name == name)
                    {
                        return row.value;
                    }
                }
                return std::uint64_t{};
            };
            const std::uint64_t staleLookupCountBefore = diagnosticsCounterValue(L"thumbnail.disk.lookup.stale");

            hyperbrowse::services::ThumbnailScheduler scheduler(
                8ULL * 1024ULL * 1024ULL,
                1,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&]()
                {
                    std::unique_lock lock(persistenceMutex);
                    persistenceEntered = true;
                    persistenceCondition.notify_all();
                    persistenceCondition.wait(lock, [&]()
                    {
                        return releasePersistence;
                    });
                },
                {},
                8ULL * 1024ULL * 1024ULL,
                (root.Root() / L"stale-lookup-cache").wstring());
            scheduler.BindTargetWindow(hwnd);

            Expect(scheduler.QueuePersistentCacheStatistics([](bool, hyperbrowse::cache::DiskThumbnailCache::Statistics)
            {
            }), "Failed to queue the stale-lookup persistence barrier");
            {
                std::unique_lock lock(persistenceMutex);
                const bool barrierStarted = persistenceCondition.wait_for(lock, std::chrono::seconds(5), [&]()
                {
                    return persistenceEntered;
                });
                if (!barrierStarted)
                {
                    releasePersistence = true;
                }
                if (!barrierStarted)
                {
                    persistenceCondition.notify_all();
                }
                Expect(barrierStarted, "Stale-lookup persistence barrier did not start");
            }

            ResetThumbnailResult(state, 60);
            scheduler.Schedule(60, 1, {{0, lookupKey, 0, true}});
            const bool lookupQueued = PumpMessagesUntil([&]()
            {
                return scheduler.GetRuntimeStatistics().inflightDecodeCount == 1;
            }, 5000);
            scheduler.Schedule(60, 2, {});
            {
                std::scoped_lock lock(persistenceMutex);
                releasePersistence = true;
            }
            persistenceCondition.notify_all();

            Expect(lookupQueued, "Stale-lookup scenario did not reach the disk lookup worker");
            Expect(PumpMessagesUntil([&]()
            {
                return diagnosticsCounterValue(L"thumbnail.disk.lookup.stale") > staleLookupCountBefore;
            }, 5000), "Stale disk lookup was not rejected after its request epoch changed");

            ResetThumbnailResult(state, 60);
            scheduler.Schedule(60, 3, {{0, lookupKey, 0, true}});
            Expect(PumpMessagesUntil([&]()
            {
                return state->thumbnailResult.readyCount >= 1;
            }, 5000), "A current-epoch retry did not decode after a stale disk lookup");
        }
    }

    void RunThumbnailDiskLookupRescheduleScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseThumbnailDiskLookupReschedule");
        const fs::path blockerPath = root.Root() / L"blocker.png";
        const fs::path lookupPath = root.Root() / L"lookup.png";
        WriteTestImage(blockerPath, TestImageFormat::Png, 48, 24);
        WriteTestImage(lookupPath, TestImageFormat::Png, 48, 24);
        const auto blockerKey = MakeCacheKey(blockerPath, 1);
        const auto lookupKey = MakeCacheKey(lookupPath, 2);

        std::mutex decodeMutex;
        std::condition_variable decodeCondition;
        bool decodeEntered = false;
        bool releaseDecode = false;
        std::mutex persistenceMutex;
        std::condition_variable persistenceCondition;
        bool persistenceEntered = false;
        bool releasePersistence = false;

        {
            hyperbrowse::services::ThumbnailScheduler scheduler(
                8ULL * 1024ULL * 1024ULL,
                1,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&]()
                {
                    std::unique_lock lock(persistenceMutex);
                    persistenceEntered = true;
                    persistenceCondition.notify_all();
                    persistenceCondition.wait(lock, [&]()
                    {
                        return releasePersistence;
                    });
                },
                [&]()
                {
                    std::unique_lock lock(decodeMutex);
                    decodeEntered = true;
                    decodeCondition.notify_all();
                    decodeCondition.wait(lock, [&]()
                    {
                        return releaseDecode;
                    });
                },
                8ULL * 1024ULL * 1024ULL,
                (root.Root() / L"cache").wstring());
            scheduler.BindTargetWindow(hwnd);
            scheduler.SetDiskCacheEnabled(false);

            ResetThumbnailResult(state, 61);
            scheduler.Schedule(61, 1, {{0, blockerKey, 0, true}});
            const bool decodeBarrierStarted = PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(decodeMutex);
                return decodeEntered;
            }, 5000);
            if (!decodeBarrierStarted)
            {
                {
                    std::scoped_lock lock(decodeMutex);
                    releaseDecode = true;
                }
                decodeCondition.notify_all();
                Expect(false, "Disk-lookup reschedule scenario never entered its decode barrier");
            }

            const bool statisticsQueued = scheduler.QueuePersistentCacheStatistics(
                [](bool, hyperbrowse::cache::DiskThumbnailCache::Statistics)
                {
                });
            bool persistenceBarrierStarted = false;
            if (statisticsQueued)
            {
                std::unique_lock lock(persistenceMutex);
                persistenceBarrierStarted = persistenceCondition.wait_for(lock, std::chrono::seconds(5), [&]()
                {
                    return persistenceEntered;
                });
            }
            if (!statisticsQueued || !persistenceBarrierStarted)
            {
                {
                    std::scoped_lock lock(persistenceMutex);
                    releasePersistence = true;
                }
                persistenceCondition.notify_all();
                {
                    std::scoped_lock lock(decodeMutex);
                    releaseDecode = true;
                }
                decodeCondition.notify_all();
                Expect(false, "Disk-lookup reschedule scenario could not block the persistence worker");
            }

            scheduler.SetDiskCacheEnabled(true);
            scheduler.Schedule(61, 2, {{1, lookupKey, 0, true}});
            const bool lookupReachedPersistence = PumpMessagesUntil([&]()
            {
                const auto statistics = scheduler.GetRuntimeStatistics();
                return statistics.inflightDecodeCount == 2 && statistics.activeWorkerCount == 1;
            }, 5000);
            if (!lookupReachedPersistence)
            {
                {
                    std::scoped_lock lock(persistenceMutex);
                    releasePersistence = true;
                }
                persistenceCondition.notify_all();
                {
                    std::scoped_lock lock(decodeMutex);
                    releaseDecode = true;
                }
                decodeCondition.notify_all();
                Expect(false, "Disk-lookup reschedule scenario did not queue its target lookup");
            }

            scheduler.Schedule(61, 3, {
                {0, blockerKey, 0, true},
                {1, lookupKey, 0, true},
            });
            scheduler.SetPressureModeEnabled(true);
            {
                std::scoped_lock lock(persistenceMutex);
                releasePersistence = true;
            }
            persistenceCondition.notify_all();

            const bool lookupReturnedToPendingQueue = PumpMessagesUntil([&]()
            {
                const auto statistics = scheduler.GetRuntimeStatistics();
                return statistics.pendingJobCount == 1
                    && statistics.inflightDecodeCount == 2
                    && statistics.activeWorkerCount == 1
                    && statistics.activeDecodeLimit == 1;
            }, 5000);
            if (!lookupReturnedToPendingQueue)
            {
                {
                    std::scoped_lock lock(decodeMutex);
                    releaseDecode = true;
                }
                decodeCondition.notify_all();
                Expect(false, "Disk-lookup reschedule scenario did not reach the completed-lookup queue");
            }

            scheduler.Schedule(61, 4, {
                {0, blockerKey, 0, true},
                {1, lookupKey, 0, true},
            });
            {
                std::scoped_lock lock(decodeMutex);
                releaseDecode = true;
            }
            decodeCondition.notify_all();

            const std::wstring lookupPathString = lookupPath.wstring();
            Expect(PumpMessagesUntil([&]()
            {
                return std::find(state->thumbnailResult.readyPaths.begin(),
                                 state->thumbnailResult.readyPaths.end(),
                                 lookupPathString) != state->thumbnailResult.readyPaths.end();
            }, 10000), "A rescheduled thumbnail was stranded after its completed disk lookup was replaced");
        }
    }

        void RunThumbnailSchedulerFailureScenario(HWND hwnd, TestWindowState* state)
        {
         TempFolder root(L"HyperBrowsePrompt5SchedulerFailure");
         const fs::path missingRawPath = root.Root() / L"missing.nef";
         const auto missingRawKey = MakeCacheKey(missingRawPath, 1);

         hyperbrowse::services::ThumbnailScheduler scheduler(8ULL * 1024ULL * 1024ULL, 1);
         scheduler.BindTargetWindow(hwnd);
         scheduler.SetDiskCacheEnabled(false);

         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 1, {{0, missingRawKey, 0, true}});
         Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.failedCount >= 1; }, 5000),
             "Thumbnail scheduler did not surface a failed decode update");
         Expect(state->thumbnailResult.failedPaths.size() == 1
                 && state->thumbnailResult.failedPaths.front() == missingRawPath.wstring(),
             "Thumbnail scheduler reported the wrong failed path");
         Expect(scheduler.FindCachedThumbnail(missingRawKey) == nullptr,
             "Failed thumbnail decodes should not populate the thumbnail cache");
         Expect(scheduler.HasKnownFailure(missingRawKey),
             "Thumbnail scheduler did not retain the failed-thumbnail state");
         Expect(scheduler.KnownFailureKind(missingRawKey) == hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed,
             "Thumbnail scheduler misclassified a generic decode failure");
         Expect(!scheduler.KnownFailureMessage(missingRawKey).empty(),
             "Thumbnail scheduler did not retain the decoder failure details");

         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 2, {{0, missingRawKey, 0, true}});
         PumpMessagesFor(300);
         Expect(state->thumbnailResult.readyCount == 0 && state->thumbnailResult.failedCount == 0,
             "Known failed thumbnails should not be requeued until the file path is invalidated");

         scheduler.InvalidateFilePaths({missingRawPath.wstring()});
         Expect(!scheduler.HasKnownFailure(missingRawKey),
             "Invalidating a file path should clear the scheduler's known-failure state");

         const fs::path pressureImagePath = root.Root() / L"pressure.png";
         WriteTestImage(pressureImagePath, TestImageFormat::Png, 48, 24);
         const auto pressureKey = MakeCacheKey(pressureImagePath, 2);
         const auto diagnosticsCounterValue = [](std::wstring_view name)
         {
             const hyperbrowse::util::DiagnosticsSnapshot snapshot = hyperbrowse::util::CaptureDiagnosticsSnapshot();
             for (const hyperbrowse::util::DiagnosticCounterRow& row : snapshot.counters)
             {
                 if (row.name == name)
                 {
                     return row.value;
                 }
             }
             return std::uint64_t{};
         };
         const std::uint64_t suppressedStoresBefore = diagnosticsCounterValue(L"thumbnail.disk.store.suppressed_pressure");
         const std::uint64_t completedStoresBefore = diagnosticsCounterValue(L"thumbnail.disk.store.completed");
         scheduler.SetDiskCacheEnabled(true);
         scheduler.SetPressureModeEnabled(true);
         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 3, {{0, pressureKey, 0, true}});
         Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.readyCount >= 1; }, 5000),
             "Pressure-mode thumbnail decode did not complete");
         PumpMessagesFor(100);
         Expect(diagnosticsCounterValue(L"thumbnail.disk.store.suppressed_pressure") > suppressedStoresBefore,
             "Pressure mode did not suppress the opportunistic persistent store");
         scheduler.TrimCacheToBytes(1);
         Expect(scheduler.CacheBytes() <= 1,
             "Explicit thumbnail-cache trimming was disabled while pressure mode was active");
         scheduler.SetPressureModeEnabled(false);
         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 4, {{0, pressureKey, 0, true}});
         Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.readyCount >= 1; }, 5000),
             "Thumbnail decode did not resume after pressure mode ended");
         Expect(PumpMessagesUntil([&]()
         {
             return diagnosticsCounterValue(L"thumbnail.disk.store.completed") > completedStoresBefore;
         }, 5000), "Persistent store did not resume after pressure mode ended");
        }

        void RunThumbnailSchedulerWorkerAllocationScenario()
        {
         hyperbrowse::services::ThumbnailScheduler minimumScheduler(8ULL * 1024ULL * 1024ULL, 1);
         Expect(minimumScheduler.WorkerCount() == 2,
             "Thumbnail scheduler should preserve two lanes when configured below the minimum worker count");
         Expect(minimumScheduler.GeneralWorkerCount() == 1,
             "Thumbnail scheduler should preserve a general worker when configured below the minimum worker count");
         Expect(minimumScheduler.RawWorkerCount() == 1,
             "Thumbnail scheduler should preserve a RAW worker when configured below the minimum worker count");

         hyperbrowse::services::ThumbnailScheduler scaledScheduler(8ULL * 1024ULL * 1024ULL, 8);
         Expect(scaledScheduler.WorkerCount() == 8,
             "Thumbnail scheduler did not preserve the requested worker count for a larger pool");
         Expect(scaledScheduler.GeneralWorkerCount() == 6,
             "Thumbnail scheduler did not allocate the expected number of general workers");
         Expect(scaledScheduler.RawWorkerCount() == 2,
             "Thumbnail scheduler did not scale the RAW worker allocation above one lane");
         Expect(scaledScheduler.CacheCapacityBytes() == 8ULL * 1024ULL * 1024ULL,
             "Thumbnail scheduler did not preserve the configured memory cache capacity");
         Expect(scaledScheduler.DiskCacheCapacityBytes()
                    == hyperbrowse::services::ThumbnailScheduler::ResolvePersistentCacheCapacityBytes(
                        0,
                        hyperbrowse::util::ResourceProfile::Balanced),
             "Thumbnail scheduler did not resolve the automatic persistent cache capacity independently");

         constexpr std::size_t sixteenGigabytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
         hyperbrowse::services::ThumbnailScheduler largeBudgetScheduler(
             sixteenGigabytes,
             2,
             hyperbrowse::util::ResourceProfile::Balanced,
             {},
             {},
             sixteenGigabytes);
         Expect(largeBudgetScheduler.CacheCapacityBytes() == sixteenGigabytes,
             "Thumbnail scheduler did not preserve a 16 GiB memory cache capacity");
         Expect(largeBudgetScheduler.DiskCacheCapacityBytes() == sixteenGigabytes,
             "Thumbnail scheduler did not preserve a 16 GiB persistent cache capacity");
        }

        void RunBackgroundServiceRetirementScenario()
        {
            struct RetirementState
            {
                std::mutex mutex;
                std::condition_variable condition;
                bool destructionStarted{};
                bool releaseDestruction{};
                bool destructionCompleted{};
            };

            struct BlockingDestruction
            {
                explicit BlockingDestruction(std::shared_ptr<RetirementState> sharedState)
                    : retirementState(std::move(sharedState))
                {
                }

                ~BlockingDestruction()
                {
                    std::unique_lock lock(retirementState->mutex);
                    retirementState->destructionStarted = true;
                    retirementState->condition.notify_all();
                    retirementState->condition.wait(lock, [&]()
                    {
                        return retirementState->releaseDestruction;
                    });
                    retirementState->destructionCompleted = true;
                    lock.unlock();
                    retirementState->condition.notify_all();
                }

                std::shared_ptr<RetirementState> retirementState;
            };

            auto state = std::make_shared<RetirementState>();
            hyperbrowse::util::BackgroundExecutor reaper(1, 0, true);
            std::thread unblocker([state]()
            {
                std::unique_lock lock(state->mutex);
                if (!state->condition.wait_for(lock, std::chrono::seconds(5), [&]()
                    {
                        return state->destructionStarted;
                    }))
                {
                    state->releaseDestruction = true;
                    lock.unlock();
                    state->condition.notify_all();
                    return;
                }

                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                lock.lock();
                state->releaseDestruction = true;
                lock.unlock();
                state->condition.notify_all();
            });

            const auto start = std::chrono::steady_clock::now();
            const bool accepted = reaper.PostDestruction(std::make_unique<BlockingDestruction>(state));
            const auto elapsed = std::chrono::steady_clock::now() - start;
            unblocker.join();

            Expect(accepted, "Background executor rejected service retirement");
            Expect(elapsed < std::chrono::milliseconds(200),
                "Posting service retirement blocked while the service destructor was busy");
            {
                std::unique_lock lock(state->mutex);
                Expect(state->condition.wait_for(lock, std::chrono::seconds(5), [&]()
                    {
                        return state->destructionCompleted;
                    }),
                    "Background service retirement did not complete after the blocked work was released");
            }
        }

        void RunThumbnailPersistenceMaintenanceScenario()
        {
         TempFolder root(L"HyperBrowseThumbnailPersistenceMaintenance");
         const fs::path imagePath = root.Root() / L"maintenance.png";
         const fs::path cacheRoot = root.Root() / L"cache";
         WriteTestImage(imagePath, TestImageFormat::Png, 32, 16);
         const auto key = MakeCacheKey(imagePath, 71);
         std::wstring decodeError;
         auto thumbnail = hyperbrowse::decode::DecodeThumbnailCpuOnly(key, &decodeError);
         Expect(thumbnail != nullptr, "Failed to create a thumbnail for persistent maintenance coverage");
         {
             hyperbrowse::cache::DiskThumbnailCache seedCache(8ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
             seedCache.Store(key, thumbnail);
         }

         constexpr std::size_t expectedOrphanCount = 300;
         std::vector<fs::path> orphanPaths;
         orphanPaths.reserve(expectedOrphanCount);
         for (std::size_t index = 0; index < expectedOrphanCount; ++index)
         {
             const fs::path orphanPath = cacheRoot / (L"orphan-" + std::to_wstring(index) + L".bin");
             std::ofstream orphanStream(orphanPath, std::ios::binary);
             orphanStream << "orphan";
             orphanPaths.push_back(orphanPath);
         }

         hyperbrowse::services::ThumbnailScheduler scheduler(
             8ULL * 1024ULL * 1024ULL,
             1,
             hyperbrowse::util::ResourceProfile::Balanced,
             {},
             {},
             8ULL * 1024ULL * 1024ULL,
             cacheRoot.wstring());

         const std::thread::id callerThread = std::this_thread::get_id();
         std::mutex callbackMutex;
         std::condition_variable callbackCondition;
         std::size_t callbackCount = 0;
         std::thread::id callbackThread;
         bool callbackSucceeded = false;
         hyperbrowse::cache::DiskThumbnailCache::Statistics callbackStatistics;
         const auto waitForCallback = [&](std::size_t expectedCount)
         {
             std::unique_lock lock(callbackMutex);
             Expect(callbackCondition.wait_for(lock, std::chrono::seconds(5), [&]()
             {
                 return callbackCount >= expectedCount;
             }), "Persistent cache maintenance callback timed out");
         };

         std::size_t expectedCallbackCount = 0;
         {
             std::scoped_lock lock(callbackMutex);
             expectedCallbackCount = callbackCount + 1;
         }
         Expect(scheduler.QueuePersistentCacheStatistics(
                    [&](bool succeeded, hyperbrowse::cache::DiskThumbnailCache::Statistics statistics)
                    {
                        std::scoped_lock lock(callbackMutex);
                        callbackSucceeded = succeeded;
                        callbackThread = std::this_thread::get_id();
                        callbackStatistics = std::move(statistics);
                        ++callbackCount;
                        callbackCondition.notify_all();
                    },
                    true),
                "Failed to queue persistent cache statistics");
         waitForCallback(expectedCallbackCount);
         Expect(callbackSucceeded, "Persistent cache statistics query failed");
         Expect(callbackThread != callerThread, "Persistent cache statistics ran on the caller thread");
         Expect(callbackStatistics.indexedEntryCount == 1,
                "Persistent cache statistics did not report the seeded entry");
         Expect(callbackStatistics.orphanFileCount == expectedOrphanCount,
                "Persistent cache statistics did not report every orphan file");
         Expect(callbackStatistics.sourceFilePaths.size() == 1
                    && callbackStatistics.sourceFilePaths.front()
                        == hyperbrowse::util::NormalizePathForComparison(key.filePath),
                "Persistent cache statistics did not return source paths for the requested background scan");
          const auto indexedShard = std::find_if(callbackStatistics.shards.begin(),
                                  callbackStatistics.shards.end(),
                                  [](const auto& shard)
                                  {
                                   return shard.indexedEntryCount == 1
                                    && shard.indexedBytes > 0;
                                  });
          Expect(indexedShard != callbackStatistics.shards.end(),
              "Persistent cache statistics did not report the seeded entry in a shard");

         {
             std::scoped_lock lock(callbackMutex);
             expectedCallbackCount = callbackCount + 1;
         }
         Expect(scheduler.QueuePersistentCacheMaintenance(
                    false,
                    [&](bool succeeded)
                    {
                        std::scoped_lock lock(callbackMutex);
                        callbackSucceeded = succeeded;
                        callbackThread = std::this_thread::get_id();
                        ++callbackCount;
                        callbackCondition.notify_all();
                    }),
                "Failed to queue persistent cache compaction");
         waitForCallback(expectedCallbackCount);
         Expect(callbackSucceeded, "Persistent cache compaction failed");
         Expect(std::all_of(orphanPaths.begin(), orphanPaths.end(), [](const fs::path& orphanPath)
                {
                    return !fs::exists(orphanPath);
                }),
                "Persistent cache compaction did not remove every orphan file");

         {
             std::scoped_lock lock(callbackMutex);
             expectedCallbackCount = callbackCount + 1;
         }
         Expect(scheduler.QueuePersistentCacheStatistics(
                    [&](bool succeeded, hyperbrowse::cache::DiskThumbnailCache::Statistics statistics)
                    {
                        std::scoped_lock lock(callbackMutex);
                        callbackSucceeded = succeeded;
                        callbackThread = std::this_thread::get_id();
                        callbackStatistics = std::move(statistics);
                        ++callbackCount;
                        callbackCondition.notify_all();
                    }),
                "Failed to queue post-compaction persistent cache statistics");
         waitForCallback(expectedCallbackCount);
         Expect(callbackSucceeded, "Post-compaction persistent cache statistics query failed");
         Expect(callbackStatistics.orphanFileCount == 0,
                "Persistent cache statistics still reported orphan files after compaction");

         {
             std::scoped_lock lock(callbackMutex);
             expectedCallbackCount = callbackCount + 1;
         }
         Expect(scheduler.QueuePersistentCacheMaintenance(
                    true,
                    [&](bool succeeded)
                    {
                        std::scoped_lock lock(callbackMutex);
                        callbackSucceeded = succeeded;
                        callbackThread = std::this_thread::get_id();
                        ++callbackCount;
                        callbackCondition.notify_all();
                    }),
                "Failed to queue persistent cache purge");
         waitForCallback(expectedCallbackCount);
         Expect(callbackSucceeded, "Persistent cache purge failed");

         {
             std::scoped_lock lock(callbackMutex);
             expectedCallbackCount = callbackCount + 1;
         }
         Expect(scheduler.QueuePersistentCacheStatistics(
                    [&](bool succeeded, hyperbrowse::cache::DiskThumbnailCache::Statistics statistics)
                    {
                        std::scoped_lock lock(callbackMutex);
                        callbackSucceeded = succeeded;
                        callbackThread = std::this_thread::get_id();
                        callbackStatistics = std::move(statistics);
                        ++callbackCount;
                        callbackCondition.notify_all();
                    }),
                "Failed to queue post-purge persistent cache statistics");
         waitForCallback(expectedCallbackCount);
         Expect(callbackSucceeded, "Post-purge persistent cache statistics query failed");
         Expect(callbackStatistics.indexedEntryCount == 0 && callbackStatistics.cacheFileCount == 0,
                "Persistent cache purge did not remove indexed entries and cache files");
         Expect(callbackStatistics.sourceFilePaths.empty(),
                "Persistent cache statistics returned source paths when no background scan was requested");
          Expect(callbackStatistics.shards.empty(),
              "Persistent cache purge did not clear per-shard statistics");

         std::mutex barrierMutex;
         std::condition_variable barrierCondition;
         bool barrierEntered = false;
         bool releaseBarrier = false;
         hyperbrowse::services::ThumbnailScheduler coalescingScheduler(
             8ULL * 1024ULL * 1024ULL,
             1,
             hyperbrowse::util::ResourceProfile::Balanced,
             [&]()
             {
                 std::unique_lock lock(barrierMutex);
                 if (!barrierEntered)
                 {
                     barrierEntered = true;
                     barrierCondition.notify_all();
                     barrierCondition.wait(lock, [&]()
                     {
                         return releaseBarrier;
                     });
                 }
             },
             {},
             8ULL * 1024ULL * 1024ULL,
             (root.Root() / L"coalescing-cache").wstring());

         const auto readDiagnosticCounter = [](std::wstring_view name)
         {
             const auto diagnostics = hyperbrowse::util::CaptureDiagnosticsSnapshot();
             for (const auto& counter : diagnostics.counters)
             {
                 if (counter.name == name)
                 {
                     return counter.value;
                 }
             }
             return std::uint64_t{};
         };
         const std::uint64_t coalescedInvalidationsBefore =
             readDiagnosticCounter(L"persistent_cache.invalidate.coalesced");
         Expect(coalescingScheduler.QueuePersistentCacheStatistics(
                    [](bool, hyperbrowse::cache::DiskThumbnailCache::Statistics)
                    {
                    }),
                "Failed to queue the coalescing barrier job");
         {
             std::unique_lock lock(barrierMutex);
             Expect(barrierCondition.wait_for(lock, std::chrono::seconds(5), [&]()
             {
                 return barrierEntered;
             }), "Persistent cache coalescing barrier did not start");
         }
         coalescingScheduler.InvalidateFilePaths({imagePath.wstring()});
         coalescingScheduler.InvalidateFilePaths({imagePath.wstring()});
         {
             std::scoped_lock lock(barrierMutex);
             releaseBarrier = true;
         }
         barrierCondition.notify_all();

         std::mutex completionMutex;
         std::condition_variable completionCondition;
         bool completed = false;
         Expect(coalescingScheduler.QueuePersistentCacheStatistics(
                    [&](bool succeeded, hyperbrowse::cache::DiskThumbnailCache::Statistics)
                    {
                        std::scoped_lock lock(completionMutex);
                        completed = succeeded;
                        completionCondition.notify_all();
                    }),
                "Failed to queue the coalescing completion job");
         {
             std::unique_lock lock(completionMutex);
             Expect(completionCondition.wait_for(lock, std::chrono::seconds(5), [&]()
             {
                 return completed;
             }), "Persistent cache coalescing completion timed out");
         }
         Expect(readDiagnosticCounter(L"persistent_cache.invalidate.coalesced")
                    == coalescedInvalidationsBefore + 1,
                "Adjacent persistent cache invalidations were not coalesced");
        }

        void RunPersistentThumbnailCacheCapacityScenario()
        {
         constexpr std::uint64_t oneGiB = 1024ULL * 1024ULL * 1024ULL;
         constexpr std::uint64_t eightGiB = 8ULL * oneGiB;
         constexpr std::uint64_t thirtyTwoGiB = 32ULL * oneGiB;

         Expect(hyperbrowse::util::ResolvePersistentThumbnailCacheCapacityBytes(
                    hyperbrowse::util::ResourceProfile::Balanced,
                    thirtyTwoGiB,
                    20ULL * oneGiB,
                    true) == eightGiB,
                "Persistent cache capacity did not enforce the 8 GiB automatic maximum");
         Expect(hyperbrowse::util::ResolvePersistentThumbnailCacheCapacityBytes(
                    hyperbrowse::util::ResourceProfile::Balanced,
                    8ULL * oneGiB,
                    3ULL * oneGiB,
                    true) == 3ULL * oneGiB,
                "Persistent cache capacity did not honor the available disk-space cap");
         Expect(hyperbrowse::util::ResolvePersistentThumbnailCacheCapacityBytes(
                    hyperbrowse::util::ResourceProfile::Conservative,
                    16ULL * oneGiB,
                    20ULL * oneGiB,
                    true) == 4ULL * oneGiB,
                "Conservative persistent cache capacity did not use its reduced RAM budget");
         Expect(hyperbrowse::util::ResolvePersistentThumbnailCacheCapacityBytes(
                    hyperbrowse::util::ResourceProfile::Balanced,
                    8ULL * oneGiB,
                    0,
                    false) == 4ULL * oneGiB,
                "Persistent cache capacity incorrectly treated an unknown disk budget as zero");
         Expect(hyperbrowse::services::ThumbnailScheduler::ResolvePersistentCacheCapacityBytes(
                    16ULL * oneGiB,
                    hyperbrowse::util::ResourceProfile::Balanced) == 16ULL * oneGiB,
                "Explicit persistent cache capacity override was not preserved");
        }
}
