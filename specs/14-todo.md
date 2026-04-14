# HyperBrowse TODO — Unimplemented Spec Items

This document collects all features mentioned in the specification documents that are not yet implemented, organized by priority and source spec.

Last reviewed: 2026-04-13

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
- **Status:** Mouse wheel always zooms. There is no option to switch to next/previous image navigation.
- **Scope:** Add a viewer setting (registry + menu) to choose between zoom and navigate for mouse wheel.

---

## P2 — Enhancement Backlog (from spec 10)

### TODO: Compare/Cull Lite (P1.1)
- **Source:** spec 10 §9.2 P1.1
- **Status:** Not implemented.
- **Scope:** Lightweight 2-up compare in the viewer for culling similar shots. Start with two-image compare only.

### TODO: Recent Destinations and Favorites (P1.2)
- **Source:** spec 10 §9.2 P1.2
- **Status:** Not implemented.
- **Scope:** Recent copy/move destinations, pinned favorites, quick access to recent folders, optional breadcrumb for current path.

### TODO: RAW+JPEG Paired Operations (P1.3)
- **Source:** spec 10 §9.2 P1.3
- **Status:** Not implemented.
- **Scope:** Optional paired-action mode: copy/move/delete RAW and matching JPEG together.

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
- **Status:** Not implemented. FolderWatchService handles external renames, but there is no in-app rename command.
- **Scope:** Add F2 rename for the focused item. Update model in place.

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
