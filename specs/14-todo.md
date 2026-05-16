# HyperBrowse TODO — Unimplemented Spec Items

This document collects all features mentioned in the specification documents that are not yet implemented, organized by priority and source spec.

Last reviewed: 2026-04-17

---

## Recently Completed (Hardening Pass — 2026-04-17)

After a deep alignment review between spec and code, the following hardening
items were addressed in priority order:

- **P0 lifetime fix:** `WriteStringValue` now copies the `wstring_view` into a
  `std::wstring` before passing to `RegSetValueExW` (eliminates a possible
  heap over-read when the view did not point to a null-terminated buffer).
- **P0 i18n:** Exception messages from STL/Win32 now go through
  `hyperbrowse::util::WidenExceptionMessage` (CP_ACP) instead of
  `std::wstring(begin, end)` widening, which mangled multi-byte text.
- **P0 cancellation:** `ThumbnailScheduler` now tracks per-batch epochs and
  skips poisoning `failedKeys_` and notifying decode failure for jobs whose
  request was cancelled before decode started.
- **P0 session end:** `WM_QUERYENDSESSION` and `WM_ENDSESSION` now save window
  state so reboots/log-offs no longer lose layout.
- **P1 priority queue:** `pendingJobs_` is now a `std::multiset` keyed by
  (priority asc, sequence asc) so the highest-priority eligible job is
  selected in O(log n) without copy-and-sort on every dequeue.
- **P1 PendingJob caches extension kind:** `isRaw`/`isJpeg` are computed once
  at insert time, removing repeated `IsRawCacheKey`/`IsJpegCacheKey`
  recomputation on the hot path.
- **P1 viewer thread pool:** Replaced `std::async` per request in
  `ViewerWindow` with a small `BackgroundExecutor` (2 workers), bounding
  thread creation under fast navigation.
- **P1 ordinal compare helper:** `EqualsIgnoreCaseOrdinal` (CompareStringOrdinal)
  removes per-call allocation in case-insensitive string comparison.
- **P1 D2D HQ cubic:** `D2DRenderer` now requests `ID2D1Factory1` and the new
  `DrawBitmapHighQuality` helper QIs the render target for
  `ID2D1DeviceContext` and uses `D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC`
  for image scaling. Used by `ViewerWindow::DrawImageBitmap` and the browser
  thumbnail preview draw. Falls back to LINEAR on Windows 7. Spec 15 was
  also corrected (the cubic enum referenced earlier did not exist on
  `ID2D1RenderTarget`).
- **P1 spec hygiene:** spec 16 now has an Implementation Status section that
  honestly reflects the partial state of the toolbar redesign (icon strip and
  flashing fix done, D2D toolbar paint and full menu restructure deferred).
- **P2 dead code:** Removed `BrowserPane::smoothScrollVelocity_` and the
  unused `kSmoothScrollDeceleration` constant; deleted empty `src/platform/`
  directory; corrected `BUILD_TESTS` option help text.
- **P2 catch-handler i18n:** `FolderEnumerationService` and
  `FolderTreeEnumerationService` use `WidenExceptionMessage` for caught
  `std::exception::what()` strings.
- **P2 build refactor:** Extracted shared compilation units into a
  `HyperBrowseCore` STATIC library so `HyperBrowse.exe` and
  `HyperBrowseTests.exe` link against the same set of object files instead of
  recompiling every translation unit twice.
- **P2 CI:** Added `.github/workflows/ci.yml` running `cmake --preset
  vs2022-x64`, debug build of tests, smoke test execution, and release build
  of `HyperBrowse.exe` on `windows-latest`.

### Deferred for a dedicated session

- **MainWindow.cpp split (~6300 lines).** Recommended decomposition:
  `ui/Toolbar.{h,cpp}`, `ui/Theme.{h,cpp}`, `ui/Registry.{h,cpp}`,
  `ui/dialogs/{TextInputDialog,AboutDialog,ImageInfoDialog}.{h,cpp}`,
  `ui/Commands.h`. This is mechanically straightforward but high-volume and
  needs its own session with focused build verification after each
  extraction.
