# Long-Horizon Prompt 06: Performance UI Surfaces and Breadcrumb Navigation

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Complete roadmap items C1, C2, C3, and C7 as a coordinated set of native UI
improvements:

- Complete the existing About dialog with build, graphics-adapter, and startup
  performance information (C1).
- Refine idle empty states with a muted app icon above their existing prompt
  text in the browser and viewer (C2).
- Add an opt-in performance HUD using existing diagnostics and resource
  telemetry (C3).
- Add a clickable breadcrumb path bar above the browser pane (C7).

Work in small, locally validated phases. Reuse the current Win32, Direct2D,
DirectWrite, diagnostics, navigation, resource-profile, and accessibility
ownership boundaries. The result must remain responsive on large folders and
must not weaken existing startup, thumbnail, or viewer performance contracts.

## Current Cloud-Execution Hold

The user has opted out of cloud-hosted GitHub Actions. The repository's only
workflow is manually disabled. Do not re-enable it, dispatch it, or trigger a
cloud run through a push or pull request without fresh, explicit user
authorization. Validate locally. If a gate cannot be satisfied without cloud
execution, report that gate instead of starting a workflow.

## Relationship to Other Work

- [The active roadmap](FUTURE-ROADMAP.md) is the source of product scope; this
  prompt is the coordinated C1/C2/C3/C7 execution brief.
- These items are not all greenfield. The current tree has a custom About
  dialog, a Direct2D browser placeholder that already draws brand art, and a
  main-window title that includes the active folder path. Verify their exact
  behavior and extend them instead of creating parallel surfaces.
- Keep the current rendering ownership split: BrowserPane and ViewerWindow
  use Direct2D/DirectWrite; shell and dialog surfaces retain their existing
  Win32/GDI or established hybrid implementation.
- Reuse `util::Diagnostics`, existing startup measurements, current adapter
  information, and `ResourceProfile` where they provide the required data.
  Do not add a second diagnostics store or a general telemetry framework.
- C3 is an in-process display feature, not a new benchmark runner. Do not
  calibrate or change A8/D3 baselines, enable hosted evidence collection, or
  change absolute performance budgets.
- C8 accessibility release verification, C9 title-bar simplification, and
  unrelated MainWindow decomposition remain separate work. New controls must
  still follow the accessibility contract already available in the touched
  surfaces.

## Starting Facts to Verify

Treat these as source-finding hints and confirm them in the current tree before
editing:

- `MainWindow::ShowAboutDialog` and `AboutDialogState` own an existing custom
  About surface, its icon/art, layout, theme, DPI handling, and build identity.
  Confirm exactly which requested C1 fields are already displayed and which
  are missing; do not replace the dialog with a MessageBox or a second window.
- The About dialog currently loads a 48-pixel hero icon. Confirm the app icon
  resource, intended 64-pixel display size, DPI scaling, and compact-screen
  layout before changing its geometry.
- `util::Diagnostics` exposes generic timing, counter, and derived-value
  snapshots plus startup benchmark recording. Trace the actual startup fields,
  their recording point, lifetime, thread-safety, and availability semantics.
  Do not infer an unavailable timing as zero or describe an uncalibrated local
  sample as a baseline.
- `BrowserPane::D2DDrawPlaceholderState` already distinguishes no-folder,
  loading, empty-folder, error, and no-match states and can draw brand art.
  Determine which idle state uses the "Select a folder to begin browsing"
  prompt, and preserve the other state-specific messages and visuals.
- Locate the viewer's actual no-image/empty state before implementing its C2
  watermark. Do not clear, cover, or replace a valid image during asynchronous
  navigation or transitions.
- `MainWindow::UpdateWindowTitle` includes `BrowserModel::FolderPath()`. Trace
  the current folder-load/navigation path, folder-tree synchronization, and
  navigation history before adding breadcrumb clicks.
- Search the full command and shortcut maps for `Ctrl+Shift+P`, inspect the
  View/Tools command surfaces and existing HUD-like overlays, and confirm
  current UI smoke/test seams before allocating commands or timers.
- Trace the actual producer and semantics for each proposed HUD value:
  decode count, scale time, thumbnail-cache hit rate, memory-pressure state,
  and thumbnail-worker queue depth. Record units, aggregation/window, and
  unavailable behavior; a cumulative counter must not be labeled as current
  in-flight work.

## Hard Scope Boundaries

- Implement only C1, C2, C3, and C7. Do not add a settings overhaul, a new
  diagnostics window, title-bar redesign, saved searches, or a rendering
  framework migration.
- Do not add dependencies, persistent settings, or new background services
  unless a verified existing ownership gap makes them necessary and the
  scope is documented before implementation.
- Do not perform filesystem enumeration, image decode, diagnostics formatting,
  blocking cache access, or other potentially slow work from `WM_PAINT`, a
  menu callback, or a window procedure.
