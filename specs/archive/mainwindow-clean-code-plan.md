# MainWindow Clean-Code Refactoring Plan

## Status

Complete for this cleanup pass. All implementation items and available
automated build, smoke-test, diagnostic, whitespace, and binary-freshness
checks are complete. Native interactive workflows requiring controlled desktop
input remain documented as not verified below.

## Goal

Make `MainWindow` a thin Win32 shell and coordination facade through staged,
behavior-preserving extractions. The target is explicit ownership and
testable state transitions, not an arbitrary line-count target.

`MainWindow.cpp` was approximately 27,000 lines at the start of this pass and
currently contains several independent state machines: folder navigation and
enumeration, file-operation reconciliation, viewer synchronization, menus,
settings, details and Quick Actions presentation, drag/drop, rendering, and
shutdown. Dialog, file-operation policy, folder-enumeration coordination,
folder-tree coordination, folder-watch policy, async message routing, menu
message policy, file-command dispatch, view-command dispatch, command-bar
policy, dynamic menu construction, and histogram policy extractions have
  reduced the current translation unit to approximately 21,208 lines. The folder-tree slice places node
ownership, child-presence
caching, lazy enumeration, request settlement, and selection restoration in
`FolderTreeController`. The folder-load slice places enumeration presentation,
history navigation, watcher lifecycle, stale watcher-result filtering, and
deferred watcher effects in `FolderLoadCoordinator`.

## Guardrails

- Preserve user-visible behavior while each slice is extracted.
- Keep blocking filesystem, decode, metadata, shell, and persistent-cache work
off the UI thread.
- Preserve cancellation, stale-result rejection, HWND lifetime guarantees,
  viewer image continuity, focus/activation behavior, and shutdown ordering.
- Preserve public `MainWindow` methods and message/command IDs by default.
  Controlled changes require an explicit compatibility review, updated call
  sites, and focused tests.
- Keep worker services, `BrowserModel`, `BrowserPane`, and `ViewerWindow` as
  owners of their existing domains.
- Preserve the hybrid GDI/Direct2D rendering architecture. Rendering migration
  is outside this hygiene pass.
- Avoid a global event bus, a generic `MainWindowContext`, unrelated features,
  broad performance rewrites, generated-file churn, and style-only edits.

## Phases

### 0. Baseline and characterization

- Run the Debug and Release builds and smoke suites before implementation.
- Record `MainWindow` method, field, message, timer, command, and dependency
  clusters.
- Add characterization coverage for stale async results, startup enumeration,
  settings persistence, tree restoration, viewer deletion, operation origin,
  focus restoration, and close-time cancellation.
- Gate every phase on no new UI-thread blocking work, no late callbacks after
  destruction, and no unintended contract changes.

### 1. Remove low-risk bulk

- Extract dialog state/layout and prompt implementations into
  `src/ui/MainWindowDialogs.*` with explicit HWND, theme, text-size, and result
  inputs.
- Extract stateless Win32, clipboard, menu, and shell helpers into focused
  modules rather than a generic utility dump.
- Move external drag/drop source and target implementations behind narrow
  callback contracts and remove `friend` coupling.
- Register every new translation unit in `CMakeLists.txt`.

### 2. Extract pure state and persistence policies

- Add `FolderHistory` for normalized opened-folder history, back/forward
  traversal, duplicate suppression, branching, and pending navigation targets.
- Add a file-operation journal for bounded undo/redo and inverse-operation
  planning.
- Introduce typed window/settings state and persistence for registry values,
  geometry validation, and the invariant that an empty transient folder never
  overwrites the last valid persisted folder.
- Separate Quick Actions persistence from view layout and menu presentation.
- Test each pure policy deterministically without constructing `MainWindow`.

### 3. Isolate folder navigation and tree coordination

- Create a `FolderTreeController` for tree nodes, child-presence caching,
  pending enumeration IDs, selection restoration, and lazy tree loading.
- Keep MainWindow-owned tree policy in MainWindow: custom draw/favorite
  coloring, selection-to-browser loading, rename/file operations, context
  menus, drag/drop, and tooltips.
- Create a `FolderLoadCoordinator` for folder enumeration presentation,
  folder history, watcher lifecycle, stale-result rejection, and pending
  startup/reload presentation state. Keep browser/model/viewer effects in
  MainWindow behind explicit callbacks.
