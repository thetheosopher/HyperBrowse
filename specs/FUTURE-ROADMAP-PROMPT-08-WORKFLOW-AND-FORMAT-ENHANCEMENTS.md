# Long-Horizon Prompt 08: Workflow Depth and Format Readiness

Created: 2026-09-30

## Mission

Execute the next practical HyperBrowse enhancements in small, verified slices.
Prioritize reusable culling workflows, honest format support, and viewer-only
animation while preserving the native browser/viewer architecture and speed.
This is a persistent execution brief, not evidence that any listed feature has
shipped. The active roadmap and product spec remain authoritative.

Primary sequence:

1. B5 saved searches and smart folders.
2. E2/E5 installed HEIC and JPEG XL codec readiness.
3. B7 animated GIF/WebP viewer playback.

Follow-on queue:

4. B6 viewer histogram and clipping overlay.
5. E1 optional AVIF support, subject to the dependency gate below.

Take C5 Open Log Folder as a small adjacent improvement after B5 when its
existing log-path ownership and shell-launch contract are clear. Do not turn
that command into a log viewer or start new benchmarking infrastructure.

## Hard Boundaries

- GitHub Actions remain manually disabled at the user's direction. Do not
  enable, dispatch, push, or open a PR to trigger cloud execution.
- Preserve all existing worktree changes, especially B4's validated profile
  lookup and smoke-test isolation fixes. Do not reset, stash, revert, commit,
  create branches, or package unless explicitly requested.
- Reuse Win32, Direct2D/DirectWrite, WIC, the existing background executor,
  cache APIs, settings, metadata store, command routing, and accessibility.
- No editor, library database, watcher-backed search index, plugin system,
  cloud synchronization, or framework migration.
- Filesystem persistence, codec discovery, decode, frame composition,
  full-image histograms, and persistent-cache work stay off UI callbacks.
- Preserve valid visible content, selection, focus, compare identity, zoom,
  pan, alpha, orientation, and cancellation/stale-result rejection.
- Keep canonical caches independent of monitor profiles. Animation and
  histogram work must respect B4's source/display distinction.
- Do not expand into A5/A6 GPU work, D1-D3/A8 evidence calibration, or unrelated
  B2/C8 refactors. Their existing acceptance gates remain tracked separately.
- Do not manufacture physical-display or screen-reader evidence. Report
  unavailable hardware and manual verification plainly.

## Execution and Resume Rules

1. Read contribution guidance and applicable instructions. Record branch,
   HEAD, dirty files, active build tree, and the latest relevant validation.
2. Start at the current slice's owning implementation and nearby test. Form
   one falsifiable local hypothesis and one cheap discriminating check.
3. Make the smallest grounded edit, then immediately run focused validation.
   Repair that same slice before opening another.
4. Continue through compatible slices without pausing after each success.
   Stop only for a concrete blocker, failing gate requiring a decision,
   dependency approval, meaningful new risk boundary, or explicit user limit.
5. Update the ledger below after each completed slice: changed surfaces,
   actual checks/results, remaining risk, and the exact next action.
6. On resume, reuse completed work and verify the latest user request. Do not
   repeat repository-wide inventory or assume the ledger proves current code.
7. Distinguish implemented, automatically validated, manually reviewed, and
   released. A planned contract must not be advertised as shipped capability.

## Verified Starting Anchors

These anchors were inspected when the brief was created; re-check locally if
the owning code has changed:

- `BrowserPane::SetFilterQuery` trims text and updates the existing ordered
  view, selection accounting, list presentation, thumbnail scheduling, and
  state notifications.
- `BrowserPane.cpp` owns `StructuredFilterQuery`, token parsing, and matching.
  The first B5 slice now supports `type:raw` and exact recognized extensions
  alongside rating/tag/filename terms. Focused thumbnail/details coverage
  verifies case/dot normalization, conjunction, and literal invalid tokens.
- `MainWindow` owns the filter edit and forwards `EN_CHANGE` to the browser.
  Its current filter edit limit is 260 characters.
- MainWindow's menu is detached into a custom command bar. Tests must not
  assume `GetMenu(mainHwnd)` returns the visible File/View menus.
