# HyperBrowse Branding and UI Assessment

## 1. Purpose

This document records the branding additions made to HyperBrowse and provides a
comprehensive assessment of the current UI layout, option surface, and design
direction based on the visual polish pass and the competitive enhancement
priorities established in `10-prioritized-enhancements.md`.

## 2. Branding — What Was Added

### Application Icon

A multi-resolution application icon was created and integrated:

- **Design concept: "Speed Frame"** — a blue photo frame containing a landscape
  silhouette (mountains + sun), overlaid by a diagonal gold lightning bolt.
  The frame communicates *image browsing*, the bolt communicates *speed*.
- **Sizes bundled:** 256, 128, 64, 48, 32, 24, 20, 16 px in a single `.ico` file.
- **File:** `assets/HyperBrowse.ico`
- **Generator:** `tools/generate_icon.py` (reproducible via `python tools/generate_icon.py`)

### Integration Points

| Surface                | Mechanism                                                |
|------------------------|----------------------------------------------------------|
| Executable file icon   | `IDI_HYPERBROWSE ICON` in `.rc` resource (ID 1)         |
| Main window title bar  | `WNDCLASSEX.hIcon` and `.hIconSm` via `LoadIcon`/`LoadImage` |
| Viewer window          | Same icon via `WNDCLASSEX` fields                        |
| Taskbar                | Inherited from window class registration                  |
| Alt-Tab                | Inherited from `hIcon` (large icon)                       |
| About dialog           | Updated text — concise version + feature summary          |

### What Was Not Changed

- No custom non-client title bar rendering was added.
- No splash screen was added.

## 3. UI Layout Assessment

### 3.1 Overall Architecture

HyperBrowse is a **three-pane, menu-driven Win32 application** with:

- **Left:** Folder tree (280 px default, resizable via 6 px splitter)
- **Right:** Browser pane (thumbnails or details mode)
- **Bottom:** 4-part status bar

There is no toolbar, no ribbon, and no sidebar option panel.

### 3.2 Strengths

1. **Clean, focused layout.** The two-pane + status bar layout avoids the
   toolbar sprawl and option clutter common in competing products. It keeps the
   photo content visually dominant.

2. **Strong keyboard access.** 16 accelerators cover the most-used commands.
   The menu mnemonics are well-chosen.

3. **Good context menu.** The right-click menu is comprehensive and
   context-sensitive (grays unavailable items, shows secondary-monitor option
   only when relevant).

4. **Thumbnail polish is solid.** Rounded cards, ClearType typography, metadata
   badges, a loading indicator, and centered placeholder states give the
   browser pane a modern, intentional feel.

5. **Theme support is complete.** Both light and dark themes are implemented with
   consistent palettes across all surfaces including the DWM title bar frame.

6. **Status bar is information-rich.** Folder context, item counts, selection
   summary, and shell state are always visible.

### 3.3 Weaknesses and Opportunities

#### A. Title Bar Is Too Busy

The title currently includes: folder name, view mode label, recursive flag,
and theme name.  Example:

```
HyperBrowse - C:\Users\Photos - Thumbnail Shell - Recursive - Dark
```

**Problem:** The view mode, recursive flag, and theme are already visible in the
status bar (part 3) and in the menu check states.  Duplicating them in the title
wastes the most prominent UI surface on low-value state echo.

**Recommendation:** Simplify the title to:

```
HyperBrowse - C:\Users\Photos
```

or just `HyperBrowse` when no folder is open.  This is the pattern used by
Explorer, FastStone, and XnView MP.

#### B. No Toolbar / Action Strip

All commands require either the menu bar, a right-click, or a keyboard shortcut.
This makes the most common actions (open, delete, copy, view mode, sort, theme)
less discoverable for new users and slightly slower for mouse-driven workflows.

**Recommendation (from visual polish spec follow-up item 1):** Add a compact
contextual action strip between the menu bar and the browser pane (or below the
browser pane).  It should contain only:

- View mode toggle (thumbnails / details)
- Sort dropdown
- Thumbnail size slider or dropdown
- Theme toggle
- (When selection exists) Delete, Copy, Move quick buttons

This would not be a full toolbar. It should be a single row of small,
flat-style controls — closer to a VS Code activity bar than a classic toolbar.

#### C. View Menu Is Overloaded

The View menu contains 17 items spanning four unrelated concerns:

