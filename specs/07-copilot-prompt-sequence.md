# Copilot Prompt Sequence

Use these prompts sequentially. Keep each prompt focused and ask Copilot or the selected model to modify only the files relevant to the current step.

## General Instructions for Every Prompt

Append this guidance to each prompt when useful:

- Use modern C++20.
- Target a pure native Win32 desktop app.
- Avoid MFC, Qt, WinUI, .NET, and web UI layers.
- Keep dependencies minimal.
- Do not block the UI thread.
- Favor explicit architecture over hidden framework magic.
- Optimize for low-latency browsing of folders containing thousands of images.
- Use WIC for common formats, LibRaw for supported mainstream RAW formats, and optional nvJPEG for JPEG thumbnail acceleration.
- Preserve existing project structure and naming unless the prompt explicitly asks to change it.
- Add logging/timing hooks around performance-critical operations.

---

## Prompt 1 - Create the repo skeleton

Create the initial project skeleton for a native Win32 C++20 application named HyperBrowse.

Requirements:
- Use CMake.
- Create a clean source tree with folders for app, ui, browser, viewer, decode, render, services, cache, platform, and util.
- Add a minimal WinMain, main window class, menu scaffolding, and status bar placeholder.
- Add basic logging and timing utility scaffolding.
- Keep the project buildable in Debug and Release on Windows 10/11 x64.
- Do not add any heavyweight frameworks.

Deliverables:
- CMakeLists.txt
- source tree skeleton
- minimal app that shows a main window

---

## Prompt 2 - Implement the main shell layout

Implement the main application shell layout.

Requirements:
- Left folder tree pane
- Resizable vertical splitter
- Right browser pane placeholder
- Bottom status bar
- Minimal menu bar
- Persist splitter position between runs
- Add theme plumbing for light and dark mode, but do not fully polish it yet

Do not implement actual image decoding yet.
Focus on layout, command routing, and window management.

---

## Prompt 3 - Build the browser model and async folder enumeration

Implement the browser model and async folder enumeration pipeline.

Requirements:
- Selecting a folder populates the browser model asynchronously.
- Support recursive browsing as a toggle, default off.
- Filter to supported formats: jpg, jpeg, png, gif, tif, tiff, arw, cr2, cr3, dng, nef, nrw, raf, rw2.
- Collect filename, path, size, modified date, and placeholder dimensions fields.
- Update status bar incrementally with folder image count and total size.
- Do not block the UI thread.
- Add cancellation when switching folders rapidly.

---

## Prompt 4 - Implement virtualized thumbnail mode and details mode

Implement the browser pane with two virtualized modes.

Requirements:
- Thumbnail mode: virtualized grid with placeholders
- Details mode: virtualized list with sortable columns
- Support single select, multiselect, Ctrl-toggle, Shift-range, rubber-band selection in thumbnail mode
- Sorting options: filename, modified date, file size, dimensions, type, random
- Keep scrolling smooth for folders containing thousands of items
- Do not yet decode real thumbnails

---

## Prompt 5 - Add WIC decode backend and thumbnail scheduler

Implement the baseline decode path and thumbnail scheduler.

Requirements:
- Add a WIC decoder backend for JPEG, PNG, GIF, and TIFF.
- Implement async thumbnail decode jobs.
- Prioritize visible items over offscreen items.
- Use placeholders until thumbnails arrive.
- Add a bounded in-memory thumbnail cache.
- Add cancellation for scrolled-away or stale requests.
- Apply EXIF orientation automatically for JPEGs.
- Use first-frame thumbnail for GIF and first-page thumbnail for TIFF.

Deliverable goal:
- real thumbnails appear in the browser without blocking the UI thread.

---

## Prompt 6 - Build the separate viewer window

Implement the separate viewer window.

Requirements:
- Double-click from browser opens selected image in a separate viewer window.
- Support next/previous navigation within current folder scope.
- Support zoom in/out, fit to window, 100%, pan, rotate, and full-screen.
- Double-click in viewer toggles full screen.
- Prefer low latency over animated transitions.
- Reset zoom/pan when changing images.
- Surface current zoom level through a model/event that the status bar can consume.

