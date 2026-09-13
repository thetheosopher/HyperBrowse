# HyperBrowse Visual Polish Pass

## 1. Purpose

This document defines the first visual polish pass for HyperBrowse.

The goal is to make the application feel more deliberate and more modern without introducing meaningful dependency growth or harming the product's startup, thumbnail, or viewer performance profile.

## 2. Constraints

### Must preserve

- immediate-feeling startup
- low-overhead native shell
- virtualized thumbnail rendering
- smooth scrolling in large folders
- fast viewer open latency

### Must avoid

- framework rewrites
- blur or acrylic effects on scrolling content
- per-thumbnail child windows
- paint-path allocations that scale with folder size
- decorative motion or continuous animations

## 3. First-Pass Scope

### Browser pane

- refine dark and light palettes
- move thumbnail mode from flat tiles to cleaner card-like cells
- improve preview framing and card hierarchy
- improve empty and loading states
- surface slightly richer item context for selected thumbnails

### Details mode

- improve row contrast and readability
- make selection feel more intentional
- keep owner-data behavior and virtualization unchanged

### Viewer

- improve loading and error presentation
- add a lightweight HUD with filename, index, zoom, and dimensions
- keep the image dominant

### Shell chrome

- align main window/tree/browser palette more closely
- avoid toolbar sprawl or custom non-client chrome work

## 4. Design Direction

HyperBrowse should feel:

- compact
- calm
- native
- photo-focused
- performance-first

The reference is not glossy consumer UI. The reference is a fast desktop tool that looks intentional.

## 5. Implementation Plan

### 5.1 Browser pane

- use the window background as the canvas surface
- render each thumbnail item as a distinct card surface
- keep borders crisp and subtle
- reserve accent emphasis for selected state only
- use preview framing to give thumbnails a more stable visual rhythm
- keep metadata density low for unselected items

### 5.2 Details mode

- use alternating row tone shifts or other low-cost row differentiation
- use stronger selected-row fill and text contrast
- avoid adding gridline clutter

### 5.3 Placeholder states

- no folder selected: centered start state
- loading folder: centered scanning state
- empty result: centered no-images state
- error: centered error state with the actual failure message

### 5.4 Viewer

- loading should be centered and composed as a state, not left-aligned raw text
- loaded images should get a small persistent HUD in a corner
- HUD content should be concise and stable
- HUD should not obscure the main image unnecessarily

## 6. Performance Rules

1. Do not add extra async work beyond lightweight selected-item metadata requests already supported by the app.
2. Do not add any work on startup for visual polish.
3. Keep thumbnail paint O(visible items), as it is today.
4. Prefer reused brushes and pens on the browser surface.
5. Keep viewer polish lightweight enough that it is dwarfed by image scaling and blitting cost.

## 7. Acceptance Criteria

This pass is successful if:

- thumbnail mode looks more layered and less flat
- selected items stand out more clearly
- empty/loading/error states look intentional
- details mode is easier to scan visually
- viewer presentation looks more finished without becoming toolbar-heavy
- build and tests still pass
- no obvious browse/viewer responsiveness regression is introduced

## 8. Follow-up Work

After this pass:

1. add a compact contextual action strip for current selection
2. add in-folder filter/search UI
3. add a selected-item details strip or side panel
4. consider light common-control theming improvements if still needed