- Do not sample or allocate HUD state on every paint while the HUD is disabled.
  Disabled means no HUD timer, polling, snapshot formatting, or extra per-frame
  work. Use existing telemetry; add only the smallest necessary producer-side
  counter if an explicitly required live value has no existing source.
- Do not change existing shortcuts without checking and documenting conflicts.
  Keep the proposed `Ctrl+Shift+P` toggle only if the complete current shortcut
  map confirms it is free.
- Do not silently change browser loading, error, no-match, empty-folder,
  selection, focus, or navigation behavior to make the new UI fit.
- Preserve all unrelated user changes and generated evidence. Do not reset,
  clean, stash, or revert the worktree. Do not commit or push unless separately
  requested.

## Phase 0: Inventory and Local Gate

1. Read `.github/copilot-instructions.md`, applicable C++ instructions,
   `docs/architecture.md`, `docs/testing.md`, the C1/C2/C3/C7 roadmap entries,
   `specs/PRODUCT_SPEC.md`, relevant user-guide sections, and the owning source
   and smoke-test code.
2. Record the current branch, HEAD, dirty files, build tree, and available
   focused tests. Preserve user work and do not inspect or start cloud Actions.
3. Trace About creation and painting, browser/viewer empty states, diagnostics
   production and snapshot access, browser layout/resizing, command routing,
   keyboard focus, folder navigation, and accessible names/states.
4. Define the HUD's initial surface, exact metric semantics, refresh behavior,
   unavailable display, toggle scope, and off-by-default behavior. The default
   scope is the main browser content surface because the listed queue/cache
   values describe thumbnail work. Do not add a viewer HUD unless current
   product behavior or a concrete user workflow establishes that need.
5. Define breadcrumb behavior for drive roots, UNC paths, long paths, narrow
   windows, and DPI changes. Segment activation must route through the existing
   folder-navigation path.

**Gate:** Before behavior edits, write down the confirmed C1 data sources,
empty-state applicability, HUD metric definitions and surface, shortcut
availability, breadcrumb path rules, and the owners each change will use. If a
required value or navigation route cannot be supplied without violating an
ownership boundary, resolve that boundary before proceeding.

## Phase 1: C1 About Information and Startup Summary

1. Extend the existing custom About dialog. Reuse its theme, DPI, modal,
   resource-lifetime, and layout behavior.
2. Show the app icon at 64 logical pixels, scaled for the active DPI; include
   the current version and available build information from generated build
   metadata rather than hard-coded strings.
3. Show the graphics-adapter vendor only from an existing safe source. If the
   adapter is unavailable or the app is using a fallback, present a truthful
   fallback label rather than probing synchronously on the UI thread.
4. Show the startup values using the established A8 definitions. Label the
   process-start-to-first-window-visible span as startup time and the
   first-window-visible-to-first-thumbnail-painted span as first-thumbnail
   time. If the established producer uses different spans, retain its exact
   semantics and label them explicitly; do not calculate a misleading value.
5. Define the behavior when a value was not recorded or is not yet available.
   Use a clear unavailable state, never `0 ms`. The About dialog need not start
   a new benchmark or trigger additional work.
6. Keep the dialog readable at supported DPI/text-size settings and on small
   work areas; preserve accessible names and logical reading order.

**Gate:** About values match their authoritative build/diagnostics sources,
unavailable states are distinguishable from zero, and opening the dialog does
not block on I/O or alter startup instrumentation.

## Phase 2: C2 Browser and Viewer Empty-State Watermarks

1. Reuse the existing app/brand icon resource where appropriate. Do not add a
   new bitmap solely to duplicate the current browser placeholder art.
2. For the idle browser state with no selected folder, place a muted icon above
   the existing prompt text. Keep loading, folder-load failure, no matches,
   and empty-folder states distinct; change them only if the existing product
   contract explicitly treats them as the same empty state.
3. Apply the matching treatment to the viewer's genuine idle no-image state.
   Preserve the current image while replacements decode and during transitions.
4. Ensure the watermark remains visually subordinate, theme-aware, high
   contrast enough to read, DPI-scaled, and clipped within narrow client sizes.
   Avoid intercepting browser/viewer input or obscuring actionable controls.
5. Reuse existing D2D resources and release/recreate them through the current
   device-loss and window-lifetime paths.

**Gate:** Idle states show the intended icon-and-text composition in browser
and viewer. Loading, errors, filters, populated content, resize, DPI changes,
and asynchronous image transitions retain their correct existing behavior.

## Phase 3: C7 Breadcrumb Path Bar

1. Add a compact breadcrumb above the browser pane, using the active normalized
   folder path as its source of truth. Do not maintain a second mutable folder
   path or synchronously inspect the filesystem while laying out or painting.
2. Render meaningful path segments from the root to the active folder. Define
   a stable overflow treatment for long paths and narrow windows without
   allowing controls or text to overlap.
