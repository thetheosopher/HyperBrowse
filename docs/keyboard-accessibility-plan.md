# Keyboard Accessibility Implementation Plan

## Goal

Make keyboard-only operation predictable across HyperBrowse while preserving the existing custom-rendered command bar, details panel, Quick Actions surface, and experimental settings UI. Native controls remain available where they already exist, but they are not the fallback implementation for custom actions.

## Non-negotiable constraints

- Do not replace custom controls or actions with native child controls solely to obtain keyboard behavior.
- Keep the existing Direct2D/GDI ownership split and painter/model architecture.
- Keep decode, enumeration, metadata, cache, shell, and file-operation work off the UI thread.
- Preserve existing accelerator routing, text-edit behavior, viewer key handling, selection, focus restoration, and visual states.
- Prefer small behavior-preserving slices with focused smoke coverage before expanding scope.

## Acceptance criteria

- Every visible actionable custom surface is reachable without a mouse.
- Tab and Shift+Tab traversal follow a documented, stable order and skip hidden or disabled targets.
- Custom targets support the appropriate Enter/Space activation, arrow navigation, Escape behavior, and disabled-state behavior.
- Every custom target has a visible keyboard focus indicator that is distinct from mouse hover and pressed states.
- Dialog fields and actions have working mnemonics or an explicit keyboard path; mnemonic collisions are rejected per dialog/page scope.
- Settings pages can be changed by keyboard and focus moves to the first enabled target on the selected page.
- Focus is restored after modal dialogs, modeless windows, viewer close, and dynamic Quick Actions updates.
- UI Automation or an equivalent accessibility bridge exposes meaningful names, roles, states, and focus events for custom targets.
- Focused smoke coverage exercises the behavior; manual NVDA/JAWS, Inspect/UI Automation, high-contrast, theme, DPI, and larger-text checks cover the platform-facing behavior.

## Work queue

### Slice 1: Main-window custom focus foundation

Status: Implemented and smoke-validated.

- Define a semantic custom focus target model in `MainWindow`, separate from mouse hot/pressed state.
- Build a stable focus sequence from the currently visible action-strip, browser, details-panel, Quick Actions, and native edit targets.
- Route Tab and Shift+Tab through `MainWindow::TranslateAcceleratorMessage()` without stealing viewer keys or text-edit input other than traversal.
- Activate the focused custom target with Enter/Space and preserve existing command handlers.
- Paint a visible focus ring for toolbar, details-panel, and Quick Actions targets.
- Add the details text editor to the main-window tab sequence without changing its custom rendering.

### Slice 2: Main-window semantics and dynamic-state behavior

- Add accessible names and semantic roles for the custom target model.
- Skip unavailable actions, hidden panels, disabled destination operations, and stale Quick Actions rows.
- Keep focus on the corresponding logical target when rows are rebuilt, sorted, scrolled, or removed.
- Add keyboard access to the details-panel tab strip, close button, Quick Actions sort button, row navigation, and row Copy/Move/Remove actions.
- Cover Apps/Shift+F10 context-menu access for focused custom targets where an action context menu exists.

### Slice 3: Dialog mnemonics without replacing custom surfaces

Status: Implemented for Experimental and consolidated Settings; both paths are covered by the active `HyperBrowseSettingsSmoke`. The separate multi-viewer Settings smoke remains disabled by repository policy.

- Add unique field and action mnemonics per dialog/page.
- Remove `SS_NOPREFIX` only where a static label is intended to transfer focus, and add explicit routing where Win32 static-label behavior is insufficient.
- Add mnemonic/page navigation to consolidated settings while preserving its native field controls and owner-drawn tab presentation.
- Give the experimental settings custom tabs, options, and buttons the same semantic mnemonic and focus model rather than converting them to native controls.

Implementation notes:

- Experimental Settings route `WM_SYSKEYDOWN` through semantic targets for page tabs, custom options, native fields, and footer actions; Direct2D text underlines the assigned mnemonic characters.
- Consolidated Settings route the same page-scoped assignments before `IsDialogMessageW`, including page switching, visible/enabled field focus, and Apply/OK/Cancel activation.
- Mnemonic assignments reserve page and footer keys and avoid collisions within each page. No custom surface was replaced with a native child control.

### Slice 4: Experimental settings custom focus model

Status: Implemented and smoke-validated.

- Add a page-aware custom focus sequence for tabs, custom checkboxes/radio options, custom choice fields, native numeric/edit fields, and footer actions.
- Implement Tab/Shift+Tab, arrow navigation for grouped options, Space/Enter activation, disabled-state skipping, and focus restoration after page changes.
- Paint focus indicators independently of hover and pressed state.
- Ensure Ctrl+Tab and Ctrl+Shift+Tab remain available and predictable.

### Slice 5: Accessibility exposure and verification

- Status: Main-window custom surfaces expose an MSAA `IAccessible` bridge and are covered by `HyperBrowseAccessibilitySmoke`. Dialog-specific custom surfaces and manual screen-reader verification remain.

- Implement a UI Automation provider or the smallest existing project-compatible accessibility bridge for custom targets.
- Expose name, role, enabled/disabled, checked/selected, expanded/open, and focused state.
- Raise focus and state-change events when custom focus or activation changes.
- Add manual verification with NVDA or JAWS and Windows Inspect/UI Automation.

Implementation notes:

- The main window serves `WM_GETOBJECT` for `OBJID_CLIENT` through an MSAA provider without changing native child controls.
- The provider exposes command-bar menus, toolbar actions/toggles/dropdowns, the image browser surface, details tabs/close, and Quick Actions rows/buttons with names, roles, screen bounds, states, default actions, and focus/state notifications.

### Slice 6: Regression and release gates

- Add smoke coverage for main-window Tab/Shift+Tab order, custom activation, hidden/disabled skipping, settings page navigation, dialog mnemonics, accessibility names/roles/focus state, initial focus, focus restoration, and filter accelerator leakage.
- Verify light/dark themes, high-contrast mode, larger text, DPI scaling, narrow layouts, and empty/loading Quick Actions states.
- Build the exact executable used for manual verification and confirm its timestamp is newer than the edited sources.

## First-slice validation

- Compile the touched production and painter files with the focused Debug `HyperBrowse` target.
- Add or extend a smoke scenario once the custom target sequence is stable enough to assert without relying on pixel coordinates.
- Manually verify Tab, Shift+Tab, Enter, and Space on the main window before starting dialog mnemonic work.

## Explicit non-goals for this plan

- Replacing custom-rendered controls with native buttons, checkboxes, radio buttons, or tabs.
- Rewriting the command bar, details panel, Quick Actions layout, or rendering pipeline.
- Solving unrelated screen-reader issues in the browser thumbnail renderer or viewer renderer before the custom focus contract exists.
