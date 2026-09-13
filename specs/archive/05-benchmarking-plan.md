# Benchmarking Plan

## 1. Purpose

Benchmarking is mandatory from the beginning of the project. The application is performance-sensitive by design, and architecture decisions must be validated with repeatable measurements.

## 2. Benchmark Categories

### Browser benchmarks
- cold startup
- warm startup
- cold folder load
- warm folder load
- first visible thumbnail latency
- thumbnail throughput
- scroll smoothness in large folders
- selection responsiveness under load

### Viewer benchmarks
- viewer window open latency
- full-image decode latency
- next/previous navigation latency
- prefetch effectiveness
- zoom/pan responsiveness

### Format-specific benchmarks
- JPEG WIC path vs nvJPEG path
- PNG thumbnail decode throughput
- TIFF first-page thumbnail throughput
- NEF embedded-preview thumbnail path
- NEF full RAW decode path

### Resource benchmarks
- peak memory usage
- steady-state memory usage while browsing
- CPU utilization
- GPU utilization
- cache hit rate

## 3. Benchmark Datasets

Prepare repeatable datasets with known characteristics.

### Dataset A: Small mixed folder
- 100-250 files
- mixed JPEG/PNG/GIF/TIFF

### Dataset B: Large JPEG-heavy folder
- 2,000-10,000 files
- mostly JPEG
- intended for nvJPEG throughput analysis

### Dataset C: Mixed-format production folder
- thousands of files
- realistic mix of JPEG/PNG/TIFF/GIF/NEF

### Dataset D: RAW-focused Nikon folder
- NEF/NRW samples from different cameras and sizes
- include files with embedded previews
- include files that require full RAW decode
- include known edge cases / partially supported variants

### Dataset E: Large-resolution stress folder
- very high-resolution images
- mixed landscape/portrait
- memory pressure scenarios

## 4. Test Matrix

Each benchmark run should record:
- Windows version
- CPU model
- GPU model
- whether NVIDIA GPU is present
- whether nvJPEG acceleration is enabled
- RAM size
- build configuration
- cold vs warm state
- recursive browsing on/off
- thumbnail size setting
- browser mode (thumbnail/details)

## 5. Metrics

### Primary metrics
- time to first usable UI
- time to first placeholder paint
- time to first visible thumbnail
- thumbnails/sec during initial fill
- scroll hitch count / dropped frames proxy
- viewer open latency
- time to current image visible
- next/previous latency

### Secondary metrics
- metadata extraction throughput
- cache hit ratio
- prefetch hit ratio
- memory high-water mark
- CPU time by pipeline stage
- GPU utilization by path

## 6. Benchmark Methodology

### Cold run
- clean process start
- clear in-memory caches
- reboot or standardized cleanup step when needed for lower-level tests

### Warm run
- second run against same folder without app restart where appropriate
- useful for cache effectiveness evaluation

### Repetition
- minimum 5 runs per benchmark scenario
- summarize median, p95, and min/max where useful

## 7. Instrumentation Requirements

Add structured timing around:
- startup
- folder enumeration
- metadata extraction
- thumbnail scheduling
- thumbnail queue wait
- decode time
- scale time
- render/upload time
- viewer decode
- prefetch decode
- batch convert operations

## 8. Example Benchmark Cases

### Case 1: Folder switch latency
1. launch app
2. open large folder
3. measure:
   - UI responsiveness
   - first placeholder paint
   - first visible thumbnail
   - time to 50 visible thumbnails

### Case 2: Fast scroll stress
1. open large folder in thumbnail mode
2. scroll rapidly across several screens
3. measure:
   - hitch count
   - stale work cancellation effectiveness
   - thumbnail miss/hit behavior

### Case 3: Viewer open test
1. open image from browser
2. measure:
   - time to viewer window shown
   - time to actual image displayed

### Case 4: Viewer navigation test
1. open image
2. navigate across 50 adjacent images
3. measure:
   - average next/previous latency
   - prefetch hit rate

### Case 5: JPEG backend comparison
1. run JPEG-heavy folder with WIC-only mode
2. run same folder with nvJPEG mode enabled
3. compare:
   - first visible thumbnails
   - total throughput
   - CPU/GPU utilization
   - memory impact

### Case 6: RAW thumbnail strategy comparison
1. run RAW-heavy folder using embedded-preview-first path
2. compare with full RAW decode fallback cases
3. measure:
   - time to first RAW thumbnail
   - overall folder responsiveness

## 9. Acceptance Thresholds (Draft)

These thresholds should be refined once real datasets exist.

### Browser
- no UI freeze on large-folder switch
- visible placeholders should paint immediately
- visible thumbnails should begin appearing quickly
- scrolling should remain subjectively smooth

### Viewer
- viewer window should appear immediately on open
- current image should display quickly
- next/previous should feel near-instant when prefetched

## 10. Reporting Format

Each benchmark report should include:
- test environment
- dataset description
- configuration flags
- summarized metrics
- variance notes
- anomalies / outliers
- conclusion and recommended action

## 11. Tooling Recommendations

Use a combination of:
- built-in app instrumentation logs
- ETW events
- Windows Performance Analyzer
- GPU profiling tools when applicable
- memory diagnostic snapshots

## 12. Benchmark Exit Criteria for v1

Before v1 lock:
- all primary benchmark categories must run automatically or semi-automatically
- JPEG acceleration decision must be backed by measured results
- RAW path behavior must be measured on representative Nikon datasets
- no known catastrophic regressions in large-folder scenarios
