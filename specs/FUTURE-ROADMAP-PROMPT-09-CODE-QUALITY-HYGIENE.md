# Long-Horizon Prompt 09: Code Quality and Structural Hygiene

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Reduce structural risk in the codebase without changing shipped behavior.
The 2026-10-24 assessment found that velocity and review quality are limited
by a few very large translation units, one UI-thread blocking wait, scattered
hard-coded colors, a single monolithic smoke-test file, and the absence of any
automatic local quality gate now that hosted CI is disabled.

This plan is hygiene, not a product feature. Every slice must be behavior
preserving, independently buildable, and independently validated.

## Findings Being Resolved

| ID | Finding | Evidence (2026-10-24 scan) |
|---|---|---|
| Q1 | `MainWindow.cpp` is a monolith | ~25.6k lines, ~320 member definitions; constructor ~750 lines; `CreateChildWindows` ~530; `HandleMessage` ~420 with ~180 `WM_` cases; `ApplyCompletedFileOperation` ~365; `UpdateDetailsPanel` ~290 |
| Q2 | UI-thread blocking wait | `MainWindow::MutateQuickSendState` calls `WaitForSingleObject(mutex, INFINITE)` on the UI thread |
| Q3 | Unaudited `catch (...)` | 32 sites in `src/`; at least 4 swallow silently (`WicDecodeHelpers.h` x2, `WicCodecReadinessService.cpp`, `RawHelperMain.cpp`) |
| Q4 | Hard-coded colors | ~78 literals: `MainWindow.cpp` 31, `BrowserPane.cpp` 27, `DiagnosticsWindow.cpp` 10, `ViewerWindow.cpp` 7, histogram painter 3 |
| Q5 | Other oversized files | `ViewerWindow.cpp` ~7.5k lines, `BrowserPane.cpp` ~5.4k, `tests/smoke.cpp` ~8k, `tests/smoke_policy.cpp` ~2.6k |
| Q6 | No automatic static-analysis or sanitizer gate | `.clang-tidy` exists, CI is disabled, nothing runs it locally |
| Q7 | No-cloud policy leaves no regression net | Perf baselines at 0/10 hosted samples; no local relative gate |

Non-finding: build trees (`/build-*/`) and `*.log` are already git-ignored.

## Operating Rules

- Work in the current workspace. Do not commit, branch, reset, stash, or
  revert. Preserve unrelated user changes.
- Read `.github/copilot-instructions.md`, `.github/instructions/cpp.instructions.md`,
  `docs/architecture.md`, `docs/mainwindow-ownership-map.md`, and
  `docs/testing.md` before editing.
- Behavior-preserving only. A slice that needs a behavior change must call it
  out and add coverage for the change.
- Pure moves must be pure: move code verbatim, add the new file to
  `HyperBrowseCore` in `CMakeLists.txt`, and make no logic edits in the same
  slice. Do not reformat moved code.
- Keep UI-thread, worker-lifetime, cancellation, stale-result, and persistent
  cache rules intact (see the Threading and lifetime guidance).
- After every slice: build the touched targets, run the focused CTest names for
  the touched area, and record the result in the ledger below. Run the full
  Debug CTest preset at each phase boundary and the full Release preset at the
  end of Phases 3, 4, and 5.
- Never run the release packaging target as validation.
- Update `docs/mainwindow-ownership-map.md` and `docs/architecture.md` when
  ownership or file layout changes. Update `docs/testing.md` when test layout
  or gates change.

## Phase 0: Baseline and Safety Net

1. Record the Debug baseline: build time and full CTest result (pass count and
   duration) in the ledger.
2. Record structural metrics in the ledger (re-run at each phase boundary):
   line counts of the files in Q1/Q5, `catch (...)` count, color-literal count.
3. Add `tools/Check.ps1` (see Phase 6) early so later phases are guarded.
   Until it exists, use the commands in `docs/testing.md`.

Exit: baseline numbers recorded; full Debug CTest green.

## Phase 1: Small Fixes With Tests

### 1.1 Bounded Quick Send mutex wait (Q2)

- Add a small header-only RAII `util::NamedMutexLock` (`src/util/NamedMutexLock.h`):
  creates or opens a named mutex, waits with a caller-supplied timeout, reports
  `Acquired`, `AbandonedAcquired`, `TimedOut`, or `Unavailable`, and releases
  and closes in the destructor.
- Replace the `INFINITE` wait in `MainWindow::MutateQuickSendState` with a
  bounded wait (target 250 ms). On timeout or unavailability, keep the existing
  degraded path: apply the mutation to in-memory state and save without the
  lock (last writer wins). Log a counter or log line on timeout.
- Add a runtime smoke scenario in `tests/smoke_runtime.cpp`: one thread holds
  the mutex, a second acquire returns `TimedOut` within a bounded time, release
  allows acquisition, abandoned-mutex handling works.
