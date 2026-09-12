# Autonomous UI Layout Consistency Implementation Prompt

Use this prompt with a coding agent operating in the HyperBrowse repository.

---

You are the implementation agent for HyperBrowse's UI layout and dialog consistency project.

Read and follow `docs/UI_LAYOUT_CONSISTENCY_PLAN.md` as the source of truth for this task. Implement the entire plan end to end in the current repository. Do not stop after producing a plan, after completing Settings, or after the first successful build. Maintain a queue of the remaining implementation slices and continue through them autonomously.

## Operating Mode

- Work directly in the current workspace.
- Preserve unrelated user changes. Never reset, revert, stash, or overwrite changes you did not make.
- Do not create a branch or commit changes.
- Use the existing Win32, C++20, Direct2D, DirectWrite, CMake, and smoke-test architecture.
- Do not introduce Qt, WinUI, MFC, .NET, web UI, or another framework during this implementation. The framework decision is a later measurement gate.
- Read the nearest owning implementation, state header, instructions, and tests before changing behavior.
- Use existing theme, font, accessibility, DPI, message, and RAII helpers where they fit.
- Keep blocking work off the UI thread.
- Use `apply_patch` for source edits and keep unrelated formatting unchanged.
- Do not ask for confirmation between normal implementation slices.
- Stop only for a genuine blocker, a destructive or security-sensitive decision, an unavailable prerequisite, or a validation failure that cannot be resolved locally.
- When a validation failure is local and understandable, repair the same slice and rerun the same validation before widening scope.

## Required Workflow

1. Read `docs/UI_LAYOUT_CONSISTENCY_PLAN.md`.
2. Read the repository contribution guidance, applicable C++ instructions, `docs/architecture.md`, `docs/THEMED_DIALOG_PLAN.md`, the current dialog state header, the current dialog implementation, and the nearest smoke tests.
3. Inspect the worktree and record existing changes. Do not remove them.
4. Run a baseline focused build and relevant tests before changing behavior when the current tree permits it.
5. Create a small implementation queue from the phases below.
6. Execute the queue without pausing after each slice.
7. After every substantive edit, run the cheapest focused validation available.
8. After each completed phase, run the phase validation gate before starting the next phase.
9. Continue until all acceptance criteria are met or a real blocker remains.
10. Finish with Debug and Release validation, the full smoke suite, executable freshness checks, and a concise final report.

## Architectural Rules

### Coordinate system

- Use logical DIPs as the only layout coordinate system.
- Convert to physical pixels only at Win32 positioning or render-target boundaries.
- Do not repeatedly scale existing rectangles.
- Do not use monitor DPI or application text size as ad hoc multipliers inside page code.
- Keep `DialogDpi.h` limited to low-level conversion and window-rect helpers.

### Metrics

Create a shared metrics value type containing at least:

- monitor DPI;
- application text size and scale;
- measured body and small-font metrics;
- margins and spacing tokens;
- row and control heights;
- button dimensions;
- focus-ring, border, and radius metrics;
- monitor work-area dimensions in logical DIPs.

Metrics must be immutable for one layout pass. Rebuild them when DPI, text size, theme, or available size changes.

### Layout results

Extract Settings layout from `src/ui/MainWindow.cpp` into a pure component, preferably:

- `src/ui/SettingsLayout.h`
- `src/ui/SettingsLayout.cpp`

Use names and locations consistent with the existing repository if a nearby pattern is better.

The layout component must accept page, settings state, metrics, available width, and available height, then return:

- required content size;
- body viewport;
- footer rectangle;
- tab rectangles;
- native-control rectangles;
- custom-content rectangles where still required;
- scroll extent and scroll requirements.

Measure text using the active font and available width. Never use fixed assumptions such as `valueLeft + 205`, fixed global page heights, or unmeasured label widths.

### Dialog shell

Create or extract a shared shell for application-owned dialogs with this structure:

```text
window frame
  title or tab header
  scrollable page body
  pinned Apply / OK / Cancel footer
```

The shell owns top-level sizing, centering, monitor work-area clamping, DPI changes, body scrolling, footer placement, common button layout, fonts, theme refresh, and focus restoration.

The page owns content and placement data only. A page must never paint or position content below the footer.

### Controls

Prefer native Win32 controls for interactive widgets because they provide keyboard behavior, focus, text input, and accessibility. Make their theme and spacing consistent through shared helpers.

Do not maintain separate interaction models for custom D2D checkboxes, radio buttons, and buttons alongside native controls unless there is a documented, tested reason. Remove duplicate hit-testing and focus paths as native controls take ownership.

## Implementation Queue

### Slice 0: Baseline and ownership map

- Identify every application-owned dialog and its current owner/state/procedure.
- Identify intentional OS-owned surfaces.
- Record current Settings paths, including the legacy fallback.
- Add or update a reproduction matrix for Small/Medium/Large and 96/120/144/168/192 DPI.
- Capture current build and test status.

Validation gate:

```powershell
cmake --build --preset debug --target HyperBrowse
cmake --build --preset debug --target HyperBrowseTests
ctest --preset debug-tests --output-on-failure
```

### Slice 1: Shared dialog metrics

- Add the shared metrics type in the nearest existing UI module.
- Centralize spacing, control, footer, focus, and typography metrics.
- Ensure text-size and DPI composition happens once per metrics snapshot.
- Add deterministic tests for metric values and rounding.

Validation gate: focused core/test build and the relevant smoke selector.

### Slice 2: Pure Settings layout engine

