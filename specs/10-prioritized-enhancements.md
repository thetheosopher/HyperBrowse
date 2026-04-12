# HyperBrowse Prioritized Enhancements

## 1. Purpose

This document reviews HyperBrowse against comparable Windows image browsers/viewers and defines the next set of product enhancements to pursue.

The goal is not to turn HyperBrowse into a general-purpose editor or digital asset manager. The goal is to make it feel more compelling, more polished, and more practical for day-to-day browsing while preserving the product's core advantage:

- immediate startup
- fast first thumbnail visibility
- smooth large-folder scrolling
- fast viewer open and navigation
- minimal dependency growth

## 2. Executive Summary

HyperBrowse already has the right technical foundation for a fast native browser/viewer:

- native Win32 shell
- async folder enumeration
- virtualized browser rendering
- background metadata extraction
- background thumbnail scheduling
- viewer prefetch
- RAW preview support
- batch convert and slideshow support

What it lacks versus the strongest competing products is not core throughput architecture. It lacks workflow depth and presentation polish.

### Most compelling features to add next

1. Native file actions: copy, move, rename, delete to Recycle Bin, permanent delete, reveal in Explorer, copy path.
2. A visual polish pass on the browser, viewer chrome, and status surfaces without introducing a new UI framework.
3. Fast in-folder filter/search and stronger culling affordances.
4. A richer metadata-forward browse experience for the selected item.
5. A lightweight compare/cull workflow built on the existing fast viewer path.

If only the top three enhancements are implemented, HyperBrowse becomes materially more competitive without undermining startup or rendering performance.

## 3. Review Basis

This review was derived from the current HyperBrowse spec/implementation and the positioning of these comparable products:

- FastStone Image Viewer
- XnView MP
- ImageGlass
- IrfanView
- qView

## 4. Competitive Review

### 4.1 FastStone Image Viewer

FastStone is the strongest reference point for practical browse workflow.

What it does well:

- Explorer-like browser workflow
- strong fullscreen/viewer affordances
- copy/move/delete workflow built into browsing
- compare workflow for culling
- metadata visibility during viewing
- ratings/tagging and strong batch tool surface
- strong folder and destination workflow for file management

What HyperBrowse should learn from it:

- file operations belong directly in the browse experience, not only in Explorer
- hidden or compact viewer controls can be powerful when they stay fast
- culling features are more valuable than broad editing features for this product category

What HyperBrowse should not copy:

- the gradual drift into a light editor suite
- large effect/filter surface area
- legacy-feeling command density

### 4.2 XnView MP

XnView MP is the reference point for power-user depth.

What it does well:

- broad format support
- strong batch convert/rename capabilities
- metadata visibility and editing
- compare and search utilities
- large feature surface for advanced users

What HyperBrowse should learn from it:

- metadata and batch workflows are sticky power-user features
- search/filtering inside large result sets is important
- filename, date, and metadata-driven operations matter for photo-heavy users

What HyperBrowse should not copy:

- sprawling command surface
- complex product feel
- management features that require database-heavy architecture before the core browse flow is perfect

### 4.3 ImageGlass

ImageGlass is the strongest reference point for contemporary visual appeal in a lightweight viewer.

What it does well:

- clean, modern, minimal chrome
- pleasant typography and spacing
- good theme and layout customization story
- lightweight positioning

What HyperBrowse should learn from it:

- visual cleanliness matters even for performance-sensitive tools
- a modern UI does not require a heavyweight framework if the visual system is coherent
- users perceive products as faster when the interface feels intentional and calm

What HyperBrowse should not copy:

- customization-first priorities that distract from browse-management fundamentals
- overinvestment in theme extensibility before core workflow polish

### 4.4 IrfanView

IrfanView is the reference point for compactness and "fast utility" credibility.

What it does well:

- compact native feel
- broad batch tooling
- fast directory/viewer workflow
- very low-overhead ethos

What HyperBrowse should learn from it:

- startup discipline is a feature
- power users reward keyboard-first workflow
- browse and batch tools can coexist without a heavy shell