- **doctest migration of `tests/smoke.cpp`.** The throw-on-first-failure
  harness works, scenarios are already isolated functions, and switching to
  doctest is purely a developer-experience change. Drop `external/doctest/`
  and convert each scenario to a `TEST_CASE` block when it next becomes
  worthwhile.

---

## P0 — High-Value Gaps in Current v1 Scope

### TODO: Sort Direction Toggle (ascending/descending)
- **Source:** spec 01 §7 (Sorting), spec 04 §3 (View menu)
- **Status:** ~~Not implemented.~~ Done. Ascending/descending toggle added to all sort menus (menu bar, action strip, context menu). Direction arrow (↑/↓) shown on action strip sort button. Persisted to registry as `SortAscending`.
- **Scope:** ~~Add ascending/descending toggle to View > Sort By and action strip Sort dropdown. Persist to registry.~~ Done.

### TODO: Date-Taken Sort Mode
- **Source:** spec 10 §7.1 (Workflow gaps), spec 10 §9.1 P0.4
- **Status:** ~~Not available as a sort mode.~~ Done. `DateTaken = 6` added to `BrowserSortMode`. Uses `dateTakenTimestampUtc` from Shell/LibRaw. Items without timestamp sort to end.
- **Scope:** ~~Add `DateTaken` to `BrowserSortMode`.~~ Done.

### TODO: Selected-Item Details Strip or Side Panel
- **Source:** spec 10 §9.1 P0.4, spec 12 §8 (follow-up item 3), spec 13 §5 (recommendation 3)
- **Status:** ~~Not implemented.~~ Done. Added a toggleable info strip below the browser pane showing filename, type, file size, dimensions, camera model, and date taken for the focused item. Toggle via View > Show Info Strip. Persisted to registry as `DetailsStripVisible`. Theme-aware via `WM_CTLCOLORSTATIC`.
- **Scope:** ~~Add a compact info strip or collapsible side panel showing metadata for the focused/selected item without opening the viewer.~~ Done.

### TODO: Configurable Slideshow Interval
- **Source:** spec 04 §9 (Slideshow)
- **Status:** ~~No UI to configure it.~~ Done. `TaskDialogIndirect` picker offers 1/2/3/5/10 second intervals before starting slideshow. Persisted to registry as `SlideshowIntervalMs`.
- **Scope:** ~~Add a slideshow settings surface.~~ Done.

### TODO: Image Information as Proper Dialog
- **Source:** spec 04 §10 (Metadata surfaces)
- **Status:** ~~Currently uses `MessageBoxW` with formatted text.~~ Implemented using `TaskDialogIndirect` with app icon, file info content area, and expandable EXIF/IPTC details section.
- **Scope:** ~~Replace with a custom dialog that shows the app icon, structured metadata fields, and optionally a thumbnail preview.~~ Done.

---

## P1 — Architecture and Rendering Gaps

### TODO: Direct2D / Direct3D 11 Rendering Backend
- **Source:** spec 02 §1 (Recommended Approach), spec 02 §6 (Rendering Architecture), spec 15
- **Status:** ~~All rendering uses GDI.~~ Done. D2D rendering implemented for BrowserPane (thumbnail grid) and ViewerWindow (full-image viewer). DirectWrite text throughout. Per-monitor DPI awareness v2. Smooth inertial scroll and smooth zoom animations. GDI fallback retained for details view and edge cases. See spec 15 for full details.
- **Scope:** ~~Deferred.~~ Done.

### TODO: Formal Decoder Abstraction Interface
- **Source:** spec 02 §5 (Decoder Abstraction)
- **Status:** Decode layer uses free functions and static methods. No formal `CanDecode`/`ReadHeader`/`ReadMetadata`/`DecodeThumbnail`/`DecodeFullImage` polymorphic interface.
- **Scope:** Deferred. Current ad-hoc decode chain (nvJPEG → WIC → LibRaw) works and the function-level interface is adequate for the current format set.

