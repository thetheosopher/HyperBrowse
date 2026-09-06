# HyperBrowse Architecture

This document describes the current implementation. The planning documents in `specs/` are useful for product intent and historical context, but source code, tests, and this document are authoritative when they disagree.

## Executables and library

`HyperBrowseCore` is the static library shared by the application and smoke tests.
`HyperBrowse.exe` owns application startup, the Win32 message loop, and the main user-facing windows.
`HyperBrowseRawHelper.exe` is the optional out-of-process RAW decode helper.
`HyperBrowseTests.exe` runs the smoke and integration checks registered by
`tests/CMakeLists.txt`. Its shared Win32/service harness remains in
`tests/smoke.cpp`, while deterministic controller, layout, presentation-policy,
and router scenarios live in `tests/smoke_policy.cpp` and shortcut/executor
runtime scenarios live in `tests/smoke_runtime.cpp`, and deterministic browser
model/Quick Send scenarios live in `tests/smoke_model.cpp`, behind the same
executable and command-line selectors. Folder-watch notification parser
coverage lives in `tests/smoke_watch.cpp`; the service lifecycle coverage
remains in `tests/smoke.cpp`. Deterministic RAW allowlist and helper-protocol
coverage lives in `tests/smoke_decode.cpp`; fixture-backed LibRaw decoding
remains in `tests/smoke.cpp`.

The core library is organized by responsibility:

- `src/app/`: process startup and application lifecycle.
- `src/ui/`: main window, diagnostics, command routing, and shared UI coordination.
- `src/browser/`: browser model and thumbnail/details presentation.
- `src/viewer/`: full-image viewer, navigation, slideshow, zoom, and prefetch.
- `src/services/`: asynchronous enumeration, watching, metadata, thumbnails, file operations, conversion, and settings-related workflows.
- `src/decode/`: WIC, LibRaw helper protocol, and optional nvJPEG decode paths.
- `src/cache/`: bounded memory thumbnail caching and persistent disk thumbnail caching.
- `src/render/`: Direct2D/DirectWrite factories and shared rendering helpers.
- `src/util/`: logging, diagnostics, path/string helpers, settings, sizing, and common utilities.

## Threading boundary

The UI thread owns HWNDs, input, layout, command routing, model presentation, and invalidation/paint coordination. It must remain responsive.

Potentially blocking or high-volume work belongs on worker paths:

- folder and tree enumeration;
- filesystem watching and event coalescing;
- thumbnail and full-image decode;
- metadata extraction;
- file operations and batch conversion;
- persistent thumbnail cache index/file access;
- RAW helper process communication.

Workers return results through the existing window-message or callback contracts. Every asynchronous path must account for cancellation, stale results, recipient lifetime, and shutdown ordering. A worker must not retain a raw HWND or object callback past the recipient's lifetime without an established lifetime guarantee.

`DiskThumbnailCache` is especially important: its index and cache files are protected by process-wide persistence coordination, so cache operations must not be called from the UI thread. `ThumbnailScheduler` owns the asynchronous disk invalidation path.

## Main data flows

### Folder navigation

1. `MainWindow` starts a folder load and resets the browser presentation state.
2. `FolderEnumerationService` enumerates asynchronously and posts batches.
3. `BrowserModel` receives incremental items and tracks enumeration state.
4. `BrowserPane` presents early items, schedules visible/near-visible thumbnails, and requests metadata as needed.
5. Coalesced UI updates keep large-folder enumeration from sorting, painting, or scheduling once per worker batch.
6. `FolderWatchService` applies external changes incrementally when safe and requests a full reload for large or ambiguous event bursts.

### Viewer navigation

1. `ViewerWindow` changes the selected item and tries the memory/prefetch cache.
2. A cache hit presents immediately.
3. A miss starts asynchronous full-image loading while preserving the last valid displayed image where possible.
4. The current image and adjacent prefetch slots are updated when decode results arrive.
5. Delete and other list mutations explicitly invalidate index-keyed render resources before an index can refer to a different file.

### File operations

`FileOperationService` performs native shell operations asynchronously and reports completion/progress to `MainWindow`. Browser and viewer workflows share operation types, so operation origin must be tracked separately from the operation type. Completion logic must also account for folder-watch echoes, optimistic viewer state, selection/focus restoration, and shell-dialog foreground activation.

## Main-window policy collaborators

Several state and shell boundaries are intentionally kept outside the HWND
controller:

- `ui/FolderHistory.*` owns normalized folder-history branching, back/forward
  traversal, duplicate suppression, and pending navigation state.
