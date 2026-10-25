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

Findings (2.1 inventory): the literals were not scattered; they lived in five
parallel hand-rolled palettes (`DialogTheme`, `MainWindow::ThemePalette`,
`BrowserPane::ThemeColors`, `DiagnosticsWindow::ThemeColors`, and the
`ViewerWindow` helper functions). Many values are identical across them (for
example `MainWindow` shares 9 of 12 fields with `DialogTheme`), but others are
near-duplicates that differ by a few RGB steps (for example text
(234,238,242) vs (236,240,245) vs (236,240,244)). The histogram channel colors
and the thumbnail rating gold are truly constant by design.

Design: `ui/ThemeTokens.h` holds the shared tokens as a pure function of
`ThemeVariant {Light, Dark, HighContrast}`; `DialogTheme` is an alias over it.
Windows take identical values from the tokens and keep window-specific values
local. Strict pixel identity is preserved, so near-duplicates are NOT merged.
Optional slice 2.3 (needs a visual sign-off): harmonize the near-duplicates onto
the tokens. This changes some colors by a few RGB steps.

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

Exit: `MainWindow.cpp` under ~8k lines; the constructor, `CreateChildWindows`
and `HandleMessage` decomposed into named helpers; ownership map updated; full
Debug and Release CTest green with the same test count. Decision 2026-10-24:
the "no function over ~200 lines" goal is relaxed. Remaining long functions
(dialog procedures, painters, context-menu builders) are cohesive, tested, and
not worth further risk; split them opportunistically when touched.

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
| 6 local quality and performance gates | 2026-10-24 | `tools/Check.ps1` (new), `tools/CheckPerf.ps1` (new), `tools/hooks/pre-push.sample` (new), `tools/TestStartupBenchmark.ps1`, `docs/perf/local-baseline.json` (new, machine `MIKE1`), `.clang-tidy`, `docs/testing.md` | `Check.ps1 -Quick` passes (11/11 focused tests, seconds); an unmatched `-Filter` fails with exit 1 (`--no-tests=error`). `Check.ps1 -Asan` passes end to end (configure, 138 s build, `HyperBrowsePersistentCacheFuzz` and `HyperBrowseRawHelperProtocolFuzz` 2/2). `Check.ps1 -Tidy -All` runs in about 72 s. On the 2560x1440 desktop: `Check.ps1 -Filter 'ItemNumberNavigation\|ViewerFit'` passes through the desktop preflight; `CheckPerf.ps1 -UpdateBaseline` recorded 9 metrics (0 failed runs) and a following `CheckPerf.ps1` compare shows no regression. Default and `-Release` full gates were not re-run through the script by the user's decision (the same CTest suites were 33/33 in Phases 4-5). | Done. **Tidy baseline: 199 findings in 142 files**, triaged in `docs/testing.md` (all `unused-return-value` are intentional `release()` hand-offs; one genuine unused local `undoDestination` in `MainWindowFileOps.cpp`). `bugprone-easily-swappable-parameters` (88) and `performance-no-int-to-ptr` (311, Win32 handle idiom) were disabled in `.clang-tidy` as noise; the first run had 598 findings. The perf gate is per-machine (keyed by computer name), advisory unless `-Strict`, and never touches `docs/perf/baseline.json` or `latest.md`. **Harness bug found and fixed by the live run:** the first live runs failed 2/5 and then 7/8 startup samples with "forced process termination after a 15-second shutdown timeout". The app was healthy; `TestStartupBenchmark.ps1` sent the close request to `Process.MainWindowHandle`, which resolved to a visible tooltip/`SysShadow` window whenever the mouse cursor hovered over the new window (so it only appeared with someone at the desk). The script now finds the `HyperBrowseMainWindow` by class for both the `WM_CLOSE` request and the File > Exit fallback; the same 8 runs then had 0 failures. A stale `build/benchmark-datasets` marker from September (manifest hash changed) also made dataset generation throw; it is git-ignored generated output and was regenerated. The pre-push hook is a sample only and is not installed. |
| 5 split `BrowserPane.cpp` and `ViewerWindow.cpp` | 2026-10-24 | `src/browser/BrowserPane.cpp`, `src/browser/BrowserPaneInternal.h` (new), 5 new `src/browser/BrowserPane<Area>.cpp` (`Layout`, `DetailsList`, `Messages`, `D2D`, `Gdi`), `src/viewer/ViewerWindow.cpp`, `src/viewer/ViewerWindowInternal.h` (new), 7 new `src/viewer/ViewerWindow<Area>.cpp` (`Compare`, `Settings`, `Rendering`, `Navigation`, `View`, `ContextMenu`, `Messages`), `CMakeLists.txt`, `docs/architecture.md` | Debug build of `HyperBrowse` and `HyperBrowseTests` compiles clean (no warnings). Line-multiset comparison against the originals: the only lines not found verbatim are the shared helpers that gained an `inline` prefix (11 in `BrowserPane`, 31 in `ViewerWindow`); everything else added is `#include`, namespace wrappers and braces. Release build also compiles clean. Focused Debug tests on the 2560x1440 displays, 11/11 passed: `HyperBrowseSmoke` (174 s), ViewerFit, ViewerInteraction, MultiViewerSettings, ColorManagement, ThumbnailStaleCompletion, ThumbnailFailure, AppTextSize, Accessibility, FolderTreeRestore, ItemNumberNavigation. By the user's decision the full Debug and Release suites were not re-run for this slice (the split is verbatim and the main smoke exercises both classes). | Done. Same method and design as Phase 4: shared helpers are `inline` in `browser_pane_shared` / `viewer_window_shared` inside the `*Internal.h` headers, single-area helpers stay in that file's anonymous namespace. No class changes; `BrowserPane.h` and `ViewerWindow.h` are untouched. Splitting `ViewerWindow::HandleMessage` (2.4k lines) is left as an opportunistic follow-up. |
| 4 steps 9-10 (constructor, `CreateChildWindows`, `HandleMessage`) | 2026-10-24 | `src/ui/MainWindow.cpp`, `src/ui/MainWindow.h` | Debug build of `HyperBrowse` and `HyperBrowseTests` compiles clean (no warnings). Line-multiset comparison against the committed file: 0 lines lost; 121 added lines are only function signatures, braces, call sites, `switch`/`default`/`break` and `return std::nullopt` scaffolding. Full Debug CTest 33/33 (333 s) and Release CTest 33/33 (229 s) on the 2560x1440 displays; registered test list identical to baseline. | Done. Constructor (642 lines) is now an init list plus `ConfigureAsyncMessageRouter`, `ConfigureTimerRouter`, `ConfigureFileCommandController` and `ConfigureViewCommandController` called in the original order. `CreateChildWindows` (531) delegates to `CreateShellControls(HFONT)`, `CreateDetailsPanelControls`, `ConfigureFolderLoadCoordinator`, `ConfigureFolderWatchChangeCoordinator`, `ConfigureFolderTreeController` and `QueueStartupFolderLoad`; its early `return false` paths stay in the parent. `HandleMessage` (418) forwards to `HandleCloseMessage`, `HandleDpiChangedMessage`, `HandleSystemStateMessage`, `HandleActivationAndMenuLoopMessage`, `HandleKeyMessage`, `HandleOwnerDrawMessage`, `HandleTimerMessage`, `HandleEraseBackgroundMessage` and `HandleDestroyMessage` in the style of the existing `Handle*Message` helpers (`std::nullopt` = fall through to `DefWindowProcW`). **Still over 200 lines (follow-up slice 4b):** `ExperimentalSettingsDialogProc` 723, `SlideshowSettingsDialogProc` 512, `PerformanceSettingsDialogProc` 472, `FileAssociationsDialogProc` 427, `ApplyCompletedFileOperation` 363, `LayoutExperimentalSettings` 313, `ImageInformationDialogProc` 293, `UpdateDetailsPanel` 290, `ShowFolderTreeContextMenu` 273, `ShowBrowserContextMenu` 273, `AboutDialogProc` 268, `PaintDetailsPanelD2D` 258, `LayoutChildren` 253, `ShowConsolidatedSettingsDialog` 247, `ConfigureViewCommandController` 234 (handler table, accepted), `PaintExperimentalSettings` 231, `OnLButtonUp` 213. |
| 4 (steps 1-8) split `MainWindow.cpp` by area | 2026-10-24 | `src/ui/MainWindow.cpp`, `src/ui/MainWindowInternal.h` (new), 17 new `src/ui/MainWindow<Area>.cpp`, `CMakeLists.txt`, `src/ui/MainWindow.h` and `src/ui/MainWindowViewers.cpp` (`ActiveViewerZoomPercent` test accessor), `tests/smoke_main_window.cpp`, `docs/mainwindow-ownership-map.md`, `docs/architecture.md` | Debug build of `HyperBrowse` and `HyperBrowseTests` compiles clean (no warnings). A line-multiset comparison against the original shows no source line lost or altered (only `#include`, namespace wrappers, the header comment and `inline` prefixes were added). Full Debug CTest 33/33 (335 s) and Release CTest 33/33 (229 s) on the 2560x1440 displays; registered test list identical to baseline. | Done for steps 1-8. Verbatim move by a one-off generator kept outside the repo. Helpers used by one area stay in that file's anonymous namespace (about 75% of helper lines); helpers used by several areas are stateless `inline` functions in `main_window_shared` inside `MainWindowInternal.h`. `MainWindow.cpp` keeps the constructor, `Create`, `CreateChildWindows`, `HandleMessage`, `WindowProc` and the `Handle*Message` helpers. **Escape-routing flake root-caused and fixed:** the first post-split Debug run failed `HyperBrowseSmoke` ("Escape routing did not apply the viewer's Fit Height behavior"). The same assertion also failed on the pre-split HEAD (1 of 7 runs in a temporary worktree), so the split is not the cause. Cause: the viewer's Escape handler leaves full screen and then `SetWindowFitMode` returns early while `currentImage_` is still null, so a test that pressed Escape as soon as the viewer title appeared raced the async decode (the window stays 1280x900). The test now waits for `MainWindow::ActiveViewerZoomPercent() > 0` first; 4 of 5 direct runs passed afterwards (the fifth failed in 0.02 s on a clipboard error, an environmental clipboard-ownership issue), then full Debug and Release were green. Other intermittent baseline flakes seen (not fixed): "Background executor retained an active-work count", "Inline Save remained enabled during a saved-search load", clipboard publish failure. Remaining for the exit criteria: steps 9-10 (constructor 642 lines, `CreateChildWindows` 531, `HandleMessage` 418, `ExperimentalSettingsDialogProc` 725, `ApplyCompletedFileOperation` 365) and `MainWindowExperimentalSettings.cpp` is 3.5k lines. |
| 3 split `tests/smoke.cpp` | 2026-10-24 | `tests/smoke.cpp`, `tests/smoke_support.h` (new), `tests/smoke_scenarios.h` (new), 10 new `tests/smoke_<area>.cpp`, `tests/CMakeLists.txt`, `docs/architecture.md`, `docs/testing.md` | Debug build compiles clean (no warnings). A line-multiset comparison against the original shows no source line lost or altered (only wrappers, includes, declarations and `inline` prefixes were added). Full Debug CTest 33/33 (433 s) and Release CTest 33/33 (236 s) on the 2560x1440 displays; registered test list identical to baseline. | Done. Verbatim move, generated by a one-off script kept outside the repo. Shared harness is `inline` in `smoke_support.h` (namespace `hyperbrowse::tests::smoke`); scenarios called from `main` are exported through `smoke_scenarios.h`, all other helpers sit in anonymous namespaces. `main`, switches and CTest registrations unchanged. Largest file is `smoke_settings.cpp` (~1.3k lines). |
| Triage (SavedSearchSmoke, Smoke) | 2026-10-24 | `tests/smoke.cpp`, `tests/CMakeLists.txt` | Focused runs, then full Debug CTest (338 s) | **32/32 green.** (1) `HyperBrowseSavedSearchSmoke`: the scenario's folder lived directly in `%TEMP%` (4,770 subfolders on the dev machine). The folder tree lists every sibling on the browsed path, which starved the modal dialog's 20 ms `WM_TIMER` responder for about 70 s, so the first tick already saw the 5 s deadline expired (`timedOut=1`). Fix: the scenario folder now lives under a dedicated parent next to the test executable (70 s to 2 s). A later "Inline Save" assertion also needs a wide desktop and fails on a 1024x768 fallback display (KVM); it passes on the 2560x1440 displays. (2) `HyperBrowseSmoke`: the suite takes about 164 s when run directly, so its 120 s limit was too short; raised to 300 s. One ctest run failed the "Escape routing ... Fit Height" assertion at about 100 s and two direct runs did not reproduce it; the message now prints the viewer rect and work area so a recurrence is diagnosable (suspect: viewer on a different monitor than the main window while the desktop was in flux). Follow-up candidate (product perf): folder-tree population of very large directories blocks the UI thread. |
| 0 Baseline | 2026-10-24 | none | Debug build + full CTest (438 s) | 30/32. Pre-existing failures: `HyperBrowseSmoke` (120 s timeout) and `HyperBrowseSavedSearchSmoke` (see Triage row). Unrelated to Phase 1. |
| 1.1 Quick Send mutex | 2026-10-24 | `src/util/NamedMutexLock.h` (new), `src/ui/MainWindow.cpp`, `tests/smoke_runtime.cpp` | `--runtime`, `--settings`, `--accessibility`, `--user-metadata`; full Debug CTest | Done. 250 ms bounded wait; timeout falls back to the existing unlocked degraded path and increments `quick_send.state_lock.timeout`. Other `INFINITE` waits (`FolderWatchService`, `Application` pipe listener) are worker loops that include a stop event; no change needed. Full Debug CTest 30/32, identical to baseline. |
| 1.2 catch-all audit | 2026-10-24 | `src/ui/MainWindowAccessibility.cpp`, `src/decode/WicDecodeHelpers.h` | Full Debug CTest | Done. 32 sites classified: 26 (a)/(b) already surface an error message or diagnostics counter; 2 (c) `WicDecodeHelpers.h` best-effort telemetry in `noexcept` helpers (comment added); 1 (a) `WicCodecReadinessService.cpp` queue failure falls through to a failed-discovery result; 3 (d) MSAA provider catches in `MainWindowAccessibility.cpp` now increment `accessibility.provider.*` counters. No new helper introduced. |
| 2.3 harmonize near-duplicates | 2026-10-24 | `src/viewer/ViewerWindow.cpp`, `src/ui/DiagnosticsWindow.cpp`, `src/browser/BrowserPane.cpp` | Debug build, then full Debug CTest (336 s) once the user returned to the interactive desktop | Done, 33/33. Visual sign-off given by the user (both themes). Rule: a value moves onto the shared token only if text, border or surface/panel and every RGB channel is within 6 steps; canvas backgrounds, muted text and other role-specific colors stay local. Changes: Viewer text/panel fill/panel border now tokens (dark text (236,240,244)->(234,238,242); dark panel (28,33,39)->(34,38,43); dark border (70,80,94)->(74,82,92); light text (28,33,40)->(32,36,40); light border (206,215,225)->(210,215,223)); Diagnostics text/panel/border now tokens (dark text +-2, dark panel (31,36,42)->(34,38,43), dark border (78,84,92)->(74,82,92)); BrowserPane text/surface now tokens, border dark identical so token, border light kept (9 steps). |
| 2.1 shared theme tokens | 2026-10-24 | `src/ui/ThemeTokens.h` (new), `src/ui/DialogTheme.h`, `src/ui/MainWindow.cpp`, `src/browser/BrowserPane.cpp`, `src/ui/DiagnosticsWindow.cpp`, `tests/smoke_policy.cpp`, `tests/CMakeLists.txt` | New `HyperBrowseThemeTokensSmoke` (`--theme-tokens`), theme/accessibility/menu smokes, full Debug CTest (339 s) | Done, 33/33. Shared tokens are a pure function of `ThemeVariant`; `DialogTheme` is an alias. `MainWindow::GetThemePalette` now takes 9 of 12 fields from tokens; `BrowserPane` takes accent/accentFill/selectionText; `DiagnosticsWindow` takes windowBackground; duplicated cache-stats highlight colors consolidated into `GetCacheStatsHighlightColor`. All values identical to before (pixel-neutral). The new smoke pins the shipped dark/light values and asserts high contrast maps to system colors. Histogram channel colors and thumbnail rating gold are constants by design. Viewer helpers have no identical values to share. |

