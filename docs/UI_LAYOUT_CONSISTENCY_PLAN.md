# UI Layout and Dialog Consistency Plan

Status: Implementation in progress
Owner: UI architecture
Created: 2026-09-12

## Decision Summary

Retain the current Win32, Direct2D, and DirectWrite stack for the next implementation phase. Do not continue fixing dialog geometry with isolated pixel constants. Build a shared measured layout and dialog-shell layer first.

A Qt or WinUI migration remains a future decision, but it should be evaluated only after the shared layout pilot has established the actual requirements and migration cost. The current failures are caused primarily by layout ownership and measurement, not by the absence of a third-party framework.

## Problem

HyperBrowse currently combines:

- Win32 native child controls;
- GDI-rendered shell and dialog surfaces;
- Direct2D/DirectWrite-rendered custom dialog content;
- fixed coordinate chains for page rows;
- separate DPI and application text-size conversions;
- dialog-specific fonts, spacing, and footer placement.

This produces recurring failures when monitor DPI, application text size, page content, or monitor work area changes. Typical symptoms are clipped labels, controls extending beyond the client area, footer buttons overlapping content, and sibling dialogs using visibly different spacing and control treatments.

The current Settings code in `src/ui/MainWindow.cpp` is the immediate example, but the solution must cover every application-owned dialog and the main shell.

## Goals

- Make application-owned surfaces visually consistent.
- Make layout correct at 96, 120, 144, 168, and 192 DPI.
- Make Small, Medium, and Large application text sizes first-class layout inputs.
- Keep labels and controls inside their measured bounds.
- Keep the action footer visible and separate from scrollable content.
- Preserve keyboard navigation, accessibility, modal ownership, theming, and native text input.
- Eliminate duplicated dialog-specific DPI and spacing rules.
- Provide deterministic geometry tests in addition to interactive smoke coverage.

## Non-goals

- Rewriting image decode, thumbnail, viewer, or service architecture.
- Replacing the Windows file picker or other deliberately OS-owned surfaces in the first phase.
- Introducing a new UI framework before the current layout pilot is measured.
- Replacing Direct2D image and thumbnail rendering merely to solve dialog layout.

## Target Architecture

### 1. Canonical coordinate system

All layout calculations use logical DIPs. A layout result stores logical rectangles and sizes. Conversion to physical pixels happens only at the Win32 or rendering boundary.

The code must not repeatedly scale an already-scaled rectangle. DPI and application text size are inputs to metrics creation, not transformations applied to previous geometry.

`DialogDpi.h` remains a low-level conversion utility. It should not contain page layout policy.

### 2. Shared metrics

Introduce a shared metrics value type containing:

- monitor DPI;
- application text size and scale;
- active font handles or measured font metrics;
- spacing tokens;
- control heights;
- button dimensions;
- border, radius, and focus-ring metrics;
- available monitor work area in logical DIPs.

Every dialog receives a metrics snapshot and derives its layout from that snapshot.

### 3. Measured layout results

Extract Settings layout into a pure component such as:

- `src/ui/SettingsLayout.h`
- `src/ui/SettingsLayout.cpp`

The component should accept page, text-size, DPI, theme-independent font metrics, and available size. It should return:

- required content size;
- body viewport;
- pinned footer rectangle;
- tab rectangles;
- native-control rectangles;
- custom-content rectangles, if any;
- scroll extent and scroll requirements.

The component must measure text and arrange rows from available space. It must not use assumptions such as `valueLeft + 205` or a fixed page height.

### 4. Shared dialog shell

Create a reusable shell for application-owned dialogs:

```text
window frame
  header/title or tab strip
  scrollable body
  pinned action footer
```

The shell owns:

- monitor work-area detection;
- DPI changes;
- top-level sizing and centering;
- body scrolling;
- footer placement;
- common button order and spacing;
- theme and font refresh;
- focus restoration.

Page implementations own only their content model and measured row layout.

### 5. Control ownership

Prefer native Win32 controls for interactive widgets because they already provide keyboard behavior, focus, text input, and accessibility. Apply the application theme consistently through shared control creation and color/style helpers.

Avoid mixing custom-painted versions of checkboxes, radio buttons, and buttons with native combos, edits, and spin controls unless there is a documented reason. A custom visual surface must not silently create a second interaction or accessibility model.

### 6. Dynamic sizing and overflow

For every dialog:

```text
desired size = measured page content + header + footer + margins
actual size   = min(desired size, monitor work area minus safe margins)
```

If desired content is taller than the available work area, the body scrolls. The footer never moves into the body and never overlaps a page row.

On page changes, text-size changes, theme changes, and DPI changes, recompute the complete layout and resize/reposition all children from the new result.

## Implementation Phases

### Phase 0: Stabilize the boundary

- [ ] Stop adding dialog-specific width, height, offset, and padding patches.
- [ ] Record current Settings, About, Shortcut Reference, Slideshow, Performance, File Associations, and Image Information dialog owners.
- [ ] Identify which surfaces are application-owned and which are intentional OS-owned boundaries.
- [ ] Preserve the existing themed-dialog palette plan and accessibility contracts.
- [ ] Add a short reproduction matrix for 96/144/192 DPI and Small/Medium/Large text.

Exit criterion: every application-owned dialog has an owner, a state type, and a current validation entry.

### Phase 1: Build the pure layout engine

