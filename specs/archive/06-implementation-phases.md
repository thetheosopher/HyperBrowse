# Implementation Phases

## Phase 0 - Repo and Engineering Foundation

### Goals
- create solution structure
- establish build system
- create app shell skeleton
- add diagnostics scaffold

### Deliverables
- CMake build working on Windows 10/11 x64
- empty Win32 shell app with menu/status bar placeholders
- basic logging/diagnostics framework
- dependency integration strategy for LibRaw and optional nvJPEG
- coding standards / formatting setup

### Exit criteria
- app opens a main window
- project builds cleanly in Debug and Release
- diagnostics output can record timing spans

---

## Phase 1 - Browser Shell and Navigation Model

### Goals
- implement main layout
- add folder tree
- add browser pane shell
- create selection and sort models

### Deliverables
- left folder tree
- splitter
- right pane with placeholder content
- menu commands for view mode and sorting
- status bar wiring
- recursive browsing toggle

### Exit criteria
- selecting a folder updates browser model
- splitter persists
- basic sort options exist in the model

---

## Phase 2 - File Enumeration, Metadata, and Virtualized Browser

### Goals
- async folder enumeration
- browser virtualization
- details/list mode support
- metadata extraction foundation

### Deliverables
- file enumeration service
- item model for thousands of images
- virtualized thumbnail grid shell
- virtualized details list shell
- metadata service (header-level first)
- status bar updates for counts and sizes

### Exit criteria
- large folders enumerate without UI freeze
- thumbnail and details modes both work structurally
- status bar updates incrementally

---

## Phase 3 - Decode Backends and Thumbnail Pipeline

### Goals
- implement common image decode path
- add scheduler and memory cache
- render visible thumbnails asynchronously

### Deliverables
- WIC decoder backend
- thumbnail scheduler with priorities and cancellation
- thumbnail memory cache
- placeholder-to-thumbnail replacement flow
- thumbnail size settings

### Exit criteria
- JPEG/PNG/GIF/TIFF thumbnails appear asynchronously
- visible items are prioritized
- scrolling cancels stale work

---

## Phase 4 - Viewer Window

### Goals
- open separate viewer window
- implement navigation and zoom/pan
- add background prefetch

### Deliverables
- viewer window
- next/previous navigation
- fit/100%/zoom controls
- pan support
- full-screen toggle on double-click
- prefetch service for next/previous
- zoom level surfaced to status bar

### Exit criteria
- double-click from browser opens viewer
- viewer navigation works
- zoom/pan is responsive

---

## Phase 5 - Nikon RAW Support

### Goals
- add LibRaw-backed RAW support
- support embedded preview strategy
- expose RAW metadata

### Deliverables
- LibRaw decoder backend
- NEF/NRW header + metadata support
- embedded preview extraction for thumbnails
- full RAW decode path for viewer when supported
- graceful unsupported RAW handling

### Exit criteria
- RAW files appear in browser
- RAW thumbnails render when preview exists
- viewer can open supported RAW files
- unsupported variants fail gracefully

---

## Phase 6 - JPEG Acceleration Path

### Goals
- add optional nvJPEG path
- benchmark against WIC baseline

### Deliverables
- runtime capability detection
- nvJPEG decoder integration
- batched JPEG thumbnail path
- config switch to enable/disable acceleration
- benchmark comparison harness

### Exit criteria
- app runs correctly with and without NVIDIA GPU
- measured results determine whether nvJPEG stays enabled by default

---

## Phase 7 - Competitive Features for v1

### Goals
- add slideshow
- add lossless JPEG rotate
- add batch convert
- deepen metadata presentation

### Deliverables
- slideshow controller
- lossless JPEG rotate command path
- batch convert dialog/flow
- EXIF/IPTC/XMP info surfaces
- browser context menus where needed

### Exit criteria
- all v1 must-have competitive features are usable end-to-end

---

## Phase 8 - Polish, Hardening, and Packaging

### Goals
- stabilize performance
- improve theme support
- finalize portable + installer output

### Deliverables
- dark/light mode complete
- error handling polish
- folder watch stabilization
- portable package
- installer package
- release notes draft

### Exit criteria
- benchmark targets are acceptable
- no major reliability issues remain in supported workflows
- both packaging formats are tested

---

## Phase 9 - Release Candidate Validation

### Goals
- run benchmark suite
- validate mixed hardware support
- validate representative datasets

### Deliverables
- benchmark report
- compatibility report
- known issues list
- release candidate build

### Exit criteria
- performance is validated against v1 priorities
- hardware fallback behavior is confirmed
- release risks are documented
