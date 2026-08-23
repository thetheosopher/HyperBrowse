# HyperBrowse 2.0 Release Hardening Plan

Status: Active
Review date: 2026-08-23
Release target: 2.0.0
Baseline: 1.2.8

## Purpose

This is the release-readiness plan for the 2.0 major release. The goal is to
make the existing browser/viewer trustworthy under damaged input, slow or
changing filesystems, shutdown, installation, and repeated use. New features
should not outrun this work.

HyperBrowse remains a native Windows image browser and viewer. The 2.0 gate is
therefore centered on responsiveness, safe file workflows, predictable state,
and a reproducible release package rather than a larger feature surface.

## Baseline Evidence

- The current branch is clean and points at `master` / `origin/master`.
- The source and generated package metadata still report version `1.2.8`.
- Release `HyperBrowse` builds successfully with the current Visual Studio 2026
  preset.
- Debug test binaries build successfully.
- Release CTest passes all three registered tests.
- The full Release packaging target succeeds, including smoke tests, portable
  staging, CUDA runtime staging, ZIP creation, and Inno Setup compilation.
- The startup benchmark works with explicit nonzero budgets. One Release run
  measured approximately 366 ms to first window, 122 ms from window visibility
  to first thumbnail, and 488 ms total.
- `HyperBrowseViewerFitSmoke` is intermittently unstable under repetition and
  must be made deterministic before it is a useful release gate.
- There is currently no committed workflow under `.github/workflows`.

## Current Implementation Status

Completed in the working tree:

- Single-instance pipe reads use overlapped I/O, observe the listener stop
   event, reject remote clients, and use an explicit current-user ACL.
- Persistent thumbnail-cache index parsing rejects malformed numeric fields and
   unsafe cache filenames; index replacement is atomic; path-traversal
   regression coverage is present.
- RAW helper payloads use checked dimensions and byte counts, exact file-length
   validation, bounded allocation, and malformed-payload regression coverage.
- Viewer, thumbnail, metadata, file-operation, batch-conversion, and shared
   executor paths contain unexpected exceptions instead of terminating the
   process. The viewer target HWND is synchronized across worker threads.
- Viewer `0` and `1` shortcuts, folder-filter reset, status details, and the
   compact-thumbnail toggle are implemented and smoke-tested.
- Installer association refresh is enabled and user metadata writes use atomic
   replacement.
- Undo refuses incomplete source/destination mappings, and batch conversion
   reports filename exhaustion instead of reusing an occupied base path.
- The offline guide and UI behavior specification now use the current Settings
   hierarchy and shortcut/slideshow behavior.

Still open before the release gate can be marked green:

- Add an idle-client single-instance shutdown test and verify the ACL manually
   across multiple Windows sessions.
- Make cache maintenance asynchronous and replace whole-index rewrite-on-hit
   behavior with a bounded persistence strategy.
- Define cancellation/close behavior for shell file operations and make undo /
   redo history transactional after inverse-operation completion.
- Add malformed-index, RAW-helper, file-operation, watcher, and install/upgrade
   coverage to automated CI; stabilize the intermittent viewer-fit test.
- Add package manifest checks, nonzero startup-budget gating, optional code
   signing, and final 2.0.0 version/tag/package verification.

## Release Gate

The following items are required before publishing 2.0.0.

| Priority | Work item | Success condition |
| --- | --- | --- |
| P0 | Single-instance IPC lifecycle | A connected client that sends no data cannot prevent application shutdown; only the intended user can submit launch requests. |
| P0 | Persistent cache trust boundary | Malformed index entries and thumbnail payloads are rejected without allocation failure, path escape, data loss outside the cache, or process termination. |
| P0 | Background exception containment | Decode, metadata, file-operation, conversion, and executor failures become logged user-visible failures instead of terminating the process or leaving an operation permanently active. |
| P0 | RAW helper payload boundary | Dimensions, byte counts, and file lengths are checked before allocation; malformed helper output is rejected and covered by tests. |
| P0 | File-operation shutdown and history | Close-during-operation has defined behavior, and undo/redo never falls back to deleting or moving an unrelated source path. |
| P0 | Release installer behavior | Associations refresh immediately, install/upgrade/uninstall are tested, and the generated package contains only intended runtime files. |
| P0 | Automated validation | CI builds Debug and Release, runs CTest, exercises the startup benchmark with real budgets, and gates packaging on those results. |
| P0 | Deterministic tests | Viewer geometry and input tests do not depend on fixed sleeps or transient desktop activation state. |

## Work Packages

### A. Native IPC and lifecycle safety

1. Convert the connected-client pipe read in `src/app/Application.cpp` to an
   overlapped read observed together with the listener stop event.
2. Cancel and drain pending pipe I/O before closing its event and handle.
3. Apply an explicit security descriptor and reject remote pipe clients where
   supported. Validate forwarded launch paths before filesystem probing.
4. Add a test or manual harness for a client that connects, writes nothing,
   and keeps the pipe open while the primary application exits.

### B. Persistent cache hardening and scalability

