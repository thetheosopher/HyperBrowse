# HyperBrowse — Direct2D Rendering Migration Status

This document records the phased migration from GDI to Direct2D (D2D) rendering, including the shipped per-monitor DPI behavior, GPU-accelerated image compositing, DirectWrite text rendering, and optional smooth interactions.

Created: 2026-04-13

---

## 1. Motivation

HyperBrowse is branded around speed. The current GDI rendering pipeline is functional but leaves significant performance and visual quality on the table:

| Concern | GDI Status | D2D Benefit |
|---------|-----------|-------------|
| Viewer zoom/pan on 30–50 MP images | Historical GDI `StretchBlt(HALFTONE)` path | Shipped D2D bitmap scaling with high-quality cubic mode where supported |
| Thumbnail grid scrolling | Historical GDI `AlphaBlend` path | Shipped D2D bitmap presentation for visible cells |
| Rotation | Historical `PlgBlt()` path | Shipped D2D matrix transform in the viewer |
| Anti-aliased geometry | GDI remains at shell/dialog boundaries | Shipped D2D geometry in BrowserPane and ViewerWindow |
| Text rendering | GDI remains at shell/dialog boundaries | Shipped DirectWrite text in BrowserPane and ViewerWindow |
| High-DPI support | Historical DPI virtualization | Shipped per-monitor DPI v2 and `WM_DPICHANGED` handling |
| Image scaling quality | Historical HALFTONE fallback | Shipped high-quality cubic interpolation with linear fallback |
| Compositing/overlays | GDI remains for selected shell paths | Shipped D2D compositing and overlays in browser/viewer surfaces |

---

## 2. Current State

- Direct2D/DirectWrite rendering is shipped in BrowserPane and ViewerWindow, including thumbnail cells, details presentation, full-image compositing, overlays, and transitions.
- `D2DRenderer` provides shared factory, render-target, bitmap-conversion, and text-format helpers; the owning windows retain their own device-dependent resources.
- MainWindow shell and dialog paths retain GDI rendering where that is still the simplest compatible Win32 path.
- Image decode and cache compatibility still use BGRA32/HBITMAP representations at the decode boundary; D2D bitmaps are created for presentation as needed.
- `d2d1` and `dwrite` are active rendering dependencies; the owning windows handle `D2DERR_RECREATE_TARGET` by rebuilding their device-dependent resources.
- Per-monitor DPI v2, `WM_DPICHANGED`, display-change recovery, and DPI-scaled layout/text paths are implemented.
- The original all-GDI baseline and the Phase 0 through Phase 2 migration work are historical context, not the current implementation state.

The remaining migration work is deliberately limited to evaluating additional shell surfaces and profiling any further GPU effects. It is not a prerequisite for the current browser/viewer rendering path.

---

## 3. Architecture Decisions

1. **No GDI/D2D abstraction layer.** BrowserPane and ViewerWindow use D2D directly through the shared `D2DRenderer` helpers; MainWindow keeps its intentional GDI shell boundary.

2. **Presentation resources follow their owners.** BrowserPane and ViewerWindow own device-dependent D2D bitmaps and brushes; the decode boundary may continue to use BGRA32/HBITMAP representations.

3. **Lazy render-target creation.** D2D render targets and dependent resources are created when an owning window first needs to paint, keeping startup independent of surface creation.

4. **Capability-driven rendering.** D2D creation failures and target loss are handled at the owning window boundary; the application does not maintain a separate WARP device abstraction.

5. **Animation philosophy.** Any future animation must be instantly interruptible by user input and must increase perceived speed, not diminish it. If profiling says otherwise, remove it.

6. **Shared factory helpers.** `D2DRenderer` lazily owns the shared `ID2D1Factory` and `IDWriteFactory`; each window creates and recovers its own `ID2D1HwndRenderTarget`.

---

## 4. Completed: Per-Monitor DPI Awareness

**Status:** Shipped at the window boundaries and in DPI-scaled layout/text paths. This work is independent of the D2D surface ownership.

**Shipped implementation:**
- `WinMain.cpp`, `MainWindow`, `BrowserPane`, and `ViewerWindow` establish per-monitor DPI v2 behavior and handle DPI changes.
- Layout metrics, fonts, thumbnail cells, and viewer overlays are DPI-scaled.

The 100% layout remains the compatibility baseline; higher DPI layouts use the same scaled ownership paths.

---

## 5. Completed: D2D Thumbnail Grid (BrowserPane)

**Status:** Shipped. BrowserPane owns the D2D thumbnail-grid and details presentation resources.