- Create a `FolderWatchChangeCoordinator` for watcher-event reconciliation,
  incremental model updates, reload escalation, and selection/cache
  preservation. Keep tree, reload, browser refresh, and presentation effects
  in MainWindow behind explicit callbacks.
- Preserve early batches, presentation coalescing, watcher shutdown ordering,
  incremental updates, full-reload escalation, and focus/selection behavior.

### 4. Refactor file-operation reconciliation

- First split `ApplyCompletedFileOperation` into named no-behavior-change
  stages.
- Represent browser, viewer, Quick Actions, and tree operations with explicit
  origin/context types. Never infer origin from operation type alone.
- Extract a `FileOperationReconciler` that returns typed effects for model,
  viewer, tree, reload, selection, focus, and failure presentation.
- Extract coordination for taskbar progress, notifications, close-pending
  cancellation, foreground capture, and journal transitions only after the
  reconciler is tested.
- Preserve optimistic viewer deletion, content-identity cache invalidation,
  watch-echo handling, activation capture at operation start, and shutdown
  cancellation rules.

### 5. Split presentation and input surfaces

- Extract toolbar/command-bar state, hit testing, keyboard navigation, and
  painting.
- Extract dynamic menu construction, owner-draw state, recent folders, and
  deferred menu refresh.
- Extract details-panel, histogram, right-pane tab, and Quick Actions
  presentation state.
- Extract shell painting and resource recovery without changing the current
  rendering split.

### 6. Make message routing explicit

- Reduce `WindowProc` and `HandleMessage` to HWND association and forwarding.
- Keep asynchronous service, browser-pane, and viewer message dispatch in
  `WindowAsyncMessageRouter` with explicit MainWindow callbacks.
- Keep `HandleCommand` as a stable boundary while moving command groups to
  owning controllers.
- Centralize message/timer ownership and heap-payload cleanup.
- Make destruction order explicit: cancel producers, revoke drop registration,
  stop callbacks, destroy UI resources, then release worker owners.

### 7. Test and documentation hygiene

- Split `tests/smoke.cpp` into focused sources while initially retaining one
  executable.
- Keep pure policy tests deterministic and end-to-end smoke coverage for HWND,
  message, focus, and lifetime behavior.
- Update `docs/architecture.md` with the ownership graph, callback/effect
  boundaries, lifecycle ordering, and authoritative message flows.
- Maintain a short ownership map for state machines, messages, threads, and
  resource families.

## Verification gates

Before each phase, run the narrowest relevant test and compile the touched
targets. After async coordination phases, run the complete Debug smoke suite.
Before completion, run the Release build and `ctest --preset release-tests`.
For UI/resource changes, manually verify the exact newly built executable with
folder loading, watch updates, file operations, viewer navigation and delete,
drag/drop, focus restoration, settings, DPI/display recovery, and repeated
create/destroy.

## First slice

The first implementation slice extracted `FolderHistory` only. Filesystem
ancestor resolution and `LoadFolderAsync` initially remained in `MainWindow`; the policy received those operations through callbacks. Later slices extracted shell drag/drop boundaries, the file-operation journal, named completion stages, folder-tree coordination, folder-load coordination, and folder-watch coordination. Startup-specific selection/viewer restoration now lives in the folder-load coordinator's pending-presentation state, while tree and presentation effects for watch events remain explicit MainWindow callbacks.

## Progress

- [x] Persist the roadmap in the repository.
- [x] Extract `FolderHistory` and wire `MainWindow` through the policy.
- [x] Add deterministic smoke coverage for history transitions.
- [x] Extract stateless shell drag data-object and drop-source creation.
- [x] Extract bounded file-operation journal state and deterministic coverage.
- [x] Extract the external OLE drop target behind a callback contract.
- [x] Split file-operation completion into typed context, tree-effect, reload,
  browser-reconciliation, and viewer-effect stages without changing the
  `MainWindow` message contract.
- [x] Extract `FileOperationReconciler` into its own translation unit with
  explicit model/pane inputs and typed tree/reload/focus effects. This is the
  first extraction in the current pass that materially reduces
  `MainWindow.cpp` rather than only adding stages inside it.
- [x] Extract text-input, rename-validation, and batch-rename dialog state and
  implementations into `src/ui/MainWindowDialogs.*`. The dialog slice reduced
  `MainWindow.cpp` from 27,035 to 23,488 lines; the new translation unit owns
  the 1,303 lines of modal dialog state and preview behavior.
