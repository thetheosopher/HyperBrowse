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
| 3 | C8 dialog accessibility | Automated names, roles, states, and focus restoration are covered by smoke tests; the owner removed separate NVDA/JAWS and Inspect sign-off as a release requirement on 2026-11-21 |
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

## Phase 3 design note: dialog accessibility

### Existing coverage and gap

The main window and Experimental Settings already reuse
`MainWindowAccessibility`, an MSAA `IAccessible` provider returned through
`WM_GETOBJECT`. The current Settings smoke checks accessible names and roles for
the selected Slideshow tab, Transition style field, and Apply action, but it
does not exercise accessible focus, page changes, or dynamic checked/disabled
state transitions.

### Approved design

- Keep the existing provider and custom-rendered controls; do not add a new
  accessibility framework or dependency.
- Extend `HyperBrowseSettingsSmoke` to verify the initially focused control,
  accessible focus and selected state when switching tabs, focus on the first
  control after a page change, and checked/unavailable state changes for
  automatic thumbnail-cache sizing.
- Assert using accessible names, roles, and states rather than pixel
  coordinates. Fix only gaps exposed by those checks.
- Keep high-contrast, theme, DPI, and larger-text checks as visual sign-off.
  Separate NVDA/JAWS and Inspect/UI Automation sign-off was removed by the
  owner on 2026-11-21.

### Out of scope

Replacing custom controls or adding a second provider.

## Phase 4 design note: in-place image label rename

### Existing behavior and constraints

The thumbnail grid paints image names itself. Details mode uses an owner-data
ListView, so both modes need an editor owned by the browser pane rather than
relying on a conventional ListView label edit. F2 currently routes to
`StartRenameSelectedImage`, which uses the modal rename prompt and then the
existing asynchronous shell rename path. The folder tree already demonstrates
inline editing with Enter/focus-loss commit and Escape cancel.

### Selected design

- When browser focus is in either view and one non-directory file is selected,
  F2 begins editing its displayed filename in a temporary native Edit child.
  The name stem is selected initially, matching the current rename dialog.
- Enter, Tab/Shift+Tab, or focus loss commits; Escape cancels. Tab traversal
  continues through the existing main-window focus sequence. Defer focus-loss
  commit until the new target has focus and pass that target through file
  operation completion. An unchanged or cancelled edit starts no operation.
- Do not force focus back to the browser after a focus-loss commit.
- Keep the modal rename dialog as fallback when focus is outside the browser,
  the target is not an eligible visible file, or the inline editor cannot be
  created.
- Reuse existing rename validation and `FileOperationType::Rename`. Capture
  the source path before editing, confirm it is still current and present at
  commit time, and let the existing operation/folder-watch path refresh the
  model rather than changing the item optimistically.
- Add focused smoke coverage for both browser views, commit/cancel behavior,
  and asynchronous refresh. Review the rendered editor at the end of the plan.

### Out of scope

Batch rename, folder-tree rename changes, optimistic model mutation, and
changes to the existing shell conflict policy.

## Phase 5 design note: simplified title bar

### Existing behavior and constraints

`MainWindow::UpdateWindowTitle` already sets the title to `HyperBrowse` or
`HyperBrowse - <full folder path>`; it does not append view, recursive-browsing,
or theme state. The two-column owner-drawn status strip currently shows folder,
selection, profile, and activity details, but not those three browser states.
The View and Appearance menus already expose their check states.

### Selected design

- Keep the full folder path in the title for identification across multiple
  main-window instances. The title must remain unchanged when view mode,
  recursive browsing, or theme changes.
- Add concise `View`, `Recursive`, and `Theme` values at the beginning of the
  secondary status text, before selection and profile details. Keep the menu
  check states as an alternate discovery path; put the browser state first so
  it retains priority when the existing status text is clipped at narrow
  widths.
- Set the owner-drawn status control's window text to the same concise state
  summary so native accessibility can expose it. Verify the window-level
  accessible name continues to include the active folder path.
- Add a focused smoke for the title and accessibility names, changing all
  three settings, two windows with distinct paths, and narrow-window geometry.

### Out of scope

