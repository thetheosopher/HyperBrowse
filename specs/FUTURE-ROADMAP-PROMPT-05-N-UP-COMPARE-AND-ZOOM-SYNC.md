# Long-Horizon Prompt 05: N-Up Compare and Synchronized Review

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Extend the shipped two-image adjacent compare into a responsive two-, three-,
and four-image review workflow. Add candidate replacement, explicit tile
focus, optional synchronized zoom and pan, and per-image rating/tag controls.
Keep the implementation native to the existing Win32 viewer and reuse the
current image, metadata, command, and persistence ownership boundaries.

Complete the feature locally, with focused tests and documentation. Do not
claim completion from a mockup, a new layout alone, or a single manual run.

## Current Cloud-Execution Hold

The user has opted out of cloud-hosted GitHub Actions. The repository's only
workflow, `HyperBrowse CI`, is manually disabled. Do not re-enable it, dispatch
it, or trigger a cloud run through a push or pull request without fresh,
explicit user authorization. Validate this task locally. If an acceptance gate
cannot be satisfied without cloud execution, stop and report that gate instead
of starting a workflow.

## Relationship to Other Work

- [The active roadmap](FUTURE-ROADMAP.md) is the source of product scope;
  this prompt is the B2 execution brief.
- Two-image compare is shipped. Preserve its established launch, adjacent
  compare, keyboard, viewer navigation, file-operation, and close behavior
  unless a deliberate, documented compatibility change is required.
- Reuse the existing ratings/tags store. Do not create a second persistence
  format or couple viewer rendering directly to disk I/O.
- B2 is independent of A5 buffer pooling, A6 GPU thumbnail scaling, B4 color
  management, and D3/A8 hosted performance calibration. Do not implement or
  unblock those items as part of this task.

## Starting Facts to Verify

Treat these as source-finding hints and confirm them in the current tree:

- `MainWindow::StartCompareSelected` currently requires exactly two selected
  images and opens a viewer with those items.
- `ViewerWindow` currently stores a current image plus an adjacent compare
  direction. `C` toggles compare, `X` activates the compared image, and
  Shift+Left/Right changes the adjacent comparison.
- `ViewerWindow` currently uses `1` for Actual Size. Do not silently repurpose
  this or any other existing key while implementing the roadmap's proposed
  `1`/`2`/`3`/`4` tile commands.
- `UserMetadataStore` is owned by `MainWindow`, keys rating and tags by file
  path, and exposes `EntryForPath`, `SetRating`, and `SetTags`. Its save worker
  owns persistence; UI code must not synchronously wait for disk writes.
- Compare bitmaps and navigation slots currently have index-based cache state.
  Trace invalidation and content identity before generalizing that state to
  multiple tiles.
- Confirm the existing test names and coverage. Do not assume a compare test
  exists just because the feature is shipped.

## Hard Scope Boundaries

- Keep B2 inside the viewer and the existing browser-to-viewer workflow. Do
  not build a catalog, asset database, general image editor, or new review
  subsystem.
- Do not add a new rendering framework, UI toolkit, persistence service, or
  third-party dependency.
- Do not move decode, metadata persistence, or blocking file operations onto
  the UI thread.
- Do not let a tile rating/tag action mutate the browser's current multi-file
  selection as a side effect.
- Preserve the two-image path and existing shortcuts unless the final,
  documented interaction contract explicitly changes them.
- Do not expand into A5, A6, B4, performance-baseline calibration, or remote
  CI work.
- Preserve unrelated work and generated evidence. Do not reset, clean, stash,
  or revert the worktree. Do not commit or push unless separately requested.

## Phase 0: Inventory and Local Gate

1. Read `.github/copilot-instructions.md`, applicable C++ instructions,
   `docs/architecture.md`, `docs/testing.md`, the B2 roadmap entry, and the
   owning viewer, MainWindow command, metadata store, and smoke-test code.
2. Record the branch, HEAD, dirty files, local build tree, and current two-up
   behavior. Preserve all user changes.
3. Trace `Compare Selected` from selection snapshot through viewer creation.
   Trace rating/tag edits from the existing command to `UserMetadataStore` and
   its UI notification path.
4. Inventory viewer keyboard shortcuts, menus, mouse gestures, focus behavior,
   and accessibility exposure before assigning new commands.
5. Keep validation local. Do not use GitHub CLI to enable, dispatch, inspect,
   or otherwise start Actions as part of this task.

**Gate:** Do not edit behavior until the interaction rules below are written
down against the real current command, focus, data, and lifetime paths.

## Phase 1: Lock the Interaction Contract

The current source and shortcut catalog support the following initial
interaction contract. Keep it in sync with implementation and user-facing help:

