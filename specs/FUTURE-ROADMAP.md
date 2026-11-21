# HyperBrowse Future Roadmap

Last reviewed: 2026-11-21 (roadmap status and documentation consolidated)

This is the single forward-looking product backlog for HyperBrowse. It is
intentionally separate from the authoritative shipped contract in
[PRODUCT_SPEC.md](PRODUCT_SPEC.md). Nothing in this document is implemented
merely because it is listed here.

This document tracks the **forward-looking** HyperBrowse backlog and current
implementation/verification status. Appendices A-B summarize completed work and
evidence; item sections may retain stable contracts and status history. A
completed item is not an open task unless an outstanding gate is stated.

The plan is organized around a single product north star:

> **HyperBrowse is the fastest practical Windows image browser/viewer.**
> Every accepted item must either (a) extend competitive workflow depth without
> diluting that brand, or (b) measurably improve perceived speed, throughput,
> or resource efficiency on real hardware.

---

## 0. Recommended Sequence (reviewed 2026-10-31; status refreshed 2026-11-21)

The original sequence review compared the backlog with release 2.7.0 source,
the [Bug Report 2 review plan](HyperBrowse-Bug-Reports-2-Review-Plan.md), and
the [UI and design polish prompt](FUTURE-ROADMAP-PROMPT-10-UI-DESIGN-POLISH.md)
ledger. Status below has since been refreshed from implementation records,
test results, and owner confirmations; manual results are dated where
recorded.

### 2.7.1 correctness and documentation patch

Items 24, 40, 43, and 46 and the README screenshot refresh are included in
release 2.7.1.

- **Bug item 40 — complete:** the Viewer setting now says "Use the slideshow
  transition when navigating manually." The default-off opt-in continues to
  apply the selected style and duration to manual navigation, without
  affecting slideshow playback. Existing policy tests cover all four
  slideshow/manual enabled combinations.
- **Bug item 43 — complete:** viewer-context `Ctrl+I` is in
  `ShortcutCatalog.h`, with a focused runtime catalog assertion.
- **Bug item 46 — complete:** the README Quick Actions section now documents
  the configurable assignment order, its `7890-=123456` default, and fallback
  order.
- **Bug item 24 — complete:** new profiles default to leaving full screen on
  Escape; a windowed viewer still closes. Existing saved Escape choices are
  preserved. The fullscreen interaction smoke passed.
- **README screenshot — complete:** refreshed `docs/MainWindow.PNG` with the
  current main-window UI and the public-domain nature example set.

### 2.8 local implementation and verification

- Prompt 10 Phases 3 (C8), 4 (C6), 5 (C9), and 6 (B3) are implemented.
- Local implementation and verification are complete. This does not publish a
  2.8 release; the current released version remains 2.7.1.
- **Visual review — complete (owner confirmed 2026-11-15):** C6/C9/B3,
  B2 DPI/forced-GDI review, B4 physical display/color, C1-C3/C7 theme/DPI,
  C8 high-contrast/theme/DPI/larger text, and Bug Report 2 item 61 Settings
  scaling.
- Full Debug and Release CTest suites passed 40/40 on 2026-11-17.
- **C6/C8 manual sign-off requirements removed by the owner on 2026-11-21.**
  Shipped rename and accessibility behavior, existing automated coverage, and
  the previously passed visual reviews remain in place.
- **B2 per-tile metadata and Debug gate — passed 2026-11-17:** the focused
  viewer smoke and full Debug/Release evidence are summarized in Appendix B.
- **E2/E5 real-fixture decode — passed 2026-11-17:** one HEIC and one JPEG XL
  sample produced thumbnails and rendered in the Debug full viewer with
  installed Microsoft codecs. This closes the sample/configuration gate only;
  broader format and machine compatibility remain codec-dependent.
- **Bug Report 2 item 69 WebP — passed 2026-11-17:** with Microsoft WebP Image
  Extension 1.2.31.0 installed, HyperBrowse enumerated and thumbnailed a
  temporary WebP derived from the public-domain Yellowstone example, rendered
  it in the Viewer, and opened it by path through Windows Run. This verifies
  only this machine's installed codec and file association.

### Next: 2.9 performance evidence