- [x] Extract folder-enumeration request, cancellation, stale-result, first-batch,
  presentation-coalescing, timer, and settled-callback coordination into
  `src/ui/FolderEnumerationCoordinator.*`. This slice reduced `MainWindow.cpp`
  from 23,488 to 23,463 lines while keeping browser refresh, history, watcher,
  and viewer synchronization callbacks in `MainWindow`.
- [x] Extract folder-tree node ownership, child-presence caching, lazy loading,
  stale-result settlement, and selection restoration into
  `src/ui/FolderTreeController.*`.
- [x] Extract folder-load history navigation, watcher lifecycle, stale watcher
  filtering, deferred watcher effects, and enumeration callback routing into
  `src/ui/FolderLoadCoordinator.*`. That slice brought `MainWindow.cpp` to
  24,698 lines; pending startup selection, pending viewer launch, reload
  selection restoration, and post-enumeration viewer settlement are
  coordinator-owned; browser/model/viewer effects remain explicit callbacks.
- [x] Extract folder-watch event reconciliation, incremental browser-model
  updates, reload escalation, thumbnail invalidation, and selection
  preservation into `src/ui/FolderWatchChangeCoordinator.*`. MainWindow keeps
  the tree, reload, browser refresh, and presentation effects as callbacks.
- [x] Extract asynchronous folder, browser-pane, service, and viewer message
  dispatch into `src/ui/WindowAsyncMessageRouter.*`. MainWindow retains the
  callback implementations and private maintenance messages.
- [x] Extract owner-draw menu mnemonic selection from `HandleMessage` into
  `src/ui/MenuMessageHandling.*`, keeping the shared menu item data record
  available to MainWindow's existing measurement and painting paths.
- [x] Extract file and selection command dispatch from `HandleCommand` into
  `src/ui/FileCommandController.*`. MainWindow retains state, shell effects,
  and operation callbacks while the controller owns the command-ID mapping.
  Deterministic smoke coverage verifies aliases and forwarded command
  parameters.
- [x] Extract view, settings, help, diagnostics, and viewer-display command
  dispatch from `HandleCommand` into `src/ui/ViewCommandController.*`.
  MainWindow retains mutable settings and presentation effects while the
  controller owns the command-ID mapping. Deterministic smoke coverage
  verifies parameterized command families and ownership boundaries.
- [x] Extract command-bar item definitions, menu and toolbar layout, hit
  testing, toolbar enabled/checked-state policy, and keyboard input policy into
  `src/ui/CommandBarController.*`. Extract GDI and Direct2D command-bar
  painting into `src/ui/CommandBarPainter.*`. MainWindow retains HWND movement,
  tooltip registration, focus, resource ownership, and command effects.
  Deterministic smoke coverage verifies item initialization, layout hit testing,
  state transitions, and keyboard navigation decisions. The current
  command-bar slice brought `MainWindow.cpp` to 23,810 lines.
- [x] Extract owner-draw menu metadata preparation, measurement, and GDI/
  Direct2D painting into `src/ui/MenuPainter.*`. MainWindow retains HMENU
  lifetimes, draw-data storage, palette construction, menu state, and message
  routing. The current `MainWindow.cpp` size is 23,290 lines.
- [x] Extract dynamic recent-folder and copy/move destination menu population
  into `src/ui/QuickAccessMenuBuilder.*`. MainWindow retains the menu handles,
  persistent owner-draw storage, and the state snapshot passed to the builder.
  Deterministic smoke coverage verifies labels, command ranges, placeholders,
  and enabled states. The current `MainWindow.cpp` size is 23,179 lines.
- [x] Extract cached-thumbnail RGB histogram calculation into
  `src/ui/DetailsPanelHistogram.*`. MainWindow retains thumbnail scheduling,
  cancellation, panel state, and rendering. Deterministic smoke coverage
  verifies channel bins, peak detection, visibility, and invalid-input reset.
  The current `MainWindow.cpp` size is 23,131 lines.
- [x] Extract pure right-pane tab, close-button, and Quick Actions sort-button
  hit testing into `src/ui/RightPaneHitTester.*`. MainWindow retains visibility
  state, mouse tracking, and the resulting actions. Deterministic smoke
  coverage verifies hidden-panel behavior and Win32 rectangle edge semantics.