1. Validate all parsed index fields strictly, including numeric overflow,
   positive dimensions, safe cache-file basenames, and representable byte
   counts.
2. Keep cache cleanup operations inside the cache directory even when the
   index is damaged.
3. Replace in-place index truncation with temporary-file write plus flush,
   close, and atomic replacement.
4. Keep cache maintenance off the UI thread and expose failures in diagnostics.
5. Follow up with an authoritative in-memory index and asynchronous access
   journaling so cache hits do not reparse and rewrite the complete TSV index.

### C. Async work and shutdown

1. Add exception boundaries at executor and worker entry points. Record the
   failure and post a completion/error update when a worker owns an active UI
   operation.
2. Define close-during-copy/move/delete/conversion behavior. Prefer canceling
   shell work where possible; otherwise keep a clear bounded shutdown state.
3. Make viewer async target-window publication synchronized with request and
   navigation generation checks.
4. Replace per-request `std::async` use in enumeration, conversion, and file
   operations with bounded executors where it materially limits thread churn.

### D. File workflows and user data

1. Make undo/redo transactional: retain exact source/destination pairs, only
   journal operations with complete inverse information, and change the undo
   and redo stacks after the inverse operation succeeds.
2. Prevent batch conversion from returning an existing filename after numeric
   suffix exhaustion.
3. Move multi-file JPEG orientation adjustment and full-image clipboard decode
   off the UI thread.
4. Make the ratings/tags file write crash-safe with atomic replacement and
   preserve the last valid snapshot after an interrupted write.

### E. UI contract and documentation

Resolve each mismatch before release by either implementing the behavior or
changing the contract consistently in the specs, README, and offline guide:

- clear the filename filter on folder changes;
- implement and document viewer `0` (fit) and `1` (actual size), or remove the
  stale shortcut claims;
- restore a real compact-thumbnail toggle, or explicitly make compact layout
  permanent and remove the dead command/state;
- show filter counts, batch progress, and viewer zoom where the UI contract
  promises them;
- reconcile the Settings menu paths, slideshow minimum, Escape behavior,
  shortcut lists, status-bar layout, and actual menu structure;
- update all version strings and release links for 2.0.0.

### F. Release engineering and distribution

1. Add committed CI for configure, Debug/Release builds, CTest, startup
   budgets, and package validation.
2. Use nonzero benchmark budgets after measuring a stable baseline and define
   an explicit variance policy for hosted runners.
3. Add package manifest checks so stale PDBs, libraries, debug artifacts, or
   unexpected DLLs cannot enter the installer through the wildcard source.
4. Review CUDA download transport settings and keep SHA256 verification
   mandatory. Document the pinned runtime versions.
5. Sign the installer and shipped executables when the distribution identity
   is ready; treat signing as a distribution polish item, not a substitute for
   functional validation.

## Test Matrix

The 2.0 candidate should pass all of the following on a clean checkout:

- Debug and Release builds with warnings visible.
- Full Release and Debug CTest suites, with repeated execution of UI-sensitive
  scenarios.
- Malformed disk-cache headers, index lines, path fields, and truncated files.
- Malformed RAW helper headers, oversized dimensions, overflowed byte counts,
  truncated payloads, and extra trailing data.
- Folder-watch add, remove, modify, and split rename notifications.
- Slow and unavailable folders, network/removable paths where available, and
  close during shell file operations.
- Copy, move, rename, duplicate, delete, paired RAW/JPEG, partial-success,
  conflict, undo, redo, and externally changed file scenarios.
- Single-instance launch of a folder, image, no argument, and an idle client
  that never writes.
- Installer first install, upgrade from 1.2.8, association refresh, default
  app handoff, uninstall cleanup, portable execution, and missing optional
  runtime fallback.
- Startup benchmark with real budgets on cold and warm runs.

## 2.0 Polish After the Gate

These are valuable but should not delay the reliability gate unless user
research makes them part of the release promise:

- synchronized 3-up/4-up compare and color-managed display;
- saved structured filters without introducing a catalog database;
- a compact status/performance inspector and redacted diagnostics export;
- modern format support such as HEIC/AVIF after decoder and licensing review;
- animated viewer playback and PSD/PSB previews.

Avoid adding a database, editor surface, telemetry, or more transition styles
to the 2.0 candidate without a separate product decision.

## Execution Order

1. Fix single-instance read cancellation and cache index validation.
2. Add malformed-input and shutdown regression coverage.
3. Contain worker failures and define file-operation shutdown/undo behavior.
4. Fix the installer association refresh and add package/CI gates.
5. Resolve UI contract mismatches and synchronize documentation.
6. Re-run the complete matrix, then decide which post-gate polish fits the
   remaining release window.

## Change Log

- 2026-08-23: Created from the comprehensive 2.0 release-readiness review.
- 2026-08-23: Confirmed Release build, Debug test build, Release packaging,
  explicit startup budgets, and current viewer-fit test flakiness.
- 2026-08-23: Implemented and validated the first IPC, cache, RAW protocol,
  exception, UI contract, persistence, and file-workflow hardening slices.