3. Clicking a segment navigates to that ancestor through the existing folder
   load command/path. Preserve folder-tree synchronization, selection rules,
   navigation history, watch behavior, and asynchronous stale-result rejection.
4. Support drive roots and UNC roots; do not create empty or duplicate segments
   from separators or trailing slashes. Keep the title-bar path behavior
   unchanged; C9 is not part of this task.
5. Support keyboard focus and activation, visible focus indication, meaningful
   accessible names/roles, DPI/theme/text-size changes, and small window sizes.
   Do not add a shortcut unless one is separately justified and conflict-checked.

**Gate:** Every displayed ancestor resolves to the correct folder through the
existing navigation path. Root handling, overflow, focus, tree synchronization,
and rapid successive navigation behave deterministically.

## Phase 4: C3 Opt-In Performance HUD

1. Add the HUD to the main browser content surface, initially hidden and off
   by default. Use `Ctrl+Shift+P` only if Phase 0 confirms it is unused; also
   expose a discoverable checked/unchecked command in the existing command
   surface.
2. Display the five roadmap metrics: live decode count, scale time, thumbnail
   cache hit rate, memory-pressure state, and thumbnail-worker queue depth.
   Use confirmed producers and document exact semantics, units, and aggregation
   window. Show an explicit unavailable marker if a source is not ready.
3. Reuse existing diagnostics and resource telemetry. Prefer a small immutable
   display snapshot or existing stats API over coupling rendering to worker
   internals. If live in-flight decode count is missing, add only a cheap,
   thread-safe counter around the owning decode work and prove it returns to
   zero across success, failure, cancellation, and shutdown.
4. Refresh at a modest bounded interval only while enabled, or use an existing
   event/update path. Snapshot and format data outside `WM_PAINT`; paint should
   consume prepared values and must not take locks that can wait on workers.
5. Render a compact translucent panel that does not obscure selection,
   thumbnails, status text, or breadcrumb controls. Keep text readable in both
   themes and expose the toggle and enabled state accessibly.
6. Turning the HUD off must stop its timer/subscription and release prepared
   display state. Do not persist the toggle unless the roadmap is explicitly
   revised; default it off on each launch.

**Gate:** Every metric agrees with its source and documented semantics; rapid
toggle, folder change, shutdown, and memory-pressure transitions are safe. With
the HUD off, there is no extra timer, polling, snapshot formatting, or per-frame
work.

## Phase 5: Focused Tests, Documentation, and Integration

Add or extend tests using existing smoke/test seams for:

- About version/build/adaptor fields, startup-span labels, unavailable values,
  and formatting without confusing missing data with zero.
- Browser and viewer idle watermark selection, while preserving loading,
  error, no-match, empty-folder, valid-image, and async-transition states.
- Breadcrumb segment construction for drive roots, UNC paths, trailing
  separators, ancestor clicks, overflow/layout policy, and navigation routing.
- HUD metric formatting/semantics, off-by-default state, shortcut and command
  routing, checked state, refresh lifecycle, toggle-off cleanup, and safe
  behavior for missing metrics.
- UI layout/accessibility state at practical widths and DPI/text-size settings
  where automated smoke coverage can exercise it.

Update the C1, C2, C3, and C7 entries in `specs/FUTURE-ROADMAP.md`, the
capability table in `README.md`, `specs/PRODUCT_SPEC.md` where the shipped
contract changes, and the relevant `docs/user-guide.html` and shortcut or
accessibility documentation. Keep labels, toggle state, metric definitions,
and navigation behavior consistent. Do not claim cloud CI validation.

Run focused local smoke tests, the Debug and Release builds, and the normal
Debug and Release CTest presets where available. Record exact commands and
results. Manually inspect light/dark themes, narrow and typical window sizes,
and 100%, 150%, and 200% DPI when available. Compare relevant startup and
first-thumbnail measurements against the local pre-change run if a suitable
dataset and binary are available; do not treat that single-machine comparison
as a calibrated baseline.

## Completion Criteria

This prompt is complete only when:

- The existing About dialog presents current build identity, the requested
  adapter information, and correctly labeled A8 startup spans with explicit
  unavailable states.
- Browser and viewer idle states show the muted app icon above their prompt
  text without conflating loading, error, filter, or valid-content states.
- The browser breadcrumb handles roots and long paths, navigates through the
  existing folder path, and preserves tree, history, focus, and async behavior.
- The optional HUD shows all five defined metrics, is accessible and
  off-by-default, and incurs no polling or paint cost while disabled.
- Focused tests, local Debug/Release builds, and applicable Debug/Release CTest
  gates pass; documentation matches the implementation.
- No cloud workflow was enabled or dispatched, no unrelated roadmap item was
  pulled into scope, and no unrelated work was committed.

If a metric's source or meaning cannot be established, a shortcut conflicts,
or a navigation/lifetime boundary cannot be preserved, stop at that gate and
report the exact evidence or decision needed. Do not hide the gap with a
placeholder value or a parallel subsystem.
