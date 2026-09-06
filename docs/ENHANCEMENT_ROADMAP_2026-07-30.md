# HyperBrowse Enhancement Roadmap

Original review: 2026-07-30
Status refresh: 2026-09-06
Reviewer: GitHub Copilot
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse

## Summary

| Item | Category | Status | Next priority | Effort | Area |
| --- | --- | --- | --- | --- | --- |
| QW1 Validate persistent thumbnail payloads | Reliability/Security | Complete in 1.2.5 | Archived | XS | `src/cache/DiskThumbnailCache.cpp` |
| QW2 Preserve split folder-rename state | Reliability | Complete in 1.2.5 | Archived | S | `src/services/FolderWatchService.*` |
| QW3 Add malformed-cache regression tests | Reliability | Complete in 1.2.5 and expanded in 2.1.0 | Archived | S | `tests/` |
| QW4 Wire startup budgets into CI | Performance | Complete in 2.0.0 | Archived | S | `.github/workflows/`, `tools/` |
| QW5 Correct architecture/UI/backlog docs | DX | Complete for the current architecture, UI behavior, D2D migration, and active backlog; historical plans remain explicitly historical | Archived | S | `docs/`, `specs/` |
| QW6 Add worker exception containment | Reliability | Complete in 2.0.0 | Archived | S | `src/services/ThumbnailScheduler.cpp` |
| MP1 Redesign persistent cache index/I/O | Performance | Complete in 2.1.0 | Archived | M | `src/cache/`, `src/services/` |
| MP2 Bound background service execution | Performance/Reliability | Partial: bounded executors, service metrics, diagnostics counters, and deterministic service coverage landed; capacity measurement remains | P0 | M | `src/services/`, `src/util/` |
| MP3 Define cancellable file-operation shutdown | Reliability/UX | Partial: close contract, wait state, post suppression, and blocked-service destruction coverage landed; real MainWindow/shell-prompt validation remains | P0 | M | `FileOperationService`, `MainWindow` |
| MP4 Establish CI quality/performance matrix | Reliability | Partial: Windows matrix now covers nvJPEG and WIC fallback with Debug/Release/startup/package gates; sanitizer/fuzz job remains toolchain work | P1 | M | CI, tests, tools |
| MP5 Split focused test targets and fixtures | DX/Reliability | Partial: broad executable with four CTest entry points | P1 | M | `tests/` |
| MP6 Saved structured filters | Feature/UX | Open | P2 | M | browser/UI/settings |
| SI1 Decompose MainWindow by ownership | DX/Reliability | Partial: current cleanup pass complete; long-term decomposition remains | P2 | L | `src/ui/MainWindow.*` |
| SI2 Pro compare and color-management path | Feature | Open | P2 | L | viewer/decode/render |
| SI3 Search/smart-folder product decision | Feature | Decision needed | P2 | S | browser/services |
| SI4 Format frontier | Feature | Open, deliberately later | P3 | L | decode/viewer |

## 2026-09-06 Reprioritization

The July quick-win queue is no longer the active execution order. The cache,
watcher, malformed-input, worker-containment, and startup-gate work has landed
and has regression coverage. Release 2.1.0 also added a consolidated themed
Settings surface, native shell drag/drop, clipboard and duplicate workflows,
undo/redo, single-instance launch forwarding, taskbar progress, viewer
inspection controls, decoder diagnostics, and stronger large-folder scheduling.

Work should now proceed in this order:

1. **P0 - Measure bounded background execution.** The four service migrations
   now have bounded executors, queue-depth accessors, cancellation metrics,
   queue-rejection diagnostics, and deterministic service lifecycle assertions.
   Measure selected capacities against slow local, removable, and network paths.
2. **P0 - Validate close during file operations.** The close contract now
   requests cooperative cancellation, keeps the HWND and shell owner alive,
   reports a five-second wait notice, suppresses late posts, and joins the
   worker before destruction. The remaining validation is a real MainWindow
   close path against shell prompts and slow storage; never detach shell work.
3. **P1 - Finish the quality matrix.** The CI matrix now runs nvJPEG-on and
   WIC-fallback Debug/Release tests and startup checks, with deterministic
   fixture and hosted-run variance policy documented. A dedicated sanitizer /
   fuzz toolchain remains future work; malformed-input smoke coverage is the
   current substitute.
4. **P1 - Improve observability.** Count invalid cache entries and watcher
   full-reload fallbacks separately from ordinary misses, and make a redacted
   diagnostics snapshot export available for issue reports.
5. **P1 - Maintain current-state documentation.** Architecture, UI, D2D, and
   backlog specifications now describe shipped behavior; update them with
   future cross-cutting changes.
6. **P2 - Add saved structured filters.** Persist named current-folder
   expressions without introducing a catalog database.
