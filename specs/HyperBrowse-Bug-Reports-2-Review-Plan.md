# HyperBrowse Bug Report 2: Status Review and Follow-up Plan

Reviewed: 2026-11-15 (follow-up status refresh; initial review 2026-09-26)
Source: [HyperBrowse-Bug-Reports 2.md](HyperBrowse-Bug-Reports%202.md)

The initial review compared the report with the source, settings UI, user documentation, and smoke tests; it made no implementation or source-document changes. This refresh records the owner-approved item 40 label correction and completion of items 43 and 46. Item numbers are preserved.

## Summary

Most entries are implemented or withdrawn. The remaining implementation/documentation follow-ups are items 15, 23, 30, 33, 36, and 48. Items 24 and 40 are complete, as are items 43 and 46. The newly reviewed tail, items 58-72, is largely addressed in current source; runtime verification remains for DPI scaling (61) and WebP decoding (69). The original CTest failures below remain historical findings pending a current full-suite recheck.

## Verification

The initial CMake Tools CTest suite was run against the existing `build` output, which was not rebuilt. 23 of 25 tests passed. No additional test run was performed while reviewing items 58-72; those statuses below are based on current source and focused policy-test inspection.

`HyperBrowseSmoke` and `HyperBrowseViewerInteractionSmoke` both failed with `Viewer full metadata pane remained visible when full metadata was disabled`. In the failing scenario, `IsFullMetadataVisible()` already reports false before the test compares a single screen pixel. The implementation clears metadata and requests a repaint, so determine whether this is a rendering defect or an ambiguous pixel fixture before changing behavior. `HyperBrowseSettingsSmoke`, `HyperBrowseMultiViewerSettingsSmoke`, `HyperBrowseViewerFitSmoke`, `HyperBrowseFileRenameSmoke`, and `HyperBrowseItemNumberNavigationSmoke` passed.

Follow-up validation on 2026-11-15: `cmake --build --preset debug --target HyperBrowse HyperBrowseTests` succeeded. `ctest --preset debug-tests -R RuntimePolicySmoke --output-on-failure` passed 1/1 for viewer `Ctrl+I`; `build\tests\Debug\HyperBrowseTests.exe --viewer-settings` passed for Escape persistence/defaults; and `ctest --preset debug-tests -R HyperBrowseViewerFitSmoke --output-on-failure` passed 1/1 for Escape behavior. `docs/MainWindow.PNG` was refreshed from the running app with the nature example set. The full CTest suite was not re-run.

## Per-Item Status