### TODO: Memory Pools for Hot-Path Buffers
- **Source:** spec 03 §7.2 (Memory pools)
- **Status:** Not implemented. Temporary buffers are allocated per-decode.
- **Scope:** Deferred. Only pursue if profiling shows allocation pressure in the decode/scale/render pipeline.

### TODO: Memory Pressure Response
- **Source:** spec 03 §7.4 (Memory pressure response)
- **Status:** Not implemented beyond basic LRU eviction. No active monitoring of system memory or adaptive prefetch reduction.
- **Scope:** Deferred. The 96 MB thumbnail cache cap and 512-entry metadata cache provide sufficient bounded behavior for current workloads.

### TODO: Configurable Mouse Wheel Behavior in Viewer
- **Source:** spec 04 §8.3 (Mouse behavior)
- **Status:** ~~Mouse wheel always zooms. There is no option to switch to next/previous image navigation.~~ Done. Added View > Viewer Mouse Wheel with Zoom and Next/Previous Image modes. Persisted to registry as `ViewerMouseWheelBehavior` and applied live to open viewer windows.
- **Scope:** ~~Add a viewer setting (registry + menu) to choose between zoom and navigate for mouse wheel.~~ Done.

---

## P2 — Enhancement Backlog (from spec 10)

### TODO: Compare/Cull Lite (P1.1)
- **Source:** spec 10 §9.2 P1.1
- **Status:** ~~Not implemented.~~ Done for the lightweight pass. Added a two-up viewer compare mode that reuses adjacent full-image slots, browser-side `Compare Selected` launch for exactly two selected items, explicit View-menu and toolbar compare entry points, and viewer controls for compare (`C` toggle, `Shift+Left/Right` choose previous/next, `X` swap into the compared image).
- **Scope:** ~~Lightweight 2-up compare in the viewer for culling similar shots. Start with two-image compare only.~~ Done for the initial lightweight viewer/browser slice.

### TODO: Recent Destinations and Favorites (P1.2)
- **Source:** spec 10 §9.2 P1.2
- **Status:** ~~Not implemented.~~ Done for the menu-shortcut pass. Added registry-backed recent copy/move destinations, pinned favorite destinations, quick access to recent folders, and File-menu shortcuts for opening recent folders or sending the current selection to recent/favorite destinations. Current-folder favorite pin/unpin is exposed from the File menu. The optional breadcrumb/path surface remains deferred.
- **Scope:** ~~Recent copy/move destinations, pinned favorites, quick access to recent folders, optional breadcrumb for current path.~~ Done except for the optional breadcrumb/path surface.

### TODO: RAW+JPEG Paired Operations (P1.3)
- **Source:** spec 10 §9.2 P1.3
- **Status:** ~~Not implemented.~~ Done for copy/move/delete plus paired-action visibility. Added a persisted `Include Paired RAW+JPEG` mode that expands selected file-operation source paths to matching same-folder/same-stem RAW or JPEG companions, exposes the mode in the File menu and browser context menu, reuses the paired expansion for `Reveal in Explorer` and `Copy Path`, and shows paired companion counts in the status bar when the mode is on.
- **Scope:** ~~Optional paired-action mode: copy/move/delete RAW and matching JPEG together.~~ Done for the file-operation pass.

### TODO: Persistent Disk Thumbnail Cache (P2.1)
- **Source:** spec 10 §9.3 P2.1
- **Status:** Not implemented. Memory-only caching per spec 01.
- **Scope:** Deferred. Requires fully lazy initialization, no startup scan, benchmark proof of warm-revisit improvement.

### TODO: Ratings/Tags (P2.2)
- **Source:** spec 10 §9.3 P2.2
- **Status:** Not implemented.
- **Scope:** Deferred. Pushes product toward organizer territory.

### TODO: Batch Rename (P2.3)
- **Source:** spec 10 §9.3 P2.3
- **Status:** Not implemented.
- **Scope:** Deferred. Should come after the core file-operation layer is mature.

---

## P2 — File Management Gaps (from spec 11)