1. View mode (thumbnails / details)
2. Browsing scope (recursive toggle)
3. Sort options (6 radio items)
4. Display options (thumbnail size, layout, details toggle)
5. Slideshow triggers
6. Theme selection
7. Acceleration toggles

**Problem:** Sort, display, and acceleration options feel hidden behind a flat
list of 17 items.  Users familiar with image browsers expect sort and display
options to be more directly accessible.

**Recommendation:** Consider:

- Moving Sort into a submenu: `View > Sort By > {options}`
- Moving Thumbnail Size into a submenu: `View > Thumbnail Size > {options}`
  (already done — but keep it)
- Moving acceleration toggles into the Help menu or a Settings/Options
  submenu — they are rarely changed and are not "view" concepts
- Moving slideshow into File or into its own top-level menu if more slideshow
  controls are added later

#### D. Status Bar Part 3 Is Too Dense

Shell state concatenates many values into a single string:

```
View: Thumbnails | Sort: Filename | Recursive: Off | Theme: Dark | JPEG: nvJPEG | RAW: out-of-process | Tiles: 192 px, Standard, Details
```

This is useful for diagnostics but is hard to scan. Most users will never read
it at normal window sizes because it truncates.

**Recommendation:**

- Move the most-used status from part 3 into the status bar's first three parts
  alongside the existing content.
- Simplify part 3 to show only the *unusual* state: acceleration status and
  current slideshow/convert operation.
- Or: drop part 3's verbose state and rely on the (future) action strip for
  visual state readability.

#### E. No Filter / Search

This is identified as enhancement #3 in `10-prioritized-enhancements.md` and
remains unimplemented.  For large folders the only narrowing mechanisms are
folder selection and recursive toggle.

**Recommendation:** Add a simple filter text box in the action strip or above
the browser pane.  Filter by filename substring.  This is the single highest
value non-file-operation feature remaining.

#### F. About Dialog Is a MessageBox

The About dialog is a plain `MessageBoxW`.  With the new icon, a custom About
dialog that shows the icon alongside version/feature info would feel more
polished and reinforce the branding.

**Recommendation (low priority):** Create a small custom About dialog that
renders the icon at 48 or 64 px alongside the text.

#### G. Context Menu Size

The context menu has 27 items.  While it is well-organized with separators, it
is reaching the threshold where users may struggle to find commands quickly.

**Recommendation:**

- Move batch convert options into a submenu (already is, but verify it reads
  cleanly)
- Consider omitting sort and view mode options from the context menu — these
  are stable settings, not contextual actions.  The context menu should focus
  on actions applicable to the current selection.

#### H. Empty State Branding

This is now addressed.  The browser pane renders the HyperBrowse icon above the
short no-folder, loading, and empty-folder placeholder copy, and the viewer
uses the same icon treatment for its loading panel.

**Result:** Initial-launch and loading states feel more intentional without
adding a splash screen or heavier chrome.

## 4. Visual Polish Status

Based on the visual polish spec (`12-visual-polish-pass.md`), the following
items are **complete**:

- [x] Thumbnail card rendering (rounded corners, accent fill, card hierarchy)
- [x] ClearType title/meta/status fonts at multiple sizes
- [x] Preview framing with loading indicator
- [x] Centered placeholder states (no folder, loading, empty, error)
- [x] Details mode custom draw (alternating rows, selection emphasis)
- [x] Light and dark theme palettes with DWM frame integration
- [x] Viewer HUD overlay (filename, index, zoom, dimensions)
- [x] Compact top action strip for high-frequency browse controls
- [x] In-folder filename filter integrated into the action strip
- [x] Branded empty/loading states with concise status copy and app icon

These follow-up items from the visual polish spec are **not yet implemented**:

- [ ] Selected-item details strip or side panel
- [ ] Common-control theming improvements

## 5. Recommended Next Steps (Priority Order)

1. **Move acceleration toggles** out of the View menu into Help or a Settings
   submenu.
2. **Trim the context menu** by removing sort and view mode radio groups.
3. **Add a selected-item details strip or side panel** for richer metadata and
  action visibility without opening the viewer.
4. *(Low priority)* Create a custom About dialog with the app icon.

## 6. What Should Not Change

- The toolbar-free purity of the main layout should be preserved even when the
  action strip is added.  The strip should be visually quiet, not a classic
  Win32 toolbar.
- The status bar should remain visible and continue to show folder and
  selection context.
- The theme system should stay as-is — it is well-implemented.
- The context menu should remain comprehensive for selection-based actions.
- No splash screen should be added.  The app's fast startup is the brand.
