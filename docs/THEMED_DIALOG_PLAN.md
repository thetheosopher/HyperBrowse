# Themed Dialogs Plan

Status: Phase 1 in progress

## Problem

The app theme currently applies to the main window and several custom surfaces, but older native dialogs use Windows system colors. The image-information surface additionally uses `TaskDialogIndirect`, whose common-control renderer cannot be made to follow the app palette.

## Scope

- Theme the six legacy custom dialog procedures in `src/ui/MainWindow.cpp`.
- Replace the image-information task dialog with an app-owned ordinary dialog.
- Keep Windows `MessageBoxW` fallback/error prompts explicitly system-themed unless they are later brought into scope.
- Preserve modal ownership, keyboard navigation, app text sizing, and existing dialog behavior.

## Phases

- [x] Record the implementation plan and acceptance criteria.
- [ ] Add a shared dialog palette and control-color helpers.
- [ ] Wire the shared palette into the single-line text dialog.
- [ ] Wire the palette into batch rename, performance settings, file associations, and slideshow settings.
- [ ] Update consolidated settings background, controls, and owner-drawn tabs to use the palette.
- [ ] Replace `ShowImageInformation` / `TaskDialogIndirect` with an ordinary themed dialog that preserves expandable metadata.
- [ ] Re-audit custom dialog code for `COLOR_WINDOW`, `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT`, and `COLOR_WINDOW + 1` dependencies.
- [ ] Add focused automated seams where practical and complete light/dark, metadata expand/collapse, theme-change, and DPI validation.

## Design Decisions

- Use a value-type palette passed into each dialog state at creation time. Dialog procedures must not depend on a live `MainWindow` object.
- Prefer explicit palette colors and owned brushes for app-owned surfaces.
- Use native control color messages only where the control honors them; use existing owner-draw/custom-paint paths for tabs and any controls that ignore them under visual styles.
- Do not attempt to skin a Windows task dialog. Replacing that task dialog is required for `Ctrl+I` to follow the app theme.
- Keep system `MessageBoxW` use documented as an intentional boundary until a themed message-dialog component is justified.

## Acceptance Criteria

- Every custom dialog opens with colors matching the selected light or dark app theme.
- Switching theme and reopening a dialog uses the new palette; modeless windows are refreshed or explicitly excluded.
- Text, fields, lists, tabs, buttons, and dialog backgrounds remain readable with sufficient contrast.
- `Ctrl+I` shows the same image information and expandable metadata as today without `TaskDialogIndirect`.
- Dialog keyboard behavior, modal owner restoration, cancellation, and existing settings/file-operation results remain unchanged.
- No new custom-dialog path reads `GetSysColor(COLOR_WINDOW*)` for its app-owned background or text colors.
- Debug and Release builds pass, followed by the relevant smoke tests and manual/UI validation on light and dark themes.

## Validation Log

- 2026-08-29: Confirmed `ShowImageInformation` calls `TaskDialogIndirect`; six legacy dialog procedures contain the reported system-color handlers. Existing `ThemePalette` and themed custom-dialog implementations provide the palette source and local patterns.
