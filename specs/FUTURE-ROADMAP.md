# HyperBrowse Future Roadmap

Last reviewed: 2026-09-26

This is the single forward-looking product backlog for HyperBrowse. It is
intentionally separate from the authoritative shipped contract in
[PRODUCT_SPEC.md](PRODUCT_SPEC.md). Nothing in this document is implemented
merely because it is listed here.

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
- WebP decoding and thumbnails through WIC, with stale thumbnail completion
  rejection and asynchronous folder-tree child-presence probing.
- HEIC (`.heic`) and JPEG XL (`.jxl`) are recognized by the browser and routed
  through WIC. Actual decode support depends on an installed WIC codec; codec
  availability detection is not implemented, and allowlist tests do not prove
  that a codec is available on a given machine.
- Runtime-adaptive thumbnail cache (128 MB–1 GB) and metadata cache (2,048–
  65,536 entries) sized from `GlobalMemoryStatusEx`.
- Optional `%LOCALAPPDATA%\HyperBrowse\thumbnail-cache` persistent cache.
- Optional nvJPEG acceleration and optional out-of-process LibRaw helper.
- File management: copy, move, rename, batch rename, delete, permanent
  delete, reveal, copy path, properties, recent destinations, pinned
  favorites, RAW+JPEG paired operations.
- Two-to-four-image N-up compare with synchronized zoom/pan, ratings/tags,
  filter-box (including `rating:>=N` / `tag:*`), date-taken sort, sort
  direction toggle, configurable viewer mouse wheel, slideshow with transition
  styles.
- Root-aware long-path breadcrumbs and an optional Performance HUD for
  thumbnail, queue, cache, and memory-pressure state.
- Diagnostics window, structured log, smoke + integration tests, GitHub
  Actions CI workflow (currently disabled; no cloud runs are active), portable
  zip + Inno Setup 6 installer.

See [PRODUCT_SPEC.md](PRODUCT_SPEC.md), [docs/architecture.md](../docs/architecture.md),
and [docs/testing.md](../docs/testing.md) for current product, architecture,
and validation contracts.

---

## 3. Theme A — Performance & Adaptive Resource Controls

The single most differentiating area HyperBrowse can press on right now.
Competitors expose almost no resource controls. HyperBrowse should ship a
small, opinionated set that is **safe by default** and **explicit when
asked**.

### `A3` Persistent Thumbnail Cache Maturity (P0)

**Implementation status: Shipped on 2026-09-14.** The first persistence pass
has been replaced by the hardened worker-owned path below. Detailed history
and validation evidence are retained in the [A3 implementation plan](A3-PERSISTENT-CACHE-PLAN.md)
and summarized in Appendix A.

- Sharded directory layout (`xx/yy/<hash>.bin`) to keep per-directory entry
  counts low on huge libraries.
- LRU eviction by size, driven by the configured persistent thumbnail-cache
  budget, default `min(totalRam/2, 8 GB)` capped by free-disk headroom on the
  cache volume.
- Background compaction pass on idle: reconcile orphan files, drop entries
  for confirmed-missing files, and enforce byte LRU using actual file sizes.
- Move all persistent-cache I/O onto one low-priority cache worker, including
  asynchronous lookup, store, invalidation, statistics, access-journal flush,
  and maintenance operations.
- Surface live cache metrics in the **Cache Stats** details-panel tab and
  detailed persistent-cache/per-shard information in the asynchronous
  **Persistent Thumbnail Cache** dialog (see D4), with compact and confirmed
  purge actions.
- Enforce cache-hit, store-throughput, compaction, cache-worker queue-delay,
  scroll-dispatch, and first-thumbnail regression bars in the benchmark suite.

### `A5` Decode/Scale Buffer Pools (P1)

