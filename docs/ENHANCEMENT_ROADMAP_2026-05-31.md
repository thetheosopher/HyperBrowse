Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse (native Windows image browser/viewer)

# HyperBrowse — Enhancement Roadmap

## Summary Table

| Item | Category | Perspective | Effort | Impact | Area |
|------|----------|-------------|--------|--------|------|
| QW1 Allocation-free thumbnail cache key hash/equality | Performance | Eng | S | High | `util/PathUtils.h`, `cache/ThumbnailCache.*` |
| QW2 Drop redundant pre-normalize in `ThumbnailCache::Find` | Performance | Eng | XS | Medium | `cache/ThumbnailCache.cpp` |
| QW3 Reuse allocation-free comparator in `FilePathsEqual` | Performance | Eng | XS | Low | `browser/BrowserModel.cpp` |
| QW4 Manual-extension scan in scheduler ext checks | Performance | Eng | S | Medium | `services/ThumbnailScheduler.cpp` |
| QW5 Log-file creation failure one-time notice | Reliability | Eng | XS | Low | `util/Log.cpp` |
| MP1 Unit tests for path/cache/scheduler pure logic | Reliability | Eng | M | High | `tests/` |
| MP2 Startup-benchmark perf regression gate in CI | Reliability | Eng | M | High | `tools/`, CI |
| MP3 Consolidate `std::async` services onto bounded pool | Performance | Eng | M | Medium | `services/*`, `util/BackgroundExecutor.h` |
| MP4 Async-first folder-tree population | UX | Both | M | Medium | `services/FolderTreeEnumerationService` |
| MP5 Faceted rating/tag filter in browser | Feature | Product | M | High | `browser/`, `services/UserMetadataStore` |
| SI1 Cross-folder search & lightweight catalog | Feature | Product | L | High | `browser/`, `services/` |
| SI2 Decompose `MainWindow.cpp` | DX | Eng | L | Medium | `ui/MainWindow.cpp` |
| SI3 Sharded/binary disk thumbnail cache index | Performance | Eng | L | Medium | `cache/DiskThumbnailCache.cpp` |
| DR1 Remove redundant normalization paths after QW1 | Debt | Eng | S | Low | `browser/BrowserModel.cpp`, `util/PathUtils.h` |
| DEP1 Documented LibRaw update cadence + pin | Security | Eng | S | Medium | `external/libraw` |

---

## Quick Wins (< 1 day, high leverage)

### QW1 — Allocation-free thumbnail cache key hash & equality  ✅ implemented in this pass
- **Problem:** `ThumbnailCacheKey::operator==` normalizes both operands and the hasher normalizes
  again, each allocating a `std::wstring` and doing full-string transforms, on a per-scroll hot path.
- **Approach:** Added `NormalizedPathEquals`, `NormalizedPathHash`, `NormalizedPathLength`,
  `NormalizedPathCharAt` to `util/PathUtils.h` (no allocation, normalize on the fly). `operator==`
  now compares cheap integer fields first, then `NormalizedPathEquals`. The hasher uses
  `NormalizedPathHash`.
- **Success metric:** zero heap allocations per cache compare/hash; lower CPU during fast scroll
  (validate with `thumbnail.queue.wait.*` timings and a profiler on `ThumbnailCache::Find`).

### QW2 — Drop redundant pre-normalize in `ThumbnailCache::Find`  ✅ implemented
- Once hashing/equality normalize internally, the pre-normalized key copy in `Find` is dead cost.
  Removed the copy; pass the key through directly.

### QW3 — Reuse allocation-free comparator in `FilePathsEqual`  ✅ implemented
- `FilePathsEqual` now calls `NormalizedPathEquals` instead of allocating two normalized strings.

### QW4 — Manual extension scan in scheduler ext checks
- Replace `fs::path(cacheKey.filePath).extension().wstring()` in `IsJpegCacheKey`/`IsRawCacheKey`
  with a last-`.`/last-separator scan over `std::wstring_view`. Removes an `fs::path` allocation per
  scheduled item. Keep behavior identical (case-insensitive compare against the existing suffix set).

### QW5 — One-time notice on log-file creation failure
- In `AppendLineToFile`, on `CreateFileW == INVALID_HANDLE_VALUE`, emit a single
  `OutputDebugStringW` notice guarded by a `std::once_flag` so on-disk logging loss is visible in a
  debugger without spamming.

> Additional quick win candidates (not yet done): surface decode-failure reason on thumbnail hover
> (uses existing `failedKeys_` kind); add a "≥N stars" quick filter toggle to the View menu.

---

## High-Impact Medium Projects (1–2 weeks)

### MP1 — Unit tests for pure hot-path logic
- Cover `NormalizePathForComparison` / `NormalizedPathEquals` / `NormalizedPathHash` invariants
  (idempotence, slash equivalence, case-insensitivity, trailing-separator trim, hash↔equals
  consistency), `ThumbnailCacheKey` equality/hash, and `PendingJobLess` ordering. Lock in QW1 and
  prevent silent regressions. Wire into `HyperBrowseTests`.