| Input/state | Two-image compare | Three/four-image compare |
| --- | --- | --- |
| `Compare Selected` | Accept 2-4 selected images; retain the existing 2-image presentation. | Same command; reject fewer than 2 or more than 4 without truncating. |
| Initial order/focus | Browser's captured visual selection order; primary-selected image is focused. | Same. |
| Candidate pool | Captured visual order of images in the current browser view; selected images seed visible tiles. | Same; never consult mutable browser state from a worker. |
| Tile focus | Click tile or `Ctrl+Tab` / `Ctrl+Shift+Tab`; bare `Tab` remains the overlay toggle. | Same, cycling among visible tiles. |
| Candidate replacement | `,` / `.` replace the focused tile with previous/next unused candidate, wrapping. `Shift+Left/Right` retain existing adjacent-compare behavior. | `,` / `.` replace the focused tile. `Shift+Left/Right` perform the same previous/next candidate action for compatibility. |
| `C` / `X` | Preserve shipped compare toggle and activate-compared-image behavior. | `C` exits compare to the focused image; `X` promotes the focused tile to the primary slot without dropping other tiles. |
| `Page Up` / `Page Down` | Preserve normal viewer previous/next navigation. | Exit compare and navigate the focused image normally. |
| Delete / file operations | Preserve current viewer behavior. | Delete acts on the focused image and exits compare before dispatch; other operations target the focused image. Reopening compare uses a fresh browser snapshot. |
| Layout | Two equal side-by-side panes. | Three tiles use a 2x2 grid with one empty cell; four tiles fill it. |
| View sync | Enabled by default for compare. A checked viewer context-menu item toggles it. | Same. Linked zoom uses each tile's fit-relative scale; linked pan uses normalized image center and clamps per tile. |
| Rating/tags | Viewer context menu edits only the focused file through `UserMetadataStore`. | Same. |

The browser supplies candidates from `CollectItemsForScope(false)` and the
ordered selection snapshot from `CollectItemsForScope(true)`; map selected paths
back to the immutable candidate vector using the project's file-path equality
helper. Confirm this mapping at implementation time. `Ctrl+Tab` and
`Ctrl+Shift+Tab` are not currently present in `ViewerShortcuts()`; `Tab` is
already assigned to toggle overlays, and `1` remains Actual Size. Do not
repurpose existing keys.

Use the following additional defaults unless current code or an explicit user
requirement makes them incompatible:

- `Compare Selected` accepts two through four selected images. Preserve the
  existing two-image behavior. With fewer than two or more than four, show a
  clear message; never silently truncate a larger selection.
- Initial tile order follows the captured browser selection order. The
  primary-selected image receives initial keyboard focus.
- Two images remain side by side. Three images use a two-by-two grid with the
  final cell empty; four images fill the grid. Resize and DPI changes must
  recompute stable, non-overlapping tile bounds without changing slot identity.
- Capture a stable, ordered candidate list from the same folder/view when
  compare starts. Selected files seed the visible slots. Comma/period move the
  focused slot to the previous/next candidate in that captured order, skipping
  paths already present in another slot and wrapping at the list boundary.
  If a safe candidate list cannot be supplied through the current ownership
  model, resolve that boundary explicitly before coding; do not query mutable
  browser UI state from a viewer worker.
- Clicking a tile focuses it. Define keyboard tile focus and candidate
  replacement as separate operations. Preserve `1` for Actual Size unless the
  user explicitly approves a shortcut change; use a conflict-free modified
  key or another discoverable binding for tile focus. Confirm every binding
  against the complete existing shortcut map.
- Provide a visible, discoverable control for synchronized zoom/pan and expose
  its state accessibly. Choose and document the default state; synchronized
  review should be the default unless an existing viewer contract argues
  otherwise.
- When synchronization is enabled, zoom follows the same scale relative to
  each tile's fit scale. Pan follows the same normalized image center and is
  clamped independently at each tile's edges. Focusing another tile must not
  unexpectedly reset the shared view.
- When synchronization is disabled, each tile retains its own zoom/pan state.
  Turning synchronization back on aligns the other tiles to the focused tile
  using the normalized zoom/pan rule above.
- Each tile's rating action applies only to that tile's file path, supports
  the existing 0-5 rating contract (0 clears), and updates the visible state.
  Each tile's tag action edits only that file's tags, reusing the existing tag
  normalization and persistence path.
- Define how rotation, fit modes, candidate replacement, deletion, rename,
  and removal from the source folder affect tile state. Never leave a tile
  showing a cache entry for a different file after an index is reused.

The roadmap's numeric-key wording is not authority to break the current `1`
Actual Size shortcut. If the proposed key mapping cannot coexist, document a
compatible replacement and update the roadmap and user guide in the same
change.

**Gate:** The candidate source, slot ordering, invalid selection behavior,
focus, key map, synchronization defaults, and per-tile metadata semantics are
unambiguous before the first behavior edit.

## Phase 2: Model Tiles and Layout

1. Introduce the smallest viewer-owned compare-session/tile state that fits
   existing ownership. Keep stable file identity separate from the display
   index. Avoid parallel copies of browser selection state.
2. Define a tile's bounds, file identity, load generation, image/decode state,
   rotation, zoom/pan state, and error/loading presentation. Reuse suitable
   existing cache and asynchronous decode paths.