- [x] Extract pure Quick Actions panel, viewport, sort-button, and destination
  row/control rectangle layout into `src/ui/QuickAccessLayout.*`. MainWindow
  retains scrollbar HWND updates, tooltip and shortcut-edit control lifetimes,
  enablement policy, and action effects. Deterministic smoke coverage verifies
  scrolled row placement and control geometry. The current `MainWindow.cpp`
  size is 23,093 lines.
- [x] Extract pure details-panel, tab, content, histogram, close-button, and
  metadata-editor rectangle layout into `src/ui/DetailsPanelLayout.*`.
  MainWindow retains text measurement, child-window movement, tooltip updates,
  panel state, and painting. Deterministic smoke coverage verifies normal,
  histogram-visible, and narrow-panel geometry. That slice was recorded at
  21,127 lines before subsequent presentation-surface work.
- [x] Extract stateless main-shell background and splitter painting into
  `src/ui/ShellPainter.*`, with both GDI and Direct2D paths consuming explicit
  palette and geometry inputs. Extract display-surface retry sequencing into
  `src/ui/DisplaySurfaceRecoveryPolicy.*`; MainWindow retains timer, resource,
  invalidation, and shutdown orchestration. Deterministic smoke coverage
  verifies first-attempt relayout, retry sequencing, reset, and exhaustion.
  The current `MainWindow.cpp` size is 23,044 lines.
- [x] Extend `src/ui/WindowAsyncMessageRouter.*` to own configured private
  MainWindow message IDs, including external-launch payload cleanup and deferred
  menu-state forwarding. Extract timer-ID dispatch into
  `src/ui/WindowTimerRouter.*`; MainWindow retains callback state checks and
  side effects. Deterministic smoke coverage verifies private-message argument
  forwarding, unknown-message fall-through, legacy zero-ID configuration, timer
  dispatch, and inactive-handler behavior.
- [x] Move the remaining custom-dialog state records, layout metrics, option
  records, and consolidated-settings enums into the internal
  `src/ui/MainWindowDialogState.h` header. MainWindow keeps dialog procedures,
  modal-loop ownership, resource lifetime, and result application. This reduced
  `MainWindow.cpp` to 22,634 lines without changing runtime paths.
- [x] Split deterministic policy smoke scenarios into
  `tests/smoke_policy.cpp` with a small declaration header while retaining one
  `HyperBrowseTests` executable, all existing CTest entries, and all focused
  command-line selectors. The shared Win32/service harness remains in
  `tests/smoke.cpp`.
- [x] Split the self-contained shortcut-catalog and BackgroundExecutor
  runtime/lifetime scenarios into `tests/smoke_runtime.cpp` while retaining
  their original invocation order and the single `HyperBrowseTests` target.
  Debug and Release test-target builds and complete smoke suites pass.
- [x] Split the deterministic BrowserModel bulk-removal and Quick Send model
  scenarios into `tests/smoke_model.cpp`, retaining the BrowserPane HWND
  scenario in `tests/smoke.cpp` and the existing invocation order.
- [x] Split folder-watch notification record construction and parser policy
  coverage into `tests/smoke_watch.cpp`, retaining FolderWatchService lifecycle
  coverage in `tests/smoke.cpp`.
- [x] Split deterministic RAW format allowlist and helper-protocol coverage
  into `tests/smoke_decode.cpp`, retaining fixture-backed LibRaw decoding in
  `tests/smoke.cpp`.
- [x] Extract Win32 clipboard text and file-selection serialization into
  `src/ui/ClipboardFileTransfer.*`. MainWindow retains selection, messaging,
  destination, and asynchronous operation policy; deterministic smoke coverage
  verifies CF_HDROP path round trips and copy-vs-cut effects.
- [x] Extract capped Quick Access path-list insertion, normalization,
  duplicate suppression, and registry serialization into
  `src/ui/QuickAccessPathList.*`. MainWindow retains registry access, Quick
  Send assignments, menu refresh, and presentation effects; deterministic
  smoke coverage verifies list mutation and round-trip ordering.
- [x] Extract the Quick Send registry value codec into
  `src/ui/QuickSendPersistence.*`. MainWindow retains registry handles,
  cross-process mutex synchronization, model synchronization, and menu/
  presentation effects; deterministic in-memory coverage verifies value
  mapping, capped favorite restoration, shortcut slots, and empty-destination
  deletion.