What HyperBrowse should not copy:

- dated visual language
- plugin-driven breadth before core UI polish

### 4.5 qView

qView is the reference point for extreme visual simplicity and instant-feeling viewing.

What it does well:

- no clutter
- quick open
- fast image switching
- preload-focused viewing model

What HyperBrowse should learn from it:

- a viewer window should prioritize image presence over chrome
- minimalism is most successful when paired with fast navigation

What HyperBrowse should not copy:

- overly sparse browse features
- viewer minimalism that leaves browsing and file actions underpowered

## 5. Product Positioning Recommendation

HyperBrowse should aim for this position:

> FastStone-level browse practicality, ImageGlass-level visual cleanliness, and IrfanView/qView-level responsiveness.

That implies the following strategic rules:

1. Stay a browser/viewer first, not an editor.
2. Add workflow features that improve culling, navigation, and file handling before adding niche media features.
3. Improve visual appeal with better chrome, layout, and state design, not with framework churn.
4. Do not introduce background work that harms cold startup or visible-thumbnail latency.

## 6. Current HyperBrowse Strengths

Based on the current spec pack and implementation, HyperBrowse already has a strong base:

- async folder enumeration
- folder watch support
- thumbnail and details modes
- sort modes for filename, modified date, file size, dimensions, type, and random
- metadata extraction and image information surfaces
- separate viewer window with prefetch
- slideshow support
- batch convert support
- dark/light theme switching
- optional nvJPEG acceleration path
- Nikon RAW support and embedded-preview-first behavior
- startup timing instrumentation already in place

These strengths mean the next product gains do not need a platform reset. They need focused workflow and UX work.

## 7. Primary Gaps Versus Competitors

### 7.1 Workflow gaps

- no integrated copy/move/delete/rename workflow
- no recent-destination or favorite-destination file actions
- no in-folder search/filter box
- no compare/cull mode
- no date-taken sort
- no streamlined reveal/open-in-Explorer path for current selection

### 7.2 Visual gaps

- current chrome is functional but visually utilitarian
- browser palette is mostly flat solid fills
- there is no modern command surface beyond menus/context menus
- selection, hover, loading, and empty states can feel more intentional
- typography and iconography do not yet convey a finished product

### 7.3 Feature-scope discipline gaps

- some competitor features are tempting but would expand the product too broadly
- HyperBrowse needs a sharper distinction between "high-value browse workflow" and "editor/organizer bloat"

## 8. Design Review: How To Make It More Visually Appealing Without Significant Dependencies

The correct visual strategy is not a WinUI/Qt/Web rewrite. The correct strategy is a better visual system on top of the current native shell.

### 8.1 Visual direction

Adopt a cleaner, calmer browse aesthetic:

- darker dark mode and warmer light mode neutrals
- fewer hard separators and more surface grouping
- clearer accent color for focus, hover, and selection
- more consistent padding and spacing
- better empty/loading/error states
- stronger selected-item hierarchy

The interface should look deliberate, not decorative.

### 8.2 Specific UI changes to prioritize

#### Browser pane

- redesign thumbnail cells as lightweight cards or framed surfaces
- use a subtle selected background plus a crisp outline instead of a single flat fill
- add a cleaner filename area with better line height and truncation behavior
- show compact metadata only for selected items or larger thumbnail sizes
- improve placeholder states so loading looks intentional rather than unfinished

#### Details mode

- modernize row density and header styling
- make hover, focus, and selection states more legible
- use clearer alignment for size, dimensions, and date columns
- add optional compact badges or subtle secondary text instead of dense full-row clutter

#### Status and command surfaces

- keep the menu bar, but add a compact contextual action strip when selection exists
- group status data into readable segments instead of one long dense readout
- surface background activity with lightweight text or status chips, not modal UI

#### Viewer window

- keep the image dominant
- add a minimal transient HUD for zoom, file position, and quick actions
- avoid persistent heavy toolbars in fullscreen mode
- use subtle overlays, not large panels

