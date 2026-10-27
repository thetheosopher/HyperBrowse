---
name: viewer-overlays
description: "Implement or resume configurable viewer overlay panes and an asynchronous RGB histogram in HyperBrowse."
agent: agent
---

# Goal: configurable viewer overlays

Deliver a complete, tested viewer overlay system for HyperBrowse. Work
autonomously through investigation, implementation, integration, documentation,
and verification. Do not stop after producing a plan or an initial plausible
implementation. On resume, inspect the current worktree and test evidence,
identify what remains, and continue without duplicating completed work.

Follow the repository contribution guidance and
[C++ instructions](../instructions/cpp.instructions.md). Read owning
implementations and tests before changing behavior. Preserve unrelated dirty
files. Do not commit, publish, package, reset, or revert user work.

## Agreed product decisions

Implement four independent panes:

| Pane | Contents | Default corner | Default importance |
| --- | --- | --- | --- |
| File information | Filename, folder position, format, file size | Top left | High |
| Image status | Dimensions, zoom percentage, fit mode | Bottom right | High |
| Full metadata | Separate Name and folder-only Path fields below the heading, then available EXIF/IPTC/XMP details | Top right | Low |
| Histogram | 256-bin RGB distribution | Bottom left | Normal |

Preserve existing visibility and text-size preferences. Histogram is disabled
by default for both windowed and full-screen modes.

Each pane has:
- Separate enabled flags for windowed and full-screen modes.
- One shared corner, stack order, and importance for both modes.
- One of four positions: top left, top right, bottom left, bottom right.
- High, Normal, or Low importance, independent of stack order.

Do not implement free dragging, arbitrary coordinates, per-field metadata
selection, per-pane opacity, docking, luminance histograms, logarithmic
histograms, or clipping warnings in this iteration.

## Layout behavior

- Top stacks grow down; bottom stacks grow up. Order 1 is nearest the corner.
- Left stacks share a left edge; right stacks share a right edge.
- Visible panes within each corner stack share the widest visible pane's width.
  Keep individual pane heights; size other corners independently. Disabled or
  temporarily suppressed panes must not determine the remaining stack's width.
- Text stays left aligned even in right-anchored panes.
- Respect DPI, current overlay text-size presets, theme, and high contrast.
- Use consistent scaled outer margins and gaps.
- Panes overlay the image without changing zoom, fit, pan, or image bounds.
- Bound pane widths/heights and metadata content without reducing selected
  text size. Full details remain available through Image Information.
- Never overlap panes, place them outside the client area, or move them to a
  different corner automatically.
- If bounded panes still cannot fit, suppress the lowest-importance pane and
  recompute until the result fits. At equal importance retain panes in the
  order File information, Image status, Histogram, Full metadata.
- Suppression is temporary, never changes persisted visibility, and reverses
  automatically when space permits. High means last to yield, not pinned.
- Distinguish disabled, master-hidden, and hidden-to-fit states in layout
  results. The configuration preview explains hidden-to-fit panes.
- Keep layout deterministic for a given viewport, settings, and measured sizes.

## Controls and persistence

Add Configure overlays... to Viewer settings and the viewer context menu.
Use existing native/themed dialog, focus, keyboard, and settings patterns.
Provide:
- A pane list with windowed and full-screen enabled controls.
- Position and importance selection.
- Accessible move-toward-corner/move-away-from-corner controls for stack order.
- A layout preview that reflects settings and identifies hidden-to-fit panes.
- Existing overlay text size, restore defaults, and standard OK/Cancel/Apply actions.

OK persists pending changes and closes. Apply persists configuration and refreshes
open viewers through the established settings propagation mechanism without
closing; disable Apply when the draft matches the last committed settings.
Cancel, Esc, and window close discard only edits since the last Apply, preserving
prior commits. OK is the default action. Restore defaults resets the draft until
applied or accepted with OK. Normalize malformed persisted values.
Migrate legacy master visibility, text size, and per-mode full metadata flags
without overriding user choices. Retain compatibility with existing setters
and settings surfaces so no parallel source of truth develops.

Keep Tab and the gamepad overlay action as the master visibility switch:

    shown = master visible AND enabled in current mode AND fits layout

Master toggling never changes pane configuration. Individual context-menu pane
checkmarks can be changed while master visibility is off.

## Histogram and asynchronous ownership