**Implementation status:** In progress. An initial low-risk slice now pools the
transient host-side nvJPEG decode buffers used to copy BGR pixels back from the
CUDA device before thumbnail construction, with reuse counters recorded through
diagnostics. The RAW decode helpers now also reuse pooled BGRA conversion and
scale scratch buffers when building thumbnails from LibRaw and raw-helper
payloads. Remaining work: extend the same idea to any WIC-controlled scale
intermediates only if the completed D1-D3/A8 evidence loop demonstrates
material allocation pressure. See the [performance-evidence prompt](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md);
do not start WIC pooling before its benchmark and baseline gates are reviewed.

- Add a small `ScratchBufferPool` (per-size class, max N buffers) consumed
  by WIC scale, nvJPEG output, and LibRaw embedded-preview decode paths.
- Initial size classes keyed off active thumbnail size preset.
- Justified only if benchmarks (D2) show meaningful allocation pressure on
  the decode/scale hot path; otherwise leave as planned-but-gated.

### `A6` GPU-Accelerated Thumbnail Scale Pipeline (P1)

**Implementation status:** Deferred until the D1-D3/A8 performance-evidence
gates are complete and reviewed. First close the shared evidence gates with
the [performance-evidence prompt](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md),
then use the [A6 GPU thumbnail-scaling prompt](FUTURE-ROADMAP-PROMPT-03-A6-GPU-THUMBNAIL-SCALING.md)
as the A6 design and acceptance contract. The
[hosted-evidence and paired-benchmark prompt](FUTURE-ROADMAP-PROMPT-04-HOSTED-EVIDENCE-AND-A6-PAIRED-BENCHMARK.md)
defines the hosted calibration sequence and hardware comparison. Hosted
collection is paused because the user has opted out of cloud-hosted GitHub
Actions; no A6 hardware adapter has been verified in this execution. Production
scaling remains gated on reviewed evidence and an explicit go decision.

- Replace CPU-side `IWICBitmapScaler` for the largest thumbnail sizes with
  a Direct2D image effect chain (`ID2D1Effect` scale + linear gamma).
- Source bitmaps land directly in an `ID2D1Bitmap1` on the render device;
  scaled outputs are cached in the existing thumbnail LRU as BGRA byte
  arrays for cheap re-upload.
- Fall back transparently when D2D device is lost or unavailable.
- Target to validate in benchmarks: ≥30 % thumb-scale CPU reduction at
  256 px on a representative folder.

### `A7` Folder Warm-Up Window (P1)

**Implementation status:** Shipped. Browser refresh already schedules
low-priority top-of-folder warm-up thumbnail and metadata work after
enumeration, request epochs cancel stale warm-up batches on scroll, and the
warm-up window now scales with effective prefetch depth instead of using only
fixed prefetch multipliers.

- After enumeration completes, schedule a small batch of "top-of-folder
  warm-up" thumbnail jobs at low priority so the first scroll feels instant
  even before the user clicks the grid.
- Window size uses the effective prefetch depth and visible row estimate;
  Auto follows `ResourceProfile`, an explicit override takes precedence, and
  the work is cancellable on scroll.

### `A8` Startup Latency Budget Gate (P1)

**Implementation status:** In progress. Startup diagnostics now capture
`process-start → first-window-visible` and
`first-window-visible → first-thumbnail-painted`, and `--bench-startup`
emits a structured JSON snapshot on shutdown. The checked-in GitHub Actions
workflow is configured to run startup and persistent-cache scenarios through
the repeatable Release runner, but the repository workflow was manually
disabled on 2026-09-26 at the user's direction; run `36270914454` was cancelled.
No hosted samples qualify, and both baseline profiles remain at 0/10. Do not
re-enable or dispatch cloud Actions without fresh explicit authorization.
Remaining: agree on an acceptable no-cloud evidence policy or explicitly
authorize hosted collection before calibrating relative baselines. The current
fixed budgets remain absolute guardrails, not calibrated relative thresholds.
The local
debug run against the repo `assets` folder currently produced roughly
`900.60 ms` to first window visible and `1265.43 ms` to first thumbnail
painted, with the second span at roughly `364.83 ms`; this is illustrative, not
a calibrated baseline. See the
[performance-evidence prompt](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md).

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

