# HyperBrowse Enhancement Plan

Last reviewed: 2026-05-16

This document tracks the **forward-looking** HyperBrowse backlog. Completed
items have been archived (see [Appendix A](#appendix-a--recently-completed-archive))
so the active plan stays focused.

The plan is organized around a single product north star:

> **HyperBrowse is the fastest practical Windows image browser/viewer.**
> Every accepted item must either (a) extend competitive workflow depth without
> diluting that brand, or (b) measurably improve perceived speed, throughput,
> or resource efficiency on real hardware.

---

## 1. Guiding Principles

1. **Performance is the brand.** Cold-start latency, first-visible-thumbnail
   latency, scroll smoothness, and viewer-open latency are budgeted and
   defended in CI (see Theme D).
2. **Browser/viewer first.** No editor surface, no organizer/database
   lock-in, no plugin ecosystem.
3. **Adapt to the host.** Thread pools, cache budgets, prefetch depth, and
   GPU paths all auto-scale to the machine they run on, with explicit user
   override available.
4. **Native and lean.** Win32 + D2D + DirectWrite + WIC + LibRaw + optional
   nvJPEG. New dependencies require justification and an opt-out.
5. **No regressions.** Every shipped feature has a measurable hold-the-line
   target captured in the benchmark suite.

---

## 2. Current Capability Snapshot

The active plan assumes the following are already shipped and stable:

- D2D/DirectWrite rendering in the browser grid and viewer; per-monitor DPI v2.
- Async folder enumeration, folder tree, metadata, watching, thumbnail
  scheduling, and batch convert.
- Runtime-adaptive thumbnail cache (128 MB–1 GB) and metadata cache (2,048–
  65,536 entries) sized from `GlobalMemoryStatusEx`.
- Optional `%LOCALAPPDATA%\HyperBrowse\thumbnail-cache` persistent cache.
- Optional nvJPEG acceleration and optional out-of-process LibRaw helper.
- File management: copy, move, rename, batch rename, delete, permanent
  delete, reveal, copy path, properties, recent destinations, pinned
  favorites, RAW+JPEG paired operations.
- Compare/cull lite (two-up viewer), ratings/tags, filter-box (including
  `rating:>=N` / `tag:*`), date-taken sort, sort direction toggle,
  configurable viewer mouse wheel, slideshow with transition styles.
- Diagnostics window, structured log, smoke + integration tests, GitHub
  Actions CI, portable zip + Inno Setup 6 installer.

See [01-product-spec.md](01-product-spec.md), [03-performance-strategy.md](03-performance-strategy.md),
and [15-d2d-rendering-migration.md](15-d2d-rendering-migration.md) for design
detail.

---

## 3. Theme A — Performance & Adaptive Resource Controls

The single most differentiating area HyperBrowse can press on right now.
Competitors expose almost no resource controls. HyperBrowse should ship a
small, opinionated set that is **safe by default** and **explicit when
asked**.

### `A1` Adaptive Resource Profile (P0)

**Goal:** A first-class, user-visible resource profile that drives every
cache budget, worker count, and prefetch depth from a single decision.

**Implementation status:** In progress. `2026-05-16` shipped the first code
slice: a persisted `Conservative` / `Balanced` / `Performance` profile is now
wired into automatic thumbnail-cache sizing, metadata-cache sizing, thumbnail
worker counts, and metadata worker counts, with a menu surface under
**Help ▸ Performance Profile**. Remaining for full completion: a dedicated
Settings dialog, live cache-budget controls, `Custom` overrides, and viewer
prefetch-radius integration.

- Introduce a `ResourceProfile` enum: `Conservative`, `Balanced` (default),
  `Performance`, `Custom`.
- Profile selection lives in a new **Help ▸ Settings… ▸ Performance** tab
  (also a Tools menu shortcut). Persisted under `Software\HyperBrowse` as
  `ResourceProfile` plus per-knob overrides for `Custom`.
- Each profile maps to concrete multipliers fed into the existing
  `ResolveThumbnailCacheCapacityBytes`, `ResolveMetadataCacheCapacityEntries`,
  and worker-count helpers. Example mapping (subject to bench tuning):

  | Profile | Thumb cache cap | Metadata cap | Prefetch radius | Workers |
  | --- | --- | --- | --- | --- |
  | Conservative | min(totalRam/16, 256 MB) | 4,096 | 1 | hc/4 |
  | Balanced (default) | min(totalRam/8, 1 GB) | 16,384 | 3 | hc, raw=hc/4 |
  | Performance | min(totalRam/4, 4 GB*) | 65,536 | 8 | hc, raw=hc/2 |
  | Custom | user-set | user-set | user-set | user-set |

  *Performance is hard-capped at `availablePhysicalBytes / 3` regardless of
  user input to keep the OS healthy.

- Implementation surface: thread the resolved profile through
  `ThumbnailScheduler`, `ImageMetadataService`, `ViewerWindow` prefetch,
  and the folder warm-up window (A7).

### `A2` Cache Sizing UI With Live Feedback (P0)

**Goal:** A compact controller users can trust, with no math.

- Sliders in the Performance tab show:
  - Thumbnail cache budget (MB), bounded by detected RAM, with live readouts
    of current bytes-in-use vs cap and current hit rate.
  - Metadata cache entry budget, with live entry count and hit rate.
  - Persistent thumbnail cache budget (MB) and on-disk size today, with a
    **Trim Now** button.
- Each slider shows a recommended range derived from `QueryMemorySnapshot()`
  plus the active `ResourceProfile`.
- Persisted as `ThumbnailCacheCapBytesOverride`,
  `MetadataCacheCapEntriesOverride`, `PersistentThumbnailCacheCapBytes`.
- Values are honored at next service construction; apply-without-restart is
  acceptable but not required for v1 of the UI.

### `A3` Persistent Thumbnail Cache Maturity (P0)

The first persistence pass shipped; now harden it.

- Sharded directory layout (`xx/yy/<hash>.bin`) to keep per-directory entry
  counts low on huge libraries.
- LRU eviction by size, driven by `PersistentThumbnailCacheCapBytes` (see
  A2), default `min(totalRam/2, 8 GB)` capped by free-disk headroom on the
  cache volume.
- Background compaction pass on idle: deduplicate, drop entries for missing
  files, repack shards.
- Move I/O off the scheduler worker threads onto a dedicated low-priority
  cache thread so decode workers never block on disk writes.
- Surfaces from the **Cache Inspector** dialog (see D4) for per-shard stats
  and one-click purge.

### `A4` Memory-Pressure Response & Adaptive Prefetch (P1)

- Sample `GlobalMemoryStatusEx` on a low-frequency timer (1–2 Hz) on a
  background thread, never the UI thread.
- When `dwMemoryLoad >= 85` or `availablePhysicalBytes < 1 GB`:
  - Halve viewer prefetch radius.
  - Cap thumbnail decode concurrency to `max(1, workers/2)`.
  - Trigger eviction toward `cacheCapBytes / 2` on the thumbnail cache.
- Recovery hysteresis: only restore when load drops below 70 % for two
  consecutive samples.
- Expose current state in the Performance HUD (C3).

### `A5` Decode/Scale Buffer Pools (P1)

- Add a small `ScratchBufferPool` (per-size class, max N buffers) consumed
  by WIC scale, nvJPEG output, and LibRaw embedded-preview decode paths.
- Initial size classes keyed off active thumbnail size preset.
- Justified only if benchmarks (D2) show meaningful allocation pressure on
  the decode/scale hot path; otherwise leave as planned-but-gated.

### `A6` GPU-Accelerated Thumbnail Scale Pipeline (P1)

- Replace CPU-side `IWICBitmapScaler` for the largest thumbnail sizes with
  a Direct2D image effect chain (`ID2D1Effect` scale + linear gamma).
- Source bitmaps land directly in an `ID2D1Bitmap1` on the render device;
  scaled outputs are cached in the existing thumbnail LRU as BGRA byte
  arrays for cheap re-upload.
- Fall back transparently when D2D device is lost or unavailable.
- Target to validate in benchmarks: ≥30 % thumb-scale CPU reduction at
  256 px on a representative folder.

### `A7` Folder Warm-Up Window (P1)

- After enumeration completes, schedule a small batch of "top-of-folder
  warm-up" thumbnail jobs at low priority so the first scroll feels instant
  even before the user clicks the grid.
- Window size = `ResourceProfile`-derived prefetch radius × visible row
  estimate; cancellable on scroll.

### `A8` Startup Latency Budget Gate (P1)

- Capture `process-start → first-window-visible` and
  `first-window-visible → first-thumbnail-painted` spans through the
  existing diagnostics system.
- Emit a structured JSON snapshot on shutdown when launched with a
  `--bench-startup` flag.
- CI job runs HyperBrowse against a small fixed dataset and fails if any
  budget regresses beyond a configurable threshold.

---

## 4. Theme B — Competitive Workflow Features

Each item targets a specific gap versus FastStone, XnView MP, ImageGlass, or
qView while staying inside HyperBrowse's identity.

### `B1` Drag-and-Drop File Operations (P0)

- **Drag out:** thumbnails as `CFSTR_FILECONTENTS` / `CF_HDROP` so users
  can drop into Explorer, mail clients, or chat apps.
- **Drag in:** drop folders or files onto the browser pane to navigate or
  copy into the current folder.
- Holding `Ctrl` forces copy, `Shift` forces move; default mirrors Explorer
  semantics by destination type.
- Reuses `FileOperationService` for in-app handling.

### `B2` n-Up Compare & Side-by-Side Zoom Sync (P0)

- Extend the current two-up compare to 3-up and 4-up with a responsive
  flex-style layout in the viewer.
- Synchronized zoom and pan across tiles (toggleable).
- Per-tile rating/tag controls so culling happens during compare.
- Keyboard: `1`/`2`/`3`/`4` cycles candidate into focused tile,
  `,` / `.` rotates the candidate set.

### `B3` Quick-Pick Destination Panel (P1)

- Optional right-rail strip listing favorite + recent destinations as drop
  targets and one-click "Send Selection To" actions.
- Backed by the existing recent/favorite destination store.
- Per-row hover hint shows folder image count and last-used time.

### `B4` Color-Managed Display Path (P1)

- Use WIC's color management transform to convert decoded bitmaps to the
  active monitor profile.
- Per-monitor refresh when the user drags the viewer across displays
  (`WM_DPICHANGED` / `WM_DISPLAYCHANGE`).
- Toggle under **View ▸ Color Management** so users on accurate displays
  can opt out for raw speed.

### `B5` Saved Searches / Smart Folders (P1)

- Persist named filter expressions (e.g. `rating:>=4 tag:keeper type:raw`)
  in `%LOCALAPPDATA%\HyperBrowse\saved-searches.tsv`.
- File ▸ Open Saved Search… exposes them; filter box gains an inline
  "Save current filter" affordance.
- Pure in-memory evaluation — no background indexer.

### `B6` Histogram + Clip Warning Overlay (P1)

- Compute a per-image RGB+luma histogram on the existing viewer decode
  thread after the image becomes stable.
- Render with a single D2D path geometry; toggle with `H`.
- Optional clip warning overlay: pixels with any channel ≥ 254 or ≤ 1 get
  a flashing tint (toggle with `Shift+H`).

### `B7` Animated GIF / WebP Playback in Viewer (P2)

- Animated playback only inside the viewer (thumbnails stay first-frame).
- Reuses the existing WIC decode pipeline; new `AnimationController` drives
  frame timing through a DWM-synced timer.
- Pause/play and frame-step (`Space`, `[`, `]`).

### `B8` PSD/PSB Composite Preview (P2)

- Read the embedded composite preview via WIC so HyperBrowse can browse
  photographer master files without depending on Photoshop.
- No layer editing.

---

## 5. Theme C — Performance Branding & Polish

The brand is "fast". The product should look the part.

### `C1` Custom About Dialog (P1)

- Replace the `MessageBoxW` About with a small custom dialog showing the
  app icon at 64 px, version, build info, GPU vendor, and a one-line
  "started in N ms / first thumbnail in M ms" performance summary pulled
  from the diagnostics layer.

### `C2` Empty-State Watermark (P2)

- Render a muted app icon above the existing "Select a folder to begin
  browsing" text. Same in viewer for "Open an image to begin viewing".

### `C3` Performance HUD Overlay (P1)

- Optional translucent overlay (toggle `Ctrl+Shift+P`) showing live decode
  count, scale ms, cache hit rate, memory-pressure state, and thumbnail
  worker queue depth.
- Reuses diagnostics counters; zero cost when disabled.

### `C4` Settings Reorganization (P1)

- Move **Enable NVIDIA JPEG Acceleration** and **Use Out-of-Process LibRaw
  Fallback** out of the View menu into the new Settings dialog (Performance
  tab).
- Trim the browser context menu to selection-relevant actions only
  (carry-over from prior P2 backlog).

### `C5` Tools Menu (P1)

- New top-level Tools menu:
  - Settings…
  - Benchmark…
  - Cache Inspector…
  - Diagnostics Snapshot
  - Open Log Folder

### `C6` Inline Rename / In-Place Label Edit (P2)

- True in-place label editing in the thumbnail and details surfaces; `F2`
  currently opens a dialog.

### `C7` Breadcrumb / Path Bar (P2)

- Lightweight breadcrumb above the browser pane with clickable segments
  for fast parent navigation; complements the folder tree.

---

## 6. Theme D — Benchmarking & Diagnostics

Performance branding requires evidence.

### `D1` Standard Benchmark Datasets (P0)

- Datasets A–E per [05-benchmarking-plan.md](05-benchmarking-plan.md)
  staged under `tests/benchmark-datasets/` with a generator script for
  synthetic inputs (and pointers to user-supplied real datasets).

### `D2` Benchmark Runner & JSON Report (P0)

- `tools/RunBenchmarks.ps1` invoking `HyperBrowse.exe --bench-startup
  --bench-folder <path>` and aggregating runs into
  `build/bench/<git-sha>/report.json`.
- Markdown summary rendered into `docs/perf/latest.md`.

### `D3` CI Perf Regression Gate (P1)

- GitHub Actions job runs a small dataset bench against the release build,
  compares against a checked-in baseline (`docs/perf/baseline.json`), and
  fails on threshold breach.

### `D4` Cache Inspector Window (P1)

- Diagnostics-style window showing thumbnail cache contents
  (count/bytes/hit rate), metadata cache stats, persistent cache shard
  stats, and a Trim/Purge control.

### `D5` ETW / WPR Trace Hooks (P2)

- Emit ETW events for enumeration, thumbnail decode, scale, viewer decode,
  cache hit/miss; ship a `tools/CaptureTrace.wprp` profile for tracing
  releases on customer machines.

---

## 7. Theme E — Format & Decode Frontier

Lower priority than A–D but where competitors are starting to differentiate.

### `E1` AVIF Support (P2)

- Add optional libavif decode behind `HYPERBROWSE_ENABLE_AVIF`.
- Thumbnail via embedded preview where present; viewer via full decode.

### `E2` HEIC Support via Microsoft HEIF Extensions (P2)

- Detect the Microsoft HEIF Image Extension at startup; route HEIC/HEIF
  through WIC when available with a clear unsupported state otherwise.

### `E3` Multipage TIFF Navigation (P2)

- Per-page navigation inside the viewer (`PgUp`/`PgDn`) for multipage
  TIFFs. Browser still uses page 0 for the thumbnail.

### `E4` Lossless JPEG Crop & Trim (P3)

- Extend the existing EXIF-only orientation pipeline with lossless crop
  alignment (mcu-aligned) via libjpeg-turbo's transform API. Strictly
  opt-in; no other editing follows.

---

## 8. Items Intentionally Deferred (Not TODOs)

- Heavy image editing, painting, layered editing, RAW develop.
- Annotations and freehand markup.
- Per-image zoom/pan persistence.
- Plugin ecosystem and scripting.
- Face detection, duplicate finding, content-based similarity search.
- Library/database back end (Lightroom-style catalog).
- Cloud sync, mobile companion, web preview.
- Formal decoder polymorphic interface (`CanDecode` / `ReadHeader` / …):
  current free-function chain (nvJPEG → WIC → LibRaw) is adequate.
- `MainWindow.cpp` decomposition into per-controller files: tracked
  separately as ongoing hygiene; not a product feature.
- doctest migration: catch2 stays for now.

These remain deferred unless product direction changes.

---

## 9. Acceptance Bars

Each accepted item must satisfy:

1. **Performance hold-the-line.** No regression in startup, first-visible-
   thumbnail, scroll smoothness, or viewer-open latency on the Theme D
   reference dataset.
2. **Adaptive defaults.** Any new resource consumer plumbs into the
   `ResourceProfile` (A1) and respects memory pressure (A4).
3. **Native dependency budget.** New dependencies require an opt-out build
   flag and a documented runtime fallback.
4. **Tested.** Smoke or integration coverage added/updated under `tests/`.
5. **Documented.** README capability table and the relevant spec are
   updated in the same change.

---

## Appendix A — Recently Completed Archive

Detail-level history was removed from the active backlog to keep this file
focused. The summarized status as of this revision:

- **Rendering:** D2D/DirectWrite pipeline in browser grid and viewer, per-
  monitor DPI v2, smooth inertial scroll, high-quality cubic scaling
  ([15-d2d-rendering-migration.md](15-d2d-rendering-migration.md)).
- **File management:** copy, move, rename, batch rename (tokenized
  preview), delete, permanent delete, reveal, copy path, properties,
  recent destinations, pinned favorites, RAW+JPEG paired operations
  ([11-file-management-workflow.md](11-file-management-workflow.md)).
- **Browse workflow:** compare/cull lite, ratings and tags with filter
  syntax, date-taken sort, sort-direction toggle, configurable viewer
  mouse wheel, slideshow interval + transitions, info strip / details
  panel, rich image-information dialog
  ([10-prioritized-enhancements.md](10-prioritized-enhancements.md)).
- **Caching:** runtime-adaptive thumbnail and metadata cache sizing keyed
  off `GlobalMemoryStatusEx`, optional persistent thumbnail cache under
  `%LOCALAPPDATA%\HyperBrowse\thumbnail-cache`.
- **Architecture / hygiene:** shared `HyperBrowseCore` static library,
  smoke + integration test suite, GitHub Actions CI, portable zip + Inno
  Setup 6 installer with CUDA redistributable bundling, static MSVC
  runtime by default ([09-hardening-pass.md](09-hardening-pass.md)).
- **Toolbar:** owner-drawn double-buffered toolbar strip with grouped icon
  buttons and right-aligned actions
  ([16-toolbar-ux-redesign.md](16-toolbar-ux-redesign.md)).

For a deeper change log, consult the git history; this appendix exists only
to anchor the active plan above.
