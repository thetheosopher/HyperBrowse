# ADR 0001: UI Thread and Worker Boundaries

- Status: Accepted
- Date: 2026-09-04

## Context

Folder enumeration, image decoding, metadata extraction, shell file operations, and persistent thumbnail-cache work can block or produce high-volume results. Running them from Win32 window procedures or completion callbacks makes interaction latency unpredictable.

## Decision

The UI thread owns HWNDs, input, layout, command routing, presentation state, and paint coordination. Potentially blocking work runs on the established service or scheduler worker paths. Results return through existing window messages or callback contracts. Asynchronous code must define cancellation, stale-result handling, recipient lifetime, and shutdown ordering.

Persistent disk-thumbnail-cache operations are not called from the UI thread. Scheduler-owned worker paths perform disk invalidation and other persistent index work.

## Consequences

- UI callbacks stay small and predictable.
- New async paths require explicit lifetime and cancellation reasoning.
- Tests should cover both worker completion and UI presentation state.
- A synchronous helper is acceptable only when it is demonstrably bounded and non-blocking for the call site.

## Rejected alternative

Moving all work to a single generic background queue was rejected because thumbnail priority, file-operation ownership, shutdown behavior, and persistent cache serialization have different requirements.
