# HyperBrowse Comprehensive Code Review

Original review: 2026-07-30
Status refresh: 2026-09-03
Reviewer: GitHub Copilot
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse

## Project Context

HyperBrowse is a native Windows 10/11 x64 image browser and viewer optimized for fast folder navigation, thumbnail throughput, and low-latency full-image viewing. It is intentionally browser/viewer-first rather than an editor or catalog product.

### Stack and architecture

- C++20, Win32, CMake, and Visual Studio 2026 presets.
- Direct2D/DirectWrite browser and viewer presentation with GDI retained for shell surfaces and fallbacks.
- WIC for standard formats, vendored LibRaw 0.22.1 for RAW files, and optional nvJPEG/CUDA acceleration.
- A UI-thread Win32 message loop with asynchronous enumeration, metadata, file operation, watch, thumbnail, and viewer pipelines.
- Bounded memory caches, a persistent thumbnail cache under `%LOCALAPPDATA%`, and a separate RAW helper process.
- One broad smoke/integration executable with four CTest entry points.

The current release is 2.1.0. The first-party `src/` tree is approximately 53,000 physical lines across 71 source/header files. The largest units are `src/ui/MainWindow.cpp` (~27,263 lines), `src/browser/BrowserPane.cpp` (~5,478), and `src/viewer/ViewerWindow.cpp` (~6,247). The code generally uses RAII, `ComPtr`, request epochs, bounded queues/caches, and ownership-safe `PostMessageW` payload transfer.

### Review scope and validation

The original review covered first-party source, tests, CMake/package scripts, README, and specs. Vendored source was reviewed only at its integration boundary. This refresh compares the findings with `master` at `cee1fa1` and the post-review history through 2026-09-03. The July worktree sort-state change is now historical; subsequent releases and fixes are included below.

The May review was used as a baseline, not copied forward. Since the original review, cache corruption handling, split-buffer rename pairing, journal persistence, off-UI-thread cache maintenance, foreground thumbnail scheduling, early folder batches, settings isolation, decoder diagnostics, and several Windows workflow features have landed.

Severity: 🔴 Critical | 🟠 High | 🟡 Medium | 🔵 Low

## Status Refresh — 2026-09-03

The highest-severity July findings are resolved in the current branch:

- Persistent thumbnail payload validation and corrupt-entry cleanup landed in 1.2.5, with regression coverage for invalid headers, dimensions, byte counts, truncation, malformed index rows, unsafe cache names, and valid round trips.
- Folder-watch rename state now survives notification-buffer boundaries. The deterministic parser has coverage for split, same-buffer, orphan, reset, and malformed records.
- The persistent cache now maintains an authoritative in-memory index, appends access and mutation records to `index.journal.tsv`, replays valid journal records, and atomically compacts `index.tsv`. Cache stores and invalidations are processed off the UI thread.
- Thumbnail scheduling now has a foreground lane, stale-work filtering, CPU-preferred visible work, early folder batches, coalesced presentation, and diagnostics for queue and persistence timings.
- Folder enumeration, folder-tree queries, batch conversion, and file operations now use bounded member-owned executors instead of request-per-task `std::async`; stale folder work checks cancellation before filesystem access, and shutdown suppresses late service completion posts.
- A committed Windows workflow builds Debug and Release, runs CTest, applies nonzero startup budgets, and validates release artifacts. The current workflow is a meaningful gate, but it is not yet a full compiler/configuration/dependency matrix.

These changes lower the immediate reliability risk substantially. The remaining priorities are listed in the revised roadmap rather than treated as unresolved July defects.

---

## Phase 2: Performance and Technical Review

### 2A. Algorithmic and computational efficiency

🟢 **Resolved: persistent-cache hits no longer rewrite the whole index.**

The July behavior was replaced by an authoritative in-memory index and append-only journal. Access touches are batched by a cache-owned persistence thread, and compaction atomically replaces the snapshot. The UI no longer calls persistent-cache invalidation directly. The process-wide filesystem mutex still serializes cache file access, so sharding and narrower lock scope remain worthwhile follow-up work, but the O(n) index rewrite on each hit is closed.

**Remaining recommendation:** measure the mutex and directory-scan cost at large cache sizes, then consider sharded storage and narrower filesystem-lock scope only if benchmarks show a user-visible regression.

🟢 **Resolved in 2026-09-03: service execution is bounded.**

Folder enumeration, folder-tree enumeration, file operations, and batch conversion now use member-owned `util::BackgroundExecutor` pools with bounded worker and pending-task counts. Request generations and cancellation checks remain intact; stale folder tasks exit before filesystem access, and service shutdown suppresses late completion/progress posts.

**Remaining recommendation:** add burst, queue-rejection, and destruction coverage, expose executor queue/active-work diagnostics, and tune capacities using slow local, removable, and network fixtures. File-operation shutdown still needs its own bounded close policy because the serialized shell task can block inside `IFileOperation::PerformOperations`.

### 2B. Persistent data layer