7. **P2 - Professional compare and color management.** Validate color-profile
   behavior first, then add synchronized 3/4-up compare and per-tile culling.
8. **P3 - Format frontier.** Consider HEIC/AVIF, animated playback, and
   multipage TIFF only after reliability and compare work has evidence behind
   it.

Items marked complete below remain as historical implementation notes. They are
not requests to repeat the work.

---

## Quick Wins: Less Than One Day

### QW1: Validate persistent thumbnail payloads (Complete)

- **Category:** Reliability, Security
- **Perspective:** Engineering
- **Effort:** XS
- **Impact:** High
- **Area:** [src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L371-L383)
- **Delivered:** `TryLoad` validates the magic, dimensions, checked BGRA byte count, source dimensions, platform size limits, and exact file length before allocation. Invalid entries are removed and become cache misses.
- **Coverage:** `tests/smoke.cpp` covers malformed headers, oversized dimensions, mismatched byte counts, truncated payloads, malformed index rows, unsafe cache names, and valid round trips.

### QW2: Preserve split folder-rename state (Complete)

- **Category:** Reliability
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Area:** [src/services/FolderWatchService.cpp](../src/services/FolderWatchService.cpp#L244-L268)
- **Delivered:** `FolderWatchNotificationParser` retains the old name across notification buffers, resets it on stop/error, and requests a full reload for orphaned or malformed records.
- **Coverage:** `tests/smoke.cpp` covers split and same-buffer renames, orphan old/new records, reset behavior, and malformed notifications.

### QW3: Add malformed-cache regression tests (Complete)

- **Category:** Reliability
- **Perspective:** Engineering
- **Effort:** S
- **Impact:** High
- **Area:** `tests/`, `src/cache/DiskThumbnailCache.*`
- **Delivered:** the cache accepts an injected test root, and the smoke suite covers payload corruption, overflow-sized dimensions/values, truncation, strict index parsing, journal replay, malformed journal tails, invalidation replay, compaction, and valid round trips.

### QW4: Wire startup budgets into CI (Complete)

- **Category:** Performance
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Area:** [tools/TestStartupBenchmark.ps1](../tools/TestStartupBenchmark.ps1#L13-L15), `.github/workflows/`
- **Delivered:** `.github/workflows/ci.yml` builds Debug and Release, runs CTest, invokes the startup script with explicit 2.5 s / 2.5 s / 5 s budgets, uploads benchmark/log artifacts, and validates release artifacts. Runner-variance tuning remains in MP4.

### QW5: Correct documentation drift (Partial)

- **Category:** DX
- **Perspective:** Both
- **Effort:** S
- **Impact:** Medium
- **Area:** [docs/architecture.md](architecture.md), [docs/mainwindow-ownership-map.md](mainwindow-ownership-map.md), [specs/02-architecture.md](../specs/02-architecture.md#L8-L14), [specs/04-ui-behavior.md](../specs/04-ui-behavior.md#L133-L140), [specs/14-todo.md](../specs/14-todo.md#L224-L227), [specs/15-d2d-rendering-migration.md](../specs/15-d2d-rendering-migration.md#L26-L35)
- **Current status:** the review, architecture, and MainWindow ownership documents reflect the current release, hybrid rendering, CI, cache, watcher, and workflow state. The architecture and D2D migration specs still require reconciliation; the UI-behavior spec needs an audit for shipped workflow gaps.

### QW6: Contain thumbnail-worker exceptions (Complete)

- **Category:** Reliability
- **Perspective:** Engineering
- **Effort:** S
- **Impact:** Medium
- **Area:** `src/services/ThumbnailScheduler.cpp`
- **Delivered:** thumbnail cache lookup, decode, cache insertion, disk persistence, and file-operation worker boundaries catch standard and unknown exceptions, record diagnostics where applicable, and report failures without process termination.

---

## High-Impact Medium Projects: One to Two Weeks

### MP1: Persistent cache index and I/O redesign (Complete in 2.1.0)

- **Category:** Performance, Reliability
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** High
- **Area:** [src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L329-L346), `src/services/ThumbnailScheduler.*`
- **Delivered:** the cache maintains an authoritative in-memory index, journals mutations and batched access touches, replays valid journal records, atomically compacts the snapshot, and keeps scheduler persistence off the UI thread.
- **Remaining scale work:** measure the process-wide filesystem mutex and directory-scan cost at large cache sizes; add sharding only if benchmarks show a user-visible regression.
- **Success evidence:** smoke coverage verifies no index rewrite on a cache hit, journal replay after restart, malformed-tail tolerance, invalidation replay, and journal truncation after compaction.

### MP2: Bounded background service execution

- **Status:** Partial. All four request-per-task service paths now use bounded executors. Shared executor capacity/destruction coverage, service queue-depth accessors, rejection and cancellation diagnostics, and deterministic service lifecycle assertions are landed; realistic burst measurement and capacity tuning remain.
- **Category:** Performance, Reliability
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** Medium
- **Area:** `FolderEnumerationService`, `FolderTreeEnumerationService`, `BatchConvertService`, `FileOperationService`, `util/BackgroundExecutor.h`
- **Delivered:** folder enumeration, folder-tree queries, batch conversion, and file operations now use member-owned `util::BackgroundExecutor` instances with bounded worker and pending-task counts. Existing request generations and cancellation checks remain in place; stale folder work exits before filesystem access, and shell operations retain COM STA setup and owner-window assignment. Completion/progress posts are suppressed after service shutdown.
- **Coverage:** smoke tests now exercise rapid folder navigation, rapid folder-tree cancellation, batch-conversion bursts, and deterministic file-operation saturation, including peak queue-depth, rejection, cancellation, worker-drain, and diagnostics assertions.
- **Remaining:** export live service queue-depth snapshots into the diagnostics window and measure whether the selected capacities are appropriate for slow local, removable, and network paths.
- **Success metrics:** rapid folder changes never create unbounded threads; stale queued work exits before filesystem access; service destruction joins in-flight work without late completion posts.

### MP3: Cancellable file-operation shutdown

- **Status:** Partial. Cancellation, progress reporting, explicit close-pending cancellation, pre-destruction post suppression, a bounded user-visible wait state, and deterministic blocked-operation destruction coverage are implemented; the shell operation can still make shutdown wait without a forceful timeout.
- **Category:** Reliability, UX
- **Perspective:** Both
- **Effort:** M
- **Impact:** High
- **Area:** [src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L477-L479), [src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L627-L673), `src/ui/MainWindow.cpp`
- **Current behavior:** closing during an active file operation requests cancellation, keeps the main window alive until the completion update, and shows a cancelling status. `FileOperationService::Shutdown()` is also called from `WM_DESTROY` before the HWND can be invalidated, preventing late completion/progress posts from targeting a destroyed window.
- After five seconds without completion, the status changes to `Waiting for Windows to finish file operation` and records `file_operation.shutdown.wait_notice`; the UI remains responsive and the shell owner remains valid.
- **Remaining:** exercise the MainWindow close path with slow local, removable, network, and shell-prompt conditions. Do not detach a worker that can still post to a destroyed HWND.
- **Success metrics:** closing during a slow copy/delete either cancels promptly or presents an explicit bounded wait state; no orphan shell UI or post-destroy completion.

### MP4: CI quality and performance matrix

- **Status:** Partial. The committed Windows workflow is now an nvJPEG-on/WIC-fallback configuration matrix with Debug/Release CTest and startup gates; sanitizer/fuzz remains a separate toolchain configuration.
- **Category:** Reliability, Performance
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** High
- **Area:** `.github/workflows/`, `CMakePresets.json`, `tools/`, `tests/`
- **Recommendation:** retain the current matrix and package gate, preserve diagnostics artifacts, and add scheduled dependency/security review. A dedicated dynamic-runtime sanitizer/fuzz preset remains the next quality-tooling step.

### MP5: Focused tests and deterministic fixtures

- **Status:** Partial. Coverage is materially broader, but most scenarios still live in one executable.
- **Category:** Reliability, DX
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** Medium
- **Area:** [tests/CMakeLists.txt](../tests/CMakeLists.txt#L1-L38), `tests/smoke.cpp`
- **Recommendation:** split cache, watcher, service, browser, and viewer cases into focused targets or named CTest cases as maintenance allows. The watcher parser is already extracted and tested without OS timing; do not redo that part.

### MP6: Saved structured filters

- **Status:** Open product feature.
- **Category:** Feature, UX
- **Perspective:** Product
- **Effort:** M
- **Impact:** Medium
- **Area:** [src/browser/BrowserPane.cpp](../src/browser/BrowserPane.cpp#L377-L400), `src/ui/MainWindow.cpp`
- **Recommendation:** let users save and recall current-folder filter expressions such as `rating:>=4 tag:keeper type:raw`. Keep evaluation in-memory and avoid a catalog dependency.

---

## Strategic Initiatives

### SI1: Decompose MainWindow by ownership

- **Status:** Partial. The current cleanup pass is complete, lower priority than lifecycle and CI reliability; the longer-term decomposition target remains open.
- **Problem:** a 21,208-line coordinator still owns unrelated UI, persistence, tree, drag/drop, file-operation, viewer, watch, and rendering orchestration concerns.
- **Approach:** continue only with narrowly owned slices where they reduce coupling or improve testability. The completed pass already extracted persistence, menu/toolbar policy, folder-tree/watch coordination, viewer lifecycle policy, and message boundaries. Keep HWND ownership and message routing explicit; avoid a framework rewrite.
- **Risks:** large mechanical diffs and hidden ordering dependencies.
- **Success metrics:** MainWindow below ~7,000 lines, narrower rebuilds, unchanged smoke/startup metrics, and dedicated tests for extracted logic.

### SI2: Professional compare and color-management path

- **Status:** Open product investment after the P0/P1 reliability queue.
- **Problem:** two-up compare lacks synchronized zoom/pan and accurate monitor-profile rendering, both important for photographic inspection.
- **Approach:** implement color transforms behind an opt-in setting, then add 3/4-up compare with synchronized transforms and per-tile rating controls.
- **Risks:** color-transform CPU cost, multi-monitor profile changes, VRAM growth, and compare interaction complexity.
- **Success metrics:** verified profile correctness, bounded viewer-open regression, and faster keeper/reject workflows in user testing.

### SI3: Search and smart-folder product decision

- **Status:** Decision needed; saved current-folder filters are the smallest compatible first step.
- **Problem:** users cannot retrieve rated/tagged work across folders, but a catalog conflicts with the stated no-database scope.
- **Approach:** validate demand first. Choose among saved current-folder filters, transient recursive search, or a lightweight opt-in index. Do not drift into organizer/database lock-in by accident.
- **Risks:** index freshness, privacy, startup/background cost, and product identity dilution.
- **Success metrics:** documented product decision; if built, warm result latency below 500 ms without measurable browse regression.

### SI4: Format frontier

- **Status:** Deliberately later than reliability, diagnostics, and compare work.
- **Problem:** HEIC/HEIF, AVIF, and animated viewer playback are increasingly expected.
- **Approach:** prioritize WIC-backed HEIC detection/fallback, then optional AVIF, then animated GIF/WebP viewer playback. Keep every dependency optional and capability-driven.
- **Risks:** codec availability/licensing, attack surface, frame-cache memory, and packaging size.
- **Success metrics:** clear unsupported states, mixed-folder resilience, and no startup cost when optional codecs are absent.

---

## Debt Retirement Candidates

1. Replace stale “current state” sections in specs 02 and 15 with an implemented-state architecture record.
2. Add service-level burst, cancellation, queue-rejection, and destruction coverage for MP2; keep service queue depth, active work, rejection, and cancellation behavior observable.
3. Split the monolithic test source as touched, without pausing product work for a wholesale test-framework migration.
4. Add cache-corruption and watcher-fallback diagnostic counters so recovery is visible in field reports.
5. Keep GDI fallback rendering only where it is exercised and documented; do not remove it solely for aesthetic consistency.

## Dependency Upgrade Path

1. **LibRaw 0.22.1:** add a dependency inventory, upstream version/revision, CVE review date, and quarterly update check. Preserve out-of-process decode and RAW fixtures.
2. **CUDA/nvJPEG:** verify the pinned cudart 12.6.77 and nvJPEG 12.3.3.54 archives at each release; test runtime-missing fallback.
3. **NanoSVG:** record the vendored revision and upstream license; update only with toolbar/render tests.
4. **CMake/VS toolchain:** keep the 4.2+ preset requirement aligned across README, presets, and CI.

---

## Prompt Handoff

### Workstream 1: Bounded background execution

> Validate the bounded-executor migration in folder enumeration, folder-tree queries, batch conversion, and file operations. Preserve request epochs, cancellation checks, shell-owner lifetime, and completion-message safety. Surface service-level queue-depth and cancellation diagnostics, then add rapid-navigation and burst coverage.

### Workstream 2: File-operation shutdown contract

> Define close behavior while `FileOperationService` is inside `IFileOperation::PerformOperations`. Keep the owner valid until completion, make cancellation state observable, and test slow local/removable/network and shell-prompt paths. Do not detach a worker that can post to a destroyed window.

### Workstream 3: Quality and measurement matrix

> Extend `.github/workflows/ci.yml` with an nvJPEG-off fallback build, sanitizer/fuzz coverage for the persistent cache and RAW-helper protocol, and a documented deterministic benchmark fixture and hosted-run variance policy. Preserve JSON/log and release artifacts.

### Workstream 4: Diagnostics and current-state documentation

> Add distinct counters for invalid persistent-cache entries and folder-watch full-reload fallbacks, plus a redacted diagnostics snapshot export. Then reconcile `specs/02-architecture.md`, `specs/04-ui-behavior.md`, and `specs/15-d2d-rendering-migration.md` with the hybrid renderer, current MainWindow ownership, structured filters, slideshow settings, F2 behavior, metadata visibility, and shell drag/drop.

### Workstream 5: Product depth

> Add saved current-folder filter expressions without a catalog dependency. Next validate color-managed display, then implement synchronized 3/4-up compare and per-tile culling. Keep HEIC/AVIF, animation, and multipage TIFF behind evidence from the reliability and compare work.
