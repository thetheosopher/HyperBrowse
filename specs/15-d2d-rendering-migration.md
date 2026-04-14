# HyperBrowse — Direct2D Rendering Migration Plan

This document describes the phased migration from GDI to Direct2D (D2D) rendering, including per-monitor DPI awareness, GPU-accelerated image compositing, DirectWrite text rendering, and optional smooth interactions.

Created: 2026-04-13

---

## 1. Motivation

HyperBrowse is branded around speed. The current GDI rendering pipeline is functional but leaves significant performance and visual quality on the table:

| Concern | GDI Status | D2D Benefit |
|---------|-----------|-------------|
| Viewer zoom/pan on 30–50 MP images | CPU `StretchBlt(HALFTONE)` — slow on large images | GPU bicubic scaling — order of magnitude faster |
| Thumbnail grid scrolling | CPU `AlphaBlend` per visible cell | GPU texture blit — essentially zero CPU cost per thumbnail |
| Rotation | `PlgBlt()` — expensive CPU transform | GPU matrix multiply during single DrawBitmap call |
| Anti-aliased geometry | GDI `RoundRect` — aliased (jaggy) edges | D2D — hardware anti-aliased rounded rectangles natively |
| Text rendering | GDI `DrawText` — decent ClearType | DirectWrite — superior sub-pixel positioning, sharper at small sizes |
| High-DPI support | **None** — app runs in DPI virtualization (blurry on modern displays) | D2D uses device-independent pixels natively |
| Image scaling quality | HALFTONE (adequate) | GPU high-quality cubic (faster AND better) |
| Compositing/overlays | CPU alpha blending | GPU compositing — zero CPU cost |

---

## 2. Current State

- All rendering: GDI (`StretchBlt`, `PlgBlt`, `AlphaBlend`, `FillRect`, `RoundRect`, `DrawText`)
- Double-buffered: `CreateCompatibleDC`/`CreateCompatibleBitmap` + `BitBlt` in both BrowserPane and ViewerWindow
- Image format: HBITMAP (DIB section, BGRA32) throughout
- `d2d1` and `d3d11` already linked in CMakeLists.txt but unused
- No DPI awareness manifest, no `WM_DPICHANGED`, no DPI scaling
- No animation or transitions anywhere (by design: "prefer low latency over animation")
- Theme system: `ThemePalette` struct with light/dark color sets, applied via GDI brushes/pens

---

## 3. Architecture Decisions

1. **No GDI/D2D abstraction layer.** Migrate directly. D2D is available on Windows 7 SP2+ with Platform Update. All target systems support it.

2. **Texture cache mirrors thumbnail cache.** When an HBITMAP enters the thumbnail LRU cache, the D2D bitmap is created at the same time. Both evicted together on LRU eviction.

3. **Deferred device creation.** The D3D11/D2D device is not created until first WM_PAINT. Startup time stays instant.

4. **WARP fallback is automatic.** D2D falls back to WARP (software rasterizer) if no GPU is available. Still benefits from anti-aliased geometry and DirectWrite.

5. **Animation philosophy.** Any future animation must be instantly interruptible by user input and must increase perceived speed, not diminish it. If profiling says otherwise, remove it.

6. **Shared device factory.** A single `ID2D1Factory` and `IDWriteFactory` are created in `Application::Run()` and passed to windows that need them. Each window creates its own `ID2D1HwndRenderTarget`.

---

## 4. Phase 0 — Per-Monitor DPI Awareness

**Goal:** Fix blurry rendering on high-DPI displays. Independent of D2D.

**Changes:**
- `WinMain.cpp`: Call `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` before window creation
- `MainWindow`: Handle `WM_DPICHANGED`, scale layout constants and fonts by DPI
- `BrowserPane`: Scale thumbnail layout metrics, fonts, and cell sizes by DPI
- `ViewerWindow`: Scale overlay layout by DPI
- All `CreateFont` / `CreateSizedUiFont` calls: use DPI-scaled point sizes

**Constraint:** No visible regression at 100% DPI. At 125%/150%/200%, must be pixel-crisp.

---

## 5. Phase 1 — D2D Thumbnail Grid (BrowserPane)

**Goal:** GPU-accelerated thumbnail compositing with anti-aliased geometry and DirectWrite text.

**New files:**
- `src/render/D2DRenderer.h` — Shared D2D/DirectWrite factory management, render target creation, HBITMAP→ID2D1Bitmap conversion
- `src/render/D2DRenderer.cpp` — Implementation