- Audit the other `INFINITE` waits (`FolderWatchService`, `Application`) and
  record that they are worker-thread waits with shutdown events, or fix them.

### 1.2 `catch (...)` audit (Q3)

- Classify every site as: (a) converts to a user-visible or logged error,
  (b) increments a diagnostics counter, (c) intentionally silent best-effort
  cleanup, or (d) silent and unjustified.
- For (d): add logging or a counter through existing helpers. For (c): add a
  one-line comment only where the reason is not obvious.
- Do not introduce a new helper unless three or more sites share an identical
  pattern.
- Record the classification table in the ledger.

Exit: Quick Send never blocks the UI thread unboundedly; every catch-all is
classified; new runtime scenario passes in Debug and Release.

## Phase 2: Theme Palette (Q4)

- Inventory each color literal and classify it as theme-dependent,
  state-dependent (selected/hover/focus/error), or truly constant.
- Introduce one palette struct per theme (light and dark, plus a
  high-contrast mapping through `IsHighContrastEnabled` and system colors) in
  a single header near `DialogTheme.h` / `SystemTheme.h`. Follow existing
  naming and the existing Direct2D vs GDI ownership split; do not change the
  rendering architecture.
- Migrate file by file, smallest first: histogram painter, `DiagnosticsWindow`,
  `ViewerWindow`, `BrowserPane`, `MainWindow`. Each migration must produce
  identical pixels in the existing themes.
- Add a policy smoke that asserts palette completeness for every theme and
  that high-contrast mode maps to system colors.

Exit: no theme-dependent literals remain outside the palette; palette smoke
passes; existing theme and accessibility smokes still pass.

## Phase 3: Split `tests/smoke.cpp` (Q5, enables later phases)

- Split by scenario family into new files (for example `smoke_ui.cpp`,
  `smoke_thumbnail.cpp`, `smoke_viewer.cpp`, `smoke_fileops.cpp`) following the
  existing `smoke_*.cpp` + `smoke_*.h` pattern.
- Keep CTest names, command-line switches, and the dispatcher in `main`
  unchanged. Move shared helpers into a small internal header.
- Verify the registered CTest names and count are identical before and after.

Exit: identical CTest list; no file in `tests/` over ~3k lines; incremental
test builds are faster.

## Phase 4: Decompose `MainWindow.cpp` (Q1)

The repository already splits `MainWindowDialogs.cpp` and
`MainWindowAccessibility.cpp`; extend that pattern. Each slice moves a cohesive
group of `MainWindow::` member functions verbatim into `MainWindow<Area>.cpp`.
No new classes in the first pass. Suggested order (lowest coupling first):

1. Quick Send state and menus (`MutateQuickSendState`, registry load/save,
   `RefreshQuickAccessMenus`, shortcut ordering).
2. Settings persistence load/save and registry helpers.
3. Clipboard (`CopyDecodedImageToClipboard` and related).
4. Menu construction and menu-state refresh.
5. Details panel (`UpdateDetailsPanel` and helpers).
6. File-operation start/completion (`StartFileOperation`,
   `ApplyCompletedFileOperation`, `CaptureFileOperationCompletionContext`).
7. Diagnostics/snapshot (`ResetDiagnosticsState` and related).
8. Browser context menu and command dispatch.
9. Constructor and `CreateChildWindows`: break into named private phase
   functions (`CreateBrowserControls`, `CreateTreeControls`, `LoadSettings`, ...)
   preserving order and error handling exactly.
10. `HandleMessage`: only after the above, extract per-message-family handlers
    in the same style as the existing `Handle*Message` helpers.