- [x] Extract persisted window-rectangle loading, overflow-safe geometry,
  minimum-size/work-area validation, and DWORD mapping into
  `src/ui/WindowBoundsPersistence.*`. MainWindow retains monitor discovery,
  HWND placement, and registry-handle ownership; deterministic coverage
  verifies signed coordinates, invalid values, overflow rejection, and write
  suppression for undersized bounds.
- [x] Extract selected-folder and selected-image persistence into
  `src/ui/SelectedPathPersistence.*`. MainWindow retains path normalization,
  startup validation, and viewer/browser routing; deterministic coverage
  verifies round trips, empty-folder preservation, and stale-image deletion.
- [x] Extract viewer mouse-wheel/Escape behavior, keyboard-panning, and
  slideshow settings persistence into `src/ui/ViewerSettingsPersistence.*`.
  MainWindow retains runtime ViewerWindow application, dialog editing, and
  registry-handle ownership; deterministic coverage verifies valid restoration,
  invalid-value defaults, and value mapping.
- [x] Extract browser presentation persistence into
  `src/ui/BrowserPresentationPersistence.*`, covering pane widths,
  browser/theme modes, text size, thumbnail presentation, sorting, and
  details-panel presentation. MainWindow retains layout application, browser
  and menu synchronization, and registry-handle ownership; deterministic
  coverage verifies valid restoration, invalid-value defaults, exact mapping,
  and minimum-width writes.
- [x] Extract image workflow persistence into
  `src/ui/ImageWorkflowPersistence.*`, covering nvJPEG and LibRaw helper
  preferences, paired RAW/JPEG behavior, and secondary-monitor viewer
  preference. MainWindow retains decoder/service application and pairing
  policy; deterministic coverage verifies defaults, enum validation, and
  exact value mapping.
- [x] Extract performance and cache persistence into
  `src/ui/PerformanceSettingsPersistence.*`, covering persistent thumbnail
  cache, resource profile, prefetch depth, cache capacities, pressure-status
  display, and close-on-Escape settings. MainWindow retains scheduler/cache
  application and dialog behavior; deterministic coverage verifies defaults,
  enum validation, QWORD saturation, and prefetch clamping.
- [x] Extract paired RAW/JPEG viewer-item substitution into
  `src/ui/PairedRawJpegResolver.*`. MainWindow retains browser-model snapshots,
  slideshow JPEG preference selection, ViewerWindow orchestration, and the
  enablement gate; deterministic coverage verifies same-folder matching,
  case-insensitive stems, preference reversal, and unmatched-item preservation.
- [x] Complete the Phase 7 verification gate after the focused test-source
  splits: Debug and Release builds plus all four registered smoke tests pass.
- [x] Extract status-strip GDI and Direct2D painting into
  `src/ui/StatusBarPainter.*`. MainWindow retains status text construction,
  theme-state selection, invalidation, and owner-draw routing while the
  painter consumes explicit colors, font, geometry, and text inputs.
- [x] Extract details-panel histogram GDI and Direct2D painting into
  `src/ui/DetailsPanelHistogramPainter.*`. MainWindow retains histogram
  scheduling, state transitions, panel composition, and backend selection;
  the painter consumes explicit histogram state, geometry, colors, and text
  resources.
- [x] Extract details-panel tab and close-button GDI and Direct2D painting
  into `src/ui/DetailsPanelChromePainter.*`. MainWindow retains interaction
  state, panel composition, and invalidation while the painter consumes
  explicit rectangles, interaction flags, palette values, and text resources.
- [x] Extract populated Quick Actions header, sort control, destination rows,
  and row action-button GDI and Direct2D painting into
  `src/ui/QuickAccessPainter.*`. MainWindow retains destination-validity
  policy, interaction state, clipping geometry, and icon-library ownership
  while the painter consumes explicit row states, layout metrics, palette,
  and text resources.
- [x] Extract details-panel title, summary, and Quick Actions empty-state text
  painting into `src/ui/DetailsPanelTextPainter.*`. MainWindow retains text
  measurement, content-state selection, and panel composition while the
  painter consumes explicit rectangles, text views, palette, and font
  resources.
- [x] Extract details-panel background and left-border GDI and Direct2D
  painting into `src/ui/DetailsPanelSurfacePainter.*`. MainWindow retains
  panel visibility, resource ownership, and content composition while the
  painter consumes the panel geometry and existing drawing resources.