### MP2 — Startup-benchmark perf regression gate
- The app already emits `startup-benchmark.json` (first window visible, first thumbnail painted).
  Add a CI step that runs the benchmark on a fixed fixture folder and fails if metrics exceed a
  budget (with tolerance). Store baselines in `tools/`.

### MP3 — Consolidate `std::async` services onto a bounded pool
- Route `FolderEnumerationService`, `FolderTreeEnumerationService`, `FileOperationService`, and
  `BatchConvertService` through a shared `BackgroundExecutor` (or one per category) instead of
  `std::async(launch::async)`. Caps thread churn during rapid folder switching. Preserve existing
  cancellation epochs.

### MP4 — Async-first folder-tree population
- Make the initial tree load fully asynchronous with a lightweight loading affordance, removing the
  brief UI stall on large/slow trees. Reuse the existing enumeration update/marshal pattern.

### MP5 — Faceted rating/tag filter
- Add View-menu filters (rating ≥ N, has tag X) that operate on the already-cached
  `UserMetadataStore` data, composed with the existing filename filter. High user value, low new
  infrastructure.

---

## Strategic Initiatives (multi-week)

### SI1 — Cross-folder search & lightweight catalog
- **Problem:** Search/filter is per-folder only; users expect cross-folder find by name/rating/tag/date.
- **Approach:** Build a background-indexed lightweight catalog (SQLite or an append-only index) keyed
  by normalized path, fed by enumeration + metadata + user-metadata. Add a search surface and result
  view that reuses the thumbnail pipeline.
- **Risks/open questions:** index storage location & size; incremental update vs. folder watch;
  privacy of indexed paths; first-index cost on huge libraries.
- **Success metrics:** time-to-first-result < 500 ms on a warm index; index size < a small % of
  thumbnail cache; correctness vs. live enumeration.

### SI2 — Decompose `MainWindow.cpp` (12.3k LOC)
- **Problem:** One giant TU concentrates menu wiring, command handling, state persistence, and
  orchestration — slow incremental builds, heavy review surface.
- **Approach:** Extract a declarative menu/command table, a `WindowStatePersistence` unit, and a
  folder-watch glue unit; keep `MainWindow` as a thin coordinator. No behavior change.
- **Risks:** large mechanical diff; mitigate with the QW1 unit-test base + smoke suite before/after.
- **Success metrics:** incremental rebuild time after a menu edit drops materially; `MainWindow.cpp`
  under ~4k LOC.

### SI3 — Sharded / binary disk thumbnail cache index
- **Problem:** Single global filesystem mutex + whole-file TSV rewrite limit disk-cache parallelism
  and scale.
- **Approach:** Shard the lock by cache-filename hash and replace the TSV index with an append-only
  binary index plus periodic compaction.
- **Risks:** cache-format migration; need a version field + safe rebuild-on-mismatch path.
- **Success metrics:** parallel disk reads across browser + details panel; lower compact latency on
  large caches.

---

## Debt Retirement Candidates
- **DR1:** After QW1, audit and simplify remaining `NormalizePathForComparison` allocations in
  comparison-only paths (`PathHasPrefix`, selection compares) to the allocation-free comparator.
- Fix the stray over-indentation in `IsSupportedImageExtension`
  ([src/browser/BrowserModel.cpp](../src/browser/BrowserModel.cpp#L304-L309)).

## Dependency Upgrade Path (ordered by risk/value)
1. **LibRaw** — establish a pinned known-good revision + documented update cadence; keep RAW decode
   out-of-process by default (highest CVE surface, lowest-risk mitigation already in place).
2. **CUDA/nvJPEG redist** — verify bundled version currency during each release packaging run.
3. **CMake/toolchain** — already on VS2026 + CMake 4.2; track preset requirements in repo memory.

---

## Prompt Handoff

> Ready-to-use prompt for an implementing model/engineer:
>
> "You are implementing the HyperBrowse enhancement roadmap dated 2026-05-31. Work item by item,
> smallest-risk first. Quick wins QW1–QW3 are already implemented in `src/util/PathUtils.h`,
> `src/cache/ThumbnailCache.h/.cpp`, and `src/browser/BrowserModel.cpp` — verify them and add the
> MP1 unit tests that lock in `NormalizedPathEquals`/`NormalizedPathHash` (hash↔equals consistency,
> slash/case/trailing-separator invariants) plus `ThumbnailCacheKey` equality. Then implement QW4
> (manual extension scan in `IsJpegCacheKey`/`IsRawCacheKey` in
> `src/services/ThumbnailScheduler.cpp` lines ~138–149) and QW5 (one-time log-failure notice in
> `src/util/Log.cpp` `AppendLineToFile`). Build with the `vs2026-x64` preset, target `HyperBrowse`,
> and run `ctest --preset debug-tests`. For MP3, route `std::async` services
> (`FolderEnumerationService`, `FolderTreeEnumerationService`, `FileOperationService`,
> `BatchConvertService`) onto `util::BackgroundExecutor`, preserving the existing request-epoch
> cancellation. Do not change the on-disk thumbnail cache filename hash in
> `DiskThumbnailCache::BuildCacheFileName` — it is persistent. Validate each change with a build and
> the smoke suite before moving on."
</content>