Per slice: update `CMakeLists.txt`, build Debug, run the focused smokes
(`--settings`, `--accessibility`, `--dialog-geometry`, `--folder-tree-restore`,
`--file-rename`, `--responsive-panel`, plus the area's own), record line counts.
After slices 1, 4, 6, and 10 run the full Debug preset; run Release at the end.

Exit: `MainWindow.cpp` under ~8k lines; no function over ~200 lines except
`HandleMessage` dispatch tables; ownership map updated; full Debug and Release
CTest green with the same test count.

## Phase 5: `BrowserPane.cpp` and `ViewerWindow.cpp` (Q5)

Same verbatim-split method as Phase 4. Candidate seams:

- `BrowserPane`: layout and hit testing, painting (thumbnail grid vs details
  list), input and drag handling, scrolling and inertia, accessibility.
- `ViewerWindow`: decode and prefetch orchestration, compare mode, slideshow and
  transitions, input handling, overlays.

Exit: neither file over ~3.5k lines; focused viewer and browser smokes plus full
Debug and Release CTest green.

## Phase 6: Local Quality and Performance Gates (Q6, Q7)

- Add `tools/Check.ps1` with switches: `-Quick` (build + focused tests),
  default (Debug build + full CTest), `-Release`, `-Asan` (reuses the documented
  `HYPERBROWSE_BUILD_FUZZ_TESTS=ON` + `/fsanitize=address` configuration in a
  separate build directory), and `-Tidy` (clang-tidy on changed files using
  `.clang-tidy` and the compile database or the VS generator equivalent).
- Document it in `docs/testing.md` as the supported no-cloud gate.
- Local perf gate: wrap `tools/RunBenchmarks.ps1` with a committed local
  baseline file, a tolerance, and an explicit `-UpdateBaseline` switch. This
  must not re-enable, dispatch, or depend on hosted Actions. Record that
  relative thresholds are machine-specific and are advisory.
- Optionally add an opt-in git pre-push hook sample (not installed
  automatically).

Exit: one command reproduces the Debug+CTest gate; ASan and tidy runs are
documented and pass or have a recorded, triaged baseline of findings.

## Ledger

Update after every slice. Columns: slice, date, files touched, focused tests,
result.

| Slice | Date | Files | Validation | Result |
|---|---|---|---|---|
| Triage (SavedSearchSmoke, Smoke) | 2026-10-24 | `tests/smoke.cpp`, `tests/CMakeLists.txt` | Focused runs, then full Debug CTest (338 s) | **32/32 green.** (1) `HyperBrowseSavedSearchSmoke`: the scenario's folder lived directly in `%TEMP%` (4,770 subfolders on the dev machine). The folder tree lists every sibling on the browsed path, which starved the modal dialog's 20 ms `WM_TIMER` responder for about 70 s, so the first tick already saw the 5 s deadline expired (`timedOut=1`). Fix: the scenario folder now lives under a dedicated parent next to the test executable (70 s to 2 s). A later "Inline Save" assertion also needs a wide desktop and fails on a 1024x768 fallback display (KVM); it passes on the 2560x1440 displays. (2) `HyperBrowseSmoke`: the suite takes about 164 s when run directly, so its 120 s limit was too short; raised to 300 s. One ctest run failed the "Escape routing ... Fit Height" assertion at about 100 s and two direct runs did not reproduce it; the message now prints the viewer rect and work area so a recurrence is diagnosable (suspect: viewer on a different monitor than the main window while the desktop was in flux). Follow-up candidate (product perf): folder-tree population of very large directories blocks the UI thread. |
| 0 Baseline | 2026-10-24 | none | Debug build + full CTest (438 s) | 30/32. Pre-existing failures: `HyperBrowseSmoke` (120 s timeout) and `HyperBrowseSavedSearchSmoke` (see Triage row). Unrelated to Phase 1. |
| 1.1 Quick Send mutex | 2026-10-24 | `src/util/NamedMutexLock.h` (new), `src/ui/MainWindow.cpp`, `tests/smoke_runtime.cpp` | `--runtime`, `--settings`, `--accessibility`, `--user-metadata`; full Debug CTest | Done. 250 ms bounded wait; timeout falls back to the existing unlocked degraded path and increments `quick_send.state_lock.timeout`. Other `INFINITE` waits (`FolderWatchService`, `Application` pipe listener) are worker loops that include a stop event; no change needed. Full Debug CTest 30/32, identical to baseline. |
| 1.2 catch-all audit | 2026-10-24 | `src/ui/MainWindowAccessibility.cpp`, `src/decode/WicDecodeHelpers.h` | Full Debug CTest | Done. 32 sites classified: 26 (a)/(b) already surface an error message or diagnostics counter; 2 (c) `WicDecodeHelpers.h` best-effort telemetry in `noexcept` helpers (comment added); 1 (a) `WicCodecReadinessService.cpp` queue failure falls through to a failed-discovery result; 3 (d) MSAA provider catches in `MainWindowAccessibility.cpp` now increment `accessibility.provider.*` counters. No new helper introduced. |

### Metrics

| Metric | Baseline 2026-10-24 | Current |
|---|---|---|
| `MainWindow.cpp` lines | ~25,600 | 25,621 |
| `BrowserPane.cpp` lines | ~5,400 | 5,369 |
| `ViewerWindow.cpp` lines | ~7,500 | 7,562 |
| `tests/smoke.cpp` lines | ~8,100 | 8,098 |
| `catch (...)` in `src/` | 32 | 32 (all classified) |
| Color literals in `src/` | ~78 | ~78 |
| Debug CTest | 30/32 (438 s) | 30/32 (359 s) |

## Stop Conditions

Stop and ask the user if a slice would change user-visible behavior, a public
message contract, persisted formats, or threading/ownership rules, or if a
validation failure cannot be attributed to the current slice.