**Implementation note:** The viewer now supports quick culling with `Delete`
and `Shift+Delete`: the active image can be sent to the Recycle Bin or
deleted permanently without an extra confirmation prompt, the viewer advances
to the next image, compare mode is preserved when a valid neighbor remains,
and browser focus is restored if the delete fails.

### `B1` Drag-and-Drop File Operations (P0)

**Implementation status:** Shipped for shell drag-out, in-app file drops, and
the existing Copy/Move operation path. Content-based duplicate finding is a
separate deferred idea and is not implied by the duplicate-file command.

- **Drag out (implemented):** selected thumbnails or image rows are exposed as
  a shell data object so users can drag them into Explorer, mail clients, or
  other shell-aware apps. The external drag begins when the pointer leaves
  the HyperBrowse window, preserving the existing in-app destination drag.
- **Drag in:** drop folders or files onto the browser pane to navigate or
  copy into the current folder.
- Holding `Ctrl` forces copy, `Shift` forces move; default mirrors Explorer
  semantics by destination type.
- Reuses `FileOperationService` for in-app handling.

### `B2` n-Up Compare & Side-by-Side Zoom Sync (P0)

**Implementation status:** The local implementation now supports two-to-four
selected images, focused candidate replacement, synchronized or independent
zoom/pan, and per-tile rating/tag commands through the existing metadata store.
The full local Debug CTest suite passes; manual DPI and forced-GDI-fallback
validation remain. See the
[B2 long-horizon prompt](FUTURE-ROADMAP-PROMPT-05-N-UP-COMPARE-AND-ZOOM-SYNC.md)
for the interaction contract and acceptance gates.

- Two images remain side by side; three/four images use a responsive 2x2
  viewer layout with a visible focus indicator.
- `Ctrl+Tab` / `Ctrl+Shift+Tab` focus tiles; `,` / `.` replace the focused
  tile from the captured browser candidate order, skipping visible images.
- Synchronized fit-relative zoom and normalized pan are on by default and can
  be toggled from the viewer context menu. Independent tile views are retained
  when synchronization is off.
- Per-tile rating/tag context-menu actions reuse the existing metadata store.
- Preserve `1` for Actual Size and bare `Tab` for overlay visibility; in n-up
  mode `Shift+Left/Right` also cycles the focused tile, while two-up retains
  adjacent comparison behavior.

### `B3` Quick-Pick Destination Panel (P1)

**Implementation status:** In progress. The existing details rail now exposes a
quick-send panel that lists favorite and recent destinations with one-click
`Copy` and `Move` actions for the current selection, visible per-row metadata,
and direct shell file drops onto destination rows. Dropping files onto a row
now copies by default and switches to move while `Shift` is held. Remaining:
decide whether the panel still needs richer hover-only affordances or a deeper
in-app drag source beyond the new row drop targets.

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

Static WebP decode and thumbnails are shipped in 2.3. This item remains
limited to animated playback; GIF and WebP thumbnails continue to use the
available first-frame WIC presentation.

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

**Implementation status: Implemented locally.** The existing DPI-aware custom About
dialog shows the 64 px app icon, generated version, build configuration, and
the display-adapter vendor for its monitor via a background query. It shows
the A8 startup spans when recorded and reports unavailable timings explicitly.

### `C2` Empty-State Watermark (P2)

**Implementation status: Implemented locally.** The browser no-folder state and viewer
no-image state show muted brand art above their prompts. Loading, error,
filtered-empty, and empty-folder states retain their distinct presentations.

### `C3` Performance HUD Overlay (P1)