3. Generalize two-up rendering to two, three, and four tiles. Keep the existing
   two-up geometry and output stable where practical. Use the project's current
   Direct2D/DirectWrite and GDI ownership split.
4. Render empty/loading/error states per tile so one slow or failed candidate
   does not blank the other valid images. Reject stale decode completions by
   session generation and stable image identity.
5. Bound work and memory: load visible tiles first, avoid decoding the entire
   candidate list, respect the existing resource profile and memory-pressure
   behavior, and cancel obsolete requests when candidates change or the viewer
   closes.

**Gate:** Two-, three-, and four-tile layouts render correct identities at
multiple window sizes and DPI settings. Existing two-up behavior remains
intact. No decode or blocking operation runs in a window procedure.

## Phase 3: Focus, Candidate Cycling, and Synchronized View

1. Implement pointer and keyboard tile focus with a visible focus indicator
   and stable focus during asynchronous loads and resizes.
2. Implement candidate cycling only for the focused tile. Skip candidates
   already visible in another slot. Preserve the previous tile image until
   its replacement is ready, and reject stale results after rapid cycling.
3. Preserve existing two-up `C`, `X`, and Shift+Left/Right behavior, or
   document and test a deliberate replacement. Define their behavior in
   three/four-up mode rather than letting adjacent-pair assumptions leak into
   the new model.
4. Implement linked zoom/pan using the Phase 1 normalization rule. Test
   differing aspect ratios and dimensions, fit modes, rotations, and pan
   limits. Independent mode must retain each tile's view state.
5. Make the synchronization state discoverable in the viewer command surface
   and accessible name/state contract. Do not rely on an undocumented key.

**Gate:** Focus, candidate cycling, linked navigation, edge clamping, and
independent-view restoration behave deterministically under rapid interaction.

## Phase 4: Per-Tile Rating and Tags

1. Route per-tile actions to the existing `UserMetadataStore` through an
   explicit MainWindow/viewer callback or equivalent owner-boundary mechanism.
   The viewer must not own a second store or call persistence directly.
2. Apply changes to a one-path target derived from the focused tile's stable
   identity. Do not alter browser selection to reuse selection-wide commands.
3. Reuse existing rating range, zero-clear behavior, tag normalization,
   persistence error reporting, and metadata-change notification paths.
4. Refresh only the necessary browser and viewer surfaces without discarding
   selection, changing focus unexpectedly, or blocking the UI on disk I/O.
5. Handle save errors visibly and keep current in-memory state consistent with
   the store's existing retry behavior.

**Gate:** A rating or tag edit changes only the intended file, survives the
existing persistence lifecycle, and leaves browser selection and other tiles
unchanged.

## Phase 5: Focused Tests and User Documentation

Add or extend local smoke coverage for:

- Two-, three-, and four-image launch, selection order, primary focus, and
  rejection of invalid selection counts without truncation.
- Responsive tile geometry, no overlap, DPI/resize behavior, and stable slot
  identity.
- Focus changes, shortcut conflicts, candidate wrapping, duplicate avoidance,
  rapid replacement, stale completion rejection, and viewer shutdown.
- Synchronized zoom and normalized pan across different image dimensions,
  pan-boundary clamping, toggle-off independent state, and toggle-on alignment.
- Per-tile rating 0-5/clear and tags; ensure edits do not modify another tile
  or the browser selection, and verify the existing save/notification path.
- Existing two-up shortcuts, navigation, deletion/rename behavior, and
  single-image viewer use.

Update the B2 entry in `specs/FUTURE-ROADMAP.md`, `docs/user-guide.html`, and
any relevant shortcut/accessibility documentation. Keep visible labels,
tooltips, keyboard help, and tests consistent. Do not claim cloud CI validation;
the workflow is disabled. Run the repository's local Debug and Release build
and CTest gates, plus focused smoke tests, and record the exact commands and
results. Perform manual review at practical window sizes and at 100%, 150%,
and 200% display scaling when available.

## Completion Criteria

This prompt is complete only when:

- The existing two-up compare remains usable and the viewer supports two,
  three, and four stable comparison tiles.
- Selection, candidate cycling, focus, and shortcut semantics are documented
  and do not silently break existing commands.
- Synchronized zoom/pan works across unequal images, remains clamped, and can
  be disabled without losing each tile's independent view state.
- Per-tile ratings and tags update only the intended file through the existing
  metadata store and persistence lifecycle.
- Async loading, cancellation, stale-result rejection, file operations, and
  close behavior remain safe and responsive.
- Focused automated tests and local Debug/Release CTest pass; user-facing
  documentation matches the implementation.
- No GitHub Actions workflow was enabled or dispatched, no A5/A6/B4 scope was
  added, and no unrelated work was committed.

If a product interaction remains ambiguous, an ownership boundary cannot be
proved, or a test environment is unavailable, stop at that gate and report the
specific decision/evidence needed. Do not conceal a shortcut conflict or
weaken the existing compare behavior to force completion.
