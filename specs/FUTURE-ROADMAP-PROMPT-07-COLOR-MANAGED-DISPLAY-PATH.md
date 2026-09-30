# Long-Horizon Prompt 07: B4 Color-Managed Display Path

## Mission

Implement an opt-out color-managed image display path for HyperBrowse using
Windows Color System (WCS) monitor profiles and Windows Imaging Component
(WIC) color transforms. Apply a source-to-display transform to browser
thumbnails, the single-image viewer, and every tile in two-, three-, and
four-image compare. Preserve the existing Win32, WIC, Direct2D, DirectWrite,
cache, and asynchronous-work ownership boundaries.

The feature must select the profile for the monitor displaying each window,
refresh converted output when that monitor or its assigned profile changes,
and preserve current behavior when color management is disabled or a usable
profile/transform is unavailable. Do not claim color accuracy from a menu
switch or a successful transform initialization alone; validate converted
pixels and real display behavior where suitable hardware is available.

## Current Cloud-Execution Hold

The user has opted out of cloud-hosted GitHub Actions. The repository workflow
is manually disabled. Do not re-enable it, dispatch it, or trigger a cloud run
through a push or pull request without fresh, explicit user authorization.
Validate locally. If an acceptance gate cannot be completed without cloud
execution, stop and report it rather than starting a workflow.

## Relationship to Other Work

- [The active roadmap](FUTURE-ROADMAP.md) is authoritative for B4 product scope.
- B4 adds display color conversion; it is not a general image-processing,
  editing, soft-proofing, HDR, or print-management feature.
- Reuse existing WIC, viewer, browser, settings, rendering, diagnostics, and
  cache ownership. Do not add a dependency or a parallel decode/cache system.
- Do not expand into B2 compare behavior, A5/A6 decode or GPU work, B4
  benchmarking infrastructure, or remote CI.

## Starting Facts to Verify

Treat these as source-finding hints and confirm them against the current tree:

- `decode::DecodeWicSource` in `src/decode/ImageDecoder.cpp` reads a WIC
  frame, applies orientation/scaling, then converts to
  `GUID_WICPixelFormat32bppPBGRA`. The corresponding WIC thumbnail path also
  converts to premultiplied BGRA. Neither currently uses WIC color contexts
  or `IWICColorTransform`.
- Full-image decode has WIC, LibRaw, and optional nvJPEG paths. Establish what
  source color information each path exposes before defining its fallback.
- `ViewerWindow` handles `WM_DPICHANGED` and `WM_DISPLAYCHANGE` to update its
  display surface, but currently does not refresh a monitor color profile.
  DPI changes alone do not prove that the monitor changed; two displays can
  have equal DPI and different profiles.
- `MainWindow` owns multiple independent `ViewerWindow` instances and applies
  shared preferences to them. Browser thumbnails and viewer full images use
  shared caches whose current identities are based on source file data, not a
  destination monitor profile.
- `ViewerSettingsPersistence` is the existing typed persistence boundary for
  viewer preferences. MainWindow owns settings application and menu state.
- Search the current tree for any newly added color-management code before
  assuming these facts remain current.

## Hard Scope Boundaries

- Keep decoding, profile-file access, WIC transforms, and full-size pixel
  conversion off window procedures and UI callbacks. UI work may capture a
  monitor/profile identity and schedule conversion, but must not block on it.
- Treat cached decoded pixels as source-oriented canonical data. Never insert
  monitor-transformed pixels into a shared cache keyed only by file path and
  modification time. A display-specific cache, if required, must be bounded
  and keyed by stable source identity plus destination-profile identity and
  relevant transform settings.
- A transform result is valid only for the source image identity, destination
  profile generation, and color-management setting that requested it. Reject
  stale results after navigation, profile/monitor changes, toggles, or close.
- Preserve the last valid visible image while a new profile-specific result is
  prepared. A missing profile or transform error must not blank the viewer or
  make the browser unusable.
- Apply conversion once. Do not double-transform through WIC, D2D, or a
  display-surface recovery path. Preserve alpha and avoid transforming
  premultiplied color channels as if they were straight-alpha pixels.
- Do not change file metadata, ratings/tags, orientation, browser sorting, or
  source image bytes as a side effect of display conversion.
- Keep the no-cloud-Actions hold above. Preserve all existing worktree changes;
  do not clean, stash, reset, commit, or push.

## Phase 0: Inventory and Local Gate

1. Read `.github/copilot-instructions.md`, applicable C++ instructions,
   `docs/architecture.md`, `docs/testing.md`, `specs/PRODUCT_SPEC.md`, the B4
   roadmap entry, and this prompt.
2. Record branch, HEAD, dirty files, build directories, and current Debug test
   status. Preserve unrelated user changes.