### 8.3 Dependency policy

Allowed:

- Win32 common controls already in use
- DWM frame theming already in use
- additional built-in Windows APIs such as `IFileOperation`, `TaskDialogIndirect`, `SHOpenFolderAndSelectItems`, `SetWindowTheme`, `DirectWrite`, or other inbox shell/UI APIs where justified
- system fonts and built-in icon fonts or small embedded vector/icon resources

Avoid by default:

- Qt
- WPF
- WinUI rewrite
- WebView-based chrome
- plugin frameworks
- a startup-time thumbnail database

### 8.4 Performance-safe visual rules

- no blur, acrylic, or transparency effects behind the scrolling thumbnail surface
- no per-thumbnail child windows
- no expensive per-cell allocations during paint
- no continuous shimmer/skeleton animations
- no startup-time theme asset loading that delays shell visibility
- if Mica or DWM backdrop effects are used, restrict them to window frame/chrome surfaces only

## 9. Prioritized Enhancement Backlog

## 9.1 `P0` - Highest-value next work

### `P0.1` Native file management workflow

Add:

- Copy
- Move
- Rename
- Delete to Recycle Bin
- Permanent Delete
- Reveal in Explorer
- Open containing folder
- Copy full path
- File properties

Why this is high priority:

- it is the most obvious gap versus FastStone and other practical viewers
- it reduces context switching to Explorer during culling
- it directly answers a common browse workflow need without changing product identity

Implementation guidance:

- use `IFileOperation` so the app gets native Windows semantics, conflict handling, recycle support, and shell compatibility without external dependencies
- run operations asynchronously from the UI point of view
- surface progress/cancel state in a lightweight non-modal way
- preserve selection and scroll position where practical after operations complete
- rely on folder watch plus targeted model updates rather than full reloads

Shortcuts to support:

- `Delete`
- `Shift+Delete`
- `F2`
- `Ctrl+Shift+C` for copy path
- `Ctrl+E` or equivalent for reveal in Explorer

### `P0.2` Visual polish pack

Add:

- refined theme tokens for light/dark
- better thumbnail cell styling
- improved selected/hover/focus states
- a compact selection action strip
- cleaner status bar grouping
- consistent iconography
- better loading/empty/error visuals

Why this is high priority:

- it changes first impressions immediately
- it improves perceived quality without broadening feature scope
- it can be done with built-in APIs and current rendering patterns

Implementation guidance:

- centralize color, spacing, border, and typography tokens
- keep painting allocation-free
- prefer simple shape language and strong hierarchy over decorative effects

### `P0.3` In-folder filter/search

Add:

- a filter box for the current browser result set
- filename and extension filtering first
- optional metadata token filtering later
- filtered-count feedback in the status bar

Why this is high priority:

- large folders become easier to work with immediately
- it improves culling and navigation without requiring a database-backed organizer model
- competitors frequently offer some form of search/filter and users expect it

Implementation guidance:

- operate on the in-memory model
- debounce lightly if needed
- do not perform blocking disk scans or startup indexing
- preserve virtualization and thumbnail priority behavior under filtering

### `P0.4` Better metadata-forward browsing

Add:

- selected-item info strip or compact details panel
- date taken sort when metadata exists
- compact dimensions/date/camera presentation for selected items and large thumbnails
- clearer viewer metadata overlay toggle

Why this is high priority:

- it helps users decide quickly which image to open, keep, move, or delete
- it leverages infrastructure already present in the codebase
- it is more aligned with the product than image editing features

Implementation guidance:

- never block paint on metadata
- use cache-first behavior
- display partial metadata progressively as it becomes available

## 9.2 `P1` - Strong follow-up work

### `P1.1` Compare/cull lite

Add a lightweight compare mode focused on speed:

- compare current image to previous/next
- optional 2-up layout in the viewer
- fast toggle between compared images
- synchronized zoom only if it does not complicate the initial release too much

Why it matters:

- compare is one of the strongest reasons users stay in FastStone/XnView-style products
- it is valuable for culling similar shots

