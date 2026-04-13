# Performance Strategy

## 1. Performance Philosophy

The application wins on user-perceived speed, not on theoretical throughput alone.

### Top priorities
1. visible thumbnail latency
2. thumbnail throughput in large folders
3. smooth scroll performance
4. fast viewer open latency
5. responsive next/previous navigation

## 2. Primary Constraints

- folders may contain thousands of images
- mixed image formats are common
- systems may or may not have NVIDIA GPUs
- v1 uses memory cache only
- UI must remain responsive at all times

## 3. Baseline Performance Rules

1. No heavy work on the UI thread.
2. Always show placeholders first.
3. Always prioritize visible items.
4. Cancel stale work aggressively.
5. Keep queues bounded.
6. Avoid allocating large temporary buffers in hot paths when reusable pools can be used.
7. Favor predictable frame time over aggressive speculative work.

## 4. Thumbnail Pipeline

## 4.1 Pipeline goals
- paint visible placeholders immediately
- create visible thumbnails first
- support fast scroll through thousands of items
- avoid wasteful decode for scrolled-away content

## 4.2 Pipeline stages
1. enumerate file list
2. produce placeholder browser items
3. extract lightweight metadata
4. enqueue visible thumbnail decode jobs
5. decode using best backend
6. scale to target size
7. store in memory cache
8. repaint target cells

## 4.3 Backend selection policy

### JPEG
- prefer nvJPEG for batch thumbnail decode when supported and beneficial
- fall back to WIC when needed

### PNG / GIF / TIFF
- use WIC

### Supported RAW formats
- ARW / CR2 / CR3 / DNG / NEF / NRW / RAF / RW2
- prefer embedded preview extraction for thumbnail path when available
- use LibRaw full decode only when needed

## 5. Viewer Performance Strategy

## 5.1 Viewer open policy
- open viewer window immediately
- show loading placeholder if full decode is not ready
- decode selected image asynchronously
- prefetch adjacent images after current image becomes stable

## 5.2 Prefetch policy
- preload next image
- preload previous image
- cancel stale prefetch when navigation jumps far away
- throttle prefetch if memory pressure rises

## 5.3 Zoom/pan policy
- prioritize low latency over animation
- use a presentation path that minimizes expensive re-decode
- avoid recomputing image data during simple pan operations

## 6. UI Virtualization Strategy

## 6.1 Thumbnail grid
- virtualized item model
- only visible + near-visible cells generate live thumbnail requests
- maintain a small lookahead region
- avoid child window per item

## 6.2 Details mode
- virtualized rows
- lazy metadata population
- sort on model data, not UI elements

## 7. Memory Strategy

## 7.1 v1 memory model
Memory cache only. No persistent disk cache.

## 7.2 Memory pools
Use reusable pools where profiling justifies them for:
- decode buffers
- thumbnail buffers
- texture objects
- temporary scale buffers

## 7.3 Cache policy
Use bounded LRU-style caches for:
- thumbnails (default 96 MB, LRU eviction by byte count)
- metadata (default 512 entries, LRU eviction by count)
- viewer current/next/previous images (3-slot adjacent cache, no eviction)

Note: Segmented-LRU and memory pool strategies from the original spec are deferred. Basic LRU eviction is sufficient for current workloads.

## 7.4 Memory pressure response
- evict far-offscreen thumbnails first
- reduce speculative prefetch
- shrink lookahead window if needed
- keep current viewer image protected from early eviction

## 8. Concurrency Strategy

## 8.1 Thread pools
Use separate logical queues for:
- enumeration (async per-request futures)
- folder tree enumeration (async per-request futures)
- metadata (dedicated worker threads with condition variable signaling)
- thumbnail decode (two pools: General and Raw workers)
- viewer decode / prefetch (async per-request futures)

### Runtime-adaptive worker count
- Total thumbnail worker count defaults to `std::thread::hardware_concurrency()` (falls back to 2 if unavailable)
- Raw workers: `max(1, total / 4)`, remaining slots assigned to General workers
- Explicit worker counts can be supplied to override auto-configuration

### Cross-queue work stealing
- When a worker's primary queue (General or Raw) is empty, it steals jobs from the other queue
- This prevents idle workers when the workload is skewed toward one format type

## 8.2 Avoiding contention
- keep lock scopes short
- prefer immutable item snapshots for cross-thread handoff
- separate scheduling from rendering state
- do not hold global locks during decode or file I/O

## 9. RAW Performance Strategy

RAW handling should optimize for responsiveness, not maximum fidelity in v1.

### Thumbnail path
- attempt embedded preview extraction first
- use full RAW decode only when necessary

### Viewer path
- decode full RAW image asynchronously
- allow a staged presentation path if preview then full image is useful

### Failure handling
- if a RAW variant cannot fully decode, surface a clear unsupported/limited state
- do not stall the entire browser for one problematic file

## 10. GPU Strategy

## 10.1 GPU usage principles
- GPU is an optimization, not a requirement
- only use GPU paths when capability detection succeeds
- keep CPU fallback paths complete and production-grade

## 10.2 nvJPEG policy
- use primarily for JPEG thumbnail batches
- benchmark before using it in more paths
- keep implementation optional and isolated

## 10.3 Presentation path
- Direct2D/Direct3D11 should minimize redraw overhead
- avoid unnecessary texture recreation during navigation and zoom/pan

## 11. Startup Strategy

### Goals
- create usable shell quickly
- defer expensive initialization
- initialize optional components lazily where possible

### Recommended startup sequence
1. init process and settings
2. create main window and basic UI chrome
3. initialize render backend
4. show window
5. start folder load and background services
6. initialize optional GPU-specific components lazily or in parallel

## 12. Performance Budgets (Initial Targets)

These are draft goals to validate with benchmark data.

### Browser
- app launch should feel immediate
- folder switch should not freeze UI
- first visible placeholders should paint immediately
- first visible thumbnails should begin appearing quickly
- scrolling should remain smooth in large folders

### Viewer
- double-click should open viewer window immediately
- current image should appear quickly
- next/previous navigation should feel near-instant when prefetched

## 13. Performance Anti-Patterns to Avoid

- decoding on UI thread
- synchronous metadata fetch during paint
- full-folder thumbnail generation before painting visible items
- child-window-per-thumbnail UI
- unconditional aggressive prefetch
- hidden disk cache work in v1
- forcing NVIDIA-specific paths in mixed-hardware environments

## 14. Required Telemetry for Optimization

Every benchmark run should capture:
- OS version
- CPU model
- GPU model
- RAM size
- image format distribution
- folder size and count
- cold vs warm run state
- whether nvJPEG path was used
- whether RAW embedded preview or full RAW decode path was used