- [x] Consolidate duplicated populated Quick Actions state and palette
  assembly behind named MainWindow helpers. MainWindow retains destination
  validity and interaction policy while both rendering backends pass the same
  explicit painter state.
- [x] Consolidate duplicated details-panel chrome, histogram, and text-palette
  assembly behind named MainWindow helpers. MainWindow retains layout,
  interaction, and backend composition while both rendering paths consume the
  same explicit painter state and colors.
- [x] Extract Quick Actions shortcut-edit reconciliation into
  `src/ui/QuickAccessShortcutEditPolicy.*`. MainWindow retains QuickSend model
  mutation, persistence, HWND synchronization, and invalidation while the
  policy owns canonical text restoration and accepted-input normalization.
- [x] Extract Quick Actions destination snapshot assembly into
  `src/ui/QuickAccessDestinationBuilder.*`. MainWindow retains metadata and
  QuickSend lookup callbacks, scrollbar ownership, layout, and HWND updates
  while the builder owns conversion to layout destinations.
- [x] Extract browser item scope filtering and ordering into
  `src/ui/BrowserItemScopeCollector.*`. MainWindow retains model and pane
  snapshot acquisition while the collector owns selection/full-folder scope
  mapping and invalid-index filtering.
- [x] Extract pure folder-tree drop path validation into
  `src/ui/FolderTreeDropPolicy.*`. MainWindow retains filesystem existence,
  drive checks, image-list handling, and move effects while the policy owns
  self, child, and parent destination rejection.
- [x] Extract common and mixed selection-rating aggregation into
  `src/ui/SelectionRatingPolicy.*`. MainWindow retains metadata-store reads and
  menu presentation while the policy owns clamping and equality semantics.
- [x] Extract ordered viewer-item collection and active-index fallback into
  `src/ui/ViewerItemSelectionPolicy.*`. MainWindow retains model and pane
  snapshots, paired RAW/JPEG resolution, viewer lifetime, and image loading
  while the policy owns invalid-index filtering and preferred/current-path
  selection precedence.
- [x] Move pure RAW/JPEG companion-path expansion into
  `PairedRawJpegResolver::ExpandPaths`. MainWindow retains feature gating,
  model snapshots, and companion-count reporting while the resolver owns
  same-folder, same-stem matching and duplicate suppression.
- [x] Extract `FolderTreeDragController` for tree-drag state, hit testing,
  drag-image lifetime, capture, cursor feedback, and callback-driven drop
  effects. MainWindow retains folder operation policy and UI callbacks.
- [x] Extract `ViewerPendingOperationState` for active and queued viewer
  deletes plus Quick Send lifecycle ownership. Viewer close now detaches
  pending viewer effects and saved viewer focus/activation targets so a later
  viewer cannot inherit stale completion state; deterministic smoke coverage
  verifies active/queued ordering, Quick Send consumption, and close clearing.
- [x] Extract `ViewerSynchronizer` for browser-model selection, empty-model
  close decisions, paired RAW/JPEG payload assembly, and selected-index
  preservation. MainWindow retains HWND close posting and `ViewerWindow`
  replacement; deterministic smoke coverage verifies preferred selection and
  slideshow propagation.
- [x] Reduce `HandleMessage` through `HandlePaintMessage` and
  `HandleControlColorMessage`, keeping paint transactions, D2D/GDI fallback,
  control colors, and default message fall-through behavior unchanged.
- [x] Extract `HandleNotifyMessage` for tooltip text preparation and
  folder-tree notification forwarding, preserving unhandled notification
  fall-through.
- [x] Extract `HandleMouseInputMessage` for mouse, drag-capture, cursor,
  drop-file, and mouse-leave effects, preserving handled/unhandled message
  results.
- [x] Extract `HandleCommandMessage` for Quick Actions edit notifications,
  filter changes, and command-controller forwarding, preserving command
  fall-through.
- [x] Perform the available manual executable verification: the exact
  `build\\Debug\\HyperBrowse.exe` started successfully and reported
  responsive. Native interactive scenarios requiring desktop input remain not
  verified in the available tooling: folder-watch changes, file operations,
  viewer navigation/deletion recovery, drag/drop, focus restoration, settings
  persistence, DPI/display recovery, and repeated interactive create/destroy.
- [x] Add `docs/architecture.md` ownership/lifecycle updates and create the
  concise `docs/mainwindow-ownership-map.md`.
