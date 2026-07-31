# HyperBrowse Comprehensive Code Review

Review conducted: 2026-07-30
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
- One broad smoke/integration executable registered with CTest.

The first-party `src/` tree is approximately 41,800 physical lines. The largest units are `src/ui/MainWindow.cpp` (~15,970 lines), `src/browser/BrowserPane.cpp` (~5,110), and `src/viewer/ViewerWindow.cpp` (~4,680). The code generally uses RAII, `ComPtr`, request epochs, bounded queues/caches, and ownership-safe `PostMessageW` payload transfer.

### Review scope and validation

The review covered first-party source, tests, CMake/package scripts, README, and specs. Vendored source was reviewed only at its integration boundary. `HyperBrowseSmoke` passed on 2026-07-30. The worktree contained an uncommitted sort-state synchronization change in `src/ui/MainWindow.cpp`; its three hunks were reviewed and no defect was found.

The May 31 review was used as a baseline, not copied forward. Allocation-free cache-key comparison, manual extension scanning, one-time log-open warnings, decode-failure tooltips, path/cache tests, and viewer delete bitmap invalidation are present today.

Severity: 🔴 Critical | 🟠 High | 🟡 Medium | 🔵 Low

---

## Phase 2: Performance and Technical Review

### 2A. Algorithmic and computational efficiency

🟡 **The persistent cache turns every hit into serialized whole-index I/O.**

`DiskThumbnailCache::TryLoad` takes a process-wide filesystem mutex, reloads the full TSV index, updates one access ordinal, and rewrites the full index before reading the thumbnail ([src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L329-L346)). `Store`, invalidation, statistics, and maintenance share the same mutex. Thumbnail workers therefore serialize behind O(n) index parsing and rewriting as the cache grows. Moving invalidation off the UI thread fixed the worst visible delete stall, but cache hits still consume decode-worker throughput.

**Recommendation:** keep an authoritative in-memory index, journal access updates asynchronously, use atomic snapshot replacement during compaction, and move all persistent-cache I/O behind one low-priority cache executor.

🟡 **Four services still create one OS thread per request.**

