# HyperBrowse Enhancement Roadmap

Review conducted: 2026-07-30
Reviewer: GitHub Copilot
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse

## Summary

| Item | Category | Perspective | Effort | Impact | Area |
| --- | --- | --- | --- | --- | --- |
| QW1 Validate persistent thumbnail payloads | Reliability/Security | Engineering | XS | High | `src/cache/DiskThumbnailCache.cpp` |
| QW2 Preserve split folder-rename state | Reliability | Both | S | High | `src/services/FolderWatchService.*` |
| QW3 Add malformed-cache regression tests | Reliability | Engineering | S | High | `tests/` |
| QW4 Wire startup budgets into CI | Performance | Both | S | High | `.github/workflows/`, `tools/` |
| QW5 Correct architecture/UI/backlog docs | DX | Both | S | Medium | `specs/` |
| QW6 Add worker exception containment | Reliability | Engineering | S | Medium | `src/services/ThumbnailScheduler.cpp` |
| MP1 Redesign persistent cache index/I/O | Performance | Engineering | M | High | `src/cache/`, `src/services/` |
| MP2 Bound background service execution | Performance/Reliability | Engineering | M | Medium | `src/services/`, `src/util/` |
| MP3 Define cancellable file-operation shutdown | Reliability/UX | Both | M | High | `FileOperationService`, `MainWindow` |
| MP4 Establish CI quality/performance matrix | Reliability | Engineering | M | High | CI, tests, tools |
| MP5 Split focused test targets and fixtures | DX/Reliability | Engineering | M | Medium | `tests/` |
| MP6 Saved structured filters | Feature/UX | Product | M | Medium | browser/UI/settings |
| SI1 Decompose MainWindow by ownership | DX/Reliability | Engineering | L | High | `src/ui/MainWindow.*` |
| SI2 Pro compare and color-management path | Feature | Both | L | High | viewer/decode/render |
| SI3 Search/smart-folder product decision | Feature | Product | L | High | browser/services |
| SI4 Format frontier | Feature | Both | L | Medium | decode/viewer |

---

## Quick Wins: Less Than One Day

### QW1: Validate persistent thumbnail payloads

- **Category:** Reliability, Security
- **Perspective:** Engineering
- **Effort:** XS
- **Impact:** High
- **Area:** [src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L371-L383)
- **Recommendation:** Before allocation, reject zero/oversized dimensions, compute `width * height * 4` with checked arithmetic, require `pixelBytes` to equal that value, and require the file to contain exactly the expected payload. Remove the index entry and file when validation fails.
- **Acceptance:** malformed headers, truncated payloads, and dimensions above limits return a cache miss without throwing or allocating unreasonable memory.

### QW2: Preserve split folder-rename state

