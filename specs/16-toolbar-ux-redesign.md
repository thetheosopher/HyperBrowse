# HyperBrowse Toolbar & UX Redesign

## 1. Purpose

This document is a full UX review of the current HyperBrowse interface and a
concrete design specification for replacing the action strip with a modern,
icon-driven toolbar that eliminates the visual flashing issue and makes the
application feel more polished and intentional.

## 2. Current State Assessment

### 2.1 Screenshot Observations

The screenshot shows the dark theme with 320 px thumbnails browsing a Cards
folder.  The action strip renders as ten flat text-only buttons spanning the
full width:

```
[Open Folder] [Recursive] [Thumbnails] [Details] [Name ↑] [320 px] [Dark]  [ Filter filenames ]  [Copy] [Move] [Delete]
```

### 2.2 What Works

1. **Thumbnail cards are solid.** Rounded cards, ClearType fonts, type badges,
   dimension text, and loading states give the browser area a modern feel.
2. **Theme is cohesive.** Dark and light palettes are well-tuned and consistent
   across the tree, browser, splitter, and status bar.
3. **Status bar is useful.** Four-part status with folder, count, selection, and
   acceleration info is informative without being cluttered.
4. **Tree pane is clean.** Standard shell tree with proper dark-mode icon
   rendering, single-slab background, no visual noise.

### 2.3 What Does Not Work

#### A. Action Strip Looks Like a Prototype

The ten text-only buttons with identical visual weight have no grouping, no
iconography, and no information hierarchy.  Every button looks the same: a
small rounded rect with a text label.  This is the single largest drag on the
application's perceived quality.

Specific issues:

- **No icons.**  Competing apps (XnView MP, FastStone, IrfanView, Eagle) all
  use icon-based toolbars.  Text-only buttons are visually heavy and slow to
  scan.
- **No visual grouping.**  Navigation, view mode, display settings, and
  selection actions all sit in one undifferentiated row.
- **Wasted space.**  The Copy/Move/Delete buttons are right-aligned and far
  from the selection they operate on.  The filter box takes whatever space is
  left, which varies with window width.
- **All buttons are the same size class.**  "Open Folder" (rare action) has the
  same visual presence as "Delete" (destructive action).

#### B. Buttons Flash During Rubber Band and Other Actions

Every `BrowserPane::NotifyStateChanged()` call triggers the full cascade:

```
NotifyStateChanged()
  → PostMessage(kStateChangedMessage)
    → OnBrowserPaneStateMessage()
      → UpdateMenuState() → DrawMenuBar()
      → UpdateActionStripState()
        → RedrawWindowNoErase(button) with RDW_UPDATENOW  ← synchronous per-button
```

During rubber band selection, `UpdateRubberBandSelection()` calls
`NotifyStateChanged()` on every mouse move.  The Copy/Move/Delete buttons'
enabled state toggles as the selection goes from 0→N or N→0 items, causing
each button to synchronously repaint — visible as a flicker across the strip.

Additional flash contributors:

1. `WM_ERASEBKGND` fills the **entire** client area with `backgroundBrush_`
   (window background), but the action strip uses a different
   `actionStripBackground` color.  When erase fires before paint, the strip
   area briefly shows the wrong color.
2. `ApplyTheme()` uses `RDW_ERASE | RDW_ALLCHILDREN`, forcing every child to
   erase then repaint.
3. Each `DrawActionButton` creates and destroys GDI objects (brush, pen) on
   every paint.  At high repaint frequency this creates allocation churn.
4. `LayoutChildren()` always invalidates the action strip rect even if nothing
   moved.

#### C. Menu Structure Is Overloaded

The View menu bundles 20+ items across five unrelated concerns:

- View mode (thumbnails / details)
- Browsing scope (recursive)
- Sort options (7 items + direction)
- Display options (thumbnail size, details, info strip)
- Slideshow (2 triggers + transition submenu with 6 styles + 6 durations)
- Theme (light / dark)
- Acceleration toggles (nvJPEG, LibRaw)

Users looking for "sort by date" must scan past slideshow and acceleration
items.  Acceleration toggles are settings, not view choices.

#### D. Context Menu Is Large

