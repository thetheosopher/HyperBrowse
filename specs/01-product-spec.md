# High-Performance Windows Image Browser - Product Specification

## 1. Product Summary

**Working name:** HyperBrowse

HyperBrowse is a native Windows image browser and viewer optimized for very fast browsing of folders containing thousands of images. The product focuses on low-latency interaction, fast thumbnail creation, smooth scrolling, fast full-image viewing, and a familiar Explorer-like layout.

The application is intentionally scoped as a **pure viewer/browser** rather than an editor. It competes primarily on browsing speed, viewing speed, metadata visibility, slideshow workflow, batch conversion, and lossless JPEG rotation.

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

## 4. Supported Formats (v1)

### Standard image formats
- JPEG / JPG
- PNG
- GIF
- TIFF / TIF

### RAW formats
- Sony ARW
- Canon CR2
- Canon CR3
- Adobe DNG
- Nikon NEF
- Nikon NRW
- Fujifilm RAF
- Panasonic RW2

## 5. High-Level Feature Scope

### Included in v1
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
- Lossless JPEG rotate
- Batch convert
- Metadata support:
  - EXIF
  - IPTC
  - XMP
- JPEG EXIF orientation handling
- Background prefetch of next/previous viewer images
- Dark mode and light mode
- Portable build and installer build

### Explicitly out of scope for v1
- Heavy image editing
- Annotations / painting
- Cropping workflows
- Compare mode
- Tagging / rating
- Contact sheet export
- Persistent disk thumbnail cache
- Animated GIF thumbnails
- Multipage TIFF navigation
- Per-image zoom/pan persistence

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
- support lossless JPEG rotate

### GIF
- use first frame for thumbnails
- no animated thumbnails in v1

### TIFF
- first page only in browser and viewer for v1

### Supported RAW formats (ARW / CR2 / CR3 / DNG / NEF / NRW / RAF / RW2)
- support metadata extraction
- support thumbnail generation from embedded preview when available
- support full-image viewing through RAW decode path when supported
- degrade gracefully for unsupported RAW variants

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
5. Memory efficiency within the v1 in-memory cache strategy

### Folder scale target
- optimized for folders with thousands of images

### Caching policy (v1)
- memory cache only
- no disk thumbnail cache

## 12. Distribution

### Required outputs
- installer build
- portable build

## 13. Acceptance Criteria

The v1 product is successful if it:
- feels immediate at startup
- remains responsive during large-folder enumeration
- begins showing visible thumbnails quickly
- allows smooth navigation through thousands of items
- opens the viewer window quickly on double-click
- handles mixed-format folders without blocking the UI
- works on systems with and without NVIDIA GPUs