Reuse or carefully extract the existing RGB computation and painter:
[computation](../../src/ui/DetailsPanelHistogram.cpp),
[painter](../../src/ui/DetailsPanelHistogramPainter.h).
The browser histogram currently uses a small thumbnail; the viewer must not
silently present that thumbnail distribution as decoded-image statistics.

- Compute 256-bin RGB counts from the decoded image being viewed, before
  monitor-specific color conversion, not from a screenshot or visible crop.
- Zoom, pan, and rotation do not change histogram counts.
- RAW statistics describe the decoded/rendered preview, not raw sensor data.
  Identify the pane as decoded/rendered RGB where necessary.
- Reuse existing decoded pixels; do not decode a file again for a histogram.
- Run pixel scanning on an established worker path, never in paint, a window
  procedure, or another UI callback. Do not access persistent caches on the UI.
- Show explicit loading/unavailable states with stable histogram dimensions.
- Do not show a histogram caption or reserve a title row; the plot fills its
  padded content area. Retain loading and error messages when applicable.
- Bind every result to displayed content identity and request generation, not
  only an item index. Reject stale results after navigation, item replacement,
  compare focus changes, mode changes, or shutdown.
- Preserve the last valid viewer image during replacement; never associate
  old histogram bins with a new image.
- Respect executor capacity, cancellation, producer shutdown, posted-result
  ownership, and HWND lifetime. Surface failures through existing logging/UI.

## Viewer coverage and compatibility

Integrate the same layout in normal viewing, full screen, supported rendering
paths, transitions, and existing partial overlay repaint paths. Rendering and
hit/repaint bounds must agree after panes move.

For comparison, show one configurable pane set following the focused image,
including one histogram and full metadata pane with clear image identity.
Preserve essential comparison identity/focus chrome and synchronized view
behavior. Do not multiply full metadata/histogram panes for each tile.

Preserve independent viewer windows, image navigation, focus/activation,
keyboard/gamepad shortcuts, file workflows, and display color management.

## Implementation and validation workflow

1. Inspect current source, nearest tests, settings propagation, async image
   ownership, comparison rendering, and CMake registration.
2. Implement a small typed settings model and deterministic layout calculation.
   Prefer existing helpers; avoid a general-purpose UI framework.
3. Add normalized persistence/migration and focused noninteractive coverage.
4. Integrate rendering, configuration UI, context menu, and async histogram.
5. Update the [README](../../README.md) and
   [user guide](../../docs/user-guide.html).
6. Configure/build using repository presets, with tests ON and CUDA bundling
   OFF for development. Never invoke release packaging as validation.
7. Run focused tests; fix failures caused by this work and rerun. Escalate to
   relevant regression suites when shared behavior changes.
8. Verify that the exact executable launched is from the just-built directory
   and newer than changed implementation files. Stop temporary helper processes
   by their exact PID.

Expected commands, refined with focused test selectors as appropriate:

    cmake --preset vs2026-x64 -DHYPERBROWSE_BUILD_TESTS=ON -DHYPERBROWSE_BUNDLE_CUDA_REDIST=OFF
    cmake --build --preset debug --target HyperBrowse HyperBrowseTests
    ctest --preset debug-tests -R "<focused overlay/viewer/settings selectors>"

Required evidence includes:
- All corner assignments, multiple panes in a stack, order independent of
  importance, opposing-corner collisions, tiny viewports, and restoration.
- Master switch, per-mode visibility, default migration, invalid setting
  normalization, OK/Apply/Cancel, dirty-state tracking, context-menu state, and persistence round trips.
- DPI/text-size changes, metadata bounds, themes, compare focus, and moved-pane
  partial repaint correctness.
- Histogram bin counts for known solid/mixed pixels, source independence from
  viewport transforms, explicit failures, stale results, and viewer destruction
  with work pending.
- Existing viewer/navigation/settings regressions remain passing.

## Autonomous execution and completion

Make reasonable implementation choices consistent with this specification and
existing code. Ask only for conflicting requirements, overlapping user edits,
destructive actions, or a genuinely blocking product decision. Do not claim
that runtime approval or permission settings were changed.

Continue until the feature is integrated and verified, or a concrete external
blocker prevents further work. If blocked, leave work safe and report the
specific blocker, completed portions, and exact next action.

The final report must distinguish implemented and verified behavior from any
unverified desktop interaction. Include exact validation commands and outcomes,
remaining limitations, and a link to this prompt. Never describe planned
features as delivered.
