# A3 Persistent Thumbnail Cache Maturity Plan

Status: Complete
Owner: HyperBrowse cache/scheduler path
Started: 2026-09-13
Roadmap item: `A3` in [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md)

Latest progress: A3.0 completed on 2026-09-13. A3.1 completed on 2026-09-13
with independent RAM/free-space-aware persistent capacity, pressure-mode store
suppression coverage, and persistent-cache diagnostics. A3.2 now uses bounded
versioned sharded migration with atomic replacement and restart continuation.
A3.3 now routes all production persistent-cache I/O through one low-priority
scheduler worker, including access-journal flushing, maintenance, and safe
adjacent-job coalescing. A3.4 idle maintenance and source-health handling and
A3.5 per-shard inspector data are implemented. A3.6 completed on 2026-09-14
with dedicated collision/long-key smoke coverage, persistent-cache performance
measurements, cache-worker queue-delay instrumentation, and Release CI gates
for cache and startup latency. The final Release startup run also fixed and
covered stale disk-lookup retry handling, then captured the first thumbnail.

This is the implementation tracker for A3. It is subordinate to the shipped
contract in [PRODUCT_SPEC.md](PRODUCT_SPEC.md) and should be updated as each
slice is implemented and validated.

## Current Baseline

- `DiskThumbnailCache` uses `index.tsv`, `index.journal.tsv`, and a
      `format.version` marker. New entries use `xx/yy/<stable-hash>.bin` shards;
      legacy flat `.thumb` entries remain readable during migration.
- Store and invalidation jobs already run on `ThumbnailScheduler`'s dedicated
  persistence worker.
- Scheduler persistent lookups now run through its persistence worker; direct
      `DiskThumbnailCache` reads remain synchronous for maintenance and tests.
- Access metadata and journal compaction are flushed by the scheduler's single
      low-priority persistence worker.
- Manual statistics, compact, and purge actions run asynchronously through that
      worker, and the inspector reports aggregate and deterministic per-shard data.
- Scheduler shutdown now joins all decode lanes before draining the persistence
      worker.

## Goals and Invariants

- Keep every persistent-cache filesystem operation off the UI thread.
- Keep persistent-cache reads and writes off thumbnail decode workers after the
  asynchronous lookup slice lands.
- Preserve explicit persistent-cache capacity overrides.
- Make automatic capacity deterministic, free-disk-aware, and independent from
  the in-memory thumbnail-cache capacity.
- Preserve valid entries across migration, interruption, malformed input, and
  process restart.
- Reject stale asynchronous lookup results by session, request epoch, and key.
- Preserve current corruption cleanup, normalized keys, and pressure-mode
  suppression of opportunistic writes.
- Do not block visible thumbnail scheduling behind maintenance work.

## Work Queue

### A3.0 Worker Priority and Lifecycle Foundation

- [x] Set the scheduler persistence worker to Windows background mode on entry.
- [x] Remove the competing cache access-persistence worker; the scheduler
      persistence worker owns access-journal flushing and shutdown draining.
- [x] Join all decode workers before stopping and joining the persistence worker.
- [x] Add a focused shutdown assertion that queued stores are either drained or
      explicitly discarded after shutdown begins.

Validation gate: build `HyperBrowseTests`, then run
`HyperBrowseThumbnailPersistenceSmoke` and the full `HyperBrowseSmoke` test.

### A3.1 Capacity Policy and Diagnostics

- [x] Add `ResolvePersistentCacheCapacityBytes` with injectable memory and
      volume-free-space inputs for deterministic tests.
- [x] Preserve the explicit `PersistentThumbnailCacheCapBytes` override.
- [x] Define the automatic budget as `min(total RAM / 2, 8 GB, usable free
      space)` and document the `ResourceProfile` interaction. Conservative
      profile uses total RAM / 4; other profiles use total RAM / 2.
- [x] Add diagnostics for resolved capacity, free-space cap, cache queue depth,
      migration state, shard count, and compaction duration.
- [x] Ensure pressure mode suppresses opportunistic disk stores without
      disabling explicit maintenance operations.

Validation gate: capacity-policy smoke coverage and settings persistence tests.

### A3.2 Sharded Storage and Migration

- [x] Replace flat files with `xx/yy/<stable-hash>.bin` shards.
- [x] Use an explicitly stable hash representation and retain the complete
      normalized key in the index for collision detection.
- [x] Add a versioned manifest/index format and atomic replacement semantics.
- [x] Read the current flat layout during migration.
- [x] Migrate incrementally on the cache worker using bounded batches,
      temporary files, and write-through renames.
- [x] Remove legacy files only after the new manifest is committed.
- [x] Make interrupted migration restartable without losing valid entries.

Validation gate: round-trip, migration, restart, Unicode, long-path, collision,
and malformed-path smoke coverage. `HyperBrowseThumbnailPathSafetySmoke`
covers the dedicated collision and long-key fixtures.

### A3.3 Single Persistent-Cache Worker

- [x] Route lookup, store, invalidation, compaction, purge, and statistics
      requests through one low-priority cache worker.
- [x] Replace synchronous scheduler-worker `TryLoad` calls with prioritized
      asynchronous lookup requests.
- [x] Return cache results through the existing scheduler result path with stale
      session/request/key rejection.
