# ADR 0003: Track File-Operation Origin Separately

- Status: Accepted
- Date: 2026-09-04

## Context

Browser and viewer actions share native file-operation types such as copy, move, and delete. Folder-watch notifications can arrive before or after operation completion, and the same operation type can require different optimistic UI behavior depending on its origin.

## Decision

Track whether an operation originated in the browser or viewer independently from its file-operation type. Completion handling must distinguish viewer optimistic state, browser model mutation, folder-watch echoes, reload thresholds, selection/focus restoration, and shell-dialog foreground activation.

Do not use the operation type alone as a proxy for origin. A full reload is reserved for large or ambiguous change bursts; safe self-originated changes should be applied incrementally.

## Consequences

- Shared completion code needs explicit origin state.
- Regression tests should cover browser and viewer operations with the same operation type.
- Folder-watch handling must be reviewed whenever file-operation completion behavior changes.
