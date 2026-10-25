# Long-Horizon Prompt 10: UI and Design Polish

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Resolve the UI and design observations selected by the product owner on
2026-10-25. Each phase starts with a short design note. Implementation begins
only after the owner approves that note. Visible changes are validated by
focused tests; the owner reviews visuals at the end of the plan.

Not selected (parked, not rejected): unifying the settings dialogs, empty and
first-run states, and shortcut/command discoverability.

## Phases (owner-ranked order)

| Phase | Item | Decision already made |
|---|---|---|
| 1 | Folder tree UI-thread stall | Move work off the UI thread; show a "Loading..." placeholder node |
| 2 | Theme leftovers and high-contrast audit | Tokenize only values within the existing 6-step similarity rule; add a high-contrast smoke; role-specific colors stay local |
| 3 | C8 dialog accessibility | Automated UIA names, roles, states and focus restoration with smoke tests; NVDA/JAWS verification stays manual |
| 4 | C6 in-place label rename | Thumbnail grid and details list both; keep the dialog as fallback |
| 5 | C9 simplified title bar | Title shows folder name or path only; view mode, recursive and theme move to the status bar and menus |
| 6 | B3 Quick Send polish | Hover hint (image count, last used) plus an in-app drag source from thumbnails to rows |

## Operating rules

Same as the code-quality plan: read `.github/copilot-instructions.md`,
`.github/instructions/cpp.instructions.md`, `docs/architecture.md` and
`docs/testing.md` first; no commits unless asked; keep UI-thread, cancellation,
stale-result and HWND-lifetime rules; update README, user guide, `docs/` and
tests in the same slice as a behavior change; run focused tests only; never run
the packaging target. UI smokes need the unlocked 2560x1440 desktop (check
`System.Windows.Forms.Screen` and that `LogonUI` is not running first).

## Phase 1 design note: folder tree stall

### Evidence (measured 2026-10-25, local NVMe, 5,000 subfolders)

`FolderTreeController::ApplyChildren` inserts every child on the UI thread.
Each `InsertItem` calls `QueryShellItemInfo` (two `SHGetFileInfoW` calls) plus
`ShouldShowFolder` (`GetFileAttributesW`). Headless timing of exactly those
calls:

| Call | 5,000 folders | Per folder |
|---|---|---|
| `GetFileAttributesW` | ~50 ms | 0.01 ms |
| `SHGetFileInfoW` display name + icon | ~3,400 ms | 0.68 ms |
| `SHGetFileInfoW` open icon | ~3,400 ms | 0.68 ms |
| Total | ~6,900 ms | 1.4 ms |

So a folder with 5,000 children freezes the window for about 7 s before
`TreeView_InsertItem` cost, and longer on network or slow media.
`SHGFI_USEFILEATTRIBUTES` (no real lookup, generic folder icon, leaf name) costs
about 77 ms for the same 5,000 calls, roughly 40x cheaper. The same shell calls
on a worker thread cost the same, so moving them off-thread removes the freeze
but not the total work.

Second suspect, not yet measured in the app: the child-presence completion
handler calls `FindItemByPath` once per result. `FindItemByPath` is a recursive
walk over the whole tree with a `TVM_GETITEM` per node, so applying 5,000
results is O(n^2) UI-thread work. `pendingChildPresenceItems_` already holds
the `HTREEITEM`s for the request, so the path search is unnecessary.

Enumeration itself is already async (`FolderTreeEnumerationService`), so the
worker side is fine; the cost is all in applying results.

### Proposed design

1. **Presence apply without tree walks** (no visual change). Build a
   path-to-item map from the request's `pendingChildPresenceItems_` entry and
   apply results through it. Guard with the existing
   `childPresenceRequestId` stale check.
2. **Time-sliced `ApplyChildren`** (no visual change). Replace the inserting
   loop with a queue drained in slices of about 8 ms (posted message to the tree
   owner, as the controller already does for async results). While a node is
   filling it shows a "Loading..." child (the existing empty placeholder text
   becomes visible text, and is removed when the last slice lands). Redraw is
   suspended per slice. `RequestChildPresence` for the batch is issued after
   the last slice. `ContinueSelectingFolder` runs only when the target's
   parent has finished filling, so restore-selection and expand-to-path keep
   working. A new request, `Refresh`, `Initialize` or `Cancel` drops the queue
   (stale-result rule).
3. **Icon and name resolution** (owner decision needed, see below).

### Decision needed from the owner: icon fidelity

| Option | Behavior | Cost |
|---|---|---|
| A. Generic icons for child folders | Fastest; custom folder icons (desktop.ini, localized names) lost for children | ~0.08 s per 5,000 |
| B. Real lookup, time-sliced only | Identical visuals; large folders fill in over several seconds but never block | ~7 s per 5,000, responsive |
| C. Hybrid (recommended) | Insert with generic icon and leaf name immediately, then refine icon index and display name in the same time-sliced passes, visible rows first | tree usable in ~0.1 s; custom icons settle over a few seconds |

Recommendation: 1 + 2 + option C. Roots (drives, Desktop, Documents, Pictures)
keep the real lookup synchronously because there are only a handful.