- `ui/FileOperationJournal.*` owns bounded Copy/Move/Rename undo/redo history
  and completion transitions. `MainWindow` still plans and starts the inverse
  shell operation and applies its asynchronous result.
- `ui/ShellDragSource.*` owns shell `IDataObject` and `IDropSource` creation
  for outbound selection drags.
- `ui/ClipboardFileTransfer.*` owns Win32 clipboard serialization for text and
  file selections, including `CF_HDROP` and preferred copy-vs-cut effects.
  MainWindow retains selection snapshots, user-facing errors, destination
  validation, and the asynchronous file-operation request.
- `ui/QuickAccessPathList.*` owns capped, normalized, duplicate-free Quick
  Access path-list insertion and registry serialization. MainWindow retains
  registry handles, Quick Send assignments, menu refresh, and presentation
  effects.
- `ui/QuickSendPersistence.*` owns the typed Quick Send registry value codec
  for favorite folders, the last destination, and shortcut assignments through
  value callbacks. MainWindow retains registry handles, mutex synchronization,
  model synchronization, and menu/presentation effects.
- `ui/WindowBoundsPersistence.*` owns overflow-safe persisted rectangle loading,
  minimum-size validation, work-area containment checks, and DWORD value
  mapping through callbacks. MainWindow retains monitor discovery, HWND
  placement, and registry-handle ownership.
- `ui/SelectedPathPersistence.*` owns the selected-folder and selected-image
  registry value codec, including the invariant that an empty transient folder
  does not overwrite a valid persisted folder. MainWindow retains path
  normalization, startup validation, and viewer/browser routing.
- `ui/ViewerSettingsPersistence.*` owns the typed DWORD codec and validation
  for viewer mouse-wheel/Escape behavior, keyboard panning, and slideshow
  settings. MainWindow retains runtime application to ViewerWindow, dialog
  editing, and registry-handle ownership.
- `ui/BrowserPresentationPersistence.*` owns the typed DWORD codec for pane
  widths, browser/theme modes, text size, thumbnail presentation, sorting, and
  details-panel presentation. MainWindow retains layout application, browser
  and menu synchronization, and registry-handle ownership.
- `ui/ImageWorkflowPersistence.*` owns the typed DWORD codec for nvJPEG and
  LibRaw helper preferences, paired RAW/JPEG behavior, and secondary-monitor
  viewer preference. MainWindow retains decoder/service application, pairing
  policy, and registry-handle ownership.
- `ui/PairedRawJpegResolver.*` owns pure viewer-item substitution for paired
  RAW/JPEG siblings, including same-folder and case-insensitive stem matching
  and the configured display preference. MainWindow retains model snapshots,
  slideshow preference selection, ViewerWindow calls, and the enablement gate.
- `ui/PerformanceSettingsPersistence.*` owns the DWORD/QWORD codec for
  persistent thumbnail cache, resource profile, prefetch depth, cache
  capacities, pressure-status display, and close-on-Escape settings. MainWindow
  retains scheduler/cache application, dialog behavior, and registry handles.
- `ui/ExternalDropTarget.*` owns the OLE `IDropTarget` COM lifetime and
  screen-to-client conversion. It calls synchronous callbacks supplied by
  `MainWindow` for drag feedback, drop handling, and visual cleanup; it does
  not retain the window as a raw host pointer.
- `ui/MainWindowDialogs.*` owns synchronous text-entry, rename-validation, and
  batch-rename preview dialogs. `MainWindow` supplies the owner HWND, theme,
  text-size, and operation-specific inputs, then consumes only the returned
  values.
- `ui/MainWindowDialogState.h` owns the private state records and settings
  enums shared by MainWindow's remaining custom dialogs. The header is an
  implementation detail of the dialog procedures; MainWindow retains dialog
  creation, modal-loop ownership, and result application.
- `ui/FolderEnumerationCoordinator.*` owns folder-enumeration request
  lifecycle, cancellation, stale-result filtering, first-batch presentation,
  50 ms presentation coalescing, and completion/failure settlement. It calls
  `MainWindow` handlers for model mutation, browser refresh, history/watcher
  updates, and viewer synchronization without owning those UI policies.
- `ui/FolderLoadCoordinator.*` owns folder-load history navigation,
  enumeration and watcher service lifetimes, stale watcher-result filtering,
  deferred watcher reload/tree effects, pending startup/reload presentation
  state, post-enumeration viewer settlement, and routing of enumeration
  presentation callbacks. MainWindow supplies model, browser-pane, viewer, and
  watch-event policy callbacks; the coordinator does not own browser or viewer
  state.