🟢 **Resolved: corrupt thumbnail payloads are rejected before allocation.**

`TryLoad` now checks the magic, dimension limits, checked BGRA byte count, source dimensions, platform size limits, and exact file length before allocating. Allocation and cache-file failures become misses, invalid entries are removed, and scheduler worker boundaries contain unexpected cache/decode exceptions. The malformed-cache regression scenario exercises these cases.

**Remaining recommendation:** add an explicit invalid-cache diagnostic counter so field reports can distinguish corruption from ordinary cache misses.

🟢 **Resolved: index compaction is crash-safer.**

Compaction writes a replacement snapshot and replaces the prior index only after the new snapshot is complete. Journal replay ignores malformed or incomplete records, and malformed snapshot rows are skipped. The cache remains recoverable data rather than user data, but corruption and recovery outcomes are not yet exposed through diagnostics.

### 2C. Concurrency, async, and I/O

🟢 **Resolved: folder renames survive split notification completions.**

`FolderWatchNotificationParser` retains the pending old name across `ReadDirectoryChangesW` completions and requests a full reload for orphaned or malformed records. `tests/smoke.cpp` now asserts split and same-buffer pairing, reset behavior, and fallback handling.

🟡 **Application shutdown can still wait for shell file operations.**

Cancellation now reaches the progress sink, and `MainWindow` requests cancellation during teardown. The service still joins the worker synchronously, and `IFileOperation::PerformOperations` may remain blocked on a long copy, unavailable network destination, elevation UI, or shell conflict prompt. There is no bounded close policy or test for destruction during a blocked shell operation.

**Recommendation:** define close-during-operation behavior explicitly. Keep the owner valid until completion, make cancellation state observable, and provide a bounded shutdown state or documented wait rather than silently appearing closed while a synchronous join continues.

### 2D. Memory and resource management

The corrupt-cache allocation risk is closed. Normal runtime cache sizing is adaptive and bounded, COM/GDI resources are generally RAII-managed, and no first-party leak was identified. The remaining resource concern is lifecycle behavior when shell operations or filesystem APIs do not return promptly.

### 2E. Network and API efficiency

The runtime product is local-first. Network/removable paths still matter because filesystem enumeration and shell operations can block in OS APIs despite request cancellation; bounded executors and shutdown policy should account for this.

### 2F. Build, bundle, and startup

🟢 **Resolved in part: CI now runs a real startup gate and package validation.**

The committed workflow builds Debug and Release, runs the four registered CTest entry points, invokes `TestStartupBenchmark.ps1` with 2.5 s / 2.5 s / 5 s budgets, uploads benchmark output, and builds release artifacts. The budget script still defaults to disabled for local use, which is appropriate for an opt-in tool; CI supplies explicit nonzero values.

**Remaining recommendation:** add a deterministic fixture policy, record hosted-run variance, and expand CI to a fallback configuration with nvJPEG disabled plus dependency/security checks. The current workflow is a gate, not yet a complete quality matrix.

🟡 **`MainWindow.cpp` has grown to approximately 27,263 lines.**

The unit still owns menus, toolbar, dialogs, persistence, folder tree, drag/drop, file operations, viewer coordination, and watch reconciliation. Recent UI work made the mismatch with the architecture spec larger, not smaller. This remains engineering hygiene rather than a reason to pause product work, but new cross-cutting behavior should be extracted behind focused helpers where practical.

### 2G. Observability and reliability

- Diagnostics and startup JSON are strong foundations, now including queue, decode, cache-persistence, cancellation, and failure timing/counter data.
- There is still no automated field snapshot export or persisted percentile rollup.
- Invalid persistent-cache entries are cleaned up but are not yet counted distinctly from ordinary misses.
- Watcher fallback/reload reasons are still not represented as dedicated diagnostics counters.

### 2H. Security/performance intersections

🟢 The unchecked cache-header allocation path is closed. The cache is local-user scoped, and malformed entries are rejected and removed before allocation.

🔵 RAW decoding has a useful out-of-process isolation option. Keep it enabled by default and test helper timeout/termination behavior with each LibRaw update.

### 2I. Dependency health