| Items | Status | Notes |
| --- | --- | --- |
| 1-6 | Complete | Consolidated Settings, corrected viewer pan and H behavior, confirmation, remembered destination, and Quick Actions naming are present. |
| 7 | Withdrawn / complete | The requested folder-tree toggle existed; its label now uses Quick Actions. |
| 8-9 | Complete | The image-information UI is custom and theme-aware; the shortcut catalog and README/user guide cover the reported keys, including viewer `Ctrl+I` after item 43 was completed. |
| 10 | Retired | Removed from the report. |
| 11 | Complete | The details pane extracts a prompt and exposes Copy Prompt when present. |
| 12-14 | Complete | Startup viewer target is retained through enumeration; rotated full-resolution decode materializes the frame before the rotator; Quick Actions mutations reload/save under a named mutex. |
| 15 | Partial | Printable punctuation and shifted printable symbols are supported, but assignments are one character and do not represent arbitrary Ctrl/Alt modifier chords. |
| 16-22 | Complete | Startup theme setup, hidden-folder test ordering, monitor-bounded FitWidth behavior, menu mnemonics, rename-path fallback, and Tab discoverability are present. The local rename smoke passed; hosted CI was not rerun. |
| 23 | Partial / needs visual confirmation | The consolidated Settings combo is owner-drawn. A legacy slideshow dialog still creates a native combo; its list colors are themed, but the closed field and drop button need dark-theme verification. |
| 24 | Complete | Missing settings now default to Exit Full Screen; Escape still closes a windowed viewer, and persisted choices such as Close remain unchanged. The fullscreen interaction smoke passed. |
| 25-26 | Complete | Fullscreen refits the image; opening a viewer explicitly foregrounds it. |
| 27 | Retired | Removed from the report. |
| 28-29 | Complete | Additional main windows are cascaded; disabling main-window Escape makes Escape a no-op. |
| 30 | Partial validation | Windowed/full-screen metadata preferences are split, but the visual smoke assertion currently fails in two tests. |
| 31-32 | Complete | Smoke settings use a per-process registry path; `Ctrl+Enter` toggles viewer fullscreen. |
| 33 | Open wishlist | No full-screen indicator for a file already in the default destination was found. |
| 34 | Complete | Successful viewer Quick Sends avoid rebuilding/resyncing the viewer item list. |
| 35 | Retired | Number retired. |
| 36 | Partial | The second full-tree walk was replaced by a local child scan, but locating the parent still recursively walks the tree once per insertion. |
| 37-39 | Complete | Copy/move suppress the shell progress dialog; mouse and arrow panning settle smooth zoom first. |
| 40 | Complete | The owner confirmed the setting should opt manual navigation into the selected slideshow transition. Its Viewer Settings label now says so; the default and slideshow behavior are unchanged. Existing policy tests cover the slideshow/manual transition combinations. |
| 41-42 | Complete | Fast navigation repaints the overlay synchronously; item-number prompts exist in browser and viewer. |
| 43 | Complete | Viewer-context `Ctrl+I` is in the shared shortcut catalog, and the runtime catalog smoke asserts its command, context, and chord. |
| 44 | Complete | New Folder is in File and browser context menus and has `Ctrl+Shift+N`. |
| 45 | Retired | Withdrawn; number retired. |
| 46 | Complete | README documents the configurable Quick Actions assignment order, its `7890-=123456` default, and the fallback order for remaining keys. |
| 47 | Complete | Opening into a new viewer window is supported, with per-window viewer actions. |
| 48 | Partial | The cache index now uses a journal, but `Store` still holds the shared filesystem mutex across bitmap extraction and cache-file I/O. |
| 49 | Complete | Thumb tracking schedules visible thumbnail work as the thumb moves. |
| 50 | Retired | Number retired. |
| 51 | Complete implementation; see item 30 | Settings update viewer defaults while no viewer is open; the current visual-test failure is tracked under item 30. |
| 52 | Complete | Text-input accelerator bypass allows punctuation and edit keys to reach main-window text fields. |
| 53-57 | Complete | PackageRelease locates Visual Studio's CMake; viewer arrow panning settles smooth zoom; Quick Actions confirms success; command-bar menus track pointer movement; fit-mode reapplication preserves window position. |
| 58 | Complete | Viewer synchronization filters directory rows; `ViewerSynchronizer` policy coverage includes mixed image/directory lists. |
| 59 | Complete | `BrowserItemScopeCollector` excludes directories in selection and whole-folder scopes; smoke-policy tests cover both. |
| 60 | Complete | Smoke settings use a per-process registry key and a scoped cleanup object removes it when the test exits. |
| 61 | Implemented; runtime check pending | Settings dimensions and fonts use DPI-aware scaling and the dialog handles `WM_DPICHANGED`; verify at 150/200% and while moving between mixed-DPI monitors. |
| 62 | Complete | Both WIC decoders retain each `Initialize` HRESULT and pass through the bitmap-allocation result instead of reporting stale success. |
| 63 | Complete | Consolidated Settings handles Escape, Enter, and Ctrl+Tab only when the message targets that dialog or one of its children. |
| 64-65 | Complete | Text-control navigation keys bypass browser navigation; `BrowserPane::HandleNavigationKey` declines `WM_SYSKEYDOWN`, preserving Alt-arrow accelerators. Smoke coverage checks that Alt arrows are not consumed as browser navigation. |
| 66 | Complete | Keep-in-notification-area is an option gated by single-instance mode; close hides to the tray, whose Open/Exit commands restore or quit. |
| 67 | Complete | Persistent cache index and journal use explicit UTF-8 encoding and decoding rather than locale-dependent wide streams. |
| 68 | Complete | A finished decode is inserted into memory cache and queued for disk storage even when its viewport request is stale; only the UI-ready notification is epoch-gated. |
| 69 | Implemented; runtime check pending | WebP is included in the supported WIC file types; successful decoding still depends on an installed WIC WebP codec. Verify listing, thumbnailing, and opening with a representative file. |
| 70 | Complete | Recovery retries snapshot the viewer HWNDs present when the retry series starts and skip viewers opened afterward. |
| 71 | Complete | External drag handling stores the data object in a WRL `ComPtr` and clears it on DragLeave or Drop. |
| 72 | Complete | Clipboard error paths free the preferred-effect allocation unless ownership transferred successfully to the clipboard. |

