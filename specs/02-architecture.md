# Architecture

## 1. Recommended Approach

Use a **low-overhead pure native Windows architecture**:

- Win32 application shell
- Direct2D rendering for 2D image presentation
- Direct3D 11 for texture/resource management and GPU interop
- WIC for baseline common-format decode
- LibRaw for Nikon RAW
- nvJPEG as an optional accelerated JPEG thumbnail path
- CMake-based build

This avoids the extra shell/framework overhead of larger UI stacks and keeps control over rendering, virtualization, threading, and memory behavior.

## 2. Architectural Principles

1. Never block the UI thread with decode or scaling work.
2. Keep format-specific code behind decoder interfaces.
3. Keep scheduling and cancellation explicit.
4. Treat visible-content latency as the top scheduler priority.
5. Prefer simple predictable behavior over fragile heuristics.
6. Keep GPU use optional and capability-driven.
7. Keep RAW support isolated from common-format code paths.

## 3. Process Model

Single-process desktop app.

### Threads
- **UI thread**
  - window procedure
  - input handling
  - layout
  - command routing
  - presentation coordination
- **File enumeration thread(s)**
- **Metadata thread pool**
- **Thumbnail decode thread pool**
- **Viewer decode / prefetch thread(s)**
- **Optional GPU work coordination thread** if profiling justifies it

## 4. Core Modules

### App shell and UI
- `AppShell`
- `MainWindow`
- `MenuController`
- `CommandRouter`
- `ThemeManager`
- `StatusBarController`

### Browser UI
- `FolderTreePane`
- `BrowserPane`
- `ThumbnailGridView`
- `DetailsListView`
- `SelectionModel`
- `SortController`
- `LayoutController`

### Viewer
- `ViewerWindow`
- `ViewerNavigationController`
- `ZoomPanController`
- `FullscreenController`
- `SlideshowController`

### Services
- `FileEnumerationService`
- `FolderWatchService`
- `MetadataService`
- `ThumbnailScheduler`
- `ViewerPrefetchService`
- `BatchConvertService`
- `SettingsService`
- `DiagnosticsService`

### Decode backends
- `DecodeServiceWIC`
- `DecodeServiceNvJpeg`
- `DecodeServiceRawLibRaw`

### Caching
- `FileListCache`
- `MetadataCache`
- `ThumbnailMemoryCache`
- `ViewerImageCache`
- `RawPreviewCache`

### Rendering
- `RenderBackendD2D`
- `GpuResourceManager`
- `TexturePool`
- `PresentationSurface`

## 5. Decoder Abstraction

All format handling must be routed through a shared decoder contract.

## 5.1 Decoder interface responsibilities
Every decoder implementation should expose operations equivalent to:

- `CanDecode(file)`
- `ReadHeader(file)`
- `ReadMetadata(file)`
- `ReadEmbeddedThumbnail(file)`
- `DecodeThumbnail(file, targetSize)`
- `DecodeFullImage(file, options)`
- `CanBatchDecode(files)`
- `GetCapabilities()`

## 5.2 Decoder implementations

### WIC decoder
Handles:
- JPEG fallback
- PNG
- GIF
- TIFF
- optional WIC-available formats later

### nvJPEG decoder
Handles:
- accelerated JPEG thumbnail batches when supported
- single-image JPEG decode if useful and justified by benchmark data

### LibRaw decoder
Handles:
- NEF / NRW metadata
- embedded preview extraction
- full RAW decode for viewer path where supported

## 6. Rendering Architecture

## 6.1 Browser rendering
Use a virtualized drawing surface rather than creating heavyweight child controls per thumbnail.

### Thumbnail mode
- one scrollable virtual canvas
- only visible + near-visible items acquire live thumbnail resources
- placeholders paint immediately
- thumbnails appear asynchronously as decode completes

### Details mode
- virtualized list with owner-drawn rows/cells where beneficial
- only visible rows are measured/rendered

## 6.2 Viewer rendering
Viewer window uses a dedicated rendering path optimized for:
- fast open
- zoom/pan responsiveness
- low-overhead redraws
- clean full-screen transitions

## 7. Data Flow

## 7.1 Folder selection flow
1. user selects folder
2. browser model issues async enumeration request
3. placeholder items appear immediately
4. metadata extraction starts
5. visible thumbnail jobs are enqueued first
6. right pane updates incrementally
7. status bar updates continuously

## 7.2 Thumbnail flow
1. scheduler receives item + priority
2. scheduler chooses decode backend
3. decoder reads minimal required data
4. scaled thumbnail bitmap is produced
5. texture/bitmap resource is cached
6. browser invalidates visible cell for repaint

## 7.3 Viewer flow
1. user double-clicks item
2. viewer opens immediately with placeholder / loading state
3. async full-image decode starts
4. current image is displayed
5. next/previous images are prefetched in background
6. zoom/pan operations use already-decoded image resources

## 8. Scheduler Design

### Priority tiers
1. visible thumbnails
2. near-visible thumbnails
3. metadata for visible rows/items
4. viewer current image
5. viewer next/previous prefetch
6. offscreen thumbnails
7. background housekeeping

### Scheduler requirements
- cancellation support
- bounded queue depth
- backpressure under memory pressure
- starvation avoidance for UI-critical work

## 9. Cache Design

## 9.1 Memory-only v1 caching
Persistent disk caching is out of scope for v1. Use memory-only bounded caches.

### Recommended caches
- `ThumbnailMemoryCache`
- `ViewerImageCache`
- `MetadataCache`
- `RawPreviewCache`

### Cache keys
At minimum include:
- normalized file path
- file size
- modified timestamp
- decode options / thumbnail size where relevant

### Eviction
Use LRU or segmented-LRU style eviction with hard memory ceilings.

## 10. Folder Watching

### Responsibilities
- detect file add/remove/update/rename events
- patch browser model incrementally
- invalidate relevant cache entries
- avoid full folder teardown unless necessary

## 11. Batch Convert Architecture

Batch convert should be its own subsystem, not a side effect of viewer code.

### Responsibilities
- command parsing and validation
- job queueing
- progress reporting
- cancellation
- output path policy
- format conversion and metadata policy

## 12. Diagnostics and Instrumentation

Instrument all key pipeline stages.

### Required timing spans
- app startup
- folder enumeration
- metadata extraction
- thumbnail queue wait time
- thumbnail decode time
- thumbnail scale time
- viewer open time
- viewer decode time
- prefetch time
- batch convert throughput

## 13. Project Structure Recommendation

```text
HyperBrowse/
  CMakeLists.txt
  cmake/
  docs/
  external/
  src/
    app/
    ui/
    browser/
    viewer/
    decode/
    render/
    services/
    cache/
    platform/
    util/
  tests/
  tools/
  assets/
```

## 14. Dependency Policy

Keep dependencies intentionally small.

### Acceptable core dependencies
- Windows SDK
- DirectX / WIC APIs from platform SDK
- LibRaw
- optional nvJPEG
- minimal test framework

### Avoid for v1
- large UI frameworks
- browser/web runtimes
- dependency-heavy image libraries where WIC/LibRaw already solve the problem