- Decide whether to adopt the per-machine local gate (`tools/CheckPerf.ps1`
  with `docs/perf/local-baseline.json`, shipped in the code-quality plan
  Phase 6) as the no-cloud evidence policy for A8 and D3. Do not re-enable
  hosted Actions.
- Use that evidence to make the A5 (WIC pooling) and A6 (GPU scaling)
  go/no-go decisions.
- **Bug item 48:** `DiskThumbnailCache::Store` holds the process-wide
  filesystem mutex across bitmap extraction and cache-file I/O, serializing
  parallel writers. Narrow it to index/journal coordination and benchmark one
  and eight writers.

### Feature queue (by user-visible value)

1. `B7` animated GIF/WebP playback (most visible gap against competitors).
2. `C10` command search over the existing shortcut catalog (see Theme C).
3. `E3` multipage TIFF navigation.
4. `B6` clip-warning overlay (the histogram itself is shipped).
5. `E1` AVIF, `B8` PSD/PSB preview, `E4` lossless JPEG crop.

### Usability observations to keep in view

- Surface area is large (about 108 catalogued shortcuts and roughly 295 menu
  entries); shortcut/command discoverability was parked in Prompt 10 and is
  the best lever for new users.
- Settings implementation has grown into a single very large source file
  (`MainWindowExperimentalSettings.cpp`); unifying the settings dialogs was
  parked, not rejected, and should be revisited after 2.8.
- The README carries eight "What's New" sections plus the full version
  history; moving history to a changelog would make the capability and
  shortcut sections easier to find.

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

## 2. Current Shipped Contract

The [Product Specification](PRODUCT_SPEC.md) is the single maintained
inventory of released behavior and supported formats. This roadmap does not
duplicate that feature list; proposed entries below remain unshipped until a
release includes them. See [architecture](../docs/architecture.md) and
[testing](../docs/testing.md) for the corresponding technical and validation
contracts.

---

## 3. Theme A — Performance & Adaptive Resource Controls

The single most differentiating area HyperBrowse can press on right now.
Competitors expose almost no resource controls. HyperBrowse should ship a
small, opinionated set that is **safe by default** and **explicit when
asked**.

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

### `B2` n-Up Compare & Side-by-Side Zoom Sync (P0)

**Implementation status:** Shipped in 2.5.0; per-tile metadata targeting,
isolation, persistence, visual review, and Debug/Release test evidence are
complete. See Appendix B and the
[B2 implementation brief](FUTURE-ROADMAP-PROMPT-05-N-UP-COMPARE-AND-ZOOM-SYNC.md).

### `B3` Quick-Pick Destination Panel (P1)

**Implementation status:** Favorite and recent destinations appear in the
details-rail panel with one-click `Copy` and `Move` actions, per-row image-count
and last-used hover hints, and the existing direct shell file-drop targets.
Image counts are gathered on a bounded worker, and the existing in-app
thumbnail drag path now has focused end-to-end coverage. `HyperBrowseQuickSendSmoke`
covers row deduplication, asynchronous count refresh, accessible hint text, and
delivery to a row. Shell drops copy by default and move with `Shift`; in-app
drags retain their existing modifier/drive-based Copy/Move behavior. Owner
visual review passed on 2026-11-15; implementation and focused smoke coverage
are complete.

### `B4` Color-Managed Display Path (P1)

**Implementation status:** Shipped in 2.5.0; local automated validation gates passed
on 2026-09-30. The focused local color smoke covers generated ICC pixel
conversion, alpha/orientation, browser/viewer/all compare counts, per-window
profiles, settings/menu accessibility, fallbacks, stale results, bounded caches,
and close during lookup. Debug and Release application/test/benchmark builds
succeeded; both exact full CTest presets passed 27/27 (50.77 s Debug, 47.03 s
Release). The prior idle-client failure was traced to live-cache journal replay
and generic WCS lookup's printer-spooler RPC, not pipe cancellation. Test-cache
isolation, exact-display DC profile lookup, and bounded message pumping closed
the software gates. The owner confirmed the physical display/color visual
review passed on 2026-11-15; the specific display/profile/DPI measurements are
not recorded here, and automated pixel checks do not certify physical color
accuracy. Cloud Actions remain disabled; no workflow, packaging, commit, or
push was performed.