- Smoke tests isolate settings and default thumbnail-cache roots. Retain
  their inherited environment overrides and 64-message deadline batches.
  B5 also isolates the saved-search profile with
  `HYPERBROWSE_SAVED_SEARCH_DIRECTORY`.
- The latest pre-brief local Debug and Release presets passed 27/27. B4
  physical wide-gamut/multi-monitor/scaling review remains unverified.

## Phase 1: B5 Saved Searches

### Product Contract

- A saved search is a named filter expression, not a stored result list.
  Applying it filters the current browser folder/recursive scope using the
  existing browser filter owner. It does not navigate to an invented library.
- Provide File > Open Saved Search and Save Current Filter commands. Users
  can select, rename, update, and delete named searches through existing menu
  or dialog patterns. Disabled/empty/loading/failure states must be coherent.
- Saving a blank name or blank expression is rejected. Define bounds and
  duplicate-name behavior explicitly; do not silently overwrite another name.
- Persist under `%LOCALAPPDATA%\HyperBrowse\saved-searches.tsv`, without a new
  dependency. Retain Unicode names/expressions, escaped separators, and stable
  order. Support an injectable test path; tests must not touch a real store.
- Use bounded, versioned, strict parsing and atomic replacement. Malformed
  records must not corrupt valid records or cause arbitrary file access.
- Load/save off the UI thread. Handle failure visibly for an explicit user
  action without losing the in-memory list or clobbering the last valid file.
- Use the existing worker/result boundary with lifetime and generation checks.
  Close safely; a late load cannot overwrite edits made after it was queued.
- Decide the multiple-window writer contract before claiming cross-window
  consistency. Avoid stale whole-file writes that lose another window's edits.
- Do not run metadata extraction or persistent-thumbnail I/O to evaluate a
  search. Reuse available metadata and existing refresh notifications.
- Preserve current filename/rating/tag behavior. Add a bounded `type:raw`
  predicate and exact extension predicates only through the existing parser;
  use the established format classifier. Unsupported tokens must follow a
  documented compatibility rule rather than silently broadening matches.
- Applying a named search updates the filter edit, displayed results, command
  state, accessibility, and active-search identity consistently. Manual filter
  edits must not leave a misleading active saved-search indicator.
- Add no undocumented shortcut. Keep labels, accessibility, README, spec,
  user guide, and tests synchronized.

### Suggested Slices

1. Lock the current filter contract and cover RAW/exact-extension predicates.
2. Add the smallest suitable saved-search value/store and bounded round-trip,
   malformed input, duplicate, atomic-write, and failure coverage.
3. Integrate asynchronous load/write ownership, command routing, dialogs/menu,
   application of expressions, rename/update/delete, and stale-load handling.
4. Verify real-window application, restart persistence, selection/focus, and
   multiple-window behavior; update product documentation and roadmap status.

**Gate:** A named culling filter survives restart, applies through the same
browser path as typing it, and can be changed/deleted without UI stalls,
metadata changes, store corruption, or a separate search engine.

## Phase 2: E2/E5 Codec Readiness

- Separate extension recognition, installed decoder discovery, and successful
  decode. None proves the others.
- Inspect the existing WIC decoder/factory and format allowlists before adding
  a discovery helper. Enumerate/query codecs on a worker, cache a bounded
  readiness snapshot, and invalidate it through a defined refresh path.
- Cover HEIC and JPEG XL. Establish whether `.heif` has a decoder-backed,
  documented contract before extending its current allowlist.
- Represent ready, missing, discovery-failed, and decode-failed states clearly.
  Discovery failure must not disable a working decoder or block browsing.
- Report missing support in an existing appropriate UI/diagnostics surface.
  Avoid per-file modal spam and do not automatically install codecs.
- Use a deterministic injected discovery provider for normal tests. Codec-
  backed fixture decode is a separate available-hardware/installed-codec gate.
- Use generated or permitted fixtures with provenance. Never claim HEIC/JXL
  decoding from allowlist or mocked-discovery tests alone.
- Preserve WIC/RAW/nvJPEG fallback, existing dimensions/error contracts, and
  B4 embedded-profile handling. Record optional-codec limitations honestly.

**Gate:** Users can distinguish recognized formats from actually usable
codecs; discovery is nonblocking and deterministic tests cover all states.

## Phase 3: B7 Viewer Animation

