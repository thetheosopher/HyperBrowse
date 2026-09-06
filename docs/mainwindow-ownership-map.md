# MainWindow Ownership Map

This map records the current owners of MainWindow-adjacent state and lifetime
boundaries. Source code and `docs/architecture.md` remain authoritative.

## State Machines

- `FolderLoadCoordinator`: folder-load requests, history navigation, watcher
  lifetime, stale-result filtering, deferred reload/tree effects, and pending
  startup/reload presentation state.
- `FolderTreeController`: tree nodes, child-presence cache, lazy enumeration,
  request settlement, and selection restoration.
- `FolderTreeDragController`: tree-drag state, drag image, hit testing,
  capture, cursor/drop feedback, and drag cleanup. MainWindow callbacks perform
  folder validation and the resulting operation.
- `ViewerPendingOperationState`: active and queued viewer deletes plus the
  active Quick Send request. Viewer close clears viewer-owned pending effects.
- `FileOperationJournal`: bounded undo/redo history and operation transitions.
- `FolderWatchChangeCoordinator`: incremental watcher reconciliation and reload
  escalation policy.

## Windows and HWNDs

- `MainWindow`: owns the main HWND, child control HWNDs, shell resources,
  focus/activation capture, and message entry through `WindowProc` and
  `HandleMessage`.
- `BrowserPane`, `ViewerWindow`, and tree/tooltip/edit controls own their
  control-specific HWND behavior and input/rendering state.
- `ExternalDropTarget`: owns the OLE drop-target COM lifetime and invokes
  callback contracts supplied by MainWindow.
- Viewer close invalidates saved viewer focus/activation targets before a later
  viewer can be opened.

## Workers and Callbacks

- Enumeration, folder-watch, thumbnail, metadata, file-operation, batch-
  conversion, and RAW-helper services own their worker threads and cancellation.
- Worker results return through posted window messages or configured callbacks.
- `WindowAsyncMessageRouter` owns asynchronous message-ID dispatch and cleanup
  of heap-owned external-launch payloads.
- `ThumbnailScheduler` owns persistent-cache invalidation work off the UI
  thread; MainWindow and UI callbacks must not call `DiskThumbnailCache`
  directly.

## Messages and Timers

- `MainWindow::WindowProc` associates the HWND with MainWindow and forwards to
  `HandleMessage`.
- `WindowAsyncMessageRouter` owns asynchronous service, browser-pane, viewer,
  and private MainWindow message routing.
- `WindowTimerRouter` owns timer-ID dispatch for shutdown notices, folder
  presentation, memory pressure, and display-surface recovery.
- `MainWindow::HandleMouseInputMessage` owns mouse, drag-capture, cursor,
  drop-file, and mouse-leave effects; `HandleNotifyMessage` owns tooltip and
  folder-tree notify forwarding; `HandleCommandMessage` owns edit-control and
  command-controller forwarding. These helpers preserve explicit handled or
  fall-through results inside MainWindow.
- `HandlePaintMessage` owns the buffered D2D/GDI paint transaction and
  `HandleControlColorMessage` owns edit/static control-color effects.
- Menu-loop, lifecycle, and remaining synchronous routing state remains
  MainWindow-owned until a narrower controller is introduced.

## File Operations

- `FileOperationService` owns asynchronous native shell execution and progress.
- `MainWindow::StartFileOperation` captures foreground/focus state, starts the
  operation, and owns taskbar/status/menu coordination.
- `CaptureFileOperationCompletionContext` snapshots typed undo/redo, viewer,
  tree, activation, and deferred-watch state before completion effects run.
- `FileOperationReconciler` owns path-based tree effects, reload policy, and
  delete-focus policy without owning HWNDs or asynchronous state.
- MainWindow remains the completion orchestrator for browser/model mutation,
  viewer effects, watcher coordination, journal completion, and focus restore.

## Viewer Lifecycle

- `ViewerWindow` owns viewer navigation, image lifetime, transitions,
  prefetch, slideshow, and viewer input.
- `ViewerItemSelectionPolicy` chooses ordered model items and the active index.
- `ViewerSynchronizer` builds replacement items, selected-index state, paired
  RAW/JPEG payloads, and empty-model close decisions.
- MainWindow owns viewer opening, HWND close posting, browser/model snapshots,
  and calls to `ViewerWindow::ReplaceItems`.
- `ViewerPendingOperationState` prevents queued deletes or Quick Send effects
  from being applied to a newly opened viewer after the original viewer closes.

## Rendering and Resources

- `D2DRenderer` owns shared Direct2D factory/resource creation and fallback.
- `BrowserPane` and `ViewerWindow` own their Direct2D presentation resources.
- MainWindow owns shell GDI resources, details/status/menu/toolbar resources,
  and the GDI/Direct2D paint orchestration.
- `ShellPainter`, `StatusBarPainter`, details-panel painters, and command-bar
  painters consume explicit state and resource inputs but do not own HWND
  lifetimes.

## Shutdown and Destruction

1. Mark close pending and cancel active file-operation or batch-conversion work.
2. Stop display-surface retries and unregister session/power notifications.
3. Kill owned timers and stop memory-pressure work.
4. Cancel folder-load and folder-tree producers.
5. Cancel batch conversion and shut down file operations.
6. Revoke OLE drop registration before releasing the callback-owning target.
7. Save persistent state and post application termination.

`FileOperationService::Shutdown()` suppresses late completion and progress
posts, but its serialized worker is joined because `IFileOperation` may still
be inside Windows shell code. The close timer's five-second notice keeps the
user informed without pretending that a shell call can be forcefully timed
out safely.

Any new callback or worker must fit this ordering and must not outlive the
object or HWND receiving its result.