- Extract page layout from `MainWindow.cpp`.
- Implement measured label/value columns.
- Implement measured radio groups that fit available width and stack when needed.
- Implement all Settings pages: Slideshow, Viewer, Appearance, Performance, and Behavior.
- Return content height rather than assuming the dialog height is sufficient.
- Preserve control IDs, settings semantics, and page order.

Validation gate:

- Build `HyperBrowseTests`.
- Add pure geometry assertions for every page at all required DPI/text-size combinations.
- Assert every rectangle is valid, inside its region, and non-overlapping with the footer.

### Slice 3: Scrollable Settings shell

- Split Experimental Settings into shell, layout, and control synchronization.
- Add a scrollable body viewport.
- Keep Apply, OK, and Cancel pinned outside the body.
- Resize the top-level window from measured content, clamped to the current monitor work area.
- Ensure a short work area scrolls instead of clipping.
- Keep the footer reachable by keyboard and mouse.

Validation gate:

- Settings accessibility smoke.
- Keyboard and mnemonic smoke.
- Manual verification at 100% on the 2560x1440 secondary monitor and 150% on the 4K primary monitor.

### Slice 4: Reflow events

Implement one complete reflow path and call it from:

- `WM_DPICHANGED`;
- application text-size changes;
- theme changes where font or metrics can change;
- page changes;
- body-size or scroll changes.

The path must rebuild metrics, measure, arrange, resize, reposition, update fonts, update scrollbars, update accessibility bounds, and repaint. Do not rescale old child rectangles.

Validation gate: repeat the full Settings geometry matrix and focused smoke tests.

### Slice 5: Control unification

- Move shared native-control creation and theming into reusable helpers.
- Replace duplicate custom checkbox/radio/footer interaction paths where practical.
- Preserve accessible names, roles, states, mnemonics, tab order, and focus behavior.
- Keep custom painting only for surfaces that cannot be represented by the chosen control model.

Validation gate: accessibility tree checks, keyboard navigation, theme checks, and focused smoke tests.

### Slice 6: Shared dialog shell migration

Migrate sibling dialogs in this order:

1. About and Shortcut Reference.
2. Slideshow Settings and Performance Settings.
3. File Associations and Text Input dialogs.
4. Image Information and Diagnostics.
5. Remaining application-owned dialogs.

For each dialog:

- use the shared metrics and shell;
- use the same font and theme rules;
- compute layout from measured content;
- keep footer behavior consistent;
- handle DPI and text-size changes through the shared reflow path;
- add focused geometry and interaction coverage.

Do not create a new dialog-specific DPI helper.

Validation gate after each dialog family: focused build, focused smoke test, full Debug smoke suite when the family is complete.

### Slice 7: Remove duplicate Settings paths

- Validate the replacement Settings path under normal and forced-fallback conditions.
- Remove the old fixed-coordinate production fallback once parity is proven.
- Remove obsolete fixed-size and fixed-offset code.
- Update architecture and themed-dialog documentation.
- Keep intentional OS-owned boundaries explicit.

Validation gate: Debug and Release builds, full Debug and Release test presets, and manual UI verification with the exact newly built executables.

### Slice 8: Framework decision gate

Do not begin a framework migration during this task. Instead, record measured results for:

- implementation complexity;
- accessibility coverage;
- startup and memory impact;
- visual consistency;
- DPI/text-size behavior;
- Direct2D integration;
- packaging and deployment;
- estimated full-shell migration cost.

Only after the current layout pilot is complete may the project decide whether to retain the current stack, evaluate Qt 6 Widgets, or evaluate WinUI 3/XAML.

## Required Invariants

Before declaring completion, prove these invariants in tests or code review:

- No application-owned label or control is clipped at supported DPI/text-size combinations.
- No page content intersects the footer.
- Long content scrolls inside the body.
- Radio and value columns remain inside the available width.
- Every child HWND has a valid logical and physical rectangle.
- DPI changes do not compound scaling.
- Text-size changes remeasure and reflow instead of merely changing fonts.
- Footer actions remain visible, reachable, and ordered consistently.
- Accessibility bounds and states match the visible controls.
- Theme changes do not introduce a second visual language.
- The UI thread performs no blocking decode, filesystem, metadata, cache, or shell work.

## Validation Commands

Use the repository CMake presets:

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug --target HyperBrowse
cmake --build --preset debug --target HyperBrowseTests
ctest --preset debug-tests --output-on-failure
cmake --build --preset release --target HyperBrowse
ctest --preset release-tests --output-on-failure
```

Before manual UI verification:

- confirm the executable is newer than the edited source;
- confirm the exact executable path being launched;
- stop only workspace-launched stale processes that hold the binary open;
- do not diagnose a stale executable as a source-layout failure.

## Stop Conditions

Continue autonomously unless one of these conditions applies:

- a required dependency or SDK is unavailable and cannot be installed through the repository workflow;
- an existing user change makes the requested slice impossible without choosing between incompatible behaviors;
- a test exposes a product decision not specified by the plan;
- a security, data-loss, or destructive migration decision is required;
- validation remains failing after three focused repair attempts and the next action requires user direction.

A normal compile error, failed focused test, missing declaration, stale binary, or local layout defect is not a stop condition. Diagnose and repair it in the current slice.

## Completion Report

When all work is complete, report:

- files and components changed;
- which implementation phases are complete;
- remaining intentional OS-owned surfaces;
- framework decision-gate measurements;
- Debug and Release build results;
- focused and full test results;
- manual DPI/text-size verification performed;
- any residual risks.

Do not report completion while the work is only planned, while only Settings is migrated, or while sibling dialogs still use unrelated layout and scaling systems.