The browser right-click menu has 27 items.  While well-organized, it includes
view-mode and sort options that are not contextual to a selection.

#### E. Filter Box Is Visually Disconnected

The filter edit sits in the action strip as a bare Win32 EDIT control with a
custom rounded border painted behind it in WM_PAINT.  The border and the edit
are separate surfaces — on resize or theme change, the border can paint before
or after the edit, creating a brief visual mismatch.

## 3. Design Goals

1. **Eliminate button flashing** during rubber band and any state-change cascade.
2. **Replace text buttons with an icon toolbar** that is faster to scan and more
   visually compact.
3. **Group toolbar items logically** with subtle visual separators.
4. **Streamline the View menu** by moving settings-class items out of the view
   concerns and making sort/display accessed via the toolbar.
5. **Keep startup latency effectively unchanged.** No extra DLLs, no network
   fetches, and only negligible local parsing work for a small bundled icon
   set.
6. **Keep the native Win32 character.** This is not an Electron app.  The
   toolbar should feel like a polished native tool, not a web skin.

## 4. Icon Strategy

### 4.1 Recommendation: NanoSVG + Bundled SVG Assets

Use **NanoSVG** to parse a small set of bundled monochrome SVG toolbar icons
and rasterize them into cached 32-bit bitmaps at runtime.  This keeps the
toolbar visually distinctive while preserving a lightweight native Win32 stack.

**Advantages:**

- Custom visual voice instead of generic OS glyphs
- Header-only dependency with no new runtime DLLs
- Resolution-independent source art with runtime tinting for light/dark states
- Easy iteration because icons live as plain SVG files in `assets/toolbar-icons`
- Negligible runtime cost once parsed and cached

**Initial icon set:**

| Action | Asset |
|--------|-------|
| Open Folder | `open-folder.svg` |
| Recursive Browsing | `recursive.svg` |
| Thumbnail View | `view-grid.svg` |
| Details View | `view-list.svg` |
| Sort | `sort.svg` |
| Thumbnail Size | `thumbnail-size.svg` |
| Copy | `copy.svg` |
| Move | `move.svg` |
| Delete | `delete.svg` |
| Filter/Search | `search.svg` |
| Dropdown Chevron | `chevron-down.svg` |

### 4.2 Rendering Approach

Each toolbar SVG is parsed once at startup, cached as a NanoSVG image, and
rasterized on demand into small premultiplied-alpha bitmaps keyed by icon name,
pixel size, and tint color.  The toolbar paint pass then alpha-blends those
cached bitmaps into the strip buffer.

### 4.3 Alternative Considered: Segoe MDL2 Assets / Segoe Fluent Icons

The built-in Windows icon fonts would have been the lowest-effort option, but
they looked too generic for the toolbar direction and limited the app's ability
to establish a stronger visual identity.

## 5. Toolbar Architecture

### 5.1 Replace Child-Window Buttons with a Single Owner-Drawn Strip

The current architecture creates 10 child HWND buttons, each with `BS_OWNERDRAW`.
This is the root cause of the flashing — each child window is a separate
repaint target with its own erase/paint cycle.

**New architecture:**

- The action strip is a single region of the main window, painted entirely in
  `WM_PAINT` (or a single child static window with `WM_PAINT`).
- Toolbar items are **logical hit regions**, not child windows.
- Mouse interaction (click, hover, press) is handled via `WM_MOUSEMOVE`,
  `WM_LBUTTONDOWN`, `WM_LBUTTONUP` in the main window with hit-testing
  against the logical item rects.
- The filter edit control remains a real HWND (EDIT) because it needs text
  input focus, IME support, and clipboard integration.

**Benefits:**

- **No per-button repaint.** The entire strip paints in one pass.
- **No child-window erase flicker.** There is no `WM_ERASEBKGND` race between
  child and parent.
- **Hit-test hover** is smooth and cheap — just track the hot item index and
  invalidate the strip rect when it changes.
- **Tooltip support** via `WM_NOTIFY` / `TTN_GETDISPINFO` on a single tooltip
  control associated with the strip region.

### 5.2 Double-Buffered Strip Paint

Paint the action strip into an offscreen `HBITMAP` / `HDC`, then `BitBlt` to
the screen.  This eliminates any remaining tearing if multiple items change
state simultaneously.

