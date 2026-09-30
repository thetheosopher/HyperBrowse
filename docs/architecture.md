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

The WIC path covers JPEG, PNG, GIF, TIFF, WebP, HEIC, and JPEG XL. HEIC and
JPEG XL decoding require a compatible installed Windows codec; codec
availability is not detected in advance. LibRaw handles the supported RAW
families, and nvJPEG is an optional accelerated JPEG path with WIC fallback.
Animated playback and multipage navigation are outside the current decode
contract; multi-frame WIC files are presented through the available frame.

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

`DiskThumbnailCache` is especially important: its index and cache files are protected by process-wide persistence coordination, so cache operations must not be called from the UI thread. `ThumbnailScheduler` owns the single low-priority persistence worker for lookup, store, invalidation, statistics, compaction, purge, access-journal flushing, and shutdown draining; visible lookup requests are prioritized ahead of queued maintenance work.

Persistent thumbnail storage uses a versioned `index.tsv` and journal alongside
sharded entry files at `xx/yy/<stable-hash>.bin`. The index retains the full
normalized source key, while new entry and index replacements use temporary
 files followed by write-through renames. A legacy flat `.thumb` index remains
 readable; valid legacy entries are copied into the sharded layout in bounded
 batches, the new index and journal are committed with write-through renames,
 and only then are each batch's legacy files removed. Subsequent cache-worker
 operations continue incomplete migration batches after restart. Access
 ordinals are batched and flushed by the same worker; threshold compaction is
 deferred until the worker queue has been idle and no visible decode work is
 pending. Statistics include aggregate and deterministic per-shard file,
 orphan, and missing-entry data.

## Main data flows

### Folder navigation

1. `MainWindow` starts a folder load and resets the browser presentation state.
2. `FolderEnumerationService` enumerates asynchronously and posts batches.
3. `BrowserModel` receives incremental items and tracks enumeration state.
4. `BrowserPane` presents early items, schedules visible/near-visible thumbnails, and requests metadata as needed.
5. Coalesced UI updates keep large-folder enumeration from sorting, painting, or scheduling once per worker batch.
6. `FolderTreeController` probes child-directory presence asynchronously so tree expansion indicators do not block folder navigation.
7. `FolderWatchService` applies external changes incrementally when safe and requests a full reload for large or ambiguous event bursts.

### Viewer navigation

1. `ViewerWindow` changes the selected item and tries the memory/prefetch cache.
2. A cache hit presents immediately.
3. A miss starts asynchronous full-image loading while preserving the last valid displayed image where possible.
4. The current image and adjacent prefetch slots are updated when decode results arrive.
5. Delete and other list mutations explicitly invalidate index-keyed render resources before an index can refer to a different file.

`MainWindow` owns a designated viewer reuse slot plus a collection of additional
`ViewerWindow` instances. Normal Open targets the reuse slot; Open in New Viewer
Window creates an independent viewer in the same process. Each viewer is
identified by its HWND, and viewer-originated commands and asynchronous file
operation completion retain that identity so Delete, Quick Actions, context-menu
commands, focus restoration, and close handling affect only the originating
window. Shared preferences are applied to every open viewer, while image and
navigation state remains per window.

### File operations

`FileOperationService` performs native shell operations asynchronously and reports completion/progress to `MainWindow`. Browser and viewer workflows share operation types, so operation origin must be tracked separately from the operation type. Completion logic must also account for folder-watch echoes, optimistic viewer state, selection/focus restoration, and shell-dialog foreground activation.

During close, `MainWindow` first enters a close-pending state and requests
cooperative cancellation. It keeps the HWND and shell owner alive until the
completion path is observed, then `WM_DESTROY` marks the service shared state as
shutting down and joins the serialized worker before posting application
termination. The five-second status notice is a user-visible wait threshold,
not a forceful worker timeout; shell code is never detached while it can still
post results.

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
- `ui/ViewCommandController.*` owns the view, tools, help, diagnostics, and
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

### B4 display-color contract

Color management is an opt-out SDR image-display feature, enabled by default
and persisted through `ViewerSettingsPersistence`. **View > Color Management**
changes the browser and every open viewer, including all two-, three-, and
four-image compare tiles. It does not transform chrome, icons, text, metadata,
histograms, exports, or source files, and adds no keyboard shortcut.

Each rendering window captures its own monitor's `MONITORINFOEX::szDevice`.
A worker creates a fresh DC for that exact display and uses `GetICMProfileW`
to obtain its effective Windows default output profile. No `SetICMProfile`
override or device-independent working space is used. `WcsGetUsePerUserProfiles`
reports whether per-user or system associations are selected. The Unicode
profile buffer size is in WCHARs. Generic device-name WCS default-profile lookup
is not used: local native stack captures showed it entering printer-spooler RPC
and blocking shutdown. Neither the primary monitor nor an owner window
substitutes for the rendering window. Profile contents, not HWND
or HMONITOR values, identify a destination. Monitor comparisons on window
movement detect equal-DPI moves; display/settings notifications and a bounded
asynchronous poll detect profile replacement without a move.