Changing the full-path title to a leaf-only title, redesigning the status strip
or menus, and replacing the existing main-window accessibility provider.

## Phase 6 design note: Quick Actions polish

### Existing behavior and constraints

The Quick Actions panel currently builds rows from favorites only, although
recent destinations remain available in the File menu and B3 calls for both.
Rows already show image-count metadata and support one-click Copy/Move, shell
drops, and an in-app thumbnail drag path to a row; that internal drag path has
no focused smoke. The image-count metadata is currently enumerated
synchronously while rebuilding rows, and Quick Send persistence stores only
the single most-recent destination path, not per-destination times.

### Selected design

- Show favorites first and recent destinations after them, deduplicating
  paths; keep shortcut assignments on favorites and preserve the existing
  favorite/recent remove behavior.
- Move image-count enumeration to a bounded background worker. Keep the
  current row text while a count is pending, then refresh it from the
  worker-result message; do not enumerate again when a tooltip is requested.
- Add a per-row tooltip with the row's image count and a local last-used
  date/time, or `Never` when unused. Persist a bounded per-destination UTC
  FILETIME history through the existing Quick Send persistence path, updating
  it when a destination operation starts. Include the same information in the
  row's accessible description.
- Keep the existing in-app thumbnail-to-row drag implementation and add a
  focused smoke that verifies the selected image reaches the row destination
  under the existing modifier-aware Copy/Move behavior, along with
  recent/favorite deduplication, asynchronous count refresh, and
  tooltip/accessibility text.

### Out of scope

Changing Copy/Move drag semantics, shell file drops, or the existing Quick Send
menu and shortcut assignment behavior.

## Ledger