### 5.3 Toolbar Item Model

```cpp
struct ToolbarItem
{
    UINT commandId;
   std::string iconName;       // bundled SVG asset key
    std::wstring tooltip;
    bool isToggle;              // e.g., Recursive, Thumbnails, Details
    bool isDropdown;            // e.g., Sort, Size, Theme
    bool isSeparator;           // visual group divider
    bool enabled;
    bool checked;               // toggle state
};
```

A flat `std::vector<ToolbarItem>` defines the strip contents.  Layout is a
left-to-right flow with fixed icon-button width and variable gaps for
separators.  The filter edit control is placed in a gap between the last
left-group item and the first right-group item.

### 5.4 Toolbar Item Layout

```
 [📁] [🔄] │ [▦] [☰] │ [↕▾] [⊞▾] │ [ 🔍 Filter filenames_________________ ] │ [📋] [→] [🗑]
  Open Recursive  Thumbs Details Sort  Size                                       Copy Move Delete
  Folder                               ▾=dropdown
```

**Groups (separated by thin vertical dividers):**

1. **Navigation:** Open Folder, Recursive Toggle
2. **View Mode:** Thumbnails (toggle), Details (toggle)
3. **Display:** Sort (dropdown), Tile Size (dropdown)
4. **Filter:** Search icon + edit box (expands to fill available space)
5. **Selection Actions:** Copy, Move, Delete (right-aligned)

Each icon button is a fixed square (32×32 or 36×36 depending on DPI).
Dropdown items show a small `▾` indicator.

### 5.5 Item Interaction States

| State | Visual |
|-------|--------|
| Normal | Icon in muted text color on strip background |
| Hover | Subtle fill behind icon (blended accent, ~12% opacity) |
| Pressed | Deeper fill (blended accent, ~24%) |
| Checked / Active | Accent fill background, accent-contrast icon color |
| Disabled | Icon at 40% opacity, no hover response |
| Dropdown Open | Same as Pressed; popup menu appears below |

### 5.6 Tooltip Integration

Create a single `TOOLTIPS_CLASS` control.  Register one tool rect per toolbar
item.  On `TTN_GETDISPINFO`, return the item's tooltip text (e.g., "Open
Folder (Ctrl+O)").

This replaces the implicit button-text affordance — users see the action name
and shortcut on hover instead of reading tiny button labels.

## 6. Flashing Fix — Regardless of Toolbar Rewrite

Even before or without the full toolbar rewrite, the flashing can be
substantially reduced:

### 6.1 Immediate Fixes

1. **Drop `RDW_UPDATENOW` from `RedrawWindowNoErase`.**  Change to
   `RDW_INVALIDATE | RDW_NOERASE` only.  Let the system coalesce repaints
   into the next `WM_PAINT` cycle.  This alone eliminates the per-button
   synchronous paint cascade during rubber band.

2. **Exclude the action strip from `WM_ERASEBKGND`.**  Either clip the erase
   rect to below `kActionStripHeight` or skip erase entirely and handle it in
   `WM_PAINT`.

3. **Debounce `UpdateActionStripState` during rubber band.**  If
   `BrowserPane::IsRubberBandActive()` is true, skip the action strip update.
   Post a deferred update for when rubber band ends.

4. **Cache GDI objects.**  Create strip background brush, button fill brushes,
   and border pens once in `ApplyTheme()` and reuse them in every paint.

### 6.2 Why the Full Rewrite Is Still Worth It

The immediate fixes reduce flashing but leave the visual design as-is: ten
text buttons with no icons and no grouping.  The toolbar rewrite solves both
the technical issue and the design issue in one pass.

## 7. Menu Restructuring

### 7.1 Current View Menu (20+ items)

```
Thumbnail Mode
Details Mode
---
Recursive Browsing
---
Sort By → (7 items + direction)
Thumbnail Size → (6 items)
Show Thumbnail Details
Show Info Strip
---
Slideshow from Selection
Slideshow from Folder
Slideshow Transition → (4 styles, duration → 6 presets)
---
Theme → Light / Dark
Enable NVIDIA JPEG Acceleration
Use Out-of-Process LibRaw Fallback
```

