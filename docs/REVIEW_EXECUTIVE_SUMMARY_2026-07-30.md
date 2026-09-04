# HyperBrowse Executive Review Summary

Original review: 2026-07-30
Status refresh: 2026-09-03
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
- The startup gate is wired into `.github/workflows/ci.yml` with explicit 2.5 s / 2.5 s / 5 s budgets, CTest runs, artifact upload, and release-package validation.

## Top Three Remaining Risks

1. **Closing during shell file work is still not bounded.** Cancellation reaches the progress sink and teardown requests cancellation, but `FileOperationService` still joins its serialized worker synchronously while `IFileOperation::PerformOperations` can wait on slow storage, network paths, elevation, or shell UI.

2. **The bounded-service migration still needs focused validation.** Burst, queue-rejection, and destruction tests are not yet present, and executor queue/active-work diagnostics are not exposed. The selected capacities should also be measured against slow local, removable, and network paths.

3. **The quality and measurement matrix is incomplete.** One Windows workflow now gates startup and packaging, but there is no nvJPEG-off fallback job, sanitizer/fuzz target, persisted percentile rollup, deterministic benchmark fixture policy, or dedicated invalid-cache/watcher diagnostic counters. The persistent cache also retains a broad filesystem mutex that should be measured at large scale.

The earlier live-refresh and malformed-cache correctness issues are no longer in this risk list because they are implemented and covered by regression tests.

## Highest-Leverage Next Action

Define and test close-during-file-operation behavior, then add focused stress coverage and diagnostics for the completed bounded-service migration. Those are now the highest-leverage reliability/performance items because the July cache, watcher, and startup-gate actions are complete. After that, finish the CI fallback/diagnostics matrix, align the current-state specs, and take on saved filters plus professional compare/color management.

## Top Three Product Opportunities

1. **Professional compare and color management.** Synchronized 3/4-up compare, per-tile ratings, and monitor-profile-aware display deepen the photographer workflow without turning HyperBrowse into an editor.

2. **Saved structured filters.** Rating/tag query parsing already exists. Saving named filter expressions offers immediate workflow value without introducing a catalog database.

3. **Inspection and format depth.** Histogram/clip-warning workflow improvements, HEIC/HEIF detection, optional AVIF support, and animated viewer playback are sensible extensions after the reliability and compare work.

Cross-folder indexed search may be valuable, but it conflicts with the deliberate no-database scope and should follow user validation rather than assumption.

Documentation still needs a focused correction pass: current architecture and migration specs describe stale rendering state, understate MainWindow size, and omit several shipped workflows. The attached roadmap now separates completed work from the remaining queue.
