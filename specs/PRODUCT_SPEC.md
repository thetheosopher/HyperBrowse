# HyperBrowse Product Specification (Authoritative)

**Current release:** 2.3.0
**Status:** Current product contract
**Authority:** Source code, smoke tests, and this document define shipped
behavior. Proposed work belongs in [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) and
must not be described here as implemented until it lands.

## 1. Product Summary

**Working name:** HyperBrowse

HyperBrowse is a native Windows image browser and viewer optimized for very fast browsing of folders containing thousands of images. The product focuses on low-latency interaction, fast thumbnail creation, smooth scrolling, fast full-image viewing, and a familiar Explorer-like layout.

The application is intentionally scoped as a **browser/viewer first** product
rather than an editor or catalog. It competes on browsing speed, viewing speed,
metadata visibility, slideshow workflow, practical file management, batch
conversion, and low-friction culling.

## 2. Core Product Goals

1. Make thumbnail creation the highest-priority performance target.
2. Keep the UI responsive in folders containing thousands of images.
3. Open a separate viewer window quickly on double-click.
4. Use GPU acceleration where it measurably improves throughput or responsiveness.
5. Degrade gracefully on systems without NVIDIA hardware.
6. Keep the UI minimal: menus, status bar, shortcuts, and compact chrome.
7. Support both installed and portable distribution models.

## 3. Supported Platforms

- Windows 10
- Windows 11
- x64 only

## 4. Supported Formats

### Standard image formats
- JPEG / JPG
- PNG
- GIF
- TIFF / TIF
- WebP

### RAW formats
- Sony ARW
- Canon CR2
- Canon CR3
- Adobe DNG
- Nikon NEF
- Nikon NRW
- Fujifilm RAF
- Panasonic RW2

## 5. Current Feature Scope

### Shipped capabilities
- Folder tree on the left
- Resizable splitter
- Right pane with:
  - thumbnail mode
  - details/list mode
- Status bar showing:
  - folder image count
  - folder total size
  - selected image count
  - selected total size
  - current zoom level while viewer window is active
- Separate viewer window
- Recursive browsing support, default off
- Folder watching / live refresh
- Slideshow
- JPEG orientation adjustment (EXIF metadata-only rotation)
- Batch convert
- Metadata support:
  - EXIF
  - IPTC
  - XMP
- JPEG EXIF orientation handling
- Background prefetch of next/previous viewer images
- Dark mode and light mode
- Portable build and installer build
- Two-up compare, ratings, tags, structured in-folder filtering, date-taken
  sorting, and ascending/descending sort direction
- Copy, move, rename, batch rename, delete, permanent delete, duplicate,
  clipboard transfer, shell drag/drop, Quick Actions, paired RAW/JPEG
  operations, taskbar progress, and supported file-operation undo/redo
- Multiple viewer windows, viewer display recovery, slideshow transitions, and
  configurable viewer input behavior
- Per-monitor DPI awareness, asynchronous folder-tree child-presence probing,
  stale-result rejection, persistent thumbnail-cache maintenance, diagnostics,
  and smoke-tested Debug/Release packaging paths

### Deferred or intentionally out of scope
- Heavy image editing
- Annotations / painting
- Cropping workflows
- Contact sheet export
- Animated GIF/WebP playback
- Multipage TIFF navigation
- Per-image zoom/pan persistence
- Drag-and-drop file operations between separate HyperBrowse instances
- Cross-folder indexed search, duplicate finding, face detection, and a
  library/database back end

WebP is supported through WIC for static decode and thumbnails. GIF and TIFF
browsing presents the available first frame or page. Animated playback remains
deferred. See [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) for proposed work and
explicit product boundaries.

## 6. Main Window Layout

### Left pane
Explorer-style folder tree.

### Center
Resizable vertical splitter.

### Right pane
Supports two modes:

1. **Thumbnail mode**
   - virtualized grid
   - variable thumbnail size
   - optimized for fast scroll + fast selection

2. **Details/List mode**
   - rows with sortable columns
   - intended for power users and fast scanning