- `ui/FolderWatchChangeCoordinator.*` owns the synchronous policy for applying
  folder-watch updates to `BrowserModel` and `BrowserPane`, including
  incremental upserts/removals, recursive reload escalation, cache
  invalidation, and selection preservation. MainWindow supplies tree, reload,
  refresh, and presentation callbacks; the coordinator does not own HWNDs or
  folder-watch service lifetime.
- `ui/WindowAsyncMessageRouter.*` owns the message-ID table for asynchronous
  folder, browser-pane, service, viewer, and private MainWindow notifications.
  It invokes explicit callbacks configured by MainWindow, owns cleanup of the
  heap-owned external-launch payload, and returns no result for messages
  outside that table.
- `ui/WindowTimerRouter.*` owns timer-ID dispatch for shutdown notices, folder
  presentation, memory-pressure sampling, and display-surface recovery. The
  callbacks retain MainWindow-owned state checks and side effects; unknown or
  inactive timers fall through to the normal window procedure behavior.
- `ui/MenuMessageHandling.*` owns the pure `WM_MENUCHAR` owner-draw mnemonic
  selection policy and the shared `MenuDrawItemData` record. MainWindow retains
  menu construction, measurement, painting, and command policy.
- `ui/FileCommandController.*` owns the file and selection command-ID mapping
  for folder navigation, clipboard actions, file operations, batch conversion,
  and undo/redo. MainWindow supplies explicit callbacks and retains window,
  model, service, and presentation state.
- `ui/ViewCommandController.*` owns the view, settings, help, diagnostics, and
  viewer-display command-ID mapping. MainWindow supplies explicit callbacks
  and retains mutable settings, presentation state, and window effects.
- `ui/CommandBarController.*` owns command-bar item definitions, menu and
  toolbar layout, hit testing, toolbar enabled/checked-state policy, and
  keyboard input policy. MainWindow retains HWND movement, tooltip
  registration, focus, resource ownership, and command effects.
- `ui/CommandBarPainter.*` owns GDI and Direct2D command-bar painting from
  explicit menu/item, palette, interaction-state, font, and icon-library
  inputs. MainWindow retains the render-target and window-resource lifetimes.
- `ui/MenuPainter.*` owns owner-draw menu metadata preparation, measurement,
  and GDI/Direct2D item painting from explicit palette, font, text-size, and
  theme inputs. MainWindow retains HMENU lifetimes, draw-data storage, menu
  state, and message routing.
- `ui/QuickAccessMenuBuilder.*` owns dynamic recent-folder and destination-menu
  population, including folder labels, command ranges, and enabled-state
  policy. MainWindow retains HMENU lifetimes, persistent owner-draw storage,
  and the state snapshot supplied to the builder.
- `ui/DetailsPanelHistogram.*` owns RGB histogram extraction from cached
  thumbnail bitmaps and returns fixed-size bin data with peak/visibility state.
  MainWindow retains thumbnail scheduling, cancellation, panel state, and
  painting.
- `ui/RightPaneHitTester.*` owns pure rectangle hit testing for right-pane tabs,
  the close button, and the Quick Actions sort button. MainWindow retains
  visibility state, mouse tracking, and the resulting actions.
- `ui/QuickAccessLayout.*` owns pure Quick Actions panel, viewport, sort-button,
  and destination-row/control rectangle layout from explicit metrics and
  destination data. MainWindow retains scrollbar HWND updates, tooltip and
  shortcut-edit control lifetimes, enablement policy, and action effects.
- `ui/DetailsPanelLayout.*` owns pure right-pane panel, tab, content,
  histogram, close-button, and metadata-editor rectangle layout from explicit
  metrics and measured text heights. MainWindow retains font measurement,
  child-window movement, tooltip updates, panel state, and painting.
- `ui/ShellPainter.*` owns stateless GDI and Direct2D painting of the main
  shell background and pane splitters from explicit palette and geometry
  inputs. MainWindow retains render-target, device-resource, and window
  paint orchestration, including toolbar and details-panel painting.
- `ui/DisplaySurfaceRecoveryPolicy.*` owns display-surface retry sequencing,
  including first-attempt relayout and retry-limit decisions. MainWindow
  retains timer ownership, resource recovery, invalidation, and shutdown
  coordination.
- `ui/FileOperationReconciler.*` owns path-based tree effects, current-folder
  reload policy, and delete-focus selection policy. It returns typed effects and
  accepts explicit model/pane snapshots and a scope predicate; it does not own
  HWNDs, services, asynchronous state, or browser/viewer mutation.