- Vendored LibRaw is 0.22.1 ([external/libraw/libraw/libraw_version.h](../external/libraw/libraw/libraw_version.h#L23-L25)); no repository dependency inventory or update cadence was found.
- CUDA redistributables are hash-pinned, which is good, but currently pin cudart 12.6.77 and nvJPEG 12.3.3.54 ([cmake/BundleCudaRedistributables.cmake](../cmake/BundleCudaRedistributables.cmake#L104-L114)). Currency should be reviewed at release time.
- NanoSVG is vendored and used only for a small integration surface.

---

## Phase 3: Product and Feature Review

### 3A. Feature completeness

The product is notably more complete than the May review indicated: structured rating/tag filters, adaptive resource profiles, viewer metadata, persistent cache controls, folder organization, browser-to-tree drag/drop, slideshow settings/transitions, and quick viewer delete are implemented.

The post-review branch also shipped a consolidated themed Settings surface, Quick Actions persistence, native shell drag-out and drop-in, clipboard and duplicate workflows, undo/redo, single-instance launch forwarding, taskbar progress, viewer keyboard/focus improvements, and broader decoder diagnostics. The strongest remaining opportunities inside the browser/viewer scope are:

1. **n-up compare with synchronized zoom/pan.** Two-up compare exists; 3/4-up culling and synchronized inspection remain planned ([specs/14-todo.md](../specs/14-todo.md#L265-L278)).
2. **Color-managed display.** This is material for photographers and remains absent ([specs/14-todo.md](../specs/14-todo.md#L289-L296)).
3. **Saved filter views.** Structured `rating:` and `tag:` parsing already exists ([src/browser/BrowserPane.cpp](../src/browser/BrowserPane.cpp#L300-L305), [src/browser/BrowserPane.cpp](../src/browser/BrowserPane.cpp#L377-L400)); persisting named expressions is a small, scope-compatible extension.
4. **Histogram and inspection polish.** A details-panel histogram tooltip exists, but clip warnings and a richer viewer inspection workflow remain open.
5. **HEIC/AVIF and animated viewer playback.** These are valid format-frontier items after reliability and color management, not before them.

Cross-folder catalog search could deliver value, but it conflicts with the explicit no-database scope. Treat it as a validated strategic decision, not an assumed missing requirement.

### 3B. UX and developer-experience gaps

🟡 **Structured filters are implemented but the UI specification still documents substring-only filtering.** See [specs/04-ui-behavior.md](../specs/04-ui-behavior.md#L133-L140). The toolbar cue text helps discovery, but there is no saved-filter affordance.

🟡 **Settings remain fragmented.** Performance settings are available, while slideshow, acceleration, theme, cache, and viewer behavior are distributed across menus. The existing roadmap's Settings/Tools consolidation is still justified if it preserves quick keyboard access.

🟡 **Documentation drift remains.** The architecture and migration specs still need a current-state pass for the hybrid D2D/GDI renderer and the 27k-line MainWindow. The UI spec also needs to reflect structured filters, slideshow settings, F2 behavior, metadata visibility, and current drag/drop workflows.

### 3C. Data and analytics

For a local MIT desktop tool, telemetry should remain opt-in. The higher-value near-term move is an explicit diagnostics export containing version, resource profile, cache stats, recent failure counters, and timing percentiles, with paths redacted by default.

### 3D. Competitive/domain gaps

Color management, synchronized compare, saved filters, histogram/clip warnings, and modern format support are the most scope-compatible gaps. Duplicate detection, face detection, and a Lightroom-style catalog remain correctly deferred.

### 3E. Investment versus value

The persistent cache currently carries high synchronization and index-rewrite complexity without a scalable persistence format. Hardening and simplifying this subsystem has higher immediate value than adding more transition effects or decode backends.

### 3F. Monetization and growth

Monetization instrumentation is not appropriate for the current local-first MIT product. Release quality, diagnostic export, and optional support links are sufficient. Do not add behavioral telemetry without a concrete product decision and explicit consent model.

---

## Test and Quality Posture

The broad smoke executable now contains focused scenarios for enumeration, decode, metadata, selection, file operations, viewer behavior, cache persistence, cache corruption, journal replay, folder-watch parsing, scheduler cancellation, and UI settings. CTest registers four entry points: `HyperBrowseSmoke`, `HyperBrowseViewerFitSmoke`, `HyperBrowseAppTextSizeSmoke`, and `HyperBrowseSettingsSmoke`. The important gaps are:

- no service-level burst, queue-rejection, or destruction tests for the new bounded executors;
- no close-during-file-operation or blocked-shell-operation test;
- no CI fallback matrix with nvJPEG disabled, sanitizers, or fuzz targets;
- no dedicated cache corruption/recovery counters in diagnostics;
- no focused test binaries for cache, watcher, and service domains; most scenarios remain in `tests/smoke.cpp`.

## Highest-Leverage Remaining Work

1. **Define file-operation shutdown.** Make close during shell work explicit and test cancellation, owner-window lifetime, progress completion, and bounded user-visible shutdown behavior.
2. **Validate bounded service execution.** Add burst, queue-rejection, and destruction tests plus executor diagnostics for the completed migration.
3. **Complete the quality matrix.** Add the nvJPEG-off fallback build, sanitizer/fuzz coverage for cache and RAW-helper boundaries, and a documented benchmark fixture/variance policy.
4. **Finish current-state documentation.** Align the architecture, UI behavior, and D2D migration specs with the implementation before adding more cross-cutting UI behavior.
5. **Then invest in product depth.** Prioritize saved filters, synchronized 3/4-up compare, and color-managed display ahead of lower-value format expansion.

See [ENHANCEMENT_ROADMAP_2026-07-30.md](ENHANCEMENT_ROADMAP_2026-07-30.md) for the prioritized implementation plan and completion ledger.
