# Issues To Resolve

This document captures the bugs and code quality issues found during the code review performed on 2026-04-12.

## Resolution Order

1. Fix correctness bugs that can write the wrong result or destabilize background work.
2. Fix user-visible responsiveness problems.
3. Fix consistency, maintainability, and rendering-efficiency issues.

## Priority Definitions

- `P0`: correctness or stability issue that should be fixed before further feature work.
- `P1`: user-visible responsiveness, behavior, or architectural risk.
- `P2`: maintainability or performance cleanup with lower immediate risk.

## 1. Negative JPEG Rotation Uses The Wrong Direction

- Type: bug
- Priority: `P0`
- Status: Resolved 2026-04-12
- Area: `src/services/JpegTransformService.cpp`

### Problem

The EXIF orientation update normalizes `quarterTurnsDelta` into a positive step count, but still branches on the original sign inside the loop. For `quarterTurnsDelta = -1`, the code performs three left-orientation remaps instead of one.

### Impact

- Rotate-left requests can write the wrong orientation value.
- Some values such as `-3` are also mapped incorrectly.

### Recommended Resolution

- Normalize the rotation into a canonical step count and apply a single consistent transform direction.
- Add unit coverage for `-3`, `-1`, `+1`, and `+3` quarter turns.

## 2. JPEG "Lossless Rotate" Only Rewrites EXIF Orientation

- Type: bug
- Priority: `P0`
- Status: Resolved 2026-04-12
- Area: `src/services/JpegTransformService.cpp`

### Problem

The current implementation edits the EXIF orientation tag but does not perform an actual JPEG block transform. That makes the command viewer-dependent rather than a true lossless file rotation.

### Impact

- Applications that ignore EXIF orientation will still show the old pixel orientation.
- The feature does not match the expected behavior of a real lossless JPEG rotate command.

### Recommended Resolution

- Replace the metadata-only approach with a real lossless JPEG transform pipeline, or rename the feature if metadata-only rotation is intentionally accepted.
- If a real transform is implemented, reset EXIF orientation back to the canonical upright value.

### Resolution Note

- The current fix path keeps the metadata-only implementation and renames the command and service surface to orientation adjustment so the UI and code match the real behavior.

## 3. `DecodeWicSource` Misses COM Cleanup On Some Early Returns

- Type: bug
- Priority: `P0`
- Status: Resolved 2026-04-12
- Area: `src/decode/ImageDecoder.cpp`

### Problem

`DecodeWicSource` receives `shouldUninitializeCom`, but several early returns exit before calling `CoUninitialize()`. The first frame-read and size-query failure paths are affected.

### Impact

- Failed decode attempts can leak per-thread COM initialization state.
- Error-heavy decode paths become harder to reason about and maintain.

### Recommended Resolution

- Replace manual cleanup branching with a small RAII COM scope helper.
- Ensure every decode path has a single exit strategy for cleanup-sensitive resources.

## 4. `FolderWatchService::Stop()` Has A Handle Lifetime Race

- Type: bug
- Priority: `P0`
- Status: Resolved 2026-04-12
- Area: `src/services/FolderWatchService.cpp`

### Problem

`Stop()` copies `directoryHandle` and `stopEvent` under the mutex, releases the mutex, and then uses those handles. The worker thread can close the same handles in `CloseHandles()` between those two steps.

### Impact

- `SetEvent()` and `CancelIoEx()` can run against handles that were already closed.
- Folder watch shutdown behavior becomes timing-sensitive.

### Recommended Resolution

- Keep ownership and signaling rules explicit so only one thread is responsible for closing the watcher handles.
- Avoid using copied handle values after releasing the lock, or switch to a dedicated shutdown protocol that does not race handle destruction.

## 5. Batch Convert Cancellation Can Freeze The UI Thread

- Type: bug
- Priority: `P1`
- Status: Resolved 2026-04-12
- Area: `src/services/BatchConvertService.cpp`

### Problem

`BatchConvertService::Cancel()` synchronously joins the worker thread. If the worker is in the middle of a slow RAW decode or encode, the menu command blocks the UI thread until the current file finishes.

### Impact

- Cancel feels unresponsive.
- The application can appear hung during batch conversion of large files.

### Recommended Resolution

- Make cancellation asynchronous from the UI point of view.
- Check for cancellation at more points inside the conversion pipeline so the worker can exit promptly.

## 6. Thumbnail Cache Path Semantics Are Inconsistent

- Type: code quality
- Priority: `P1`
- Status: Resolved 2026-04-12
- Area: `src/cache/ThumbnailCache.h`, `src/cache/ThumbnailCache.cpp`

### Problem

Thumbnail cache keys compare raw `filePath` strings, while other path-sensitive code in the project normalizes case and slash style before comparing paths. Cache invalidation already performs its own normalization pass, which is a sign that the key semantics are not aligned with the rest of the model.

### Impact

- Cache behavior depends on callers supplying identical path spellings.
- Path handling rules are duplicated instead of enforced in one place.

### Recommended Resolution

- Normalize paths when creating cache keys, or define a canonical path type used by the browser model, cache, and watcher services.

## 7. Detached Background Threads Make Shutdown Non-Deterministic

- Type: code quality
- Priority: `P1`
- Status: Resolved 2026-04-12
- Areas: `src/services/FolderEnumerationService.cpp`, `src/viewer/ViewerWindow.cpp`

### Problem

Folder enumeration and viewer decode/prefetch work use detached `std::thread` instances instead of owned worker lifetimes.

### Impact

- Shutdown and object lifetime are coordinated through shared flags rather than explicit joins.
- Future changes in these areas will be harder to reason about and test.

### Recommended Resolution

- Replace detached threads with owned worker objects, task queues, or `std::jthread`-style cancellation-aware lifetimes.

### Resolution Note

- The current fix replaces detached work with owned asynchronous task futures that are reaped during normal operation and awaited during teardown.

## 8. Decode And Metadata Helpers Are Repeated Across Files

- Type: code quality
- Priority: `P2`
- Status: Resolved 2026-04-12
- Areas: `src/decode/ImageDecoder.cpp`, `src/decode/WicThumbnailDecoder.cpp`, `src/cache/ThumbnailCache.cpp`, `src/services/ImageMetadataService.cpp`

### Problem

Orientation parsing, bitmap allocation, scale calculations, hash-combine helpers, and path normalization logic are duplicated across several translation units.

### Impact

- Fixes and behavior changes are easy to apply in one place and miss in another.
- Review and maintenance cost is higher than necessary.

### Recommended Resolution

- Extract shared helper utilities into focused internal headers or implementation units.
- Keep decode-specific helpers in one place so WIC thumbnail decode and full-image decode cannot drift.

### Resolution Note

- Shared path normalization, hash-combine, and WIC decode helpers now live in common headers so cache, metadata, and decode paths share the same behavior.

## 9. Thumbnail Painting Does Avoidable GDI Allocation Work

- Type: code quality
- Priority: `P2`
- Status: Resolved 2026-04-12
- Area: `src/browser/BrowserPane.cpp`

### Problem

The thumbnail paint path creates and destroys brushes and pens per cell during `WM_PAINT`.

### Impact

- Rendering does extra GDI allocation work for every visible thumbnail.
- This increases paint overhead in the exact view that is supposed to remain smooth at scale.

### Recommended Resolution

- Reuse a small set of themed brushes and pens across paint calls.
- Keep the paint path allocation-free where practical.

### Resolution Note

- BrowserPane now owns themed brushes and pens across paint calls, and the selection-restore warning cleanup landed with the same pass.