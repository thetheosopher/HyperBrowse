# Thumbnail Performance Plan

Status: Work in progress
Updated: 2026-09-01

## Goal

Keep thumbnail creation responsive while browsing large folders. Thumbnails in the current viewport must be prioritized over speculative work, and persistent-cache maintenance must not delay visible pixels.

## Current Findings

- `BrowserPane` assigns priority 0 to visible rows, priority 1 to near-visible rows, and priority 2 to proactive prefetch.
- `ThumbnailScheduler` orders queued work by priority, but running decodes cannot be interrupted when the viewport changes.
- Scrollbar thumb tracking updates the viewport without scheduling new visible work until the drag completes.
- JPEG batches can include work that becomes stale during a scroll.
- A completed decode currently performs the persistent disk-cache write before posting the ready notification to the UI.
- `DiskThumbnailCache` serializes file access and index updates behind a process-wide mutex; stores can therefore delay unrelated workers.

## Work Plan

### Phase 1: Foreground readiness and instrumentation

1. [x] Post a successful thumbnail-ready update immediately after inserting into the memory cache.
2. [x] Queue or perform the persistent disk write after the UI notification so disk I/O is never on the visible-thumbnail critical path. Stores and invalidations now share a scheduler-owned persistence worker.
3. [x] Add separate timing metrics for disk lookup, store, invalidation, ready-message posting, and queue wait by worker kind and priority. Actual paint latency remains a UI instrumentation step.
4. [x] Add a regression test that verifies a ready update is observable before a deliberately slow persistent write completes, using an injectable scheduler persistence hook.

Success criteria: a decoded visible thumbnail can paint without waiting for persistent-cache index/file I/O, and the timings identify any remaining queue or decode bottleneck.

### Phase 2: Viewport-priority scheduling

1. [x] Reserve a foreground worker lane for priority-0 visible work while visible work is pending; other workers retain normal throughput when no visible item is queued.
2. [x] Make viewport changes invalidate queued stale work immediately and prevent stale batches from starting. Request-epoch filtering clears queued work, rejects fully stale batches, and removes stale members from partially relevant batches before decode.
3. [x] Keep visible and near-visible items out of proactive JPEG batches. The browser marks priorities 0 and 1 as CPU-preferred, and the scheduler excludes CPU-preferred jobs from nvJPEG batches.
4. [x] Continue scheduling during scrollbar thumb tracking with row-level throttling, then flush at thumb release.

Success criteria: newly visible rows begin work ahead of old prefetch work during rapid wheel, scrollbar, and keyboard navigation.

### Phase 3: Adaptive prefetch

1. [x] Track scroll direction.
2. [x] During active scrolling, request the viewport plus a small directional buffer.
3. [x] After the user becomes idle, expand prefetch to several screens and warm the top of a folder progressively.
4. [x] Avoid decoding both directions equally when movement is strongly directional.

Success criteria: rapid scrolling produces less stale decode work while idle browsing retains warm look-ahead behavior.

### Phase 4: Hardware and decoder tuning

1. Compare CPU and nvJPEG paths for visible JPEG work using queue-wait, decode, and paint timings.
2. Reuse per-worker WIC factories where that reduces setup cost without increasing lifetime or shutdown complexity.
3. Keep RAW work isolated and tune its concurrency separately from WIC/JPEG work.
4. Lower background worker priority or concurrency only while active scrolling; restore throughput after idle.

Success criteria: improved time-to-first-visible-thumbnail without unacceptable idle-folder warm-up regression or memory pressure.

## Guardrails

- Preserve request session and epoch checks so stale work cannot update the browser model.
- Do not block the UI thread on `DiskThumbnailCache`.
- Do not make cancellation depend on forcibly interrupting WIC or RAW decoders.
- Keep cache correctness and crash recovery unchanged while changing write scheduling.
- Benchmark normal, large JPEG, mixed-format, and RAW-heavy folders separately.

## Validation Metrics

Record at minimum:

- Queue wait by priority and worker kind.
- Decode duration by decoder path.
- Persistent-cache lookup and store duration.
- Ready-message post latency after thumbnail bookkeeping.
- Count of cancelled queued jobs and stale batch items.
- Visible-thumbnail time-to-paint during rapid scrolling.

## Decision Log

- 2026-09-01: Start with ready-before-disk-store because the disk cache holds a process-wide filesystem lock and currently sits before the UI notification. This is a small, reversible change with a direct user-visible latency benefit.