**Shipped implementation:**
- `src/render/D2DRenderer.h/.cpp` provides shared factory, render-target, bitmap-conversion, and text-format helpers.
- BrowserPane creates an `ID2D1HwndRenderTarget` and draws visible thumbnails, placeholders, details, geometry, and text through its D2D resources.
- Device-dependent resources are rebuilt after target loss.

**Performance invariants preserved:**
- Only visible cells painted (virtualization unchanged)
- Async decode pipeline untouched
- LRU cache eviction untouched (releases both HBITMAP and ID2D1Bitmap)

---

## 6. Completed: D2D Viewer Window

**Status:** Shipped. ViewerWindow owns the D2D full-image, overlay, metadata-panel, and transition presentation paths.

**Shipped implementation:**
- ViewerWindow creates its own `ID2D1HwndRenderTarget` for image, overlay, metadata-panel, and transition presentation.
- High-quality cubic interpolation is used when the runtime exposes a device context, with linear interpolation as the fallback.
- Rotation and transitions use D2D transforms; overlay and metadata-panel text use DirectWrite.

**Key benefit:** Smooth pan/zoom on 30–50 MP images because GPU handles all scaling.

---

## 7. Deferred: Additional Shell Surfaces

The hybrid split is intentional. MainWindow action-strip, folder-tree, status-bar, menu owner-draw, and native/custom dialog paths retain GDI because they are shell surfaces with established Win32 ownership. Replacing those surfaces with D2D is optional follow-up work, not an unfinished prerequisite for browser or viewer rendering.

**Current boundary:**
- MainWindow action strip buttons, folder tree, status bar, menu owner-draw, and native/custom dialog paths intentionally retain GDI.
- A D2D action strip or DirectWrite treatment for selected custom dialogs may be evaluated later, but is not required by the browser/viewer migration.

---

## 8. Deferred: Optional Smooth Interactions

**Goal:** Evaluate and optionally add GPU-powered interaction polish. Only ship what makes the app feel faster.

**Candidates for later evaluation:**
- Smooth inertial scrolling (vsync-aligned, decelerating) in thumbnail grid
- Animated zoom in viewer (smooth step rather than instant jump)
- Slideshow crossfade transitions
- Subtle selection/hover opacity transitions (< 150ms)

**Hard constraints:**
- Every animation must be instantly cancellable by user input
- No animation may delay the response to user input
- If profiling shows any animation hurts perceived latency, remove it
- No blur, acrylic, mica, or effects that scale cost with folder size

---

## 9. Risks and Mitigations

| Risk | Severity | Mitigation |
|------|----------|------------|
| Texture upload cost (HBITMAP → ID2D1Bitmap) | Medium | One-time per presentation resource. Thumbnails and viewer images are uploaded when their owning surface needs them. |
| VRAM pressure | Low | 500 visible thumbnails ≈ 128 MB VRAM. LRU eviction releases GPU textures. |
| D2D target loss | Low | Handle `D2DERR_RECREATE_TARGET`: recreate the owning target and all dependent resources. |
| Startup surface creation | Medium | Defer render-target creation until first paint. Don't block WinMain. |
| Complexity | Medium | Phased migration — one component at a time, each phase independently shippable. |
| Weak/missing GPU | Low | Keep the D2D path capability-driven and allow the owning window's existing recovery behavior to remain usable. |

---

## 10. Files Affected

| File | Status | Current responsibility |
|------|-------|--------|
| `CMakeLists.txt` | Shipped | Links D2D/DWrite/D3D dependencies and includes renderer sources |
| `src/app/WinMain.cpp` | Shipped | Establishes process DPI behavior |
| `src/render/D2DRenderer.h/.cpp` | Shipped | Shared factory, render-target, bitmap, and text helpers |
| `src/browser/BrowserPane.h/.cpp` | Shipped | D2D browser presentation and device-dependent resource recovery |
| `src/viewer/ViewerWindow.h/.cpp` | Shipped | D2D image, transform, overlay, metadata, and transition presentation |
| `src/ui/MainWindow.cpp` | Intentional GDI | Shell orchestration and compatible shell/dialog painting |

---

## 11. Verification

1. Build and pass HyperBrowseSmoke after rendering changes
2. Verify browser and viewer D2D surfaces at 100%, 150%, and 200% DPI scaling
3. Profile scroll FPS in a 5,000+ image folder
4. Profile viewer zoom/pan latency on a 50 MP image
5. Monitor resource pressure during large-folder browsing
6. Exercise D2D target/device-loss recovery and confirm resource recreation
7. Test on integrated GPU and confirm the D2D capability/recovery path remains usable