Canonical memory and disk cache entries retain source-oriented PBGRA pixels
and source color information, independent of any destination. Valid embedded
RGB ICC profiles take precedence; untagged images are sRGB. Malformed profiles,
unsupported profile/pixel-format combinations, inaccessible destination
profiles, and failed transforms use the existing untransformed image and
record diagnostics without a per-image dialog. LibRaw processed RGB is sRGB;
an embedded JPEG preview retains its exposed ICC information. nvJPEG supplies
RGB pixels but not ICC metadata, so WIC reads the original encoded bytes on the
decode worker and attaches that context to the canonical result, with the same
untagged sRGB fallback. Legacy cache entries lacking source context are resolved
on a worker, never in paint or a window procedure; legacy RAW pixels assume
sRGB. A codec that exposes neither ICC context nor profile metadata is treated
as untagged. Rejected PNG/TIFF profile metadata selects unchanged fallback.

Orientation and scaling remain in the canonical decoder. Display conversion
uses straight-alpha BGRA with `IWICColorTransform`, then restores the original
alpha and premultiplies once for D2D/GDI. Low-alpha 8-bit round trips can incur
rounding; premultiplied channels are never treated as straight colors. Non-RGB
embedded profiles incompatible with the canonical RGB surface are explicitly
unsupported and fall back unchanged rather than applying an incorrect profile.

A bounded per-window display cache keys results by the existing source identity,
canonical pixel-object identity, destination content/version, and setting
generation. Navigation/content replacement, monitor/profile changes, toggles,
and recipient shutdown reject obsolete completions. Previous valid output
remains visible while conversion runs; disabling immediately selects canonical
pixels. Display recovery reuses these representations, never transforms an
already converted bitmap, and preserves selection, tile identity, zoom, and pan.
Display entries use weak canonical references after conversion so they do not
pin evicted source-cache images; upload identity references are also weak and
validate ownership before reusing an HBITMAP. Each window retains at most 128 entries and
64 MiB (browser) or 512 MiB (viewer) of converted pixels. Larger images use
canonical rendering with `color.display_cache.too_large` diagnostics.
ICC payloads are limited to 4 MiB at extraction, persistence, and profile lookup;
oversized profiles select unchanged fallback.

Correctness gates use generated ICC profiles and expected pixel tolerances,
not successful initialization or whichever monitor profile is installed.
Physical sRGB/wide-gamut multi-monitor and 100/150/200% scaling review remains
a separate manual gate. Cloud Actions are disabled and are not a validation
dependency for this work.

## Dialog Layout

Application-owned dialog frames use `src/ui/DialogShell.*` for monitor work-area discovery, application text-size metrics, frame clamping, and centering. `src/ui/SettingsLayout.*` is the pure measured layout engine for the Settings pages; `MainWindow` converts its logical rectangles to physical pixels only when positioning Win32 child windows or drawing through Direct2D.

The Settings dialog keeps its footer outside the scrollable body, recomputes its preferred frame on DPI and application text-size changes, and updates accessibility bounds from the same logical geometry. About, Shortcut Reference, Slideshow Settings, Performance Settings, File Associations, Diagnostics, text input, rename, batch rename, and Image Information retain their existing content procedures but use the shared frame-placement policy for DPI transitions and work-area containment. The removed legacy consolidated Settings window is not an alternate route; `PromptForExperimentalSettings` is the only application-owned Settings entry point.

### Dialog surface inventory

Application-owned shells are responsible for their frame geometry, app text-size metrics, theme, focus, and accessibility contract:

- About, Shortcut Reference, Slideshow Settings, Image Information, Performance Settings, File Associations, Diagnostics, text input, rename, and batch rename use custom Win32 window procedures and the shared dialog DPI/work-area helpers.
- Experimental Settings uses the measured `SettingsLayout` engine, Direct2D content, native edit/combo controls where needed, and the shared dialog shell policy.

Windows-owned surfaces remain intentional exceptions and are not expected to follow the application palette or app-owned geometry metrics:

- Persistent thumbnail-cache status and maintenance use the native Windows Task Dialog.
- File pickers, shell property dialogs, and default-apps settings are delegated to Windows shell APIs.
- Short validation, confirmation, and error prompts use `MessageBoxW`; these are transient OS-owned prompts rather than application-owned dialog families.

This inventory is the boundary for layout and accessibility coverage: deterministic geometry tests exercise application-owned shells, while native Windows surfaces are covered by API result and ownership checks. The framework decision remains deferred until the Win32 pilot has measured accessibility, startup, memory, rendering, and migration costs.

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