- [x] Coalesce adjacent duplicate stores and invalidations without crossing
      lookup, store, or maintenance ordering barriers.
- [x] Fold access-journal persistence into the worker and remove the competing
      cache-owned I/O thread.
- [x] Keep visible lookups ahead of warm-up and maintenance jobs.

Validation gate: scheduler stale-result, cancellation, ordering, and thread-
affinity smoke coverage. The maintenance smoke additionally covers worker
thread affinity, per-shard statistics, orphan cleanup, purge ordering, and
adjacent invalidation coalescing.

### A3.4 Idle Compaction and Storage Hygiene

- [x] Run compaction only after the cache worker has been idle for a defined
      interval and no visible work is pending.
- [x] Reconcile the index against shard files and remove orphans.
- [x] Remove entries whose source files are confirmed missing while preserving
      entries for access-denied or temporarily unavailable paths.
- [x] Deduplicate logical entries through the authoritative normalized-key map
      and keep migrated files in canonical shards.
- [x] Enforce byte LRU using actual file sizes after compaction.
- [x] Bound orphan cleanup to a finite number of files per compaction pass so
      large caches can resume cleanup across idle windows.
- [x] Preserve crash recovery during repacking and index replacement.

Validation gate: large synthetic cache compaction test, budget enforcement test,
and before/after queue-latency measurement.

### A3.5 Cache Inspector

- [x] Extend statistics with per-shard counts, indexed bytes, file bytes,
      orphan bytes, and missing-entry counts.
- [x] Route inspector queries and maintenance commands through the shared cache
      worker rather than constructing competing cache instances.
- [x] Add per-shard details to the existing asynchronous maintenance dialog.
- [x] Retain one-click Compact and confirmed Purge actions.

Validation gate: deterministic statistics formatting tests and manual dialog
verification with a populated multi-shard cache.

Latest Release evidence: the startup gate captured first-window visibility at
237.39 ms, first-thumbnail paint at 368.65 ms, and the visibility-to-thumbnail
interval at 131.25 ms. The persistent-cache gate recorded 0.52 ms average disk
hit latency, 487.48 stored entries/second, 15.01 ms compaction, 61.09 ms
average and 63 ms maximum cache-worker queue delay, and 3472.52 synthetic
scroll messages/second. The stale disk-lookup retry regression is covered by
`HyperBrowseThumbnailStaleCompletionSmoke`.

### A3.6 Performance, Fuzzing, and Documentation

- [x] Capture baseline and post-change measurements for cache-hit latency, store
      throughput, first-thumbnail latency, scroll throughput, and compaction.
- [x] Add regression thresholds for cache-worker queue delay and first-thumbnail
      presentation.
- [x] Extend persistent-cache fuzzing for manifests, shard paths, malformed
      records, and traversal attempts.
- [x] Update `PRODUCT_SPEC.md`, `docs/architecture.md`, `docs/testing.md`, and
      the README capability table after behavior ships.
- [x] Mark A3 complete in `FUTURE-ROADMAP.md` and archive this detail there.

Benchmark contract: `HyperBrowsePerformanceBenchmark` emits a JSON snapshot
for cache hits, store throughput, compaction, cache-worker queue delay, and
hidden-browser scroll dispatch. `TestPersistentCacheBenchmark.ps1` applies the
CI thresholds of 10 ms average hit latency, 100 stores/second, 5 seconds for
compaction, 500 ms maximum queue delay, and 250 scroll messages/second.
`TestStartupBenchmark.ps1` applies the separate 2500/2500/5000 ms startup and
first-thumbnail thresholds. CI uploads both snapshots for repeated-run
baseline and post-change comparison.

## Completion Criteria

A3 is complete: all work-queue items are checked, focused Debug and Release
smoke gates cover the cache behavior, the cache worker owns all persistent-cache
I/O, migration is exercised across restart and malformed-input cases, and CI
has explicit startup, first-thumbnail, cache-worker, and scroll-performance
regression bars.

## Validation Commands

```powershell
cmake --build --preset debug --target HyperBrowseTests
ctest --preset debug-tests -R HyperBrowseThumbnailPersistenceSmoke --output-on-failure
ctest --preset debug-tests -R HyperBrowseThumbnailPathSafetySmoke --output-on-failure
ctest --preset debug-tests -R HyperBrowseSmoke --output-on-failure
cmake --build --preset debug --target HyperBrowsePerformanceBenchmark
powershell.exe -ExecutionPolicy Bypass -NoProfile -File .\tools\TestPersistentCacheBenchmark.ps1 -BuildDir .\build -Configuration Debug
cmake --build --preset release --target HyperBrowseTests
ctest --preset release-tests -R HyperBrowseThumbnailPersistenceSmoke --output-on-failure
ctest --preset release-tests -R HyperBrowseThumbnailPathSafetySmoke --output-on-failure
cmake --build --preset release --target HyperBrowsePerformanceBenchmark
powershell.exe -ExecutionPolicy Bypass -NoProfile -File .\tools\TestPersistentCacheBenchmark.ps1 -BuildDir .\build -Configuration Release
powershell.exe -ExecutionPolicy Bypass -NoProfile -File .\tools\TestStartupBenchmark.ps1 -BuildDir .\build -Configuration Release
```
