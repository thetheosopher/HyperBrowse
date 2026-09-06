# HyperBrowse Executive Review Summary

Original review: 2026-07-30
Status refresh: 2026-09-06
Reviewer: GitHub Copilot
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse

HyperBrowse is a native Windows image browser/viewer built around a clear promise: fast thumbnail browsing, responsive large-folder navigation, and low-latency image viewing without the weight of an editor or catalog. The implementation is mature for its size. It uses Direct2D/DirectWrite, asynchronous decode and metadata pipelines, adaptive memory limits, optional GPU JPEG acceleration, a persistent thumbnail cache, practical file-management workflows, and broad smoke/integration coverage.

The current branch is release 2.1.0. Since the original review, the branch has resolved the cache corruption and split-rename findings, added journal-based persistent-cache updates, moved cache maintenance off the UI thread, improved foreground thumbnail scheduling and large-folder presentation, and added a committed Windows CI workflow with nonzero startup budgets. The smoke executable now includes cache, watcher, scheduler, decoder, settings, and viewer regression scenarios, with four CTest entry points.

## Resolved Since July

- Persistent thumbnail payloads are validated for dimensions, exact BGRA byte counts, platform limits, and file length before allocation; invalid entries are removed without escaping worker exceptions.
- Folder-watch rename state survives split `ReadDirectoryChangesW` completions, with deterministic full-reload fallback and parser tests.
- Persistent-cache hits no longer rewrite the complete TSV index. An in-memory index, append-only journal, replay, and atomic compaction are now implemented, with persistence work off the UI thread.
- Folder enumeration, folder-tree queries, batch conversion, and file operations now run through bounded member-owned executors instead of request-per-task `std::async`, with request cancellation and shutdown-aware completion posting preserved.
- `BackgroundExecutor` now has deterministic coverage for exception isolation, active and pending work, queue rejection, and destruction behavior; all four service wrappers expose pending, active, rejected, and cancellation/supersession state, with queue rejection and cancellation diagnostics.
- Service-level smoke coverage now checks bounded state after completion and deterministic file-operation saturation/destruction behavior. The startup gate is wired into `.github/workflows/ci.yml` with explicit 2.5 s / 2.5 s / 5 s budgets, CTest runs, artifact upload, and release-package validation.
- The current-state architecture, UI behavior, D2D migration, and backlog specifications were audited and synchronized with the shipped implementation.

## Top Three Remaining Risks

1. **Closing during shell file work can still delay destruction.** The user-visible close state is explicit and reports a five-second wait notice, but `FileOperationService` still joins its serialized worker synchronously while `IFileOperation::PerformOperations` can wait on slow storage, network paths, elevation, or shell UI. Detaching that worker would risk late posts and orphaned shell state.

2. **The bounded-service migration still needs production-scale measurement.** Deterministic executor and service tests now cover queue rejection, cancellation/supersession, active/pending counts, completion suppression, and destruction behavior. Selected capacities still need measurement against slow local, removable, and network paths, including realistic burst patterns; live queue snapshots are not yet shown in the diagnostics window.

3. **The quality and measurement matrix is incomplete.** The Windows workflow now runs nvJPEG-on and WIC-fallback Debug/Release test and startup legs, with deterministic fixture and hosted-run variance policy documented. A dedicated sanitizer/fuzz preset, persisted percentile rollup, and invalid-cache/watcher diagnostic counters remain; the persistent cache also retains a broad filesystem mutex that should be measured at large scale.

The earlier live-refresh and malformed-cache correctness issues are no longer in this risk list because they are implemented and covered by regression tests.

## Highest-Leverage Next Action

Validate the MainWindow close path against shell prompts and slow-storage conditions, then measure the bounded services under realistic burst patterns. Those are now the highest-leverage reliability/performance items because the service contract, utility/service lifecycle coverage, CI decode matrix, and current-state documentation are in place. After that, add live queue snapshots and a dedicated sanitizer/fuzz preset, then take on saved filters plus professional compare/color management.

## Top Three Product Opportunities

1. **Professional compare and color management.** Synchronized 3/4-up compare, per-tile ratings, and monitor-profile-aware display deepen the photographer workflow without turning HyperBrowse into an editor.

2. **Saved structured filters.** Rating/tag query parsing already exists. Saving named filter expressions offers immediate workflow value without introducing a catalog database.

3. **Inspection and format depth.** Histogram/clip-warning workflow improvements, HEIC/HEIF detection, optional AVIF support, and animated viewer playback are sensible extensions after the reliability and compare work.

Cross-folder indexed search may be valuable, but it conflicts with the deliberate no-database scope and should follow user validation rather than assumption.

The architecture, MainWindow ownership, UI behavior, and Direct2D migration documents now reflect the shipped implementation and current `MainWindow.cpp` size. The attached roadmap marks the MainWindow cleanup pass complete while preserving the remaining measurement, tooling, and product queue.