- Use WIC and its frame/metadata interfaces for parsing and decode. Do not
  hand-roll a GIF/WebP parser or add a parallel codec/cache system.
- Thumbnails remain first-frame. Playback is viewer-only. Define still-image,
  compare, slideshow, and unavailable animated-codec behavior before editing.
- Preserve frame timing, loop counts, frame rectangles, transparency, disposal,
  and blend rules. Use bounded original fixtures to prove composition, not
  frame-count discovery alone.
- Decode/composite on workers. Keep a bounded frame window and honor resource
  profiles/memory pressure; do not decode all frames of an unbounded input.
- Use existing timer/DWM scheduling patterns for presentation. Avoid busy
  loops and accumulated timing drift; slow decode must not freeze input.
- Provide pause/play and frame stepping with accessible state. The roadmap's
  `Space`, `[`, and `]` bindings require a conflict audit against current input
  ownership before being installed and documented.
- Reject frames from prior source, navigation, playback, color-profile,
  setting, or window generations. Close and pause safely during decode.
- Keep composed frames canonical. Apply display conversion once, preserving
  alpha and valid visible content during profile/monitor changes.
- Tests cover loop/timing policy, disposal/blending, single-frame fallback,
  pause/step, rapid navigation, missing codec, bounded memory, and close.

**Gate:** Animated GIF and supported WebP playback is correct, bounded,
responsive, and compatible with color management; unsupported animation falls
back to an explicit stable first-frame presentation.

## Phase 4: Follow-On Queue

### B6 Histogram and Clipping

First verify what the details panel already computes. Reuse its data/helpers
where suitable. Define source-space versus display-space analysis explicitly;
do not infer exposure from arbitrary monitor-transformed pixels. Compute on a
worker, reject stale results, keep overlays out of source pixels, and preserve
alpha/orientation. Audit `H` and `Shift+H` for conflicts. Use bounded channel
fixtures for exact bins and clipping thresholds; avoid an editor or soft-proof
feature. Validate overhead and keep the overlay opt-in.

### E1 Optional AVIF

First establish installed-WIC capability and whether a fallback already exists.
If libavif is necessary, document license, transitive dependencies, supported
input bounds, maintenance, build opt-out, and packaging impact. Do not fetch,
vendor, enable a new dependency, or alter release packaging before that plan
is reviewed or explicitly authorized. Reuse the canonical decode and source-
profile boundaries; add malformed-input/fallback tests and permitted fixtures.

### C5 Open Log Folder

Resolve the directory through the existing log owner. Launch it through the
established shell helper, preserving focus/error behavior and respecting any
test log-path override. Add a Tools command with existing accessibility and
no new shortcut. Test the resolved destination through an injected shell seam;
do not open Explorer during unattended tests.

## Local Validation

- Use `vs2026-x64`, normal Debug/Release build presets, and tests enabled.
  Keep CUDA bundling disabled for normal development.
- Use the narrowest relevant smoke target first. Reuse existing test files and
  helpers; add a focused CTest selector where it enables a useful cheap gate.
- Complete full local Debug/Release build and CTest gates at a feature boundary.
  Do not use the release packaging target for routine validation.
- Confirm the exact launched executable is newer than its implementation
  sources; confirm test binaries are newer than their touched test sources.
- Preserve test cache/registry isolation, bounded pumps, and outer timeouts.
- Update README, product spec, user guide, architecture/testing docs, and
  roadmap as behavior ships. Do not bulk-edit unrelated roadmap statuses.
- Manual DPI/theme/accessibility/installed-codec/hardware gates remain separate
  from automated checks. Record unsupported or unavailable gates, not passes.
- Do not trigger GitHub Actions, commit, push, or package to close a gate.

## Completion Criteria

Each queue item is complete only when its product behavior, persistence and
lifetime contract, focused coverage, local build/test gates, and documentation
are verified. Unavailable external/manual gates are recorded plainly. Never
mark the entire queue complete because B5 passed, nor claim animation, AVIF,
codec availability, or physical color accuracy from a partial implementation.

## Persistent Execution Ledger

Update this section as work proceeds; keep evidence factual and brief.

