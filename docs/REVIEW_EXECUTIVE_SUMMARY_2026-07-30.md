# HyperBrowse Executive Review Summary

Review conducted: 2026-07-30
Reviewer: GitHub Copilot
Perspectives: Software Optimization Engineering + Product Management
Codebase: HyperBrowse

HyperBrowse is a native Windows image browser/viewer built around a clear promise: fast thumbnail browsing, responsive large-folder navigation, and low-latency image viewing without the weight of an editor or catalog. The implementation is mature for its size. It uses Direct2D/DirectWrite, asynchronous decode and metadata pipelines, adaptive memory limits, optional GPU JPEG acceleration, a persistent thumbnail cache, practical file-management workflows, and broad smoke/integration coverage.

The current smoke suite passes. Several concerns from the May review are already resolved, including allocation-heavy cache-key comparisons, scheduler extension parsing, log-open diagnostics, decode-failure tooltips, and viewer delete cache invalidation.

## Top Three Performance and Reliability Risks

1. **Persistent-cache corruption can terminate the app.** The thumbnail reader trusts width, height, and byte-count fields before allocation. A partial or malformed cache file can request an unreasonable allocation from a worker thread and trigger process termination.

2. **Persistent-cache hits do too much serialized work.** Every hit reloads and rewrites the complete TSV index under a process-wide mutex. This no longer blocks the UI during delete invalidation, but it still serializes thumbnail workers and will scale poorly with large caches.

3. **Performance promises are not enforced in CI.** A startup benchmark script exists, but its budgets default to disabled, CTest registers only the smoke suite, and the repository contains no GitHub Actions workflow despite documentation claiming a gate exists.

A related correctness issue affects live refresh: rename old/new notifications are paired only within one watcher buffer. A split notification can leave the old thumbnail visible until reload. Closing during a long shell file operation can also block while the application waits synchronously for an operation it cannot cancel.

## Top Three Product Opportunities

1. **Professional compare and color management.** Synchronized 3/4-up compare, per-tile ratings, and monitor-profile-aware display deepen the photographer workflow without turning HyperBrowse into an editor.

2. **Saved structured filters.** Rating/tag query parsing already exists. Saving named filter expressions offers immediate workflow value without introducing a catalog database.

3. **External drag/drop and modern formats.** Explorer/mail drag-out, browser drop-in, HEIC/HEIF detection, and optional AVIF support are practical extensions after reliability work is complete.

Cross-folder indexed search may be valuable, but it conflicts with the deliberate no-database scope and should follow user validation rather than assumption.

## Highest-Leverage First Action

Harden the persistent thumbnail-cache reader and add malformed-cache tests. This is a small, isolated project that removes the only identified process-termination path and establishes the validation boundary needed for future cache scalability work. Next, fix split rename handling and wire the existing startup benchmark into CI with real budgets.

Documentation also needs a focused correction pass: current architecture and migration specs still describe the pre-Direct2D application, understate MainWindow size, and claim CI behavior that is not present in the repository.