Scope guard:

- do not build a full asset-review suite first
- start with two-image compare only

### `P1.2` Recent destinations, favorites, and navigation shortcuts

Add:

- recent copy/move destinations
- pinned favorite destinations
- quick access to recent folders
- optional breadcrumb/path surface for the current folder

Why it matters:

- it makes file actions materially faster after `P0.1` lands
- it improves the feel of the application as a daily-use browser rather than a viewer-only tool

### `P1.3` RAW+JPEG paired operations

Add an optional paired-action mode for photographers:

- copy/move/delete RAW and matching JPEG together
- reveal paired items clearly in the UI

Why it matters:

- FastStone explicitly supports paired RAW/JPEG handling
- this is a strong niche differentiator for a photo-heavy Windows browser

## 9.3 `P2` - Valuable, but only after the above

### `P2.1` Optional persistent thumbnail cache

This remains valuable, but it should not be part of the immediate enhancement pass.

Why it is deferred:

- it changes cache, invalidation, and storage complexity materially
- it can accidentally add cold-start work if implemented carelessly
- the current product direction should first improve visible browse workflow and polish

If explored later, require:

- fully lazy initialization
- no startup scan
- benchmark proof that warm large-folder revisits improve materially without harming cold start

### `P2.2` Ratings/tags

Useful, but not the best next move.

Why deferred:

- it pushes the product toward organizer territory
- it introduces storage and persistence policy decisions
- file actions, filtering, and compare are more universally valuable first

### `P2.3` Batch rename

High utility, but below basic file actions in priority.

Why deferred:

- rename is essential, but batch rename is a second-step power feature
- it should come after the core file-operation layer exists

## 10. Features That Are Compelling But Should Not Be Prioritized Now

Do not prioritize these ahead of the backlog above:

- editing tools beyond current scope
- annotation/draw tools
- plugin ecosystems
- duplicate finder
- face detection
- library/database organization features
- extensive theme marketplace support

These may be useful later, but they dilute the speed-first browse/viewer identity.

## 11. Performance Guardrails For Every Enhancement

Every item above must obey the existing performance strategy.

### Non-negotiable rules

1. No new heavy work on the UI thread.
2. No startup-time indexing or cache maintenance.
3. No new full-folder blocking passes before visible placeholders paint.
4. No visual effect that increases scroll hitches in thumbnail mode.
5. No metadata fetch performed synchronously during paint.
6. File operations must feel asynchronous even when individual files are slow.

### Benchmark gates

For `P0` and `P1` changes, verify at minimum:

- cold startup median does not regress materially
- first placeholder paint does not regress materially
- first visible thumbnail latency does not regress materially
- rapid-scroll hitch behavior does not regress materially
- viewer open latency does not regress materially

Recommended working rule:

- treat a regression beyond roughly 5% to 10% in the primary browse/viewer metrics as a stop-and-measure event before landing more surface polish

## 12. Suggested Implementation Order

### Pass A

- `P0.1` Native file management workflow
- `P0.2` Visual polish pack

Reason:

- these two changes create the largest immediate user-visible improvement

### Pass B

- `P0.3` In-folder filter/search
- `P0.4` Better metadata-forward browsing

Reason:

- these deepen the browse experience without significant architectural risk

### Pass C

- `P1.1` Compare/cull lite
- `P1.2` Recent destinations and favorites
- `P1.3` RAW+JPEG paired operations

Reason:

- these make HyperBrowse genuinely sticky for photo-heavy workflows

## 13. Final Recommendation

The best next version of HyperBrowse is not the one with the most features. It is the one that feels fastest, looks more finished, and keeps users inside the app during browse-and-cull workflows.

The highest-value path is:

1. add native file management
2. modernize the chrome and state design
3. add fast filtering and richer metadata-driven browsing
4. add a lightweight compare/cull workflow

That path moves HyperBrowse closer to the best parts of FastStone, ImageGlass, and IrfanView without taking on their weakest tradeoffs.