Folder enumeration, folder-tree enumeration, file operations, and batch conversion use `std::async(std::launch::async)` ([src/services/FolderEnumerationService.cpp](../src/services/FolderEnumerationService.cpp#L244), [src/services/FolderTreeEnumerationService.cpp](../src/services/FolderTreeEnumerationService.cpp#L179), [src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L498), [src/services/BatchConvertService.cpp](../src/services/BatchConvertService.cpp#L353)). Rapid navigation normally cancels old enumeration through request IDs, but blocked network/removable-volume calls can leave multiple threads alive.

**Recommendation:** route cancellable short work through bounded executors and reserve a dedicated serialized executor for shell file operations.

### 2B. Persistent data layer

🟠 **A corrupt thumbnail file can allocate attacker-controlled memory and terminate the process.**

The cache reader validates only nonzero dimensions and byte count, then directly sizes a vector from `header.pixelBytes` before checking whether it equals `width * height * 4` or fits the file ([src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L371-L383)). Cache files are user-writable and are written directly with truncation. A partial write, disk corruption, or local modification can advertise a huge byte count. `std::bad_alloc` or `std::length_error` then escapes the thumbnail worker's `std::thread` entry point and invokes `std::terminate`.

**Recommendation:** reject dimensions above explicit limits, use checked multiplication, require the exact BGRA byte count, compare it with the indexed/actual file length, and catch cache/decode exceptions at the worker boundary. Delete or quarantine invalid entries.

🟡 **Index snapshots are not crash-safe.**

`SaveIndexLocked` opens `index.tsv` with `std::ios::trunc` and writes in place ([src/cache/DiskThumbnailCache.cpp](../src/cache/DiskThumbnailCache.cpp#L778-L786)). A crash or storage failure can leave a partial index and orphan otherwise-valid thumbnails. This is recoverable cache data, not user data, but startup hit rate and disk usage degrade silently.

**Recommendation:** write `index.tsv.tmp`, flush/close it, then atomically replace the index. Treat malformed lines as a signal to compact or rebuild.

### 2C. Concurrency, async, and I/O

🟠 **Folder renames can lose the old path across notification completions.**

`pendingRenameOldPath` is created inside each completed `ReadDirectoryChangesW` buffer processing pass ([src/services/FolderWatchService.cpp](../src/services/FolderWatchService.cpp#L244-L268)). If `FILE_ACTION_RENAMED_OLD_NAME` ends one completion and `FILE_ACTION_RENAMED_NEW_NAME` arrives in the next, the service emits a rename with an empty old path. MainWindow then upserts the new path but cannot remove the old model entry ([src/ui/MainWindow.cpp](../src/ui/MainWindow.cpp#L12347-L12357)), leaving a stale thumbnail until a reload.

The only watcher smoke coverage starts and stops the service; it does not assert event semantics ([tests/smoke.cpp](../tests/smoke.cpp#L858-L868)).

**Recommendation:** retain the pending old name in watcher state across completions. If a new name arrives without an old name, emit a full-reload requirement or a conservative add event rather than a malformed rename.

🟡 **Application shutdown can wait indefinitely for shell file operations.**

`FileOperationService::~FileOperationService` synchronously waits for every future ([src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L477-L479), [src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L665-L673)). A worker may be blocked in `IFileOperation::PerformOperations` ([src/services/FileOperationService.cpp](../src/services/FileOperationService.cpp#L627)) on a long copy, unavailable network destination, elevation UI, or shell conflict prompt. There is no service-level shutdown/cancel signal.

**Recommendation:** explicitly define close-during-operation behavior. Prefer requesting cancellation through a progress sink, keeping the owner alive until completion, and showing a bounded shutdown state rather than blocking an apparently closed app.

### 2D. Memory and resource management

🟠 The corrupt-cache allocation above is the primary memory safety/reliability concern. Normal runtime cache sizing is adaptive and bounded, COM/GDI resources are generally RAII-managed, and no first-party leak was identified.

### 2E. Network and API efficiency

The runtime product is local-first. Network/removable paths still matter because filesystem enumeration and shell operations can block in OS APIs despite request cancellation; bounded executors and shutdown policy should account for this.

### 2F. Build, bundle, and startup

🟠 **The repository claims a CI performance gate that is not wired.**

The startup script supports budgets, but all three default to disabled (`0`) ([tools/TestStartupBenchmark.ps1](../tools/TestStartupBenchmark.ps1#L13-L15)). CTest registers only `HyperBrowseSmoke` ([tests/CMakeLists.txt](../tests/CMakeLists.txt#L38)), and no `.github/workflows/` files exist. This conflicts with the statement that GitHub Actions runs the gate ([specs/14-todo.md](../specs/14-todo.md#L224-L227)). Performance is the product's stated differentiator, so this is a release-control gap rather than optional polish.

**Recommendation:** commit a CI workflow with nonzero budgets, a deterministic fixture, artifact upload, and a documented variance policy.

🟡 **`MainWindow.cpp` is now approximately 15,970 lines.**

The architecture spec still describes it as ~3,900 lines ([specs/02-architecture.md](../specs/02-architecture.md#L52)). The unit owns menus, toolbar, dialogs, persistence, folder tree, drag/drop, file operations, viewer coordination, and watch reconciliation. This raises incremental build time and makes high-risk workflow changes difficult to isolate.

### 2G. Observability and reliability

- Diagnostics and startup JSON are strong foundations.
- There is no automated field snapshot export or persisted percentile rollup.
- Persistent-cache corruption is neither logged nor counted.
- Watcher fallback/reload reasons are not represented as diagnostics counters.

### 2H. Security/performance intersections

🟠 The unchecked cache header is both a denial-of-service path and an unbounded allocation path. The cache is local-user scoped, which lowers remote exploitability but does not make corrupted-file handling optional.

🔵 RAW decoding has a useful out-of-process isolation option. Keep it enabled by default and test helper timeout/termination behavior with each LibRaw update.

### 2I. Dependency health

- Vendored LibRaw is 0.22.1 ([external/libraw/libraw/libraw_version.h](../external/libraw/libraw/libraw_version.h#L23-L25)); no repository dependency inventory or update cadence was found.
- CUDA redistributables are hash-pinned, which is good, but currently pin cudart 12.6.77 and nvJPEG 12.3.3.54 ([cmake/BundleCudaRedistributables.cmake](../cmake/BundleCudaRedistributables.cmake#L104-L114)). Currency should be reviewed at release time.
- NanoSVG is vendored and used only for a small integration surface.

---

## Phase 3: Product and Feature Review

### 3A. Feature completeness

The product is notably more complete than the May review indicated: structured rating/tag filters, adaptive resource profiles, viewer metadata, persistent cache controls, folder organization, browser-to-tree drag/drop, slideshow settings/transitions, and quick viewer delete are implemented.

The strongest evidence-based opportunities still inside the browser/viewer scope are:

1. **n-up compare with synchronized zoom/pan.** Two-up compare exists; 3/4-up culling and synchronized inspection remain planned ([specs/14-todo.md](../specs/14-todo.md#L265-L278)).
2. **Color-managed display.** This is material for photographers and remains absent ([specs/14-todo.md](../specs/14-todo.md#L289-L296)).
3. **Saved filter views.** Structured `rating:` and `tag:` parsing already exists ([src/browser/BrowserPane.cpp](../src/browser/BrowserPane.cpp#L300-L305), [src/browser/BrowserPane.cpp](../src/browser/BrowserPane.cpp#L377-L400)); persisting named expressions is a small, scope-compatible extension.
4. **External drag-out/drop-in.** Browser-to-tree organization exists, but Explorer/mail/chat drag-out and browser drop-in remain a practical workflow gap.
5. **HEIC/AVIF and animated viewer playback.** These are valid format-frontier items after reliability and color management, not before them.

Cross-folder catalog search could deliver value, but it conflicts with the explicit no-database scope. Treat it as a validated strategic decision, not an assumed missing requirement.

### 3B. UX and developer-experience gaps

🟡 **Structured filters are implemented but the UI specification still documents substring-only filtering.** See [specs/04-ui-behavior.md](../specs/04-ui-behavior.md#L133-L140). The toolbar cue text helps discovery, but there is no durable syntax reference or saved-filter affordance.

🟡 **Settings remain fragmented.** Performance settings are available, while slideshow, acceleration, theme, cache, and viewer behavior are distributed across menus. The existing roadmap's Settings/Tools consolidation is still justified if it preserves quick keyboard access.

🟡 **Documentation drift is substantial.** The architecture documents claim GDI-only rendering and no D2D implementation ([specs/02-architecture.md](../specs/02-architecture.md#L8-L14), [specs/02-architecture.md](../specs/02-architecture.md#L93-L97)); the D2D migration plan also describes the pre-migration state as current ([specs/15-d2d-rendering-migration.md](../specs/15-d2d-rendering-migration.md#L26-L35)). The UI spec says slideshow interval UI and F2 rename are absent even though both are implemented.

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

`HyperBrowseSmoke` passed and covers a broad set of enumeration, decode, metadata, selection, file operation, and viewer behaviors. The important gaps are narrower than “no unit tests”:

- no persistent `DiskThumbnailCache` read/write/corruption tests;
- no folder-watch add/remove/rename semantic tests;
- no close-during-file-operation test;
- no CI workflow or enforced startup budget;
- no sanitizer/fuzz target for cache/index and RAW-helper protocol parsing.

## Highest-Leverage Action

Harden `DiskThumbnailCache::TryLoad` and add malformed-cache tests first. It is a small change that closes the only identified process-termination path, creates a foundation for cache-format changes, and is independently verifiable.

See [ENHANCEMENT_ROADMAP_2026-07-30.md](ENHANCEMENT_ROADMAP_2026-07-30.md) for the prioritized implementation plan.