## Follow-up Decisions and Remaining Work

1. **Triage the metadata rendering test failure (items 30/51).** Inspect the full-metadata draw path and the single-pixel assertion in `../tests/smoke.cpp#L4821`. Establish a test fixture where the enabled and disabled states must produce distinguishable pixels, then make the relevant smoke tests pass without weakening state coverage.

2. **Resolved 2026-11-15 — item 40 label.** The owner wants the existing opt-in to apply the selected slideshow style and duration during manual navigation. The Viewer Settings label now describes that behavior. The preference remains off by default, slideshow playback remains independent, and existing `ViewerTransitionPolicy` tests cover both paths.

3. **Reduce persistent-cache write contention (item 48).** `../src/cache/DiskThumbnailCache.cpp#L793` acquires the process-wide filesystem mutex before bitmap extraction and retains it through file writing. Narrow serialization to cache-index/journal coordination, preserve collision and eviction safety, then benchmark one and eight writers at representative cache sizes.

4. **Resolve Quick Actions chord scope (item 15).** Confirm whether the request includes Ctrl/Alt chords beyond printable and shifted printable characters. If so, represent virtual keys and modifiers explicitly, update assignment capture and popup dispatch, and test conflicts with application shortcuts. Current docs explicitly exclude control and navigation keys.

5. **Resolved 2026-11-15 — item 24.** The default is now Exit Full Screen; windowed Escape still closes the viewer. Existing persisted choices are preserved. Persistence/default and fullscreen interaction tests passed.

6. **Finish combo-box dark theming (item 23).** Verify the closed field, list, text, and drop button for every reachable combo in dark mode, including the legacy slideshow dialog. Use a consistently themed control or remove the legacy path if it is no longer supported.

7. **Measure the remaining folder-tree cost (item 36).** The current path performs one recursive `FindItemByPath` and then scans only the found parent's children. Benchmark file-operation completion with a deeply expanded tree; optimize further only if the remaining traversal reproduces meaningful latency.

8. **Complete 2026-11-15 — item 43.** Added the viewer-context `Ctrl+I` entry to `../src/ui/ShortcutCatalog.h` and a runtime smoke assertion. Help > Keyboard Shortcuts reads the shared viewer catalog.

9. **Complete 2026-11-15 — item 46.** Updated `../README.md` to describe the configurable Quick Actions order, the default `7890-=123456`, and fallback order.

10. **Defer the duplicate-destination indicator (item 33).** This remains an unimplemented wishlist request. If approved later, compare the current file with the default destination by name and size, show the destination key/path in fullscreen, and cover same-name/different-size files and completed move/copy operations.

11. **Verify Settings DPI scaling (item 61).** Exercise the consolidated Settings dialog at 150% and 200%, then move it between monitors with different scale factors. Confirm that the frame, tabs, custom text, and native controls resize together and remain usable.

12. **Verify WebP end to end (item 69).** On Windows with the Webp Image Extension installed, test browser enumeration, thumbnail decode, command-line/Explorer launch, and viewer decode using a representative `.webp` file. Keep codec availability as an explicit runtime prerequisite.

## External Validation Notes

- Item 13's decode path now uses `WICBitmapCacheOnLoad` before a dimension-swapping rotation, addressing the repeated-streaming-decode mechanism. The reported full-size sample is not in the workspace, so its original timing was not reproduced.
- Item 21's rename callback now derives the created path from the source parent and requested new name when the shell omits `newlyCreated`; the local rename smoke passed. A GitHub-hosted runner was not available during this review.

No follow-up implementation is authorized by this plan alone. Confirm the desired scope before any source or documentation changes.