- `ui/FolderTreeController.*` owns the folder tree's node data, shell-root
  population, child-presence cache, lazy child enumeration, stale request
  settlement, and asynchronous selection restoration. MainWindow retains the
  tree presentation and workflow policy that connects selection, rename,
  context menus, drag/drop, tooltips, and favorite coloring to the rest of the
  application.
- `ui/FolderTreeDragController.*` owns tree-drag state, drag-image lifetime,
  coordinate translation, hit testing, capture, cursor/drop feedback, and
  cleanup. MainWindow supplies callbacks for tree lookup, destination policy,
  folder-operation effects, and invalidation.
- `ui/ViewerPendingOperationState.*` owns active and queued viewer deletes and
  the active Quick Send request. Viewer close clears this state and invalidates
  saved viewer focus/activation targets so completion from an old viewer cannot
  affect a newly opened viewer.
- `ui/ViewerSynchronizer.*` owns replacement-item selection, preferred/current
  path fallback, empty-model close decisions, selected-index preservation, and
  paired RAW/JPEG payload assembly. MainWindow retains viewer HWND operations
  and calls `ViewerWindow::ReplaceItems` with the returned payload.
- `MainWindow::ApplyCompletedFileOperation` remains the completion orchestrator
  for context capture, model/viewer mutation, watcher coordination, and focus
  restoration. The extracted reconciler preserves the existing operation-origin,
  watcher-echo, optimistic-viewer, selection/focus, and activation rules while
  making the policy independently compilable and testable.

These helpers are registered as explicit `HyperBrowseCore` translation units,
and pure policy behavior is covered by deterministic smoke scenarios. OLE
registration is revoked before the callback-owning drop target is released.
`WindowProc` only associates the HWND with its `MainWindow` instance and
forwards messages; asynchronous private messages and timers are routed through
the explicit collaborators above before synchronous input and paint handling.
The current synchronous message boundaries also include
`MainWindow::HandlePaintMessage` for the buffered D2D/GDI shell paint
transaction and `MainWindow::HandleControlColorMessage` for edit/static
control-color effects; both preserve normal message fall-through behavior.
`MainWindow::HandleNotifyMessage` owns tooltip text preparation and the
folder-tree notify fallback, while `MainWindow::HandleMouseInputMessage`
owns mouse, drag-capture, cursor, drop-file, and mouse-leave effects.
`MainWindow::HandleCommandMessage` owns Quick Actions edit notifications,
filter-edit changes, and command-controller forwarding. Each helper returns
an explicit handled-or-fall-through result to preserve the window procedure's
default behavior.

The concise state, HWND, worker, message, timer, file-operation, viewer,
rendering, and shutdown ownership map is maintained in
[`docs/mainwindow-ownership-map.md`](mainwindow-ownership-map.md).

## Rendering

The current rendering split is intentional:

- `BrowserPane` and `ViewerWindow` use Direct2D/DirectWrite for image presentation, thumbnail cells, overlays, and related text surfaces.
- Main-window legacy shell surfaces, menus, dialogs, status areas, and some details paths still use GDI.
- `D2DRenderer` centralizes factory/resource setup and fallback behavior, including WARP when hardware rendering is unavailable.

When changing rendering code, preserve resource recovery on device/display loss, DPI-aware dimensions, and the distinction between content identity and list position. A numeric index alone is not a safe render-cache key across insertions, removals, or reordering.

## State and persistence

Application settings live under the per-user registry location described in the README, with an environment-variable override for isolated development/test runs. Window geometry is restored only when it fits the current monitor work area. Do not replace a valid persisted folder path with an empty value during shutdown or transient no-selection states.

Persistent thumbnail entries include file identity information such as normalized path and file metadata. User metadata and cache indexes are bounded/coalesced to limit UI and disk contention.

## Change ownership guide

- Folder loading, tree actions, menu commands, focus, and application state: `src/ui/MainWindow.*`.
- Browser item storage and path-based mutation: `src/browser/BrowserModel.*`.
- Thumbnail painting, selection, visible-range scheduling, and details rows: `src/browser/BrowserPane.*`.
- Viewer navigation, image lifetime, transitions, and viewer input: `src/viewer/ViewerWindow.*`.
- Decode selection and format behavior: `src/decode/`.
- Scheduling, worker counts, cancellation, and cache invalidation: `src/services/ThumbnailScheduler.*` and related services.
- Persistent thumbnail files/index: `src/cache/DiskThumbnailCache.*`.
- Shared logging and timing: `src/util/Log.*`, `src/util/Diagnostics.*`, and `src/util/Timing.h`.

Start at the smallest owning component, then follow its nearest call site and test. Avoid moving responsibilities between these boundaries as part of an unrelated bug fix.