**Implementation status: Implemented locally.** The off-by-default HUD is available from
View and `Ctrl+Shift+P` in thumbnail and details modes. It shows active
thumbnail decodes and pending jobs as live counts, average thumbnail scale time
from recorded diagnostics samples, the in-memory thumbnail-cache hit rate,
and current memory-pressure state. Missing scale/cache samples are shown as
unavailable. Its one-second refresh timer and prepared display text are removed
when disabled; no preference is persisted.

### `C4` Settings Reorganization (P1)

**Implementation status:** Shipped. Performance, diagnostics, and integration
surfaces are grouped under Tools, while the consolidated Settings dialog owns
the appearance, viewer, performance, behavior, and slideshow preferences.

- Move **Enable NVIDIA JPEG Acceleration** and **Use Out-of-Process LibRaw
  Fallback** out of the View menu into the new Settings dialog (Performance
  tab).
- Trim the browser context menu to selection-relevant actions only
  (carry-over from prior P2 backlog).

### `C5` Tools Menu (P1)

**Implementation status:** Shipped. The current command bar exposes Tools with
Settings, Performance, Diagnostics, and Integration submenus. Benchmark and
log-folder commands remain backlog ideas.

- New top-level Tools menu:
  - Settings…
  - Benchmark…
  - Cache Inspector…
  - Diagnostics Snapshot
  - Open Log Folder

### `C6` Inline Rename / In-Place Label Edit (P2)

**Implementation status:** Folder-tree inline rename is shipped. In-place
editing of image labels in thumbnail/details surfaces remains deferred.

- True in-place label editing in the thumbnail and details surfaces; `F2`
  currently opens a dialog.

### `C7` Breadcrumb / Path Bar (P2)

**Implementation status: Implemented locally.** A breadcrumb above the right-side browser
content exposes drive/UNC roots and parent folders. Long paths collapse to a
root, current folder, and a **More parent folders** menu; all navigation uses
the existing folder-load path and keeps the folder tree and history in sync.

The coordinated implementation brief for C1, C2, C3, and C7 is the
[long-horizon UI surfaces prompt](FUTURE-ROADMAP-PROMPT-06-PERFORMANCE-UI-SURFACES.md).

**Validation note:** The full Debug CTest matrix passes 25/25 and the final
Release application target builds. Release CTest and manual theme/DPI visual
review remain unverified.

### `C8` Accessibility Completion and Release Verification (P1)

**Implementation status:** Main-window custom surfaces expose semantic names,
roles, states, focus, and state-change notifications. Dialog-specific custom
surfaces and platform-facing verification remain.

- Complete the smallest project-compatible accessibility bridge for the
  remaining custom dialog surfaces.
- Expose meaningful names, roles, enabled/disabled, checked/selected,
  expanded/open, and focused state consistently across dialogs.
- Add focused smoke coverage for dialog semantics and focus restoration.
- Complete NVDA or JAWS, Inspect/UI Automation, high-contrast, theme, DPI, and
  larger-text verification before treating the accessibility contract as
  release-complete.
- Keep the custom-rendered controls and existing Win32, Direct2D, and GDI
  ownership split; accessibility work must not become a framework migration.

The detailed implementation queue remains in
[docs/keyboard-accessibility-plan.md](../docs/keyboard-accessibility-plan.md).

### `C9` Simplified Window Chrome (P2)

- Remove redundant view mode, recursive-browsing, and theme indicators from
  the title bar when the active folder path already identifies the window.
- Keep the current folder path and essential state discoverable through the
  title bar, menus, and accessible window name.
- Revalidate narrow layouts, multiple main-window instances, and screen-reader
  names after the chrome is simplified.

---

## 6. Theme D — Benchmarking & Diagnostics

Performance branding requires evidence.

D1-D3 and the remaining A8 work are one evidence-closure effort. Its execution
brief is the [performance-evidence prompt](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md).

### `D0` Trustworthy Validation Baseline (P0)

