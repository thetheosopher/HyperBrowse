# HyperBrowse

![Version](https://img.shields.io/badge/Version-2.2.0-2EA043)
![Windows](https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011-0078D6)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)
![CMake](https://img.shields.io/badge/CMake-3.23%2B-064F8C)
![License](https://img.shields.io/badge/License-MIT-blue)
![Status](https://img.shields.io/badge/Status-Release-2EA043)

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-Support-FFDD00?logo=buy-me-a-coffee&logoColor=000)](https://buymeacoffee.com/theosopher)

HyperBrowse is a native Windows image browser and viewer focused on fast folder navigation, responsive thumbnail browsing, quick full-image viewing, and practical desktop workflows. It is intentionally a browser/viewer first, not a general-purpose editor.

## Main Window

![HyperBrowse main window](docs/MainWindow.PNG)

## Highlights

- Native Win32 desktop application built with CMake and modern C++20.
- Direct2D and DirectWrite rendering in the browser and viewer, with per-monitor DPI awareness v2.
- Asynchronous folder enumeration, folder tree loading, metadata extraction, folder watching, and thumbnail scheduling.
- WIC baseline decode path, LibRaw-based RAW support, and optional nvJPEG acceleration with runtime fallback.
- Thumbnail and details modes, optional Explorer-style subfolder entries, recursive browsing, sorting, filename/rating/tag filtering, thumbnail ratings, and multi-selection workflows.
- Full-screen viewer with zoom, pan, rotate, edge-hover previous/next navigation, side-by-side compare, scalable info overlays, current-folder slideshow launch, full metadata pane, adjacent-image prefetch, and multiple independent viewer windows within one HyperBrowse instance.
- Performance profiles (Conservative, Balanced, Performance, and Aggressive) with adaptive cache sizing and configurable 1-16 item lookahead; Auto follows the active profile and memory pressure reduces speculative work.
- Quick Actions with saved destinations, persistent key assignments, F4 filing-position resume, F7 move, and F8 copy for the currently displayed image or selected browser files.
- Persistent thumbnail cache statistics, compact/purge maintenance actions, and safer remembered window/folder restore on startup.
- Expanded slideshow system with richer transition controls, keyboard shortcut access, and effect-backed Direct2D transition styles.
- Consolidated tabbed Settings dialog covering slideshow, viewer, appearance, performance, and behavior preferences with Apply, OK, and Cancel workflow; open it with Ctrl+Shift+T.
- Folder tree workflow upgrades with validated folder moves, inline folder creation, Quick Actions destinations, back-navigation history, a toolbar back button, and image drag-and-drop from the browser into tree folders or shell-aware apps such as File Explorer.
- Selected thumbnails and details rows can be dragged to File Explorer, mail clients, and other shell-aware applications using native Windows file-drop data.
- Keyboard navigation now covers browser and viewer workflows with consistent focus, selection, and folder-history behavior.
- Display changes and graphics-surface loss trigger redraw and resource recovery across the main window, viewer, diagnostics window, and thumbnail pipeline.
- Files dropped onto the application can open directly in the viewer or be copied into the current folder through the existing shell-aware workflows.
- Portable and installer packaging outputs, plus smoke-tested release packaging targets.
- A committed Windows CI workflow that builds Debug and Release, runs CTest and startup-budget checks, validates release manifests, and publishes build artifacts.
- An offline HTML user guide available from Help > User Guide or by pressing F1.

## What's New In 2.2.0

This release expands HyperBrowse's image-review and desktop file-management workflows while tightening the background services that keep large folders responsive.

- Added multiple independent viewer windows, side-by-side comparison, viewer-specific settings, smooth zoom completion, improved fit-width navigation, and more reliable transition behavior when replacing images asynchronously.
- Added Image Information, Copy Image Pixels, and JPEG orientation commands through a bounded background service with cancellation and safe UI-thread completion.
- Added keyboard accessibility improvements across the main window, command bar, text input, focus handling, shortcut routing, and screen-reader-facing action semantics.
- Added Quick Actions shortcut ordering and normalization, filing-position resume, saved destination persistence, richer completion confirmation, and consistent move/copy behavior for paired RAW and JPEG files.
- Added new-folder and rename workflows, clipboard copy/cut/paste, duplicate selection, shell context-menu integration, native drag-and-drop feedback, drag-out to Explorer and other shell-aware applications, and undo/redo for supported file operations.
- Added details-panel RGB histograms, richer metadata prompts and reports, expanded image information commands, and improved paired RAW/JPEG candidate resolution.
- Improved folder enumeration and folder-watch coalescing, incremental large-folder updates, visible-thumbnail prioritization, adaptive prefetching, bounded background executors, cancellation, and shutdown handling.
- Improved persistent thumbnail caching with journal-based updates, cache maintenance actions, safer invalidation, corrupt-entry cleanup, and validation that cached thumbnails retain source dimensions so the thumbnail footer does not intermittently fall back to `...`.
- Fixed stale render-cache reuse after deletions, viewer focus loss after shell operations, delayed viewer refreshes after successful deletes, folder reload races, RAW failure presentation, metadata persistence and remapping edge cases, and several file-operation cancellation and shutdown paths.
- Improved release packaging and CI validation with capability-derived manifests, portable and installer content checks, dependency notices, version checks, artifact hashes, startup-budget checks, WIC-only fallback coverage, and optional CUDA/nvJPEG shipping validation.

## Current Capabilities

| Area | Included today |
| --- | --- |
| Browser | Explorer-style folder tree, resizable splitter, thumbnail mode, details mode, recursive browsing, live filename/rating/tag filter, thumbnail detail toggle with inline star ratings, selected-item info strip, remembered window/folder restore, back-folder history, and folder context workflows for create/rename/delete plus favorite-aware move destinations, in-tree folder drag-drop move, image drag-drop into tree folders, and drag-out to shell-aware apps |
| Viewer | Separate viewer windows within one HyperBrowse instance, normal Open reuse, explicit Open in New Viewer Window, full-screen open, side-by-side compare, zoom, pan, fit-to-window, 100% view, rotate, edge-hover/click previous-next navigation, overlay HUD with size presets, full metadata pane, slideshow with current-folder launch from the active image, transition styles, and multi-monitor open |
| Formats | JPEG, PNG, GIF, TIFF via WIC; RAW support for ARW, CR2, CR3, DNG, NEF, NRW, RAF, and RW2 via LibRaw |
| File workflows | Open, reveal in Explorer, open containing folder, copy path, copy/move/delete, multi-file Properties, tags and ratings, EXIF-only JPEG orientation adjustment, and batch convert to JPEG/PNG/TIFF |
| Performance pipeline | Prioritized thumbnail scheduling, profile-scaled browser/viewer lookahead, memory-bounded thumbnail cache, persistent disk thumbnail cache with stats/compact/purge, metadata cache, folder watch refresh, and optional GPU-assisted JPEG decode |
| Distribution | Debug and Release presets, smoke tests, startup-budget checks, portable layout, installer layout, zipped portable release, Inno Setup 6 installer with per-user or per-machine install mode, and Windows CI artifact validation |

### Viewer Quick Actions

Quick Actions supports saved destinations with one-character shortcuts from digits, letters, and supported printable punctuation. Each newly added destination is automatically assigned the lowest available key in `0` through `9`, then `A` through `Z`, followed by punctuation; its key field accepts one supported character and can be edited later. Assignments persist by folder path and remain associated with the same destination when the list is reordered. Recent folders are not included.

In the viewer, press `F7` to move the currently displayed image to a selected favorite or `F8` to copy it. If the image has a paired RAW or JPEG companion, the companion is included in the same operation. With files selected in the main window, the same shortcuts open the chooser for moving or copying the selection. The destination chooser can be dismissed with `Escape`, by clicking outside it, or by making no selection. A successful move advances the viewer; a copy leaves the current image displayed. Press `F4` in the main window to restore the most recently recorded filing position for the current folder. Positions follow renamed or moved folders and keep up to 64 folders; the target must still be present in the current view.

The filter field accepts ordinary filename terms plus `tag:value`, `tags:value`, `rating:rated`, `rating:unrated`, and numeric rating constraints such as `rating:4`, `rating:>=3`, or `rating:<2`. Whitespace-separated terms are combined, and tag matching is case-insensitive.

### Multiple viewer windows

Opening an image normally reuses the existing viewer window, replacing its image collection. To compare unrelated images or keep separate reviews open side by side, select an image and choose **File > Open in New Viewer Window**, use the same command from the browser context menu, or press `Ctrl+Shift+Enter`. All viewer windows belong to the same HyperBrowse instance, so they share browser state and Quick Actions destinations. Viewer commands such as Delete, Quick Actions, image information, and context-menu actions apply to the window that received the input.

## Keyboard shortcuts

The same catalogue is available in the application from Help > Keyboard Shortcuts. Shortcuts are listed by the window that has focus.

### Main window

| Shortcut | Action |
| --- | --- |
| `F1` | Open the user guide |
| `Ctrl+O` | Open a folder |
| Mouse Back button / `Backspace` / `Alt+Left` | Navigate to the previous folder |
| Mouse Forward button / `Alt+Right` | Navigate to the next folder |
| `Esc` | Close the main window when enabled in Settings; otherwise do nothing |
| `Ctrl+W` | Minimize the main window |
| `F5` | Refresh the folder tree |
| `F2` | Rename the selected item |
| `Ctrl+Shift+Enter` | Open the selected image in a new viewer window |
| `F7` / `F8` | Move / copy the selection to a Quick Actions destination |
| `F4` | Resume the saved filing position in the current folder |
| `Ctrl+I` | Show image information |
| `Ctrl+C` / `Ctrl+X` | Copy / cut selected files |
| `Ctrl+Shift+C` | Copy selected paths |
| `Ctrl+Shift+I` | Copy displayed image pixels |
| `Ctrl+V` | Paste files into the current folder |
| `Ctrl+A` | Select all items |
| `Ctrl+D` | Duplicate selected files |
| `Ctrl+G` | Go to a file by its number |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo the last supported file operation |
| `Ctrl+E` | Reveal the selection in Explorer |
| `Alt+Enter` | Show file properties |
| `Del` / `Shift+Del` | Move the selection to the Recycle Bin / delete it permanently |
| `Ctrl+1` / `Ctrl+2` | Use thumbnail / details mode |
| `Ctrl+3` | Toggle the details panel |
| `Ctrl+R` | Toggle recursive browsing |
| `+` / `=` / Numpad `+` | Increase thumbnail size |
| `-` / `_` / Numpad `-` | Decrease thumbnail size |
| `Ctrl+Shift+S` / `Ctrl+Shift+F` | Start a slideshow from the selection / current folder |
| `Ctrl+Shift+T` | Open Settings |
| `Ctrl+Shift+D` / `Ctrl+Shift+X` | Capture a diagnostics snapshot / reset diagnostics |

### Viewer

| Shortcut | Action |
| --- | --- |
| `Esc` | In full screen, use the Viewer Settings action: close, fit width, fit height, or actual size; when windowed, close the viewer |
| `Ctrl+W` | Close the viewer |
| Arrow keys | Navigate, or pan when zoomed |
| `Shift` + Arrow keys | Navigate the comparison pair |
| `Page Up` / `Page Down` | Navigate to the previous / next image |
| `Ctrl+Home` / `Ctrl+End` | Go to the first / last image |
| `Ctrl+G` | Go to an image by its number |
| `Ctrl+Shift+F` | Start a slideshow from the current folder |
| `F7` / `F8` | Move / copy the displayed image to a Quick Actions destination |
| `Ctrl+I` | Show image information |
| `Ctrl+Shift+I` | Copy the displayed image |
| `Tab` | Toggle image information overlays |
| `+` / `=` / Numpad `+` | Zoom in |
| `-` / `_` / Numpad `-` | Zoom out |
| `Enter` | Toggle between fit and actual-size viewing |
| `0` / `1` | Fit the image to the window / show it at actual size |
| `H` / `W` | Fit the image to the window height / width |
| `Ctrl+Shift+H` / `Ctrl+Shift+W` | Size the window to the monitor work-area height / width (windowed mode only; no effect in full-screen mode) |
| `L` / `R` | Rotate the image left / right |
| `C` | Toggle comparison mode |
| `X` | Activate the compared image |
| `Space` | Start or stop the slideshow |
| `F11` / `Ctrl+Enter` | Toggle full-screen mode |
| `Del` / `Shift+Del` | Move the displayed file to the Recycle Bin / delete it permanently |

## Architecture Overview

HyperBrowse is organized as a native desktop app with a shared core library and a small helper toolchain around it:

- `HyperBrowseCore` contains the browser, viewer, decode, render, service, and utility code shared by the app and the smoke tests.
- `HyperBrowse.exe` is the main Win32 desktop application.
- `HyperBrowseRawHelper.exe` provides the optional out-of-process RAW decode path.
- `HyperBrowseTests.exe` is the smoke and integration test harness.

The current implementation combines a Win32 shell with Direct2D and DirectWrite presentation, asynchronous services, and bounded in-memory caches. The main decode chain is:

1. nvJPEG for the optional accelerated JPEG path when available.
2. WIC for standard formats and safe fallback behavior.
3. LibRaw for supported RAW formats, either in-process or through the helper executable.

## Build Requirements

- Windows 10 or Windows 11, x64.
- Visual Studio 2026 Build Tools or Visual Studio 2026 with Desktop development for C++.
- CMake 4.2 or newer for the bundled Visual Studio 2026 presets. Manual generator builds support the project minimum of CMake 3.23. The CMake bundled with Visual Studio or the Visual Studio Build Tools is supported; it does not need to be on `PATH`.
- PowerShell and Inno Setup 6 for release packaging.
- Optional internet access when `HYPERBROWSE_BUNDLE_CUDA_REDIST=ON`, because CMake downloads NVIDIA redistributables for packaging.

## Build

### Recommended: CMake presets

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
ctest --preset debug-tests
```

For a Release build:

```powershell
cmake --build --preset release
ctest --preset release-tests
```

Smoke tests select a per-process registry subkey under `HKCU\Software\HyperBrowse\SmokeTests`, remove it when the test process exits, and therefore do not read or modify the installed application's preferences. The application normally uses `HKCU\Software\HyperBrowse`; for an isolated manual run, set `HYPERBROWSE_SETTINGS_REGISTRY_PATH` to a registry subkey path before launching it:

```powershell
$env:HYPERBROWSE_SETTINGS_REGISTRY_PATH = 'Software\HyperBrowse\ManualTest'
.\build\Debug\HyperBrowse.exe
```

The override is process-inherited and is intended for development and test isolation. It must be a subkey path relative to `HKCU`, without the `HKCU\` prefix.

### Visual Studio generator

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug --target HyperBrowse
```

Launch the `HyperBrowse` startup project from Visual Studio, or run the built executable from the selected configuration output directory.

### Useful configure options

| Option | Default | Purpose |
| --- | --- | --- |
| `HYPERBROWSE_BUILD_TESTS` | `ON` | Build the smoke and integration test suite |
| `HYPERBROWSE_BUILD_FUZZ_TESTS` | `OFF` | Build deterministic cache and RAW-helper boundary fuzz tests |
| `HYPERBROWSE_ENABLE_LIBRAW` | `ON` | Enable vendored LibRaw support and the RAW helper executable |
| `HYPERBROWSE_ENABLE_NVJPEG` | `ON` | Compile the optional nvJPEG acceleration path |
| `HYPERBROWSE_BUNDLE_CUDA_REDIST` | `ON` | Download and stage official NVIDIA runtime redistributables for packaging |
| `HYPERBROWSE_STATIC_MSVC_RUNTIME` | `ON` | Link the MSVC runtime statically to simplify deployment |
| `HYPERBROWSE_INNO_SETUP_COMPILER` | empty | Optional full path to `ISCC.exe` for the release packaging target |
| `HYPERBROWSE_WARNINGS_AS_ERRORS` | `OFF` | Promote compiler warnings to errors |

Examples:

```powershell
cmake --preset vs2026-x64 -DHYPERBROWSE_BUNDLE_CUDA_REDIST=OFF
cmake --preset vs2026-x64 -DHYPERBROWSE_ENABLE_NVJPEG=OFF
```

If nvJPEG is compiled in but the runtime is unavailable on the machine, HyperBrowse falls back to WIC automatically.

## Testing

Run the smoke suite through CTest:

```powershell
ctest --preset debug-tests
ctest --preset release-tests
```

The smoke coverage includes folder enumeration, folder tree enumeration, thumbnail scheduling and caching, WIC decode behavior, LibRaw decode behavior, metadata caching, file operations, batch convert cancellation, browser selection behavior, viewer interaction, and persisted UI state.

The consolidated Settings dialog is available from Tools > Settings or with Ctrl+Shift+T. Its Slideshow, Viewer, Appearance, Performance, and Behavior tabs stage changes until Apply or OK; Cancel and closing the dialog discard uncommitted changes. Performance includes the resource profile, adaptive or explicit cache caps, and Auto or explicit 1-16 item prefetch depth.

The Direct2D/DirectWrite Settings surface is the application Settings dialog. It uses measured layout, a scrollable body, a pinned action footer, native text-entry controls, and the shared DPI/work-area shell.

The release packaging path builds the release binaries, runs the smoke executable, stages both portable and installer layouts, and then emits the zipped portable package plus the Inno Setup installer.

## Packaging

Create the portable layout after building:

```powershell
cmake --install build --config Release --component Portable --prefix build/dist/HyperBrowse-2.2.0-portable
```

Create the installer-friendly staging layout:

```powershell
cmake --install build --config Release --component Runtime --prefix build/dist/HyperBrowse-2.2.0-installer-layout
```

Create the full release artifact set, including a zipped portable package and an Inno Setup 6 installer:

```powershell
cmake --preset vs2026-x64-release-package
cmake --build --preset release-package
```

If you prefer the standalone packaging script, point it at the dedicated packaging build tree:

```powershell
powershell -ExecutionPolicy Bypass -NoProfile -File .\tools\PackageRelease.ps1 -BuildDir .\build-release-package
```

The packaging script can use the CMake and CTest bundled with Visual Studio or the Visual Studio Build Tools. It checks `PATH`, the CMake recorded in the selected build tree, Visual Studio installations discovered with `vswhere.exe`, and known standalone installation locations.

The dedicated release-packaging configure preset keeps the static MSVC runtime enabled, keeps LibRaw linked statically, and keeps CUDA redistributable bundling enabled so the portable zip and installer carry the RAW helper executable plus the nvJPEG runtime DLLs they need. The packaging target runs the release smoke tests, derives its required-file checks from the configured capabilities, validates executable and installer version metadata, verifies the portable archive contents, and writes SHA-256 manifests for each layout and the final artifacts.

The generated installer supports either current-user or all-users installation, writes application and Open With registration in the matching user or machine scope, creates a Start Menu shortcut automatically, and offers an optional desktop shortcut. Uninstall removes registration owned by the installer and retains each user's HyperBrowse preferences by default.

Both the portable package and installer include the offline user guide under `docs/`, the main-window screenshot used by the guide, the project license, a reviewed third-party dependency inventory, and all applicable vendored notices. Installed builds place the application executable under `bin/` and the guide and notices under the neighboring `docs/` directory.

When CUDA redistributable bundling is enabled, CMake downloads the official NVIDIA `cuda_cudart` and `libnvjpeg` redistributable archives, verifies their SHA256 hashes, and stages the runtime DLLs and license files beside the application. That keeps nvJPEG deployment self-contained instead of depending on a machine-wide CUDA install or `PATH` setup.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `src/app` | Application entry point and lifecycle |
| `src/ui` | Main window shell, diagnostics UI, toolbar assets, dialogs |
| `src/browser` | Browser model and browser pane logic |
| `src/viewer` | Full-image viewer window and navigation |
| `src/services` | Async services for enumeration, watching, file ops, metadata, conversion, and scheduling |
| `src/decode` | WIC, nvJPEG, LibRaw, and RAW-helper decode paths |
| `src/render` | Direct2D and DirectWrite rendering helpers |
| `tests` | Smoke and integration-style test coverage |
| `specs` | Product, architecture, UX, performance, and roadmap documents |
| `tools` | Packaging scripts and development utilities |

## Project Documentation

The `specs/` directory tracks both design intent and implementation follow-up. Useful entry points:

- [docs/user-guide.html](docs/user-guide.html) for the practical, user-facing application guide.
- [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow, validation expectations, and pull request checklist.
- [docs/architecture.md](docs/architecture.md) for current component ownership, threading boundaries, data flows, and rendering responsibilities.
- [docs/testing.md](docs/testing.md) for build, smoke-test, benchmark, diagnostics, and manual validation guidance.
- [docs/RELEASE_POLISH_PLAN_2026-09-08.md](docs/RELEASE_POLISH_PLAN_2026-09-08.md) for the 2.2 implementation ledger, validation evidence, and remaining release gates.
- [.github/copilot-instructions.md](.github/copilot-instructions.md) for repository-wide coding guidance used by GitHub Copilot.
- [docs/decisions/README.md](docs/decisions/README.md) for durable architecture decisions and invariants.
- [specs/01-product-spec.md](specs/01-product-spec.md) for product scope and supported workflows.
- [specs/02-architecture.md](specs/02-architecture.md) for subsystem layout and pipeline design.
- [specs/04-ui-behavior.md](specs/04-ui-behavior.md) for the implemented UI contract.
- [specs/15-d2d-rendering-migration.md](specs/15-d2d-rendering-migration.md) for the rendering migration details.
- [specs/16-toolbar-ux-redesign.md](specs/16-toolbar-ux-redesign.md) for current toolbar implementation status.
- [specs/14-todo.md](specs/14-todo.md) and [specs/10-prioritized-enhancements.md](specs/10-prioritized-enhancements.md) for the current backlog.

## Current Scope Boundaries

HyperBrowse is already a capable browser/viewer, but it is still deliberately scoped. Current non-goals or deferred items include:

- Heavy image editing, annotations, cropping, and organizer-style database features.
- Drag-and-drop file operations between multiple HyperBrowse instances.
- Multipage TIFF navigation and animated GIF thumbnails.
- Plugin ecosystems, duplicate finders, face detection, and library/database back ends.

If you want the current backlog in detail, start with [specs/14-todo.md](specs/14-todo.md).

## Version

Current release: **2.2.0**. The version is defined by the top-level `project(HyperBrowse VERSION ...)` call in [CMakeLists.txt](CMakeLists.txt) and flows into the generated build metadata, the Windows version resource, the About dialog, and all release artifact names (for example `HyperBrowse-2.2.0-portable-win64.zip` and `HyperBrowse-2.2.0-installer.exe`).

Release **2.0.0** expands HyperBrowse from a fast image browser into a more complete, resilient desktop workflow while preserving asynchronous browsing and viewing. It adds richer shell integration, safer file operations, single-instance launch forwarding, persistent state and cache improvements, and reproducible Windows release validation.

## Version History

### 2.2.0

- Made ratings and tags durable for Unicode paths and tags, folder and case-only renames, multiple app instances, atomic-save failures, and later retries; failed saves now remain pending and produce a recoverable warning.
- Hardened file-operation undo/redo with result identity checks and authoritative shell mappings, and moved image information, pixel-copy decode, and multi-JPEG orientation work to a cancellable bounded worker.
- Made batch conversion totals consistent, kept cancellation active until the worker drains, and publish converted files atomically without overwriting a newly occupied name.
- Re-enabled multi-viewer Settings coverage, applied slideshow intervals to every active viewer, added semantic Settings accessibility, adopted Windows high-contrast colors, and bounded Quick Actions confirmations at larger text and DPI settings.
- Strengthened package capability checks, distribution notices, installer ownership, startup benchmark isolation, and the 2.2 release validation ledger.

Known limits: GIF and TIFF browsing displays the first frame or page; JPEG orientation adjustment edits EXIF orientation metadata; converted JPEG/PNG/TIFF output is a rendered copy and may not preserve all source metadata, color profiles, animation/pages, or alpha when the target format cannot represent it. Optional nvJPEG acceleration depends on a supported NVIDIA GPU and runtime and otherwise falls back to WIC. Final screen-reader, high-contrast, mixed-DPI, clean-machine installer, and hosted-CI evidence remains recorded in the release polish plan until those external gates are run.

### 2.1.0

- Added a consolidated, themed Settings experience with tabs for slideshow, viewer, appearance, performance, and behavior preferences, Apply/OK/Cancel staging, persisted cache and slideshow values, a Direct2D/DirectWrite presentation path, and an environment-controlled legacy-dialog fallback.
- Expanded Direct2D rendering and resource recovery across the main window and viewer, while improving dialog color handling and text rendering helpers.
- Added viewer Fit Height and expanded fit/window sizing controls, keyboard panning while zoomed, independent full-metadata visibility for windowed and full-screen modes, context-menu toggles for overlays and metadata, configurable full-screen Escape behavior, and improved focus, activation, and fullscreen restoration.
- Renamed Quick Send to Quick Actions, added persistent shortcut and destination state that is safe across multiple instances, remembered the last destination, added confirmation before clearing favorites, and expanded assignments from digits and letters to supported printable punctuation.
- Added a shared shortcut catalog and Help documentation for browser and viewer keyboard workflows, plus command-bar mnemonic support and clearer viewer overlay metrics.
- Improved startup viewer synchronization when a file is opened before a large folder finishes enumerating, and added cascading placement for multiple main-window instances.
- Hardened settings registry access and isolated smoke-test settings so tests do not overwrite the user's application preferences.
- Improved WIC and general image decoding with on-demand caching, EXIF orientation handling, scaled decode paths, safer unexpected-property handling, and detailed thumbnail failure diagnostics.
- Expanded smoke coverage for settings, viewer metadata and fit behavior, startup enumeration, JPEG decoding and orientation, Quick Actions shortcut persistence, and the associated regression fixes.

### 2.0.0

- Added a redesigned command bar and reorganized menus with clearer browser, viewer, slideshow, metadata, organize, convert, and advanced workflows.
- Added native Windows drag-and-drop in both directions: drag selected images to folders, File Explorer, mail clients, or other shell-aware applications, and drop files onto HyperBrowse to open them in the viewer or copy them into the current folder.
- Added clipboard copy and paste for files, image-pixel copying, duplicate-to-same-folder, shell context menus, multi-file Properties, and taskbar progress for long-running file operations.
- Added Quick Actions destinations, recent-folder integration, Quick Actions move/copy shortcuts, folder-history navigation, inline folder rename and creation, guarded folder moves, and improved folder-tree feedback.
- Added bounded undo and redo for copy, move, and rename workflows, with completion-aware history updates that never journal incomplete operations.
- Added viewer context actions, scalable overlay text, full metadata presentation, multi-monitor opening, improved keyboard and focus behavior, persistent slideshow settings, and richer transition controls.
- Added opt-in single-instance launch forwarding through a current-user named pipe, with overlapped shutdown-safe IPC, remote-client rejection, and current-user ACL enforcement.
- Added tray notifications for background operation completion and display/resource recovery across the main window, viewer, diagnostics window, and thumbnail pipeline.
- Hardened asynchronous services and worker boundaries so decode, metadata, enumeration, watching, file operations, conversion, scheduling, and UI-owned failures are contained and reported.
- Hardened RAW-helper and persistent-thumbnail-cache boundaries with checked dimensions and byte counts, exact payload validation, strict index parsing, safe cache paths, atomic index replacement, authoritative in-memory indexing, asynchronous access journaling, and off-UI-thread cache maintenance.
- Improved large-folder and large-selection performance with coalesced enumeration presentation, early result batches, bulk normalized path removal, hashed fallback checks, and asynchronous persistent-cache invalidation.
- Added release packaging manifest checks and committed Windows CI covering Debug and Release builds, CTest, startup benchmark budgets, package generation, portable staging, and installer artifacts.

### 1.2.8

- Added consistent keyboard navigation handling across browser and viewer workflows, including focus and selection behavior.
- Added display-change redraw handling and graphics-surface recovery so windows restore their Direct2D resources after monitor or display changes.
- Added file-drop workflows for opening files in the viewer and copying files into the current folder.

### 1.2.7

- Added an Advanced setting to show immediate subfolders in thumbnail and Details views, with folder rows that navigate into the selected subfolder.
- Added Explorer-style folder rendering with shell folder icons, larger centered folder names, and folders-first ordering.
- Added a toolbar back-arrow button that invokes the same folder-history action as Backspace.

### 1.2.6

- Added native drag-out for selected thumbnails and details rows to File Explorer, mail clients, and other shell-aware applications.
- Preserved existing in-app browser-to-tree and Quick Actions drag workflows while starting external shell drags when the pointer leaves HyperBrowse.
- Initialized OLE on the application thread so Windows `DoDragDrop` provides normal drag feedback and drop effects.

### 1.2.5

- Hardened persistent thumbnail-cache loading with checked dimensions, exact BGRA payload validation, truncated-file detection, and corrupt-entry cleanup.
- Added regression coverage for malformed cache headers, invalid byte counts, truncated payloads, and valid cache round trips.
- Preserved folder rename state across `ReadDirectoryChangesW` completions and added deterministic fallback behavior for orphaned or malformed notifications.

### 1.2.4

- Fixed browser delete refresh anomalies where deleted selections could leave stale thumbnails visible until a manual refresh.
- Removed a UI-thread stall path during delete completion by moving persistent thumbnail-cache invalidation off the main thread.
- Restored viewer keyboard focus after delete operations so navigation and escape handling continue working without an extra click.

### 1.2.3

- Added a new Viewer menu toggle, **Use Slideshow Transition**, so transition effects can be enabled or disabled without changing slideshow settings.
- Changed default viewer image navigation behavior to a simple cut between images for faster, cleaner stepping.
- Preserved existing slideshow transition style and duration settings so they apply immediately when the new toggle is enabled.

### 1.2.2

- Added browser-to-tree image drag-and-drop so selected thumbnails or image rows can be dropped onto folder-tree destinations.
- Added cross-drive drop prompting for those image drags so the app asks whether to copy or move when the destination lives on a different drive.
- Added Quick Actions destination toggling, move-to-new-child-folder workflows, and back-folder navigation history to speed up repeat organization tasks.
- Refreshed the slideshow settings transition list so Random stays pinned at the top while the remaining transition options are easier to scan alphabetically.

### 1.2.1

- Added folder tree drag-and-drop moving with guarded destination validation (same-drive moves only, and protection against moving into self, descendants, or current parent).
- Added New Folder creation from the folder tree context menu with interactive naming, Windows-safe name validation, and immediate tree refresh/insertion behavior.
- Expanded folder tree move workflows with a Move Folder To submenu that supports quick destinations (favorites and recents) and manual folder browsing.
- Aligned release metadata to 1.2.1 across build versioning, README documentation, and artifact naming.

### 1.2.0.0

- Fixed slideshow settings numeric entry behavior so spinner controls and manual values remain parse-safe, while introducing clearer lower bounds of 250 ms for slide duration and 100 ms for transition duration.
- Added the consolidated Settings dialog and moved the `Ctrl+Shift+T` shortcut to its five-tab configuration surface, while preserving the expanded slideshow transition catalog.
- Expanded classic transition options with new directional and wipe-based styles, including Fade to Black, Diagonal Slide, Push, Center Wipe, Venetian Blinds, Split Wipe, Horizontal Blinds, Checkerboard Wipe, and Zoom Fade.
- Added Direct2D effect-backed transition styles: Blur Crossfade, Motion Blur, Color Wash, Sepia Drift, Flashbulb, Prism, and Monochrome Reveal.

### 1.1.0.3

- Improved thumbnail hot-path efficiency by removing redundant normalized path allocations in cache lookup and browser path comparisons.
- Added a one-time debug warning when file logging cannot open its output file.

### 1.1.0.2

- Added viewer overlay text size presets with more prominent lower-right image data so HUD text can be tuned for different display sizes.
- Added `Ctrl+Shift+F` in the viewer to start a current-folder slideshow from the currently displayed image.
- Added an optional translucent full metadata pane in the viewer, including larger metadata text scaling and content-sized panel height that hides and shows with the existing `Tab` overlay toggle.

### 1.1.0.1

- Added viewer mouse-edge previous/next navigation with directional hover cursors and click-to-navigate behavior.
- Narrowed the navigation hit zones and reduced the custom arrow cursor size so edge navigation stays available without overpowering drag-to-pan.

### 1.1.0.0

- Added side-by-side compare fixes so compare-selected opens the intended files and renders them in separate panes.
- Added persistent thumbnail cache management with statistics, compact, and purge actions from the UI.
- Added multi-file Properties support plus richer thumbnail details with inline star ratings.
- Improved startup persistence by restoring the last folder and window placement more reliably, with off-screen placement rejection.
- Reworked slideshow defaults and menu organization so slideshow duration is configurable ahead of launch and the main/context menus are easier to scan.

### 1.0.0

- Initial public Windows release of the native Win32 browser/viewer with thumbnail browsing, RAW support, slideshow playback, file operations, packaging, and smoke-test coverage.

## Next Features

Planned near-term areas after 2.1.0 include:

- Viewer polish and workflow depth improvements that keep navigation fast while expanding compare/cull ergonomics.
- Additional browser workflow refinements for high-volume folder organization.
- Ongoing metadata-forward UX updates and presentation polish while preserving startup and scroll performance.

## License

HyperBrowse is released under the [MIT License](LICENSE).

Copyright (c) 2026 Michael A. McCloskey.

Third-party components retain their own licenses. The generated distribution includes `THIRD-PARTY-NOTICES.txt` with the candidate's versions, source tree revisions, linkage choices, review date, and review owner. Notable bundled components:

- [LibRaw](external/libraw) is dual-licensed under [LGPL 2.1](external/libraw/LICENSE.LGPL) and [CDDL 1.0](external/libraw/LICENSE.CDDL).
- [NanoSVG](external/nanosvg) is distributed under the [zlib license](external/nanosvg/LICENSE.txt).
- When CUDA redistributable bundling is enabled, NVIDIA CUDA Runtime and nvJPEG redistributables are governed by their respective NVIDIA Software License Agreements, staged beside the application as `NVIDIA-CUDA-RUNTIME-LICENSE.txt` and `NVIDIA-NVJPEG-LICENSE.txt`.

## Support the Project

If HyperBrowse is useful to you, you can support continued development:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-buymeacoffee.com%2Ftheosopher-FFDD00?logo=buy-me-a-coffee&logoColor=000)](https://buymeacoffee.com/theosopher)

Thank you!