### Metrics

| Metric | Baseline 2026-10-24 | Current |
|---|---|---|
| `MainWindow.cpp` lines | ~25,600 | 3,451 (17 new area files, largest `MainWindowExperimentalSettings.cpp` 3,526; plus `MainWindowInternal.h` 1,084) |
| `BrowserPane.cpp` lines | ~5,400 | 1,573 (5 new area files, largest `BrowserPaneLayout.cpp` 1,244; plus `BrowserPaneInternal.h`) |
| `ViewerWindow.cpp` lines | ~7,500 | 803 (7 new area files, largest `ViewerWindowMessages.cpp` 2,495 which holds the 2.4k-line `HandleMessage`; plus `ViewerWindowInternal.h`) |
| `tests/smoke.cpp` lines | ~8,100 | 247 (dispatcher only; largest test file `smoke_policy.cpp` 2,665, largest new file `smoke_settings.cpp` ~1,330) |
| `catch (...)` in `src/` | 32 | 32 (all classified) |
| Color literal lines in `src/` (RGB/0xRRGGBB grep) | 96 | 69 (18 of them are the single `ThemeTokens.h`) |
| Debug CTest | 30/32 (438 s) | 33/33 (335 s) |
| clang-tidy findings (`Check.ps1 -Tidy -All`) | not measured (598 before disabling two noise checks) | 199 in 142 files, triaged |

## Stop Conditions

Stop and ask the user if a slice would change user-visible behavior, a public
message contract, persisted formats, or threading/ownership rules, or if a
validation failure cannot be attributed to the current slice.