### Tests

- New headless-friendly smoke (`--folder-tree-large`): create a temp directory
  with 5,000 subfolders, expand it, and assert that the UI thread is never
  blocked longer than a budget (measure the longest gap between pumped messages
  during the fill), that all 5,000 nodes appear, that presence results land,
  and that selection restore still works. Register it in `tests/CMakeLists.txt`
  and document it in `docs/testing.md`. It needs a window, so it runs on the
  desktop.
- Existing: `HyperBrowseFolderTreeRestoreSmoke`, `HyperBrowseSmoke`.
- Log the apply time (`FolderTreeController::ApplyChildren took N ms`) with the
  existing diagnostics helper so the before/after is recorded in the ledger.

### Out of scope

Pagination or a cap for huge directories (owner chose async loading only);
changing which folders are shown; changing the tree's visual design.

## Phase 2 design note: theme leftovers and high contrast

### Audit (36 hard-coded `RGB(...)`/`ColorF` values in 9 files)

Compared with the shared tokens in `ThemeTokens.h` using the existing
6-step similarity rule (every channel within 6 of an existing token):

| Location | Value(s) | Verdict |
|---|---|---|
| `BrowserPane.cpp` dark: (18,22,27) (23,27,33) (26,31,37) (27,32,38) | within 6 of dark `windowBackground` (24,28,32) | tokenize (four near-duplicates collapse to one) |
| `BrowserPane.cpp` light: (245,248,252) (241,245,250) | within 6 of light `windowBackground` (243,245,248) | tokenize |
| `BrowserPane.cpp` light: (236,240,245) (228,234,242) (236,243,251) (201,210,220) (96,105,115) and dark (34,50,68) (152,161,171) (138,189,239) | 7+ away from every token | role-specific, stay local, named constants |
| `ViewerWindowContextMenu.cpp` light selection (222,235,250) | within 6 of light `accentFill` | tokenize |
| `ViewerWindowContextMenu.cpp` dark selection (54,68,84) | 7 away from dark `accentFill` | stay local |
| `MainWindowLayout.cpp` tree line, splitter, action strip | role-specific; high contrast already maps to system colors | stay local |
| `DiagnosticsWindow.cpp` muted text | already documented as window-specific | stay local |
| `MainWindowExperimentalSettings.cpp` edit background | (247,249,252) is within 6 of light `windowBackground`; dark (45,51,59) is not | light tokenize, dark local |
| `DetailsPanelHistogramPainter.cpp` R/G/B channel colors | data visualization, must stay R/G/B | stay local; high contrast uses system text color outlines only if needed |
| `MainWindowAppDialogs.cpp` About support accent and fills | brand accent for one button | stay local |
| `MainWindowDetailsPanel.cpp` warning amber | status color | stay local |
| `ViewerWindowMessages.cpp` white brushes | overlay glyphs on dark image | stay local |

So about 10 values tokenize; the rest are named, documented local constants.

### Real gap: high contrast

Several windows resolve colors from `darkTheme_` alone, so in Windows high
contrast mode they ignore the system palette: the viewer context menu
(`PanelFillColor(darkTheme_)` plus the selection fill), the About dialog link
and support buttons, and the experimental settings edit background. The shared
tokens already handle high contrast, so these are fixed by routing them through
`ResolveThemeVariant` and using `COLOR_HIGHLIGHT`/`COLOR_WINDOW` there.

### Test

High contrast cannot be toggled from a test, so add a seam:
`SetHighContrastOverrideForTesting(std::optional<bool>)` in `SystemTheme.h`,
read by `IsHighContrastEnabled()`. Extract the per-window palette builders that
are currently private members into pure functions taking a `ThemeVariant`
(`BrowserPane` already does), then add `HyperBrowseThemeAuditSmoke`
(headless, no windows): for every window palette under the forced
high-contrast variant, assert every color is one of the `GetSysColor` values;
under Light and Dark, assert the tokenized colors equal the tokens.

### Out of scope

Redesigning the palettes, changing any visible Light or Dark color beyond the
within-6-steps collapse, and the data-visualization and status colors above.

## Ledger

| Slice | Date | Files | Validation | Result |
|---|---|---|---|---|
| 1 design note | 2026-10-25 | this file | Headless timing of the per-folder shell calls (see Evidence) | Approved; owner chose option C |
| 1 implementation | 2026-10-25 | `FolderTreeController.*`, `tests/smoke*.cpp`, `tests/CMakeLists.txt`, `docs/testing.md` | Debug build clean. `HyperBrowseFolderTreeLargeSmoke` passed (5,000 subfolders, longest UI dispatch 51.5 ms, 3 s total; before the change the shell calls alone blocked the UI thread for ~6.9 s). `HyperBrowseFolderTreeRestoreSmoke` passed. Both create only hidden windows. Visual check is the owner's at the end | Time-sliced fill (8 ms budget, 10 ms timer), "Loading..." row, generic-icon insert plus visible-first refinement, presence applied through an item map (no tree walks), selection restore gated on fill completion |