---

## Prompt 7 - Add viewer prefetch and memory caches

Add prefetch and viewer-side memory caching.

Requirements:
- Prefetch next and previous images in background.
- Keep caching in memory only; do not add disk cache.
- Implement bounded caches for current/next/previous images.
- Cancel stale prefetch if navigation jumps.
- Keep current image protected from premature eviction.

Include instrumentation so prefetch hit rate can be measured later.

---

## Prompt 8 - Add LibRaw backend for mainstream RAW

Implement mainstream RAW support using LibRaw.

Requirements:
- Add a LibRaw decoder backend for ARW, CR2, CR3, DNG, NEF, NRW, RAF, and RW2.
- Extract metadata and embedded previews where possible.
- Use embedded preview as the preferred thumbnail path.
- Add full RAW decode path for viewer when supported.
- If a RAW variant is unsupported, fail gracefully and show a clear unsupported state.
- Integrate RAW items into browser and viewer without special-case UI branching.

---

## Prompt 9 - Add metadata support (EXIF, IPTC, XMP)

Implement metadata extraction and presentation.

Requirements:
- Support EXIF, IPTC, and XMP where available.
- Expose core metadata in a structured metadata model.
- Populate details mode columns where practical.
- Add an Image Information dialog or side panel.
- Ensure metadata failures never block browsing.

---

## Prompt 10 - Add folder watching and live refresh

Implement folder watching.

Requirements:
- Detect add/remove/update/rename events in the current folder.
- Patch the browser model incrementally.
- Invalidate relevant thumbnail and metadata cache entries.
- Preserve selection where practical.
- Avoid full folder reload unless necessary.

---

## Prompt 11 - Add slideshow, lossless JPEG rotate, and batch convert

Implement the remaining v1 must-have features.

Requirements:
- Slideshow from current selection or current folder scope
- Lossless JPEG rotate commands
- Batch convert for selected files or current folder images
- Supported output formats initially: JPEG, PNG, TIFF
- Batch convert must be async and cancellable
- Keep the app positioned as a pure viewer/browser, not an editor

---

## Prompt 12 - Add optional nvJPEG acceleration path

Integrate an optional NVIDIA acceleration path.

Requirements:
- Add runtime detection for NVIDIA/nvJPEG availability.
- Use nvJPEG for batched JPEG thumbnail generation when supported.
- Keep WIC as a complete fallback path.
- Add a setting to enable or disable NVIDIA JPEG acceleration.
- Instrument both paths so performance can be compared.
- Do not make app correctness depend on NVIDIA hardware.

---

## Prompt 13 - Polish themes, menus, shortcuts, and context menus

Polish the UI for v1.

Requirements:
- Finalize dark and light theme support.
- Add concise menus and keyboard shortcuts.
- Add context menus for browser items where appropriate.
- Keep the interface minimal and efficient.
- Avoid bloated toolbars.

---

## Prompt 14 - Add benchmark harness and diagnostics outputs

Create a benchmark and diagnostics layer.

Requirements:
- Add instrumentation for startup, folder enumeration, thumbnail decode, viewer open, next/previous navigation, and prefetch hit rate.
- Add benchmark modes or hidden diagnostics commands that can be used repeatedly.
- Make it easy to compare WIC-only and nvJPEG-enabled runs.
- Make it easy to compare RAW embedded-preview and full RAW decode paths.

---

## Prompt 15 - Package for portable and installer distribution

Finalize packaging.

Requirements:
- Produce a portable build layout.
- Produce an installer-friendly build layout.
- Keep runtime dependencies explicit and minimal.
- Add version/resource metadata.
- Write a short README for running the portable build.

---

## Prompt 16 - Hardening pass

Perform a hardening pass.

Requirements:
- Review for UI-thread blocking calls.
- Review cancellation correctness.
- Review cache eviction correctness.
- Review RAW failure handling.
- Review large-folder behavior.
- Review memory spikes during scrolling.
- Add targeted tests for core non-UI services.

Provide a list of risky areas and concrete fixes.
