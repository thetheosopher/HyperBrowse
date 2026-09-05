# MainWindow Clean-Code Refactoring Plan

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
message policy, file-command dispatch, view-command dispatch, and command-bar
policy extractions have reduced the current translation unit to 24,337 lines. The folder-tree slice places node
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
  verifies parameterized command families and ownership boundaries. The
- [x] Extract command-bar item definitions, menu and toolbar layout, hit
  testing, and toolbar enabled/checked-state policy into
  `src/ui/CommandBarController.*`. MainWindow retains HWND movement,
  tooltip registration, painting, focus, and command effects. Deterministic
  smoke coverage verifies item initialization, layout hit testing, and state
  transitions. The current `MainWindow.cpp` size is 24,337 lines.