- **Category:** Reliability
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Area:** [src/services/FolderWatchService.cpp](../src/services/FolderWatchService.cpp#L244-L268)
- **Recommendation:** Move `pendingRenameOldPath` into watch-loop state that survives multiple `ReadDirectoryChangesW` completions. Clear it on stop/error/request change. If a new-name record arrives unpaired, request a full reload.
- **Acceptance:** old/new records split across two parser inputs produce one rename with both paths; orphan records produce deterministic fallback behavior.

### QW3: Add malformed-cache regression tests

- **Category:** Reliability
- **Perspective:** Engineering
- **Effort:** S
- **Impact:** High
- **Area:** `tests/`, `src/cache/DiskThumbnailCache.*`
- **Recommendation:** allow a test cache root injection or extract payload parsing into a pure helper. Cover huge byte counts, multiplication overflow, negative-cast dimensions, truncated files, wrong magic, and valid round trips.

### QW4: Wire startup budgets into CI

- **Category:** Performance
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Area:** [tools/TestStartupBenchmark.ps1](../tools/TestStartupBenchmark.ps1#L13-L15), `.github/workflows/`
- **Recommendation:** add a Windows CI job that builds Release, runs smoke tests, invokes the script with nonzero budgets, and uploads JSON/log artifacts. Begin with generous budgets and tighten from observed runner variance.

### QW5: Correct documentation drift

- **Category:** DX
- **Perspective:** Both
- **Effort:** S
- **Impact:** Medium
- **Area:** [specs/02-architecture.md](../specs/02-architecture.md#L8-L14), [specs/04-ui-behavior.md](../specs/04-ui-behavior.md#L133-L140), [specs/14-todo.md](../specs/14-todo.md#L224-L227), [specs/15-d2d-rendering-migration.md](../specs/15-d2d-rendering-migration.md#L26-L35)
- **Recommendation:** describe the actual D2D/DirectWrite scope, current MainWindow size/responsibilities, implemented filter syntax, slideshow settings, F2 rename, and real CI status. Mark migration documents as historical/completed where appropriate.

### QW6: Contain thumbnail-worker exceptions

- **Category:** Reliability
- **Perspective:** Engineering
- **Effort:** S
- **Impact:** Medium
- **Area:** `src/services/ThumbnailScheduler.cpp`
- **Recommendation:** catch `std::exception` and unknown exceptions at each worker entry boundary, record a diagnostic, unwind in-flight accounting, and report a decode/cache miss rather than allowing `std::terminate`. Keep QW1 as the root fix.

---

## High-Impact Medium Projects: One to Two Weeks

### MP1: Persistent cache index and I/O redesign

- **Category:** Performance, Reliability
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** High
- **Area:** [src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L329-L346), `src/services/ThumbnailScheduler.*`
- **Recommendation:** maintain one in-memory index per process; enqueue loads/stores/access touches on a dedicated low-priority cache executor; append access records or batch them; atomically snapshot during compaction; shard files by hash prefix.
- **Success metrics:** no whole-index rewrite on cache hit, no decode worker blocked on persistent-cache write, bounded compaction time, and correct recovery after forced termination.

### MP2: Bounded background service execution

- **Category:** Performance, Reliability
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** Medium
- **Area:** `FolderEnumerationService`, `FolderTreeEnumerationService`, `BatchConvertService`, `FileOperationService`, `util/BackgroundExecutor.h`
- **Recommendation:** replace per-request `std::async` with bounded category-specific executors. Preserve request epochs and add queue-depth/cancellation diagnostics.
- **Success metrics:** rapid folder changes never create unbounded threads; stale queued work is dropped before filesystem access.

### MP3: Cancellable file-operation shutdown

- **Category:** Reliability, UX
- **Perspective:** Both
- **Effort:** M
- **Impact:** High
- **Area:** [src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L477-L479), [src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L627-L673), `src/ui/MainWindow.cpp`
- **Recommendation:** add operation state and cancellation through the progress sink, define close behavior, and keep the shell owner valid until completion. Do not silently detach a worker that still references HWNDs.
- **Success metrics:** closing during a slow copy/delete either cancels promptly or presents an explicit bounded wait state; no orphan shell UI or post-destroy completion.

### MP4: CI quality and performance matrix

- **Category:** Reliability, Performance
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** High
- **Area:** `.github/workflows/`, `CMakePresets.json`, `tools/`, `tests/`
- **Recommendation:** build Debug/Release with LibRaw on, exercise a fallback build with nvJPEG off, run CTest, run the startup gate, and preserve diagnostics artifacts. Add a scheduled dependency/security job.

### MP5: Focused tests and deterministic fixtures

- **Category:** Reliability, DX
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** Medium
- **Area:** [tests/CMakeLists.txt](../tests/CMakeLists.txt#L1-L38), `tests/smoke.cpp`
- **Recommendation:** split cache, watcher, service, browser, and viewer cases into focused targets or at least named CTest cases. Extract the watcher notification parser so split-buffer behavior can be tested without relying on OS timing.

### MP6: Saved structured filters

- **Category:** Feature, UX
- **Perspective:** Product
- **Effort:** M
- **Impact:** Medium
- **Area:** [src/browser/BrowserPane.cpp](../src/browser/BrowserPane.cpp#L377-L400), `src/ui/MainWindow.cpp`
- **Recommendation:** let users save and recall current-folder filter expressions such as `rating:>=4 tag:keeper type:raw`. Keep evaluation in-memory and avoid a catalog dependency.

---

## Strategic Initiatives

### SI1: Decompose MainWindow by ownership

- **Problem:** a ~15,970-line coordinator owns unrelated UI, persistence, tree, drag/drop, file-operation, viewer, and watch concerns.
- **Approach:** first extract pure registry settings, then menu/toolbar command state, then folder-tree/watch coordination. Keep HWND ownership and message routing explicit; avoid a framework rewrite.
- **Risks:** large mechanical diffs and hidden ordering dependencies.
- **Success metrics:** MainWindow below ~7,000 lines, narrower rebuilds, unchanged smoke/startup metrics, and dedicated tests for extracted logic.

### SI2: Professional compare and color-management path

- **Problem:** two-up compare lacks synchronized zoom/pan and accurate monitor-profile rendering, both important for photographic inspection.
- **Approach:** implement color transforms behind an opt-in setting, then add 3/4-up compare with synchronized transforms and per-tile rating controls.
- **Risks:** color-transform CPU cost, multi-monitor profile changes, VRAM growth, and compare interaction complexity.
- **Success metrics:** verified profile correctness, bounded viewer-open regression, and faster keeper/reject workflows in user testing.

### SI3: Search and smart-folder product decision

- **Problem:** users cannot retrieve rated/tagged work across folders, but a catalog conflicts with the stated no-database scope.
- **Approach:** validate demand first. Choose among saved current-folder filters, transient recursive search, or a lightweight opt-in index. Do not drift into organizer/database lock-in by accident.
- **Risks:** index freshness, privacy, startup/background cost, and product identity dilution.
- **Success metrics:** documented product decision; if built, warm result latency below 500 ms without measurable browse regression.

### SI4: Format frontier

- **Problem:** HEIC/HEIF, AVIF, and animated viewer playback are increasingly expected.
- **Approach:** prioritize WIC-backed HEIC detection/fallback, then optional AVIF, then animated GIF/WebP viewer playback. Keep every dependency optional and capability-driven.
- **Risks:** codec availability/licensing, attack surface, frame-cache memory, and packaging size.
- **Success metrics:** clear unsupported states, mixed-folder resilience, and no startup cost when optional codecs are absent.

---

## Debt Retirement Candidates

1. Replace stale “current state” sections in specs 02 and 15 with an implemented-state architecture record.
2. Remove dead or redundant cache-loading helpers after MP1; `EnsureLoadedLocked` should either become the real load boundary or be deleted.
3. Retire per-request `std::async` once MP2 lands.
4. Split the monolithic test source as touched, without pausing product work for a wholesale test-framework migration.
5. Keep GDI fallback rendering only where it is exercised and documented; do not remove it solely for aesthetic consistency.

## Dependency Upgrade Path

1. **LibRaw 0.22.1:** add a dependency inventory, upstream version/revision, CVE review date, and quarterly update check. Preserve out-of-process decode and RAW fixtures.
2. **CUDA/nvJPEG:** verify the pinned cudart 12.6.77 and nvJPEG 12.3.3.54 archives at each release; test runtime-missing fallback.
3. **NanoSVG:** record the vendored revision and upstream license; update only with toolbar/render tests.
4. **CMake/VS toolchain:** keep the 4.2+ preset requirement aligned across README, presets, and CI.

---

## Prompt Handoff

### Workstream 1: Cache hardening

> Harden `DiskThumbnailCache` in `src/cache/DiskThumbnailCache.cpp` around lines 371-383. Validate width, height, checked BGRA byte count, file length, and configured limits before allocating. Invalid cache entries must become misses and be removed without throwing. Add corruption and round-trip tests under `tests/`, then run `HyperBrowseSmoke`. Do not change the persistent filename hash without a migration plan.

### Workstream 2: Folder watcher correctness

> Fix split rename handling in `src/services/FolderWatchService.cpp` around lines 244-268. Preserve the old rename path across `ReadDirectoryChangesW` completions and define fallback behavior for orphan old/new records. Extract a deterministic parser/state helper and add tests for same-buffer, split-buffer, orphan, stop, and overflow cases. Verify MainWindow reconciliation around lines 12347-12357.

### Workstream 3: CI performance gate

> Add a Windows CI workflow for HyperBrowse. Build Release with the supported preset, run CTest, then invoke `tools/TestStartupBenchmark.ps1` with explicit nonzero budgets and upload its JSON/log outputs. Use a deterministic image fixture and document runner variance. Correct `specs/14-todo.md` only after the workflow passes in CI.

### Workstream 4: Background and shutdown lifecycle

> Replace request-per-thread `std::async` usage in the four services with bounded executors while preserving request IDs. Separately add explicit cancellation/shutdown behavior for `FileOperationService` around `PerformOperations` and `WaitForWorkers`. Add diagnostics for active/queued work and tests for rapid cancellation and close during an operation.

### Workstream 5: Documentation alignment

> Reconcile `specs/02-architecture.md`, `04-ui-behavior.md`, `14-todo.md`, and `15-d2d-rendering-migration.md` with current code. Document D2D/DirectWrite scope, current MainWindow ownership, structured filters, slideshow settings, F2 rename, and actual CI status. Preserve historical design rationale but label it as historical rather than current behavior.
