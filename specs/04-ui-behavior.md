# UI Behavior Specification

## 1. UX Goals

The UI should feel:
- familiar to Windows users
- compact and efficient
- optimized for keyboard + mouse workflows
- visually minimal
- fast above all else

## 2. Main Window Layout

## 2.1 Regions
- top menu bar
- action strip (44 px, owner-drawn compact toolbar with view mode, sort, thumbnail size, theme, filter box, and file action buttons)
- left folder tree
- center splitter
- right browser pane
- bottom status bar

## 2.2 Splitter behavior
- vertical splitter between tree and browser
- drag-resizable
- persisted across sessions
- double-click may reset to default layout if implemented

## 3. Menu Structure

Keep menus compact and practical.

### File
- Open Folder
- Refresh Folder Tree
- Open
- View on Secondary Monitor
- Image Information
- Reveal in Explorer
- Open Containing Folder
- Copy Path
- Properties
- Copy Selection...
- Move Selection...
- Delete (Recycle Bin)
- Delete Permanently
- Adjust JPEG Orientation Left
- Adjust JPEG Orientation Right
- Batch Convert Selection → JPEG / PNG / TIFF
- Batch Convert Folder → JPEG / PNG / TIFF
- Cancel Batch Convert
- Exit

### View
- Thumbnail Mode
- Details/List Mode
- Recursive Browsing
- Thumbnail Size presets (96, 128, 160, 192, 256, 320 px)
- Compact thumbnail layout toggle
- Show thumbnail details toggle
- Sort By (submenu: Filename, Modified Date, File Size, Dimensions, Type, Random)
- Slideshow from Selection
- Slideshow from Folder
- Slideshow Transition (Cut / Crossfade / Slide / Ken Burns, duration presets: 200 / 350 / 500 / 800 / 1200 / 2000 ms)
- Theme (Light / Dark)
- Enable NVIDIA JPEG Acceleration
- Use Out-of-Process LibRaw Fallback

Note: Sort Direction (ascending/descending) is not yet implemented.

### Image
- Open in Viewer
- Next / Previous
- Rotate Left / Rotate Right (viewer)
- Adjust JPEG Orientation Left / Right (EXIF metadata-only)
- Slideshow
- Image Information

Note: "Lossless JPEG Rotate" from the original spec was replaced with EXIF-only orientation adjustment.

Note: The Tools menu from the original spec was not implemented. Refresh is available via F5 in the File menu. Settings are managed through View menu toggles and persisted to the Windows registry.

### Help
- About
- Diagnostics Snapshot
- Reset Diagnostics

## 4. Folder Tree Behavior

### Requirements
- Explorer-like hierarchy
- Special folder roots: Desktop, Documents, Pictures (via `SHGetKnownFolderPath`)
- Drive roots: all logical drives
- Shell display names and icons (via `SHGetFileInfo`)
- keyboard navigable
- expand/collapse support with async child directory enumeration (non-blocking)
- current selection synchronized with browser pane
- live refresh integration where practical

### Selection behavior
When a folder is selected:
- current browser contents are replaced
- placeholders appear immediately
- status bar updates as information becomes available

## 5. Browser Pane Modes

## 5.1 Thumbnail Mode

### Requirements
- virtualized grid
- variable thumbnail sizes with practical presets from 96 px to 320 px
- visible placeholders before thumbnail decode completes
- smooth mouse wheel and keyboard scroll
- strong multi-select behavior
- compact spacing option for denser browsing
- thumbnail-only option that hides filename and metadata rows

### Thumbnail cell content
Each cell may include:
- image thumbnail
- filename
- metadata badge and file size footer when details are visible

## 5.2 Details/List Mode

### Requirements
- virtualized list
- sortable columns
- efficient keyboard navigation
- selection parity with thumbnail mode

### Suggested columns
- Name
- Type
- Size
- Modified
- Dimensions
- Camera / Metadata summary (optional)

## 5.3 In-folder filter

### Requirements
- live filename filter via text box in the action strip
- case-insensitive substring match
- filters the current browser view without affecting the underlying model
- status bar shows "N of M" when filter is active
- filter cleared on folder change

## 6. Selection Behavior

Support all of the following in both browser modes:
- single click select
- Ctrl-click toggle
- Shift-click range select
- rubber-band selection (thumbnail mode)
- keyboard arrows for focus movement
- Shift+arrows range extension
- Ctrl+A select all

### Double-click behavior
- open selected item in separate viewer window

