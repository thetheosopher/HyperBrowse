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
- Open Containing Folder
- Batch Convert
- Exit

### View
- Thumbnail Mode
- Details/List Mode
- Recursive Browsing
- Thumbnail Size presets
- Sort By
- Sort Direction
- Theme
- Full Screen (viewer)

### Image
- Open in Viewer
- Next / Previous
- Rotate Left
- Rotate Right
- Lossless JPEG Rotate
- Slideshow
- Image Information

### Tools
- Refresh
- Settings

### Help
- About
- Diagnostics / Performance Info (optional)

## 4. Folder Tree Behavior

### Requirements
- Explorer-like hierarchy
- keyboard navigable
- expand/collapse support
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
- variable thumbnail sizes
- visible placeholders before thumbnail decode completes
- smooth mouse wheel and keyboard scroll
- strong multi-select behavior

### Thumbnail cell content
Each cell may include:
- image thumbnail
- filename
- optional compact metadata badges later

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
Open context menu relevant to current selection.

## 7. Status Bar Behavior

Status bar should update incrementally and never block UI.

### Required fields
- total image count in current folder scope
- total size of images in current folder scope
- selected image count
- selected total size
- current viewer zoom level when applicable

### Optional future fields
- background activity indicator
- decode backend hint
- sort mode

## 8. Viewer Window Behavior

## 8.1 Open behavior
- separate top-level window
- opens immediately on double-click
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
- wheel behavior configurable:
  - zoom
  - next/previous image
- click-drag pans when zoomed

## 8.4 Zoom/Pan behavior
- low-latency response preferred over animation
- reset state on each new image

## 9. Slideshow Behavior

### v1 requirements
- launch from browser selection or current folder scope
- respect recursive mode when appropriate
- allow pause/resume
- allow next/previous during slideshow
- support full-screen slideshow mode
- configurable interval

## 10. Theme Behavior

### Supported themes
- light
- dark

### Requirements
- all primary UI surfaces must theme correctly
- selection, focus, and hover states must remain clear in both themes
- image rendering itself must not be altered by app theme

## 11. Keyboard Shortcut Expectations

Suggested baseline shortcuts:
- `Enter` / `Space`: open in viewer
- `F11`: full screen
- `Left` / `Right`: previous / next image
- `+` / `-`: zoom in / out
- `0`: fit to window
- `1`: actual size
- `Ctrl+A`: select all
- `Delete`: delete or prompt later if implemented
- `F5`: refresh
- `Esc`: exit full screen / cancel operations where appropriate

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