### TODO: Rename In Place (F2)
- **Source:** spec 11 §2 (deferred items)
- **Status:** ~~Not implemented. FolderWatchService handles external renames, but there is no in-app rename command.~~ Done for the shortcut gap. `F2` now invokes the existing single-item rename command. The app still uses its rename dialog rather than inline label editing in the browser surface.
- **Scope:** ~~Add F2 rename for the focused item. Update model in place.~~ Done for `F2`. True inline label editing remains deferred.

### TODO: Drag-and-Drop File Operations
- **Source:** spec 11 §2 (deferred items)
- **Status:** Not implemented.
- **Scope:** Drag thumbnails to Explorer or other targets for copy/move.

### TODO: Undo Surface Inside HyperBrowse
- **Source:** spec 11 §2 (deferred items)
- **Status:** Not implemented. File operations rely on `IFileOperation` undo behavior (Explorer-level undo).
- **Scope:** Low priority. The system Recycle Bin provides adequate undo for deletes.

---

## P2 — UI Polish Gaps (from specs 12, 13)

### TODO: Move Acceleration Toggles Out of View Menu
- **Source:** spec 13 §5 (recommendation 1)
- **Status:** "Enable NVIDIA JPEG Acceleration" and "Use Out-of-Process LibRaw Fallback" are in the View menu.
- **Scope:** Move to Help menu or a Settings submenu. These are rarely changed and are not "view" concepts.

### TODO: Trim Context Menu
- **Source:** spec 13 §3.3 G (recommendation)
- **Status:** Context menu has ~27 items including sort and view mode radio groups.
- **Scope:** Remove sort, view mode, and folder tree commands from the context menu. Keep selection-specific actions only.

### TODO: Custom About Dialog
- **Source:** spec 13 §3.3 F
- **Status:** About dialog is a plain `MessageBoxW`.
- **Scope:** Low priority. Create a small custom dialog rendering the app icon at 48–64 px alongside version/feature info.

### TODO: Empty-State Icon Watermark
- **Source:** spec 13 §3.3 H
- **Status:** Centered placeholder states use plain text only.
- **Scope:** Low priority. Render a muted version of the app icon above the "Select a folder to begin browsing" text.

### TODO: Common-Control Theming Improvements
- **Source:** spec 12 §8 (follow-up item 4)
- **Status:** Not implemented.
- **Scope:** Low priority. Explore `SetWindowTheme` and other light theming tweaks for TreeView scrollbars and other common controls.

---

## P3 — Benchmarking Infrastructure (from spec 05)

### TODO: Benchmark Datasets
- **Source:** spec 05 §3
- **Status:** No formal benchmark datasets exist. Testing uses ad-hoc folders.
- **Scope:** Create standardized datasets A–E as defined in the benchmarking plan.

### TODO: Benchmark Harness and Reporting
- **Source:** spec 05 §6, §10
- **Status:** Diagnostics infrastructure captures timing spans and counters, but there is no automated benchmark runner or reporting pipeline.
- **Scope:** Create a repeatable benchmark runner that uses the diagnostics system and produces structured reports.

### TODO: Performance Budget Validation
- **Source:** spec 05 §9 (Acceptance Thresholds)
- **Status:** No automated threshold checking.
- **Scope:** Define concrete numeric thresholds and validate in CI or manual benchmark passes.

---

## Items Intentionally Deferred (Not TODOs)

These items were explicitly scoped out and should remain deferred unless product direction changes:

- Heavy image editing, annotations, cropping (spec 01 out-of-scope)
- Animated GIF thumbnails (spec 01 out-of-scope)
- Multipage TIFF navigation (spec 01 out-of-scope)
- Per-image zoom/pan persistence (spec 01 out-of-scope)
- Plugin ecosystems, duplicate finder, face detection, library/database (spec 10 §10)
- Separate module decomposition for controllers (ZoomPanController, SlideshowController, etc.) — consolidated into ViewerWindow/MainWindow/BrowserPane for pragmatic simplicity
- SettingsService as separate module — registry calls are simple enough to inline
- RawPreviewCache as separate module — embedded previews are decoded on demand through the LibRaw path