3. Trace every displayed-pixel path: WIC thumbnail decode, full-size WIC
   decode, LibRaw output, optional nvJPEG output, browser thumbnail cache and
   D2D upload, viewer full-image cache and D2D upload, and compare-tile render.
4. Trace window-to-monitor identity for the main window and every viewer.
   Inspect `WM_MOVE`/`WM_WINDOWPOSCHANGED`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`,
   and existing display-surface recovery. Identify which notification or
   monitor comparison detects a move between equal-DPI displays.
5. Trace current global/per-window setting persistence and application to
   already-open MainWindow and ViewerWindow instances. Inspect menu state,
   custom accessibility state, and all existing viewer input bindings.
6. Identify a testable seam for monitor profile lookup and profile changes.
   Do not depend on the machine's current monitor profile for deterministic
   automated tests.

**Gate:** Before editing behavior, write down the source-profile rules,
monitor-profile lookup/API semantics, default and fallback behavior, affected
surfaces, and cache/result identity rules. Confirm the chosen Windows profile
API really returns the profile associated with the monitor displaying the
window, including current-user versus system association behavior.

## Phase 1: Lock the Color Contract

Use this contract unless source evidence or an explicit product decision
requires a documented change:

- Color management is **enabled by default**. Add a checked **View > Color
  Management** toggle that immediately changes all open browser and viewer
  surfaces and persists through the existing settings boundary. The toggle is
  the opt-out for users who prefer the current path's speed or behavior.
- Apply the path consistently to browser thumbnails, the single-image viewer,
  and all compare tiles. Do not color-manage UI chrome, overlay text, icons, or
  metadata. If the current architecture cannot support a surface without
  violating cache/threading boundaries, stop at the gate and document the
  smallest compatible scope adjustment instead of silently leaving that
  surface unconverted.
- Use an embedded image ICC profile when one is present, valid, and available
  through the source decoder. Treat an image with no usable embedded profile
  as sRGB. A malformed or unsupported profile falls back to the untransformed
  current path and is reported through existing diagnostics/logging without
  a per-image modal dialog.
- Resolve the destination profile for the monitor currently displaying each
  relevant HWND. Do not use the primary monitor, owner window, or a process-wide
  profile as a substitute when the image window is elsewhere.
- Use WIC color contexts and `IWICColorTransform` for supported WIC sources.
  Establish the correct transform order relative to orientation, scaling, and
  conversion to PBGRA. Preserve alpha and ensure profile conversion precedes
  premultiplication or otherwise uses a WIC pixel format with correct alpha
  semantics.
- For LibRaw and nvJPEG results, determine whether a trustworthy source color
  profile is available. Use it when the decoder provides one. Otherwise apply
  the documented sRGB fallback exactly once; do not infer a camera profile or
  apply an unrelated monitor transform twice.
- Refresh profile-specific output when the HWND moves to another monitor,
  Windows changes the relevant display/profile association, the feature is
  toggled, or the display surface is recreated. Do not rely solely on
  `WM_DPICHANGED`.
- A lookup, profile parse, or conversion failure must preserve current
  usability. Retain the last valid rendering or show the existing image while
  falling back to the current untransformed display path, and record enough
  diagnostics to distinguish unavailable profile from transform failure.
- Color management is an SDR display-conversion feature. HDR tone mapping,
  soft proofing, gamut warnings, print output, and user-selected working
  spaces remain out of scope unless separately approved.

**Gate:** Document this contract in user-facing terms before implementation.
Confirm the menu's checked state, persistence scope, sRGB fallback, alpha
handling, RAW/nvJPEG behavior, and multi-window/multi-monitor semantics.

## Phase 2: Profile and Transform Ownership

1. Add the smallest suitable immutable profile value containing the resolved
   destination profile bytes or stable profile source, a stable identity, and
   a generation/version for invalidation. Avoid retaining an HMONITOR or HWND
   as a cache identity; handles can be reused.
2. Isolate profile resolution behind an injectable/testable boundary. Resolve
   the monitor from each rendering HWND and obtain its current WCS/ICM profile
   using the verified API. Handle no association, inaccessible profile files,
   invalid ICC data, and profile changes explicitly.
3. Keep the canonical shared decode/cache result independent of a destination
   profile. Choose a display-conversion boundary that can serve browser and
   viewer surfaces without putting WIC work on the UI thread. If converted
   pixels are cached, bound their memory and include source identity, profile
   identity/generation, and transform mode in the key.
4. Reuse the existing async decode/executor and result-posting paths where
   practical. Capture immutable input/profile state before dispatch. Validate
   image identity, profile generation, setting generation, and window/session
   lifetime before publishing results.
5. Make toggling off restore the current non-color-managed rendering, and
   toggling on refresh every open surface. Keep prior valid pixels visible
   during conversion and cancel or discard obsolete work.
6. Ensure each browser window and viewer can use a different destination
   profile concurrently. Shared settings may be global; transformed render
   state remains per destination surface.

**Gate:** Tests prove no display-specific pixels leak through the shared
source cache, no stale transform can replace a newer image/profile result, and
multiple windows can render the same source for different profiles.

## Phase 3: UI, Display Changes, and Accessibility

1. Add the **View > Color Management** command with checked state and a stable
   command ID. Follow existing menu and command-bar ownership; do not create a
   parallel settings system.
2. Persist the enabled state using the existing registry/settings APIs with a
   safe default for missing or invalid values. Apply changes to existing
   browser and viewer windows as well as newly opened windows.
3. Detect the monitor displaying each image surface after window movement,
   display topology changes, and relevant profile-association changes. Avoid
   expensive profile I/O in the window procedure; schedule profile resolution
   and pixel conversion asynchronously.
4. Invalidate only display-specific representations when a profile changes.
   Preserve canonical decoded images, selection, navigation, compare tile
   identity, zoom/pan state, and current visible pixels while replacement
   output is prepared.
5. Expose the toggle's accessible name, role, checked state, and state-change
   notification through the existing accessibility path. Do not add an
   undocumented keyboard shortcut.
6. Log profile selection/fallback and conversion failures with enough context
   for diagnosis without logging full profile contents or image pixels.

**Gate:** Toggling applies consistently across all open windows. Moving one
viewer between displays updates only that window's rendered colors and does
not alter another viewer on its previous monitor.

## Phase 4: Focused Tests and Documentation

Add focused local coverage for:

- Default-on setting, persistence round-trip, invalid persisted values, menu
  checked state, and immediate propagation to open windows.
- Monitor-to-profile resolution using an injected deterministic provider,
  including equal-DPI monitors with distinct profile identities and profile
  replacement while a window remains open.
- Embedded valid source ICC profile, untagged sRGB fallback, malformed or
  unsupported profile fallback, transform failure, and diagnostic reporting.
- WIC color conversion with stable expected-pixel tolerances, preserving alpha
  and orientation. Include a small generated or explicitly licensed fixture;
  do not rely on whichever profile is installed on the test machine.
- Browser thumbnail, normal viewer, and two-/three-/four-tile compare paths.
  Verify a source image shared by windows on different displays receives
  distinct display conversion without contaminating shared decoded pixels.
- LibRaw and optional nvJPEG source-profile/fallback behavior, with tests that
  do not require a supported GPU or installed optional codecs.
- Toggle off/on, monitor move, profile change, resize/display recovery, rapid
  navigation, stale async completion, and close during profile resolution.
- Bounded display-cache behavior and invalidation without evicting or
  corrupting canonical image data.
- No regression to current output when color management is disabled.

Update `specs/FUTURE-ROADMAP.md`, `specs/PRODUCT_SPEC.md`, `docs/user-guide.html`,
`docs/architecture.md`, and `docs/testing.md` as appropriate. Document the
default, toggle location, profile fallback, supported decode paths, and known
limitations. Keep menu text, accessible state, and documentation consistent.

**Gate:** Automated tests do not require a particular monitor, ICC association,
GPU, or network access. All decode backends have an explicit tested or
 documented fallback contract.

## Phase 5: Local Validation and Manual Review

- Configure/build and run focused tests in the normal Debug tree; run the full
  Debug CTest preset.
- Build the Release application and run the full Release CTest preset.
- Do not use the packaging target as a routine validation command.
- Compare enabled/disabled output using a known color-profile fixture and
  obtain pixel-level checks with tolerances appropriate to WIC conversion.
- When available, manually review one sRGB display and one display with a
  distinct wide-gamut or calibrated profile. Move both the main window and
  viewer between displays; include two viewers on different monitors.
- Review at 100%, 150%, and 200% display scaling where available. DPI is not a
  substitute for validating distinct monitor profiles.
- Exercise missing/invalid profile behavior and verify that display changes do
  not blank the current image or block input.
- Do not trigger GitHub Actions. If suitable second-display or profile
  hardware is unavailable, report that manual gate as unverified.

## Completion Criteria

B4 is complete only when:

- Color management is enabled by default, can be toggled off from View, and
  persists through the existing settings owner.
- Every in-scope image surface uses the destination profile of its own monitor
  or a documented, tested fallback.
- WIC source profiles are transformed correctly; untagged, malformed, and
  unsupported-profile inputs follow the explicit fallback contract.
- RAW/nvJPEG behavior is explicit and does not double-transform pixels.
- Source-oriented shared cache entries remain profile-independent; async
  transform results are rejected when image, setting, profile, or window
  identity becomes stale.
- Monitor moves and profile changes refresh affected render output without
  resetting navigation/compare state, blanking valid content, or blocking UI.
- Focused tests, local Debug/Release build and CTest gates, documentation, and
  available manual display review are complete, with unverified hardware gates
  reported plainly.
- No cloud workflow was enabled or dispatched and no unrelated work was
  included.
