# Keyboard Shortcuts Consistency and Coverage Plan

Status: Main-window shortcut proposals implemented. Generated documentation parity and interactive UI verification remain.

## Goal

Make frequent HyperBrowse actions quick to reach from the keyboard, consistent between the browser and viewer where their meanings match, and discoverable in menus and shortcut help. Follow familiar Windows conventions without assigning a global chord to every setting or low-frequency command.

This plan concerns command accelerators and shortcut consistency. Focus traversal, focus visuals, dialog access keys, and screen-reader exposure remain covered by the [keyboard accessibility plan](keyboard-accessibility-plan.md).

## Current-State Findings

- `ui/ShortcutCatalog.h` is the source for MainWindow accelerator-table creation and the in-app shortcut reference. It also describes Viewer shortcuts, but ViewerWindow currently dispatches most of those keys in its own `WM_KEYDOWN` handling.
- The catalog, menu labels, toolbar tooltips, in-app reference, and HTML user guide are maintained through separate code or markup. They can drift.
- `Ctrl+3` is catalogued and documented as toggling the Details Panel, but the View menu item `Show Details Panel` does not display that chord. The separate `Show Thumbnail Details` command has no accelerator. These are distinct features and should not be conflated.
- The main-window catalog checks duplicate chords. Smoke coverage separately checks Viewer chord uniqueness and selected catalog entries, but does not enforce that every bound menu command displays its chord or that high-value menu actions were considered.
- Several useful Windows conventions are already present: `F2` rename, `F5` refresh, `Alt+Enter` properties, `Alt+Left`/`Alt+Right` navigation, `Backspace` back, and `Ctrl+Shift+N` new folder.
- `F6`, `Ctrl+F`, and `Alt+Up` are not currently assigned in the main-window shortcut catalog or its manual key handling. They are strong candidates for pane traversal, filter focus, and parent-folder navigation respectively.
- Some behavior is intentionally context-specific: `Ctrl+G` means go to item number in both browser and viewer; `F7`/`F8` perform Quick Actions in both; arrow and zoom keys belong to the viewer. The main accelerator router explicitly leaves viewer windows to their own keyboard handling.

## Existing Shortcuts to Preserve and Review

Keep existing widely recognized editing and file-management chords stable unless a separate compatibility decision is approved: `Ctrl+A/C/X/V`, `Ctrl+Z/Y`, `F2`, `F5`, `Alt+Enter`, `Delete`, and `Shift+Delete`.

Current main-window view shortcuts include `Ctrl+1` thumbnails, `Ctrl+2` details mode, `Ctrl+3` Details Panel, `Ctrl+R` recursive browsing, and `+`/`-` thumbnail-size stepping. Do not reuse these chords for neighboring view toggles.

The Viewer already has a broad, context-local set for navigation, zoom, fit modes, rotation, comparison, slideshow, full screen, image information, and Quick Actions. Treat these as reserved within the Viewer even when a chord is available in the MainWindow.

Compatibility decisions for this implementation:

- Preserve `Ctrl+W` minimizing the main window and closing the viewer as an intentional, documented top-level-window distinction; do not add another conflicting binding.
- Preserve `F4` for resuming the Quick Actions filing position. HyperBrowse has no address bar, so retain the existing behavior rather than copying File Explorer's `F4` address-bar action.

`Ctrl+D` duplicates the selection in HyperBrowse, while current Windows File Explorer guidance assigns `Ctrl+D` to delete. HyperBrowse already uses `Delete` for recycling and exposes Duplicate in its menu, so preserve the current behavior for now, document it, and include it in compatibility checks rather than introducing a second delete binding.

## Proposed Assignments

| Priority | Surface | Proposal | Rationale and constraints |
| --- | --- | --- | --- |
| P0 | Details Panel | Add `Alt+Shift+P` as an alias for the existing `Ctrl+3` command. Show both chords on the View menu item and in the shortcut reference. | File Explorer uses `Alt+Shift+P` for its Details pane. Preserve `Ctrl+3` for existing users; both chords must invoke the same toggle and checked state. |
| P1 | Browser filter | Assign `Ctrl+F` to focus the existing filter field; when already focused, select its query for replacement. | `Ctrl+F` is the familiar Find chord and the filter is HyperBrowse's closest equivalent. Do not consume ordinary text editing or IME input. |
| P1 | Main-window panes | Assign `F6` / `Shift+F6` to cycle forward/backward through the major visible regions: folder tree, browser, and visible details/Quick Actions panel. | Microsoft documents F6 for cycling panes/important regions. Keep the order stable, skip hidden or unavailable regions, and reuse the existing focus model rather than creating duplicate tab stops. |
| P2 | Folder navigation | `Alt+Up` navigates to the parent of the current browser folder, independent of pane focus. | Matches File Explorer and remains distinct from `Alt+Left` history navigation. It is disabled/no-op at a root or while navigation/enumeration is unsettled; text editors retain the key. |

Do not assign a dedicated global accelerator to every View or Settings option in the first pass. `Show Subfolders`, `Show Thumbnail Details`, compact thumbnail layout, sort modes/direction, fixed thumbnail-size presets, ratings, tags, batch conversion, RAW/JPEG preferences, and diagnostics are available through menus or Settings and are less frequent, more numerous, or more context-sensitive. Preserve menu access keys and existing `+`/`-` stepping. Revisit a specific option only when usage evidence or user testing shows a recurring mouse-only bottleneck.

In particular, keep `Show Thumbnail Details` separate from the Details Panel. It changes per-thumbnail metadata presentation rather than showing the right-side panel; do not give it `Alt+Shift+P`. Consider a dedicated chord only after the first pass, with a collision and discoverability review.