### Right-click behavior
Open context menu relevant to current selection. The context menu includes:
1. Open / View on Secondary Monitor / Image Information
2. Reveal in Explorer / Open Containing Folder / Copy Path / Properties
3. Copy Selection / Move Selection / Delete / Delete Permanently
4. Slideshow from Selection / Batch Convert Selection (submenu)
5. Adjust JPEG Orientation Left / Right
6. Thumbnail Mode / Details Mode / Sort By (submenu)
7. Open Folder / Refresh Folder Tree

Items are enabled/disabled based on current selection state and active operations.

## 7. Status Bar Behavior

Status bar should update incrementally and never block UI.

### Required fields (4-part status bar)
- Part 0 (240 px): current folder name
- Part 1 (650 px): image count, total size, load state, active filter query, batch convert progress, file operation status
- Part 2 (930 px): selection count and selected total size
- Part 3 (stretch): JPEG acceleration state, RAW decode state, viewer zoom level, slideshow status

## 8. Viewer Window Behavior

## 8.1 Open behavior
- separate top-level window
- opens immediately on double-click, in fullscreen by default on the target monitor
- multi-monitor support: "View on Secondary Monitor" places viewer on a non-primary monitor
- may reuse existing viewer window or open a new one based on future settings

## 8.2 Viewer commands
- next image
- previous image
- zoom in
- zoom out
- fit to window
- 100%
- pan
- rotate
- slideshow start/stop
- full screen toggle
- image information

## 8.3 Mouse behavior
- double-click toggles full screen
- mouse wheel always zooms in viewer (configurable wheel behavior is deferred)
- click-drag pans when zoomed

## 8.4 Zoom/Pan behavior
- low-latency response preferred over animation
- reset state on each new image
- zoom-out is clamped to the window-fit scale (cannot zoom out beyond the point where the image fits the window)
- if zoom reaches fit-to-window scale, mode automatically switches to Fit

## 8.5 Info Overlay / HUD
- toggled via Tab key
- visibility persisted to registry
- top-left panel: filename, position (N/Total), file type, file size
- bottom-right panel: dimensions, zoom percentage, zoom mode (Fit/Custom)
- loading and error states always visible regardless of overlay toggle

## 9. Slideshow Behavior

### v1 requirements
- launch from browser selection or current folder scope
- respect recursive mode when appropriate
- allow next/previous during slideshow
- support full-screen slideshow mode
- default interval 3000 ms (minimum 1000 ms)
- Space bar toggles slideshow on/off in the viewer

Note: Configurable slideshow interval is not yet exposed in the UI. The API accepts an interval parameter but there is no settings surface for it. Pause/resume is currently handled by the Space bar toggle (stop/start).

## 10. Theme Behavior

### Supported themes
- light
- dark

### Requirements
- all primary UI surfaces must theme correctly
- selection, focus, and hover states must remain clear in both themes
- image rendering itself must not be altered by app theme

## 11. Keyboard Shortcut Expectations

### Implemented shortcuts
- `Ctrl+O`: open folder
- `F5`: refresh folder tree
- `Ctrl+I`: image information
- `Ctrl+Shift+C`: copy selected paths
- `Ctrl+E`: reveal in Explorer
- `Alt+Enter`: file properties
- `Delete`: delete to Recycle Bin
- `Shift+Delete`: permanent delete
- `Ctrl+1`: thumbnail mode
- `Ctrl+2`: details mode
- `Ctrl+R`: recursive browsing toggle
- `Ctrl+Shift+S`: slideshow from selection
- `Ctrl+Shift+F`: slideshow from folder
- `Ctrl+L`: light theme
- `Ctrl+D`: dark theme
- `Ctrl+Shift+D`: diagnostics snapshot
- `Ctrl+Shift+X`: reset diagnostics

### Viewer-specific shortcuts
- `Left` / `Right`: previous / next image
- `+` / `=`: zoom in
- `-`: zoom out
- `0`: fit to window
- `1`: actual size
- `L`: rotate left
- `R`: rotate right
- `F11`: toggle fullscreen
- `Escape`: exit fullscreen
- `Space`: toggle slideshow
- `Tab`: toggle info overlay HUD

### Not yet implemented
- `Ctrl+A`: select all (implemented in browser via menu, no dedicated accelerator)
- `F2`: rename in place

## 12. Error UX

### Browser failures
- broken images show placeholder icon/state
- unsupported RAW variants show clear unsupported marker
- metadata failures do not block browsing

### Viewer failures
- show non-blocking error message in viewer surface
- allow next/previous navigation to continue

## 13. UX Constraints

- avoid crowded toolbars in v1
- avoid modal dialogs for routine operations when a non-blocking surface is enough
- avoid expensive animations
- avoid unpredictable selection behavior between thumbnail and details modes