### 7.2 Proposed Menu Structure

**File** (unchanged except minor reorder):

```
Open Folder...                     Ctrl+O
Refresh Folder Tree                F5
---
Open
View on Secondary Monitor
Image Information                  Ctrl+I
---
Reveal in Explorer                 Ctrl+E
Open Containing Folder
Copy Path                          Ctrl+Shift+C
Rename...
Properties                         Alt+Enter
---
Copy Selection...
Move Selection...
Delete                             Del
Delete Permanently                 Shift+Del
---
Adjust JPEG Orientation Left
Adjust JPEG Orientation Right
---
Batch Convert Selection →
Batch Convert Folder →
Cancel Batch Convert
---
Exit
```

**View** (streamlined — settings moved out):

```
Thumbnail Mode                     Ctrl+1
Details Mode                       Ctrl+2
---
Recursive Browsing                 Ctrl+R
---
Sort By →
    Filename
    Modified Date
    File Size
    Dimensions
    Type
    Date Taken
    Random
    ---
    Descending
Thumbnail Size →
    96 / 128 / 160 / 192 / 256 / 320 px
Show Thumbnail Details
Show Info Strip
---
Slideshow from Selection           Ctrl+Shift+S
Slideshow from Folder              Ctrl+Shift+F
Slideshow Transition →
    Cut / Crossfade / Slide / Ken Burns
    ---
    Duration → 200 / 350 / 500 / 800 / 1200 / 2000 ms
```

**Tools** (new — absorbs settings-class items):

```
Theme →
    Light                          Ctrl+L
    Dark                           Ctrl+D
---
Enable NVIDIA JPEG Acceleration
Use Out-of-Process LibRaw Fallback
```

**Help** (unchanged):

```
About
---
Diagnostics Snapshot              Ctrl+Shift+D
Reset Diagnostics                 Ctrl+Shift+X
```

### 7.3 Rationale

- **NVIDIA and LibRaw toggles are not view settings.** They are engine
  configuration.  Moving them to Tools keeps the View menu focused on what the
  user sees.
- **Theme is a preference, not a view.** It fits better under Tools alongside
  other configuration.
- **View menu shrinks from ~20 to ~14 top-level items** (counting submenus as
  one item each), which is easier to scan.
- **The File menu is unchanged** — it is already well-organized.

### 7.4 Context Menu Simplification

Current: 27 items.  Proposed: remove view-mode and sort items from the context
menu.  These are persistent settings, not contextual selection actions.

Proposed context menu:

```
Open
View on Secondary Monitor
Image Information
---
Reveal in Explorer
Open Containing Folder
Copy Path
Rename...
Properties
---
Copy Selection...
Move Selection...
Delete
Delete Permanently
---
Slideshow from Selection
Batch Convert Selection →
---
Adjust JPEG Orientation Left
Adjust JPEG Orientation Right
```

This drops from 27 to ~19 items: tighter, faster to scan, and every item is
relevant to "I right-clicked on a selection."

## 8. Implementation Plan

### Phase 1: Fix Flashing (Low Risk, High Impact)

1. Remove `RDW_UPDATENOW` from `RedrawWindowNoErase`.
2. Exclude action strip from `WM_ERASEBKGND`.
3. Add rubber-band debounce for action strip updates.
4. Cache GDI objects in `ApplyTheme`.

**Estimated scope:** ~50 lines changed in `MainWindow.cpp`.

### Phase 2: Icon Toolbar Rewrite

1. Define `ToolbarItem` model and layout engine.
2. Parse bundled SVG art with NanoSVG and cache tinted toolbar bitmaps.
3. Replace 10 child-window buttons with single-surface toolbar paint.
4. Implement hit-testing, hover tracking, press, and click dispatch.
5. Implement dropdown behavior for Sort, Size items.
6. Keep filter EDIT as real HWND, position it in the toolbar gap.
7. Add tooltip control.
8. Double-buffer the strip paint.

**Estimated scope:** ~400–600 lines new, ~300 lines removed (old button
creation, `DrawActionButton`, per-button `UpdateActionStripState` logic).

### Phase 3: Menu Restructuring