- [ ] Extract `SettingsLayout` from `MainWindow.cpp`.
- [ ] Define spacing, row, column, and footer tokens in one metrics type.
- [ ] Implement measured label/control columns.
- [ ] Implement radio groups that use available width and stack or wrap when necessary.
- [ ] Implement page content-height calculation.
- [ ] Add pure tests for every Settings page and every text-size/DPI combination.

Exit criterion: layout tests prove that every returned rectangle is inside the body or footer region and that no page control intersects the footer.

### Phase 2: Add the dialog shell and scrolling

- [ ] Split Experimental Settings into shell, page layout, and control synchronization.
- [ ] Add a body viewport with a vertical scrollbar.
- [ ] Keep Apply, OK, and Cancel pinned outside the scroll region.
- [ ] Resize the top-level dialog from measured content, clamped to monitor work area.
- [ ] Recompute all geometry on `WM_DPICHANGED` instead of scaling existing child rectangles.
- [ ] Recompute all geometry when Application Text Size changes.

Exit criterion: Settings remains usable without clipping on a small work area and at all supported DPI/text-size combinations.

### Phase 3: Unify Settings controls

- [ ] Replace custom checkbox, radio, and footer-button interaction paths with consistently themed native controls where practical.
- [ ] Keep one focus, keyboard, and accessibility path per control.
- [ ] Move owner-draw/theme logic into shared helpers.
- [ ] Remove duplicate custom-control hit testing where a native control now owns the interaction.

Exit criterion: Settings has one visual, keyboard, and accessibility model for all interactive controls.

### Phase 4: Migrate sibling dialogs

Migrate in this order:

1. About and Shortcut Reference.
2. Slideshow Settings and Performance Settings.
3. File Associations and Text Input dialogs.
4. Image Information and Diagnostics.
5. Remaining application-owned dialog procedures.

Each migration must use the shared shell, metrics, theme, font, and validation helpers. Do not create another dialog-specific DPI helper.

Exit criterion: all application-owned dialogs use the same shell and design tokens, with intentional OS-owned exceptions documented.

### Phase 5: Remove duplicate paths

- [ ] Remove the legacy consolidated Settings fallback after the replacement path is validated.
- [ ] Remove obsolete fixed-coordinate layout code.
- [ ] Remove unused dialog-specific scaling helpers.
- [ ] Update `docs/THEMED_DIALOG_PLAN.md` and architecture documentation.
- [ ] Update user-facing screenshots or guides when visible layout changes are complete.

Exit criterion: no production Settings path depends on the old fixed-coordinate layout.

### Phase 6: Framework decision gate

After the shared layout pilot, measure:

- implementation complexity;
- accessibility coverage;
- startup and memory impact;
- visual consistency;
- DPI/text-size behavior;
- Direct2D integration cost;
- packaging and deployment impact;
- time required to migrate the main shell.

Only then decide whether to retain the current stack, evaluate Qt 6 Widgets, or evaluate WinUI 3/XAML. Do not start a framework migration while the current requirements are still represented only by dialog-specific constants.

## Validation Requirements

### Deterministic layout tests

For every Settings page, test:

- Small, Medium, and Large text sizes;
- 96, 120, 144, 168, and 192 DPI;
- narrow available width;
- narrow available height requiring scrolling;
- disabled and enabled optional controls;
- light and dark themes where text metrics or control visibility differ.

Assert:

- every control is inside the body viewport or footer;
- no control intersects the footer;
- radio columns remain inside the available width;
- labels have measurable, nonzero bounds;
- required content height is reported correctly;
- scroll extent is nonnegative and sufficient;
- footer buttons remain reachable and ordered consistently.

### Interactive smoke tests

Retain accessibility and keyboard smoke coverage for:

- opening each page;
- tab navigation;
- mnemonic navigation;
- Apply, OK, and Cancel;
- text-size changes while the dialog is open;
- monitor-DPI changes where the environment permits;
- modal owner restoration.

### Manual checks

Use the exact executable produced by the build. Verify at minimum:

- 100% DPI on the 2560x1440 secondary display;
- 150% DPI on the 4K primary display;
- Small, Medium, and Large application text;
- a work area too short to display the full page without scrolling;
- light and dark themes.

Required commands:

```powershell
cmake --build --preset debug --target HyperBrowse
cmake --build --preset debug --target HyperBrowseTests
ctest --preset debug-tests --output-on-failure
```

Before manual verification, confirm the Debug executable is newer than the edited source and that no older HyperBrowse process is holding the binary open.

## Acceptance Criteria

The UI layout effort is complete when:

- all application-owned dialogs use shared metrics and shell behavior;
- no dialog clips text or controls at the supported DPI/text-size matrix;
- long pages scroll their body instead of overlapping the footer;
- native interactive controls and custom rendering share one visual language;
- keyboard navigation and accessibility remain intact;
- theme changes do not create a second visual system;
- layout tests cover every page and supported scaling combination;
- the framework decision is documented using measurements rather than frustration-driven rewrites.

## Current Recommendation

The Settings pilot now has a pure measured layout engine, a scrollable body with pinned footer, shared work-area frame helpers, accessibility geometry conversion, and focused geometry/interaction smoke coverage. The application no longer routes through the legacy Settings environment override, and the fixed-coordinate Settings layout routine has been removed. Sibling dialogs use the shared frame-clamping policy on DPI transitions while retaining their existing content procedures.

Remaining work is intentionally bounded: migrate sibling content layout onto shared measured metrics where their current procedures still own dialog-specific tokens, add broader light/dark and non-96-DPI runtime coverage, remove the now-unreachable consolidated-dialog implementation, and record framework-decision measurements before considering Qt or WinUI.