| Slice | Date | Files | Validation | Result |
|---|---|---|---|---|
| 1 design note | 2026-10-25 | this file | Headless timing of the per-folder shell calls (see Evidence) | Approved; owner chose option C |
| 1 implementation | 2026-10-25 | `FolderTreeController.*`, `tests/smoke*.cpp`, `tests/CMakeLists.txt`, `docs/testing.md` | Debug build clean. `HyperBrowseFolderTreeLargeSmoke` passed (5,000 subfolders, longest UI dispatch 51.5 ms, 3 s total; before the change the shell calls alone blocked the UI thread for ~6.9 s). `HyperBrowseFolderTreeRestoreSmoke` passed. Both create only hidden windows. Visual check is the owner's at the end | Time-sliced fill (8 ms budget, 10 ms timer), "Loading..." row, generic-icon insert plus visible-first refinement, presence applied through an item map (no tree walks), selection restore gated on fill completion |
| 2 implementation | 2026-10-25 | `SystemTheme.h`, `ThemeTokens.h`, `BrowserPane.cpp`, `ViewerWindowContextMenu.cpp`, `MainWindowAppDialogs.cpp`, `MainWindowExperimentalSettings.cpp`, `tests/smoke_policy.cpp` | Debug build clean; `HyperBrowseThemeTokensSmoke` (now with forced high-contrast and forced-off checks) and the three `*PolicySmoke` tests passed. Headless only; visual and real high-contrast review is the owner's | Added `HighContrastOverrideForTesting()` seam and `MakeMenuSelectionFill`. Viewer context menu uses the highlight fill and highlight text in high contrast; About link buttons use system colors only in high contrast; settings edit brush initial value now uses the field token. Light browser-pane placeholder and stripe colors use `windowBackground`. Deviations from the note: the dark browser-pane colors stay local (merging them would flatten the row stripes), and the edit background uses `fieldBackground` to match the brush `ApplyExperimentalSettingsTheme` already installs |
| 3 design note | 2026-11-15 | this file | Source audit of `WM_GETOBJECT`, the shared provider, and Settings smoke coverage | Approved by the owner; reuse the current provider and target the untested focus/page/state behavior |
| 3 implementation | 2026-11-15 | `tests/smoke_settings.cpp`, `docs/keyboard-accessibility-plan.md`, `docs/testing.md`, `specs/FUTURE-ROADMAP.md`, this file | CMake Tools Release build of `HyperBrowseTests`; `HyperBrowseSettingsSmoke` and `HyperBrowseAccessibilitySmoke` passed 1/1 each; `git diff --check` passed | Added accessible focus/page navigation and automatic-cache state-transition assertions. The owner removed separate NVDA/JAWS and Inspect/UI Automation sign-off as a release requirement on 2026-11-21; automated coverage remains |
| 4 design note | 2026-11-15 | this file | Source review of thumbnail rendering, owner-data details, file rename handling, and folder-tree inline edit | Selected the roadmap-aligned native Edit overlay and existing asynchronous rename path under the broader request to proceed with 2.8 |
| 4 implementation | 2026-11-15 | `src/browser/BrowserPane.*`, `src/ui/MainWindow*.{h,cpp}`, `src/ui/WindowAsyncMessageRouter.*`, `tests/smoke*.{cpp,h}`, `tests/CMakeLists.txt`, `README.md`, `docs/user-guide.html`, `docs/testing.md`, `specs/FUTURE-ROADMAP.md`, this file | CMake Tools Release builds of `HyperBrowse` and `HyperBrowseTests`; `HyperBrowseInlineRenameSmoke`, `HyperBrowseFileRenameSmoke`, `HyperBrowseFileOperationSmoke`, `HyperBrowseRuntimePolicySmoke`, and `HyperBrowseSmoke` passed 1/1 each; `git diff --check` passed | F2 inline rename works in both views; Enter, Tab, focus loss, Escape, modal fallback, and async refresh are covered. Owner visual review passed 2026-11-15; the owner removed a separate cross-pane focus-loss walkthrough as a release requirement on 2026-11-21 |
| 5 design note | 2026-11-15 | this file | Source audit of main-window title/status updates, status-strip painting, menus, and window/status accessibility | User was unavailable; applied the recommended default to retain the full path and move concise browser state to the status strip |
| 5 implementation | 2026-11-15 | `src/ui/MainWindowLayout.cpp`, `tests/smoke_main_window.cpp`, `tests/smoke.cpp`, `tests/smoke_scenarios.h`, `tests/CMakeLists.txt`, `README.md`, `docs/user-guide.html`, `docs/testing.md`, `specs/FUTURE-ROADMAP.md`, this file | CMake Tools Release builds of `HyperBrowse` and `HyperBrowseTests`; `HyperBrowseWindowChromeSmoke`, `HyperBrowseAccessibilitySmoke`, and `HyperBrowseResponsivePanelSmoke` passed 1/1 each; `git diff --check` passed | Preserved the full-path title; surfaced view, recursive, and theme state in the status bar and its accessible name. Multiple windows and narrow geometry are automated; owner visual review passed 2026-11-15; separate screen-reader name review was removed as a release requirement on 2026-11-21 |
| 6 design note | 2026-11-15 | this file | Source audit of Quick Actions row construction, persistence, tooltip routing, image-count work, and existing thumbnail drag flow | User was unavailable; applied the recommended full B3 scope while preserving existing modifier-aware Copy/Move behavior |
| 6 implementation | 2026-11-15 | `src/services/QuickAccessDirectoryMetadataService.*`, `src/ui/QuickAccessDestinationBuilder.*`, `src/ui/QuickAccessLayout.h`, `src/ui/QuickAccessPainter.cpp`, `src/ui/QuickSendPersistence.*`, `src/ui/MainWindow*.{h,cpp}`, `src/ui/WindowAsyncMessageRouter.*`, `tests/smoke_main_window.cpp`, `tests/smoke_model.cpp`, `tests/smoke_policy.cpp`, `tests/smoke.cpp`, `tests/smoke_scenarios.h`, `tests/smoke_support.h`, `tests/CMakeLists.txt`, `README.md`, `docs/user-guide.html`, `docs/testing.md`, `specs/FUTURE-ROADMAP.md`, this file | CMake Tools Release builds of `HyperBrowse` and `HyperBrowseTests`; full Release CTest suite (40/40) passed; `git diff --check` passed | Added deduplicated favorite/recent rows, bounded background image counts, per-destination last-use persistence, and hover/accessibility hints. The existing thumbnail-to-row drag path is covered end to end; owner visual review passed 2026-11-15 |