**Implementation status: Shipped on 2026-09-26.** The normal Debug and Release
CTest matrices now pass with 23/23 tests in each configuration. Single-instance
smoke coverage uses a per-test mutex and named-pipe namespace, so an installed
or separately running HyperBrowse process cannot contaminate the result. The
stale-completion scenario uses fresh persistent-cache identities on each run
and verifies both memory retention and worker-owned disk persistence without
posting a stale UI update. Optional fuzz tests are excluded from the normal
preset and the opt-in configuration builds and runs both boundary tests.

The execution brief and root-cause history are retained in the
[validation-baseline prompt](FUTURE-ROADMAP-PROMPT-01-VALIDATION-BASELINE.md).
This baseline is a prerequisite for starting new roadmap feature work and
remains the hold-the-line gate for future cache, startup, and decode changes.

### `D1` Standard Benchmark Datasets (P0)

**Implementation status:** Shipped for deterministic generated fixtures and
manifest-driven local RAW staging. Dataset D remains unavailable until a user
supplies permitted NEF/NRW samples and a completed staging manifest. The small
checked-in `assets` startup fixture is not the formal A-E suite.

- Datasets A-E per [the archived benchmarking plan](archive/05-benchmarking-plan.md)
  are defined in `tests/benchmark-datasets/manifest.json`; generate and verify
  synthetic inputs with `tools/GenerateBenchmarkDatasets.ps1`.

### `D2` Benchmark Runner & JSON Report (P0)

**Implementation status:** Shipped for the currently automated startup and
persistent-cache scenarios. Archived browser/viewer interaction categories
that lack reliable measurement producers are classified in
[the performance scenario matrix](../docs/perf/scenario-matrix.md).

- `tools/RunBenchmarks.ps1` invokes the supported benchmark scenarios against
  formal datasets, retains each raw sample, and aggregates runs into
  `build/bench/<git-sha>/<run-id>/report.json`.
- Markdown summary rendered into `docs/perf/latest.md`; CI artifacts retain
  per-run JSON, logs, and Dataset A inputs for 90 days.

### `D3` CI Perf Regression Gate (P1)

**Implementation status:** Partial and paused. The workflow definition runs
five repetitions per scenario on each Release decode path and retains JSON
evidence when enabled. The only repository workflow is currently disabled by
the user's no-cloud-Actions decision; both checked-in baseline profiles remain
collecting at 0/10 samples. Absolute guardrails remain in the workflow, but no
hosted relative comparison is currently running. Do not re-enable cloud CI
without fresh explicit authorization. Relative comparisons require reviewed
samples and thresholds under an explicitly approved evidence policy.

- When enabled, GitHub Actions runs the standard automated dataset suite
  against the Release build, compares compatible results against a reviewed
  baseline (`docs/perf/baseline.json`), retains JSON results, and fails on a
  supported threshold breach.

### `D4` Cache Inspector Window (P1)

**Implementation status:** Functionally shipped through existing UI surfaces.
The Cache Stats details-panel tab is the live, non-modal view for thumbnail and
metadata cache usage, hit rates, decode queue, scale timing, memory pressure,
and persistent-cache totals. The asynchronous Persistent Thumbnail Cache
dialog provides expanded aggregate and per-shard details plus compact and
confirmed purge actions; Settings provides persistent-cache trim. Decision:
keep these focused surfaces and do not build a separate Diagnostics-style
Cache Inspector window unless user research identifies a concrete gap.

The original single-window proposal is superseded by this split-surface
implementation; there is no remaining D4 feature work.

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

**Implementation status:** Partial. The `.heic` extension is in the browser
and WIC decoder allowlists and is routed through WIC. Startup detection of the
Microsoft HEIF Image Extension, explicit codec availability reporting, and a
clear unsupported state remain open. `.heif` is not currently in the
allowlist.

- Detect the Microsoft HEIF Image Extension at startup and report whether
  HEIC decoding is available; retain graceful failure when its WIC codec is
  absent.