## Consistency Rules

1. Reserve `Ctrl` combinations for frequent commands and familiar editing/file operations. Use function keys for established desktop actions and domain-specific navigation. Avoid bare letters in the MainWindow, where they can interfere with typing; existing bare-letter Viewer controls remain local to the Viewer and documented there.
2. Keep a chord's meaning stable across windows when the action is equivalent. Context-specific reuse is acceptable only when the focused window owns the input and the action is clearly different, as with MainWindow versus Viewer navigation.
3. Use menu access keys (`Alt` plus an underlined letter) for discoverability of less-frequent commands. Keep access keys unique within their menu scope and localize them; they are not substitutes for a global accelerator on a frequent command.
4. Do not bind a shortcut to a destructive or stateful action merely to make the menu comprehensive. Preserve confirmation, enabled-state, undo, and selection rules. Keep permanent deletion on the established `Shift+Delete` path.
5. A shortcut must have one owner in each active input context. Explicitly define precedence for text fields, modal dialogs, menus, the browser, and each viewer window. Text entry, caret movement, selection, and IME composition take priority over app commands.
6. Every new or changed chord must be visible in the relevant menu item and the in-app shortcut reference. Keep toolbar tooltips and the user guide synchronized. Aliases for the same command should be shown together and tested as aliases, not treated as conflicting command ownership.
7. Do not intercept a chord in an inactive or unrelated window. MainWindow accelerators must continue to yield to ViewerWindow; Viewer handling must remain local to its window.

## Implementation Sequence

### Phase 1: Close the verified gap

Status: Implemented and smoke-validated.

- Add `Alt+Shift+P` as a Details Panel alias while retaining `Ctrl+3`.
- Display both bindings on the View menu item, and ensure the in-app reference and user guide agree.
- Add focused smoke assertions for both chords invoking the same command, checked-state behavior, and allowed same-command aliases.
- Add a consistency check so a catalogued menu accelerator cannot silently disappear from its visible label.

### Phase 2: Add high-value navigation shortcuts

Status: Implemented and smoke-validated for catalog/controller behavior. Interactive focus verification remains.

- Add `Ctrl+F` filter focus and `F6`/`Shift+F6` pane cycling using the existing focus and command-routing ownership.
- `Alt+Up` follows the current browser folder regardless of pane focus, no-ops at a root, and is disabled while navigation or enumeration is unsettled. Text editors retain the key.
- Keep the existing `Ctrl+W` and `F4` assignments as the compatibility decisions recorded above.

### Phase 3: Make the shortcut inventory authoritative

Status: Partially implemented. MainWindow accelerators and the in-app reference use the shared catalog; the Details Panel menu label is shared and smoke-checked. General menu/user-guide generation remains deferred because those surfaces are currently hand-authored in Win32 and static HTML.

- Keep one structured definition per chord/action/context, including aliases, display text, group, and input-scope/precedence information needed by the router and help surfaces.
- Generate or validate MainWindow accelerator registration, Viewer shortcut documentation, menu display text, the in-app reference, and user-guide entries from that inventory. Viewer may retain custom `WM_KEYDOWN` dispatch; the catalog must still describe and test the same behavior.
- Extend validation to check duplicates within each active context, alias ownership, command reachability, menu-label parity, and any intentionally reused chord across separate windows.
- Keep dynamic Quick Actions character assignments in their existing separate namespace and validate collisions against each other and active text input, rather than treating them as static global accelerators.

### Phase 4: Validate usability and compatibility

Status: Runtime catalog/controller smoke tests pass. Manual interactive verification remains unavailable in this implementation pass.

- Smoke-test each chord through its real message-routing path, not only by inspecting catalog entries.
- Verify focus order and shortcut behavior with the folder tree, browser, filter, details panel, Quick Actions, settings fields, modal dialogs, viewer, and multi-image comparison active in turn.
- Verify that hidden/disabled targets are skipped or ignored, text editing and IME input remain intact, and repeated keydown does not accidentally repeat one-shot operations.
- Manually test the in-app reference, menu chord labels, screen-reader names, keyboard-only use, and high-contrast focus visibility. Update the user guide in the same change as each shipped binding.

## Completion Criteria

- All high-priority proposals are either implemented and validated or have a recorded reason for deferral.
- No duplicate chord has ambiguous ownership within an active window/input context; cross-window reuse has explicit routing and tests.
- Every shipped shortcut agrees across runtime behavior, menus/tooltips, the in-app reference, and the user guide.
- Common Windows conventions are followed for frequent actions, and deviations such as `Ctrl+W`, `F4`, or `Ctrl+D` are deliberate and documented.
- Keyboard shortcuts do not compromise text input, focus traversal, destructive-operation safeguards, or accessibility.

## References

- [Microsoft Learn: Keyboard interactions for Windows apps](https://learn.microsoft.com/en-us/windows/apps/develop/input/keyboard-interactions) - recommends consistent accelerators for common commands, Alt-based access keys, predictable focus order, and F6 pane navigation.
- [Microsoft Support: Keyboard shortcuts in Windows](https://support.microsoft.com/en-us/accessibility/windows/keyboard-shortcuts-in-windows) - documents File Explorer conventions including `Alt+Shift+P` for the Details pane, `Alt+Up` for parent navigation, `F6` pane cycling, `F2` rename, `Alt+Enter` properties, and `Ctrl+Shift+N` new folder.

This document records the implemented bindings and the remaining consistency and manual-verification work.
