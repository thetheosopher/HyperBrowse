# HyperBrowse

![Version](https://img.shields.io/badge/Version-2.0.0-2EA043)
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
- Thumbnail and details modes, optional Explorer-style subfolder entries, recursive browsing, sorting, in-folder filename filtering, thumbnail ratings, and multi-selection workflows.
- Full-screen viewer with zoom, pan, rotate, edge-hover previous/next navigation, side-by-side compare, scalable info overlays, current-folder slideshow launch, full metadata pane, and adjacent-image prefetch.
- Quick Actions with saved destinations, persistent key assignments, F7 move, and F8 copy for the currently displayed image or selected browser files.
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

## Current Capabilities

| Area | Included today |
| --- | --- |
| Browser | Explorer-style folder tree, resizable splitter, thumbnail mode, details mode, recursive browsing, live filename filter, thumbnail detail toggle with inline star ratings, selected-item info strip, remembered window/folder restore, back-folder history, and folder context workflows for create/rename/delete plus favorite-aware move destinations, in-tree folder drag-drop move, image drag-drop into tree folders, and drag-out to shell-aware apps |
| Viewer | Separate viewer window, full-screen open, side-by-side compare, zoom, pan, fit-to-window, 100% view, rotate, edge-hover/click previous-next navigation, overlay HUD with size presets, full metadata pane, slideshow with current-folder launch from the active image, transition styles, and multi-monitor open |
| Formats | JPEG, PNG, GIF, TIFF via WIC; RAW support for ARW, CR2, CR3, DNG, NEF, NRW, RAF, and RW2 via LibRaw |
| File workflows | Open, reveal in Explorer, open containing folder, copy path, copy/move/delete, multi-file Properties, tags and ratings, EXIF-only JPEG orientation adjustment, and batch convert to JPEG/PNG/TIFF |
| Performance pipeline | Prioritized thumbnail scheduling, memory-bounded thumbnail cache, persistent disk thumbnail cache with stats/compact/purge, metadata cache, viewer prefetch, folder watch refresh, and optional GPU-assisted JPEG decode |
| Distribution | Debug and Release presets, smoke tests, startup-budget checks, portable layout, installer layout, zipped portable release, Inno Setup 6 installer with per-user or per-machine install mode, and Windows CI artifact validation |

### Viewer Quick Actions

Quick Actions supports up to 36 saved destinations. Each newly added destination is automatically assigned the lowest available key in `0` through `9`, then `A` through `Z`; its key field accepts one digit or letter and can be edited later. Assignments persist by folder path and remain associated with the same destination when the list is reordered. Recent folders are not included.

In the viewer, press `F7` to move the currently displayed image to a selected favorite or `F8` to copy it. If the image has a paired RAW or JPEG companion, the companion is included in the same operation. With files selected in the main window, the same shortcuts open the chooser for moving or copying the selection. The destination chooser can be dismissed with `Escape`, by clicking outside it, or by making no selection. A successful move advances the viewer; a copy leaves the current image displayed.

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
- CMake 4.2 or newer for the bundled Visual Studio 2026 presets.
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

The consolidated Settings dialog is available from View > Settings or with Ctrl+Shift+T. Its Slideshow, Viewer, Appearance, Performance, and Behavior tabs stage changes until Apply or OK; Cancel and closing the dialog discard uncommitted changes.

The release packaging path builds the release binaries, runs the smoke executable, stages both portable and installer layouts, and then emits the zipped portable package plus the Inno Setup installer.

## Packaging

Create the portable layout after building:

```powershell
cmake --install build --config Release --component Portable --prefix build/dist/HyperBrowse-2.0.0-portable
```

Create the installer-friendly staging layout:

```powershell
cmake --install build --config Release --component Runtime --prefix build/dist/HyperBrowse-2.0.0-installer-layout
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

The dedicated release-packaging configure preset keeps the static MSVC runtime enabled, keeps LibRaw linked statically, and keeps CUDA redistributable bundling enabled so the portable zip and installer carry the RAW helper executable plus the nvJPEG runtime DLLs they need. The packaging target runs the release smoke tests, stages both install components under `build-release-package/dist/`, creates `HyperBrowse-<version>-portable-win64.zip`, and compiles `HyperBrowse-<version>-installer.exe` with Inno Setup 6.

The generated installer supports either current-user or all-users installation, writes the correct Add/Remove Programs entry for the selected scope, creates a Start Menu shortcut automatically, and offers an optional desktop shortcut.

Both the portable package and installer include the offline user guide under `docs/`, along with the main-window screenshot used by the guide. Installed builds place the application executable under `bin/` and the guide under the neighboring `docs/` directory.

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

Current release: **2.0.0**. The version is defined by the top-level `project(HyperBrowse VERSION ...)` call in [CMakeLists.txt](CMakeLists.txt) and flows into the generated build metadata, the Windows version resource, the About dialog, and all release artifact names (for example `HyperBrowse-2.0.0-portable-win64.zip` and `HyperBrowse-2.0.0-installer.exe`).

Release **2.0.0** expands HyperBrowse from a fast image browser into a more complete, resilient desktop workflow while preserving asynchronous browsing and viewing. It adds richer shell integration, safer file operations, single-instance launch forwarding, persistent state and cache improvements, and reproducible Windows release validation.

## Version History

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

Planned near-term areas after 1.2.7 include:

- Viewer polish and workflow depth improvements that keep navigation fast while expanding compare/cull ergonomics.
- Additional browser workflow refinements for high-volume folder organization.
- Ongoing metadata-forward UX updates and presentation polish while preserving startup and scroll performance.

## License

HyperBrowse is released under the [MIT License](LICENSE).

Copyright (c) 2026 Michael A. McCloskey.

Third-party components retain their own licenses. Notable bundled components:

- [LibRaw](external/libraw) is dual-licensed under [LGPL 2.1](external/libraw/LICENSE.LGPL) and [CDDL 1.0](external/libraw/LICENSE.CDDL).
- [NanoSVG](external/nanosvg) is distributed under the [zlib license](external/nanosvg/LICENSE.txt).
- When CUDA redistributable bundling is enabled, NVIDIA CUDA Runtime and nvJPEG redistributables are governed by their respective NVIDIA Software License Agreements, staged beside the application as `NVIDIA-CUDA-RUNTIME-LICENSE.txt` and `NVIDIA-NVJPEG-LICENSE.txt`.

## Support the Project

If HyperBrowse is useful to you, you can support continued development:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-buymeacoffee.com%2Ftheosopher-FFDD00?logo=buy-me-a-coffee&logoColor=000)](https://buymeacoffee.com/theosopher)

Thank you!