The shipped display-color contract is in [PRODUCT_SPEC.md](PRODUCT_SPEC.md).
The implementation brief and manual-review criteria are in the
[B4 long-horizon prompt](FUTURE-ROADMAP-PROMPT-07-COLOR-MANAGED-DISPLAY-PATH.md).

### `B5` Saved Searches / Smart Folders (P1)

**Implementation status:** Software surface shipped in 2.5.0 on 2026-09-30.
RAW/exact-extension filtering, bounded asynchronous UTF-8 storage, and File
open/save/update/rename/delete/reload commands have Debug/Release smoke coverage,
including native dialogs and restart. The original B5 checkpoint passed 28/28
Debug/Release tests before codec-readiness tests were registered; the 2.5.0
feature gate passed 30/30, and the current full Debug/Release CTest suites
passed 40/40 on 2026-11-17 (see Prompt 08's ledger). The
inline Save icon shares the File command, exposes accessible state/action, and
yields filter space in narrow layouts. Viewer animation remains the next
candidate in this workflow sequence.

Execution history is in the
[workflow and format enhancements prompt](FUTURE-ROADMAP-PROMPT-08-WORKFLOW-AND-FORMAT-ENHANCEMENTS.md).
B5, E2/E5 codec readiness, and C5 Open Log Folder shipped in 2.5.0. B7
animation is the next candidate; B6 and E1 remain in the follow-on queue.

### `B6` Histogram + Clip Warning Overlay (P1)

**Implementation status:** The histogram half is shipped: the details panel
shows an RGB histogram, and 2.7.0 added a configurable, asynchronously
computed RGB histogram viewer overlay that follows the focused compare tile.
Remaining: the clip warning overlay only.

- Optional clip warning overlay: pixels with any channel ≥ 254 or ≤ 1 get
  a tint, toggled from the overlay configuration or a free shortcut (`H` is
  already assigned in the viewer; confirm the binding against the shortcut
  catalog before choosing one).

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

Execution sequence for the owner-selected 2026-10-25 UI observations (folder
tree stall, theme leftovers, dialog accessibility, in-place rename, title bar,
Quick Send polish): [UI and design polish prompt](FUTURE-ROADMAP-PROMPT-10-UI-DESIGN-POLISH.md).
Phases 1 (folder tree stall) and 2 (theme tokens and high contrast) shipped
in 2.7.0; Phases 3 (C8), 4 (C6), 5 (C9), and 6 (B3) are implemented.

### `C10` Command Search (P2)

Shortcut and command discoverability was parked in Prompt 10, but the
product now has about 108 catalogued shortcuts and a deep menu tree.

- A keyboard-invoked search box that filters the existing `ShortcutCatalog`
  and menu command IDs by name, shows each command's shortcut, and dispatches
  through the existing `WM_COMMAND` path. No new command registry.
- Must stay accessible (UIA names/roles) and theme-aware; confirm a free
  shortcut against the catalog before assigning one.

### `C5` Tools Menu (P1)

The core Tools menu and Settings organization are shipped and summarized in
Appendix A. **Open Log Folder** shipped in 2.5.0 and was later removed from
the product (commit `74c1a4c`); it is not an active item. **Benchmark**
remains deferred; the separate Cache Inspector window was superseded by D4's
existing surfaces.

### `C6` Inline Rename / In-Place Label Edit (P2)

**Implementation status:** Folder-tree inline rename remains shipped. F2 now
edits a single selected file in place in both thumbnail and details views,
with the existing modal rename dialog as fallback. The editor uses the current
rename validation and asynchronous file-operation path; the focused inline
rename smoke covers Enter, Tab, focus loss, Escape, and model refresh.
Owner visual review passed on 2026-11-15. The separate cross-pane focus-loss
walkthrough is no longer a release requirement per the owner (2026-11-21).

- Preserve the filename-stem selection, file-operation semantics, and modal
  fallback when extending this workflow.

### C1-C3 / C7 UI Surface Verification

The local implementations are summarized in Appendix B. The coordinated
implementation brief is the
[long-horizon UI surfaces prompt](FUTURE-ROADMAP-PROMPT-06-PERFORMANCE-UI-SURFACES.md).

- Owner theme/DPI visual review passed on 2026-11-15.
- Full Debug and Release CTest gates passed 40/40 on 2026-11-17.

### `C8` Accessibility Support (P1)

**Implementation status:** Main-window and Experimental Settings custom
surfaces reuse the shared `MainWindowAccessibility` MSAA/`IAccessible` provider.
`HyperBrowseSettingsSmoke` now covers Settings names and roles, focused and
selected tab state, page-focus restoration, and checked/unavailable state
transitions for automatic cache sizing. The owner confirmed high-contrast,
theme, DPI, and larger-text visual checks passed on 2026-11-15. The separate
NVDA/JAWS and Inspect/UIA sign-off requirement was removed by the owner on
2026-11-21.

- Keep using the existing bridge for custom dialog surfaces; no framework
  migration or replacement of custom-rendered controls is required.
- Preserve meaningful names, roles, enabled/disabled, checked/selected,
  expanded/open, and focused state as dialog surfaces evolve.
- Keep the custom-rendered controls and existing Win32, Direct2D, and GDI
  ownership split; accessibility work must not become a framework migration.

The detailed implementation queue remains in
[docs/keyboard-accessibility-plan.md](../docs/keyboard-accessibility-plan.md).

### `C9` Simplified Window Chrome (P2)

**Implementation status:** The main-window title keeps `HyperBrowse` and the
full active folder path. The status bar now surfaces view mode,
recursive-browsing, and theme state, with the same concise summary exposed as
the status control's accessible name. `HyperBrowseWindowChromeSmoke` covers
state changes, accessible names, independent window paths, and narrow geometry.
Owner visual review passed on 2026-11-15; automated accessibility-name
coverage remains in place. A separate screen-reader name review is no longer
a release requirement per the owner (2026-11-21).

- Remove redundant view mode, recursive-browsing, and theme indicators from
  the title bar when the active folder path already identifies the window.
- Keep the current folder path and essential state discoverable through the
  title bar, menus, and accessible window name.
- Revalidate narrow layouts and multiple main-window instances after the
  chrome is simplified; preserve automated accessibility-name coverage.

---

## 6. Theme D — Benchmarking & Diagnostics

Performance branding requires evidence.

D1-D3 and the remaining A8 work are one evidence-closure effort. Its execution
brief is the [performance-evidence prompt](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md).

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
- **Recommendation (2026-10-31; owner decision pending):** a per-machine local gate exists
  (`tools/CheckPerf.ps1`, `docs/perf/local-baseline.json`; advisory unless
  `-Strict`). Decide whether it is acceptable as the no-cloud evidence policy
  for A8/D3 so the A5/A6 decisions can proceed without hosted runners.

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

**Implementation status:** Readiness software shipped in 2.5.0. `.heic`
remains recognized and routed through WIC. Tools > Diagnostics > Snapshot
explicitly refreshes bounded worker-owned decoder discovery and reports
created/missing/discovery-failed states separately from observed thumbnail
and full-image decode outcomes. Discovery never blocks browsing or disables a
working decoder; no codec is installed automatically. Discovery is on demand,
not startup work. `.heif` remains outside the allowlist.

On 2026-11-17, `basic-8bit-srgb.heic` (1600 x 1200, HEVC Main Still Picture,
8-bit 4:2:0; [CC0 1.0 sample](https://heic.fyi/heic-sample-files)) rendered as
a browser thumbnail and in the full viewer in `build\Debug\HyperBrowse.exe`.
The installed codecs were Microsoft HEIF Image Extension 1.2.62.0 and HEVC
Video Extension 2.5.48.0. Fixture SHA-256:
`3fe7d32e5568f8bfd6b5d3290e61ac9b964d08464d388fa064f69cddddfee82c`.
This closes the basic HEIC fixture gate for this installed-codec
configuration only; it does not establish support for other HEIC variants or
`.heif`. The feature-readiness Debug/Release suites passed 30/30 at delivery;
the latest full Debug/Release CTest suites passed 40/40 on 2026-11-17.

### `E3` Multipage TIFF Navigation (P2)

- Per-page navigation inside the viewer (`PgUp`/`PgDn`) for multipage
  TIFFs. Browser still uses page 0 for the thumbnail.

### `E4` Lossless JPEG Crop & Trim (P3)

- Extend the existing EXIF-only orientation pipeline with lossless crop
  alignment (mcu-aligned) via libjpeg-turbo's transform API. Strictly
  opt-in; no other editing follows.

### `E5` JPEG XL Support via WIC (P2)

**Implementation status:** Readiness software shipped in 2.5.0. `.jxl`
remains recognized and routed through WIC, with the same on-demand worker
discovery, independent decode outcomes, explicit refresh, failure fallback,
and redacted diagnostics as HEIC. Mocked discovery and renamed PNGs are not
JPEG XL decode evidence. The feature-readiness Debug/Release suites passed
30/30 at delivery; the latest full Debug/Release CTest suites passed 40/40 on
2026-11-17.

On 2026-11-17, [OpenPrinting's
`4x6-landscape-photo-srgb.jxl`](https://github.com/OpenPrinting/sample-files/blob/main/jxl/4x6-landscape-photo-srgb.jxl)
rendered as a browser thumbnail and in the full viewer in
`build\Debug\HyperBrowse.exe`. The sample is covered by the repository's
[Apache-2.0 license](https://github.com/OpenPrinting/sample-files/blob/main/LICENSE);
its size is 234,933 bytes and SHA-256 is
`CE10D94C01A690227EE5CC5DE7B261A4CB27FAE90950390A691767242E9F8F8E`.
The installed Microsoft JPEG XL Image Extension was 1.2.68.0, with WIC
`Microsoft JPEG XL Decoder` version 10.0.26100.1. This closes the basic JPEG XL
fixture gate for this installed-codec configuration only; out-of-box and
broader format compatibility remain unverified.

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
- Further `MainWindow` decomposition beyond the per-area file split completed
  in the [code-quality hygiene plan](FUTURE-ROADMAP-PROMPT-09-CODE-QUALITY-HYGIENE.md)
  (Phases 4-5); not a product feature.
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
  thresholds ([archived A3 implementation plan](archive/A3-PERSISTENT-CACHE-PLAN.md)).
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
- **A7 Folder Warm-Up Window:** cancellable top-of-folder thumbnail and
  metadata warm-up uses the effective prefetch depth and visible-row estimate.
- **B1 Drag-and-Drop File Operations:** shell drag-out and browser drops use
  the existing file-operation path; modifier keys preserve Explorer-style
  copy/move behavior.
- **C4/C5 Core UI Organization:** Settings owns the consolidated appearance,
  viewer, performance, behavior, and slideshow preferences. The Tools menu
  groups Settings, Performance, Diagnostics, and Integration commands.
- **D0 Trustworthy Validation Baseline:** shipped 2026-09-26 with per-test
  single-instance isolation and stale-completion coverage; the original gate
  recorded 23/23 Debug and Release CTest passes. See the
  [validation-baseline prompt](FUTURE-ROADMAP-PROMPT-01-VALIDATION-BASELINE.md).
- **D4 Cache Inspection:** shipped through the Cache Stats tab, asynchronous
  Persistent Thumbnail Cache dialog, and Settings trim action; no separate
  Diagnostics-style Cache Inspector window is planned.

For a deeper change log, consult the git history; this appendix exists only
to anchor the active plan above.

## Appendix B — Completed UI and Verification Evidence

- **B2 N-Up Compare:** shipped in 2.5.0 with two-to-four tiles, captured
  candidate cycling, synchronized or independent zoom/pan, and focused-tile
  rating/tag actions through the existing metadata store. Focused viewer smoke
  coverage verifies per-tile targeting, file isolation, and persistence through
  store flush/reopen. The full Debug and Release CTest gates passed 40/40 on
  2026-11-17. Owner visual review at the supported DPI settings and forced-GDI
  fallback passed on 2026-11-15.
- **C1/C2/C3/C7 UI Surfaces:** the About dialog, empty-state watermarks,
  Performance HUD, and breadcrumb/path bar are implemented and present in
  2.5.0. Full Debug/Release CTest gates passed 40/40 on 2026-11-17; owner
  theme/DPI visual review passed on 2026-11-15.