### Bottom
Status bar with folder totals, selection totals, and viewer zoom level.

## 7. Browsing Behavior

### Recursive browsing
- Supported
- Default is off
- Can be toggled at runtime
- Affects right-pane enumeration and slideshow navigation scope

### Sorting
Support custom sort options:
- filename
- modified date
- file size
- image dimensions
- type
- random
- date taken

Sort direction (ascending/descending) is implemented and persisted per session.

### Selection modes
Support:
- single selection
- multi-select
- Ctrl-toggle
- Shift-range selection
- rubber-band selection
- keyboard navigation and selection

### Folder watching
- watch current folder for add/remove/update
- update UI incrementally
- preserve selection where practical
- avoid full reload when a smaller delta update is sufficient

## 8. Viewer Window

### Viewer interactions
- next / previous image
- zoom in / out
- fit to window
- actual size / 100%
- pan
- rotate
- slideshow
- full screen
- keyboard shortcuts
- mouse wheel support

### Viewer policies
- double-click toggles full screen
- prefer low latency over animated zoom/pan
- reset zoom/pan state for each image
- prefetch next/previous images in background

## 9. File Format Behavior

### JPEG
- always apply EXIF orientation automatically
- support JPEG orientation adjustment (EXIF metadata-only rotation)

### GIF
- use first frame for thumbnails
- animated playback is not currently supported

### TIFF
- browser and viewer currently present the first page only

### Supported RAW formats (ARW / CR2 / CR3 / DNG / NEF / NRW / RAF / RW2)
- support metadata extraction
- support thumbnail generation from embedded preview when available
- support full-image viewing through RAW decode path when supported
- degrade gracefully for unsupported RAW variants
- RAW decode available in-process or out-of-process (via HyperBrowseRawHelper.exe, default)

## 10. Metadata

Expose EXIF, IPTC, and XMP data where available.

### Metadata surfaces
- details/list view columns where practical
- image information dialog/panel
- optional viewer info overlay

## 11. Performance Requirements

### Priority order
1. Thumbnail creation speed
2. Smooth scrolling in large folders
3. Viewer open latency
4. Next/previous navigation latency
5. Memory efficiency across the adaptive in-memory and persistent thumbnail
  cache strategies

### Folder scale target
- optimized for folders with thousands of images

### Caching policy
- runtime-adaptive in-memory thumbnail cache sized to host RAM (default ~`min(totalRam/8, availableRam/5)`, clamped 128 MB–1 GB)
- runtime-adaptive metadata cache (default sized from host RAM, clamped 2,048–65,536 entries)
- optional persistent thumbnail cache under `%LOCALAPPDATA%\HyperBrowse\thumbnail-cache`, maintained off the UI thread

## 12. Distribution

### Required outputs
- installer build
- portable build

## 13. Product Invariants

Every shipped or proposed change must preserve these invariants:
- feels immediate at startup
- remains responsive during large-folder enumeration
- begins showing visible thumbnails quickly
- allows smooth navigation through thousands of items
- opens the viewer window quickly on double-click
- handles mixed-format folders without blocking the UI
- works on systems with and without NVIDIA GPUs
- keeps decode, metadata, cache, filesystem, and shell work off the UI thread
- rejects stale asynchronous results before they mutate a newer folder, viewer,
  or thumbnail request
- preserves valid viewer content during asynchronous image transitions unless
  an explicit error or empty state is required
- keeps public command and message contracts stable unless the product change
  explicitly requires a new contract

## 14. Related Contracts

- [docs/architecture.md](../docs/architecture.md) defines ownership,
  threading, rendering, and lifetime boundaries.
- [docs/user-guide.html](../docs/user-guide.html) provides the user-facing
  current UI and shortcut reference; source and smoke tests remain authoritative
  for behavior.
- [docs/testing.md](../docs/testing.md) defines build, smoke, benchmark, and
  manual validation expectations.
- [FUTURE-ROADMAP.md](FUTURE-ROADMAP.md) contains deferred ideas and their
  acceptance bars; it is not evidence that those ideas are shipped.