| Item | State | Evidence / Next Action |
| --- | --- | --- |
| Baseline preservation | Recorded | B4 profile/isolation fixes are present in HEAD (`6e82fff`); preserve them. Prior exact Debug/Release presets passed 27/27. No commit, cloud run, or packaging operation was performed for Prompt 08. |
| B5 filter contract | Focused Gate Passed | Existing BrowserPane owner supports RAW/exact-extension conjunction and rating/tag/name composition, including details mode. |
| B5 persistence | Focused Gate Passed | SavedSearchStore: strict bounded UTF-8 TSV, restart, Unicode/escaping, duplicates, reload-under-lock mutations, committed snapshots, atomic replacement, and failure preservation. |
| B5 UI/integration | Software Surface Validated | File menu and inline Save share the existing dialog/filter/worker path. Open/save/update/rename/delete/reload, native accessible action, empty/busy state, icon pixels, and restart smoke passed in Debug/Release (1.71 s / 1.65 s in the final full runs). No activation refresh; it competed with modal save enqueueing. |
| B5 broader software gates | Passed | Debug/Release app, tests, and benchmark targets built; exact final B5/C5 presets passed 28/28 (55.04 s / 50.63 s). Tests on, fuzz off, CUDA bundling off. All six exact binaries verified newer than current changed/untracked implementation and test sources, including preserved user edits. |
| Validation lifecycle repair | Regression Covered | Native exception-teardown stack exposed a display-profile refresh posting through the viewer's retired borrowed executor. DisplayColorService now shuts down before executor release and rejects late requests. Color regression passed in both configurations; temporary probes and timeout overrides were removed, and original viewer pixel assertions are unchanged. |
| B5 inline Save | Completed | Fixed scaled Lucide icon with ISC notice, shared File command/state, narrow-width fallback, both painter guards, tooltip/MSAA action, and 96/144/192-DPI-metric/all-text-size layout checks. Native fixture restores maximized startup state before wide geometry. Physical DPI/theme review remains separate. |
| E2/E5 readiness | Software Validated | Bounded one-worker WIC discovery, cached state, explicit Snapshot refresh, native async diagnostics, independent thumbnail/full-image outcomes, concurrent observation preservation, shutdown/stale rejection, and redacted external text. Recognized extensions unchanged; `.heif` excluded. Final full Debug/Release passed 30/30 (56.05 s / 52.66 s). All six exact app/test/benchmark binaries verified fresh; C++ diagnostics and `git diff --check` clean. |
| E2/E5 installed inventory | Observed | Worker inventory created Microsoft HEIF Decoder and Microsoft JPEG XL Decoder on 2026-09-30. This proves discovery/creation only, not file decode. No optional-codec fixture was present under `tests`; actual HEIC/JXL fixture compatibility remains unverified. Renamed PNGs and injected providers are not optional-codec evidence. |
| B7 animation | Next | Begin at WIC frame/metadata ownership; lock composition/timing/cache rules with bounded original fixtures. |
| B6 histogram | Queued | Inspect existing details histogram before adding another producer. |
| E1 AVIF | Dependency Gated | Review optional decoder/dependency plan before downloading or packaging. |
| C5 Open Log Folder | Completed | Tools command uses the logger's real parent and existing shell/error helper. View-command callback tests capture the destination without Explorer, covering Unicode/relative/UNC paths and no new shortcut. Logging destination/behavior is unchanged. |
| Physical/release verification | Unverified | Preserve B2/B4/C8 manual sign-off; do not substitute mocks or this execution brief. |

Continuation note: the focused `--runtime` selector does not run command-bar
policy scenarios; use the new `--command-bar` selector. Rendered checks were
intermittent while the user was KVM-away; both final full gates passed after
return to the live session. Keep hardware/manual review distinct from these
software results.

E2/E5 validation note: initial Debug full run passed 29/30 with a startup-viewer
Escape/Fit Height assertion failure; unchanged combined smoke then passed,
followed by a full 30/30 Debug pass. No keyboard/viewer fix, skipped assertion,
timeout increase, codec/dependency installation, cloud run, commit, or packaging.

Immediate next action: begin B7 at the WIC frame/metadata owner and lock
viewer-only composition, timing, disposal/blending, loop, and bounded-cache
contracts. Keep genuine HEIC/JXL fixture validation separate, `.heif` excluded,
physical/manual gates unverified, and E1 dependency approval queued.