### `E3` Multipage TIFF Navigation (P2)

- Per-page navigation inside the viewer (`PgUp`/`PgDn`) for multipage
  TIFFs. Browser still uses page 0 for the thumbnail.

### `E4` Lossless JPEG Crop & Trim (P3)

- Extend the existing EXIF-only orientation pipeline with lossless crop
  alignment (mcu-aligned) via libjpeg-turbo's transform API. Strictly
  opt-in; no other editing follows.

### `E5` JPEG XL Support via WIC (P2)

**Implementation status:** Partial. The `.jxl` extension is in the browser and
WIC decoder allowlists and is routed through WIC. Actual decoding depends on a
compatible installed WIC codec; HyperBrowse does not currently detect codec
availability or guarantee out-of-box JPEG XL decoding. Existing smoke coverage
validates allowlisting and routing, not codec installation or image decoding.

- Detect and report JPEG XL codec availability, and add codec-backed decode
  verification on a supported Windows configuration before describing JPEG XL
  as generally available.

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
2. **Adaptive defaults.** Any new resource consumer plumbs into the shipped
  `ResourceProfile` contract and respects memory pressure (A4).
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
  (the archived D2D migration plan).
- **File management:** copy, move, rename, batch rename (tokenized
  preview), delete, permanent delete, reveal, copy path, properties,
  recent destinations, pinned favorites, RAW+JPEG paired operations
  ([the archived file-management workflow](archive/11-file-management-workflow.md)).
- **Browse workflow:** compare/cull lite, ratings and tags with filter
  syntax, date-taken sort, sort-direction toggle, configurable viewer
  mouse wheel, slideshow interval + transitions, info strip / details
  panel, rich image-information dialog
  ([archived browse enhancements](archive/10-prioritized-enhancements.md)).
- **Caching:** runtime-adaptive thumbnail and metadata cache sizing keyed
  off `GlobalMemoryStatusEx`, optional persistent thumbnail cache under
  `%LOCALAPPDATA%\HyperBrowse\thumbnail-cache`.
- **A3 Persistent Thumbnail Cache Maturity:** Shipped sharded storage,
  bounded restartable legacy migration, collision-safe indexing, idle
  compaction and source-health reconciliation, per-shard inspection, and one
  low-priority worker for all production persistent-cache I/O. Release CI now
  retains cache-performance and startup benchmark snapshots with regression
  thresholds ([archived A3 implementation plan](A3-PERSISTENT-CACHE-PLAN.md)).
- **Adaptive resource controls (A1/A2):** persisted resource profiles,
  profile-following cache and prefetch controls, explicit cache-cap overrides,
  the non-modal Cache Stats details tab, and asynchronous persistent-cache
  trimming. The shipped contract is documented in
  [PRODUCT_SPEC.md](PRODUCT_SPEC.md).
- **A4 memory-pressure response:** shell-owned background pressure sampling
  with recovery hysteresis, reduced prefetch depth, throttled thumbnail and
  metadata workers, cache trimming, viewer full-image cache trimming, and
  suppression of opportunistic disk-thumbnail writes while pressure is
  active. Cache Performance exposes the pressure state, queue and in-flight
  decode telemetry, cache hit rates, and scale timing.
- **Architecture / hygiene:** shared `HyperBrowseCore` static library,
  smoke + integration test suite, a GitHub Actions CI workflow (currently
  disabled), portable zip + Inno Setup 6 installer with CUDA redistributable
  bundling, static MSVC runtime by default
  ([the archived hardening plan](archive/09-hardening-pass.md)).
- **Toolbar:** owner-drawn double-buffered toolbar strip with grouped icon
  buttons and right-aligned actions
  ([archived toolbar redesign](archive/16-toolbar-ux-redesign.md)).

For a deeper change log, consult the git history; this appendix exists only
to anchor the active plan above.