1. Create the Tools menu.
2. Move Theme, NVIDIA, and LibRaw items.
3. Slim the context menu.
4. Update accelerators and menu labels.
5. Update specs.

**Estimated scope:** ~100 lines changed in `MainWindow.cpp`.

### Phase 4: Polish

1. Tune hover/press animations (single-frame opacity shift, no timers).
2. Add DPI awareness to icon size and toolbar height.
3. Verify light and dark themes.
4. Verify tooltip text includes keyboard shortcuts.
5. Update spec 04, 12, and 13.

## 9. Third-Party Library Assessment

| Library | What It Offers | Size | Startup Cost | Verdict |
|---------|---------------|------|-------------|---------|
| **NanoSVG** | SVG parsing and rasterization | ~125 KB vendored source | negligible | **Use this** |
| Segoe MDL2 Assets (OS built-in) | Generic vector glyphs | 0 bytes | 0 ms | Rejected for visual reasons |
| Dear ImGui | Immediate-mode UI | ~200 KB source | requires D3D init | Overkill for a toolbar |
| WinUI 2.x / XAML Islands | Modern controls | ~10 MB runtime | 200–500 ms cold start | Too heavy |
| Fluent UI (React-based) | Full design system | N/A (web) | N/A | Wrong platform |
| stb_truetype | TTF rasterizer | 60 KB | negligible | Wrong tool for SVG icon art |

**Recommendation:** Use NanoSVG with a very small curated SVG set.  This adds
no runtime DLL baggage, keeps startup impact negligible, and gives the toolbar a
more intentional visual language than a stock font icon set.

## 10. Toolbar Visual Mockup (ASCII)

### Dark Theme

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 📁  🔄 │ ▦  ☰ │ ↕▾  ⊞▾ │  🔍 Filter filenames                │ 📋  →  🗑 │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
  ╰─────╯   ╰──╯   ╰───╯    ╰──────────────────────────────────╯   ╰───────╯
   Nav       View   Display                Filter                   Selection
```

### Item Details

- **📁 Open Folder** — always enabled
- **🔄 Recursive** — toggle, checked = accent fill
- **▦ Thumbnails** — radio toggle, checked = accent fill
- **☰ Details** — radio toggle, checked = accent fill
- **↕▾ Sort** — dropdown, shows popup with sort options and direction
- **⊞▾ Size** — dropdown, shows popup with size presets
- **🔍 Filter** — decorative icon prefix for the EDIT control
- **📋 Copy** — enabled when selection > 0
- **→ Move** — enabled when selection > 0
- **🗑 Delete** — enabled when selection > 0

### Hovered State Example

```
 🔄 ← normal (muted icon on strip bg)
[🔄] ← hovered (subtle rounded fill behind icon)
```

## 11. Acceptance Criteria

1. Button flashing is eliminated during rubber band selection.
2. Button flashing is eliminated during all other state-change cascades.
3. The toolbar uses bundled SVG icons, not text labels.
4. Toolbar items are visually grouped with subtle separators.
5. Tooltip on hover shows the action name and keyboard shortcut.
6. Sort and Size dropdowns open a popup menu on click.
7. Toggle items (Recursive, Thumbnails, Details) show checked state via accent
   fill.
8. Disabled items (Copy/Move/Delete with no selection) are visually muted.
9. The filter edit control remains functional with keyboard focus and IME.
10. The View menu no longer contains acceleration toggles.
11. The context menu is reduced to ~19 selection-relevant items.
12. Light and dark themes both look correct.
13. No new DLL dependencies are introduced.
14. No noticeable startup latency is added.
15. Build and HyperBrowseSmoke pass.

## 12. Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| SVG asset missing or unreadable at runtime | Log the failure and keep the toolbar functional with blank icon slots until assets are restored |
| Hit-test precision on small icon targets | Use 36×36 hit rect minimum; add 4 px padding |
| Tooltip delay feels slow | Use `TTM_SETDELAYTIME` with 400 ms initial, 0 ms reshow |
| Filter EDIT focus ring clashes with toolbar paint | Paint a custom border around the edit in the strip paint pass |
| Dropdown menus need keyboard access | Keep `ACCEL` table for all shortcutted commands |