**BrowserPane changes:**
- Replace `WM_PAINT` handler: create `ID2D1HwndRenderTarget`, render directly (D2D is inherently double-buffered via present)
- Replace `AlphaBlend` thumbnail draws → `ID2D1RenderTarget::DrawBitmap()`
- Replace `RoundRect` → `ID2D1RenderTarget::FillRoundedRectangle()` + `DrawRoundedRectangle()` (anti-aliased)
- Replace `DrawText` → `IDWriteFactory::CreateTextFormat()` + `ID2D1RenderTarget::DrawText()`
- Replace GDI brush/pen resource management → D2D `ID2D1SolidColorBrush` (lightweight, no kernel objects)
- Thumbnail HBITMAP→ID2D1Bitmap conversion at cache insertion time

**Performance invariants preserved:**
- Only visible cells painted (virtualization unchanged)
- Async decode pipeline untouched
- LRU cache eviction untouched (releases both HBITMAP and ID2D1Bitmap)

---

## 6. Phase 2 — D2D Viewer Window

**Goal:** GPU-accelerated image scaling, rotation, and overlay compositing.

**ViewerWindow changes:**
- Replace `WM_PAINT` handler: create `ID2D1HwndRenderTarget`, render directly
- Replace `StretchBlt(HALFTONE)` → `DrawBitmap()` with `D2D1_BITMAP_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC`
- Replace `PlgBlt()` rotation → `SetTransform()` with rotation matrix + `DrawBitmap()`
- Replace info overlay panels: D2D rounded rectangles + DirectWrite text
- Upload decoded full-resolution image as `ID2D1Bitmap` once on decode completion

**Key benefit:** Smooth pan/zoom on 30–50 MP images because GPU handles all scaling.

---

## 7. Phase 3 — DirectWrite Text and Polished Geometry

**Goal:** Replace all remaining GDI text and geometry with D2D/DirectWrite equivalents.

**Changes:**
- MainWindow action strip buttons: D2D rounded rectangles + DirectWrite text (if action strip gets a D2D render target, otherwise keep GDI for the thin strip)
- Placeholder states: D2D rendering with anti-aliased panels
- All `CreateFont`/`CreateSizedUiFont` calls → `IDWriteTextFormat` objects
- Theme resource management simplified: D2D brushes are lightweight (no `DeleteObject` lifecycle)

---

## 8. Phase 4 — Optional Smooth Interactions

**Goal:** Evaluate and optionally add GPU-powered interaction polish. Only ship what makes the app feel faster.

**Candidates (evaluate post-Phase 2):**
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
| Texture upload cost (HBITMAP → ID2D1Bitmap) | Medium | One-time per cache entry. Thumbnails ~256KB each. Viewer images uploaded once. |
| VRAM pressure | Low | 500 visible thumbnails ≈ 128 MB VRAM. LRU eviction releases GPU textures. |
| D3D device loss | Low | Handle `D2DERR_RECREATE_TARGET`: recreate device + all resources. Rare in practice. |
| Startup time (D3D11 device creation ~50–100 ms) | Medium | Defer to first paint. Don't block WinMain. |
| Complexity | Medium | Phased migration — one component at a time, each phase independently shippable. |
| Weak/missing GPU | Low | D2D auto-falls back to WARP. Still benefits from anti-aliased geometry and DirectWrite. |

---

## 10. Files Affected

| File | Phase | Change |
|------|-------|--------|
| `CMakeLists.txt` | 1 | Add `dwrite` link, add new source files |
| `src/app/WinMain.cpp` | 0 | DPI awareness context |
| `src/app/Application.cpp` | 1 | Create shared D2D/DWrite factories |
| `src/render/D2DRenderer.h` | 1 | New: factory management, render target creation, bitmap conversion |
| `src/render/D2DRenderer.cpp` | 1 | New: implementation |
| `src/browser/BrowserPane.h` | 1 | D2D render target, brush, text format members |
| `src/browser/BrowserPane.cpp` | 1 | Replace all GDI paint code with D2D equivalents |
| `src/viewer/ViewerWindow.h` | 2 | D2D render target, brush members |
| `src/viewer/ViewerWindow.cpp` | 2 | Replace all GDI paint code with D2D equivalents |
| `src/ui/MainWindow.cpp` | 3 | Optional: D2D action strip buttons |
| `src/cache/ThumbnailCache.h` | 1 | No change (HBITMAP retained for decode compatibility) |

---

## 11. Verification

1. Build and pass HyperBrowseSmoke after each phase
2. Visual comparison screenshots at 100%, 150%, and 200% DPI scaling
3. Profile scroll FPS in 5,000+ image folder (before/after)
4. Profile viewer zoom/pan latency on 50 MP image (before/after)
5. VRAM monitoring during large-folder browsing
6. Test device-loss recovery
7. Test on integrated GPU to ensure WARP fallback works
