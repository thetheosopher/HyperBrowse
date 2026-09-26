# Long-Horizon Prompt 03: Prove and Implement A6 GPU Thumbnail Scaling

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Address roadmap item A6 with evidence rather than assumption. First establish a
repeatable, apples-to-apples comparison of the existing WIC thumbnail scaling
path and a Direct2D GPU scaling candidate. Implement the production path only
if the measured benefit clears the acceptance gate below. Otherwise, preserve
the WIC path, publish the negative or inconclusive result, and keep A6 deferred.

The scope is decode-time scaling for browser thumbnails. Do not confuse it with
Direct2D's existing interpolation when drawing an already-decoded bitmap in the
browser or viewer. Do not expand this work into viewer resizing, general render
architecture changes, or A5 buffer pooling.

## Relationship to Other Work

- [Prompt 02](FUTURE-ROADMAP-PROMPT-02-PERFORMANCE-EVIDENCE.md) owns D1-D3 and
the remaining A8 evidence system. Its dataset, runner, retention, and reviewed
baseline gates are prerequisites to enabling a production A6 path.
- [Prompt 04](FUTURE-ROADMAP-PROMPT-04-HOSTED-EVIDENCE-AND-A6-PAIRED-BENCHMARK.md)
coordinates publishing that workflow, collecting and reviewing hosted
artifacts, calibrating the profiles, and running the benchmark-only paired A6
comparison on verified hardware. It stops before production scaling changes.
- Verify those gates and their current evidence at execution time. Do not
assume a previously reported baseline state is still current.
- A6-specific comparative measurements are still required even after D3/A8
baseline calibration: current startup and persistent-cache results do not
measure WIC-versus-GPU scale cost.
- Do not start additional A5 WIC pooling as part of this prompt. Keep any
independent A5 recommendation separate.

## Starting Observations to Verify

Treat these as source-finding hints, not immutable facts:

- `src/decode/WicThumbnailDecoder.cpp` and `src/decode/ImageDecoder.cpp` use
  `IWICBitmapScaler` for thumbnail scaling. Diagnostics include a
  `thumbnail.scale` timing, but the aggregate runner does not currently provide
  a paired CPU/WIC-versus-GPU scenario.
- `src/render/D2DRenderer.h` has a high-quality draw helper for rendering
  existing bitmaps. That draw-time interpolation is not A6's decode-time
  thumbnail production path.
- `src/cache/ThumbnailCache.h` currently represents `CachedThumbnail` using an
  `HBITMAP`. The A6 roadmap proposal describes BGRA byte-array cache entries;
  reconcile this mismatch before changing cache representation or APIs.
- `src/render/D2DRenderer.cpp` creates a single-threaded Direct2D factory.
  Trace actual D2D device/context ownership and thread use before designing a
  worker-side GPU scaler.
- `docs/perf/scenario-matrix.md` and `docs/perf/baseline.json` describe current
  measured scenarios and calibration status. Confirm them against the current
  runner, workflow artifacts, and source before drawing conclusions.

## Operating Rules

- Read `.github/copilot-instructions.md`, applicable C++ instructions,
  `docs/architecture.md`, `docs/testing.md`, the A6 section of
  `specs/FUTURE-ROADMAP.md`, Prompt 02, the archived benchmark plan, and the
  owning decoder, cache, renderer, scheduler, tests, and CI code before changing
  behavior.
- Preserve user work and generated evidence. Do not reset, revert, stash, or
  overwrite unrelated changes. Do not commit or create a branch.
- Keep decode, scaling, GPU synchronization/readback, and benchmark work off the
  UI thread. Preserve cancellation, stale-result rejection, shutdown ordering,
  content-identity cache invalidation, and the last valid visible image.
- Use the existing generated datasets and runner where suitable. Do not use
  personal libraries, unlicensed media, network downloads, or private media as
  checked-in fixtures. Dataset D remains local-only unless permitted media is
  available.
- Do not represent WARP/software rendering as a hardware GPU performance
  result. Record which adapter and D2D device type actually ran each sample.
- Never infer GPU completion from command submission time alone. Timings must
  include completion and any transfer or readback required by the production
  cache representation.
- Do not loosen existing guardrails or claim calibration from a single local
  machine. Retain raw samples, environment metadata, and failures.
- Make a small change, run its focused validation, and continue autonomously
  through independent queue items. Stop at a failed acceptance gate, external
  evidence dependency, or material architecture decision that cannot be
  resolved from repository evidence.

## Phase 0: Confirm the Prerequisites

1. Record the worktree state, source revision, build tree, Release executable,
   benchmark schema, current D1/D2/D3/A8 status, and A6-relevant code owners.
2. Verify that formal dataset inventories and the runner are valid and that
   per-run evidence is retained on both success and failure.
3. Inspect the existing hosted CI artifacts. D3/A8 remains a prerequisite until
   each required Release decode-path profile has at least ten successful,
   independent workflow artifacts, compatible metadata, and reviewed
   distribution-based thresholds.
4. Confirm a real supported GPU test environment is available for the paired
   measurement. Record adapter vendor/device, driver, Windows version, D2D
   device type, and whether the application actually obtained hardware
   acceleration.

**Gate:** If D3/A8 calibration or a representative hardware-GPU environment is
unavailable, do not enable a production A6 path. Complete locally actionable
benchmark design and documentation if possible, preserve any exploratory data
as explicitly uncalibrated, and report the exact external evidence still
required. Never fabricate workflow artifacts or silently substitute WARP.

## Phase 1: Define the Paired Benchmark

Add a documented A6 scenario to the benchmark contract before interpreting
results. Reuse the aggregate runner/report format where practical; extend it
without changing the meaning of existing metrics.

The scenario must compare the same inputs and output requirements through:

- the current WIC CPU scaling path, including its existing supported
  interpolation and conversion behavior;
- a benchmark-only Direct2D scale candidate using the proposed effect chain,
  including the proposed linear-gamma behavior.

Use deterministic common-format inputs from Datasets B and C and the
large-resolution inputs from E where their dimensions exercise the target
path. Include Dataset A as a small-folder sanity case if useful. Dataset D is
optional and only valid with legitimate local staged RAW samples; do not imply
synthetic images represent RAW decoding. Record format and dimension strata so
a result is not hidden by one easy image class.

At minimum, measure and retain per-sample:

- source decode time, separate from scaling;
- CPU time spent in the scaling path and elapsed scale completion time;
- end-to-end time until the final thumbnail is available in the representation
  consumed by the existing cache;
- completed thumbnails per second and p50/p95 latency where the sample count
  supports them;
- peak and steady process memory, plus GPU utilization or memory only when a
  reliable supported sampler is available;
- output dimensions, adapter/device mode, decode path, target size, cache state,
  and all relevant build and machine metadata.

Exercise the 256 px target explicitly and the largest supported thumbnail
preset. Keep decode, transform, upload, synchronization, readback, allocation,
and cache insertion boundaries visible. The GPU implementation must be timed
through completed output, not merely through effect construction or queued GPU
commands. Define warm-up and cache-state policy; run the two paths in an
interleaved or otherwise controlled order to reduce thermal and order bias.
Use at least five measured repetitions per path and scenario per invocation,
with raw values retained. Explain any unavailable metric rather than reporting
zero.

**Gate:** A reviewer can reproduce both paths from the same manifest and
arguments, identify every timed boundary, confirm output parity, and distinguish
hardware-GPU results from WARP or unsupported-device results.

## Phase 2: Measure and Make the Go/No-Go Decision

Run the paired scenario on a named reference machine with the Release build.
Repeat the experiment enough times to estimate run-to-run noise; retain the
executable identity, source revision, full JSON, and logs. Do not compare
measurements from unlike GPUs or device modes as if they were one profile.

A6's roadmap target is at least a 30% reduction in thumbnail-scale CPU time at
256 px on representative inputs. Treat that as necessary, not sufficient. Also
require:

- the reduction to exceed observed measurement noise and appear across the
  representative format/dimension mix, not only one favorable fixture;
- no meaningful regression in end-to-end thumbnail-ready latency or throughput
  after transfers and readback are included;
- no unexplained increase in peak memory, cache footprint, queue delay, or
  shutdown/cancellation cost;
- decoded dimensions, orientation, alpha, color/gamma handling, and visual
  quality to remain acceptable under a documented comparison.

**Decision gate:**

- **No-go:** If the target is missed, the result is within noise, or end-to-end
  costs erase the CPU saving, do not implement or enable the production path.
  Retain the report, document the reason, and leave A6 deferred.
- **Go:** Proceed only after the paired report demonstrates the target and the
  tradeoffs above are reviewed. Record the accepted hardware/profile scope and
  the reason the GPU path is worth maintaining.
- **Inconclusive:** Identify the missing dimensions, formats, repetitions, or
  hardware evidence. Do not convert an inconclusive result into a go decision.

## Phase 3: Resolve the Production Architecture

Before implementing the optimized path, write down the chosen ownership and
representation model based on the current source:

- Decide whether `CachedThumbnail` remains HBITMAP-backed or whether a
  byte-buffer representation is warranted. The roadmap's BGRA-byte proposal
  does not match the current cache contract; update the design and documentation
  rather than introducing an unreviewed parallel cache.
- Choose where the D2D device/context lives and which thread owns it. Respect
  the single-threaded factory and existing resource lifetimes. Do not share
  thread-affine D2D resources across decoder workers or move GPU operations onto
  the UI thread.
- Define what happens when hardware device creation fails, the device is lost,
  a transform fails, cancellation arrives, or an asynchronous result becomes
  stale. The WIC path must remain a reliable fallback.
- Define how effect completion is synchronized and how output reaches the
  existing cache. Account for readback costs in the performance model.
- Identify cache identity, thumbnail-size, orientation, color, and invalidation
  rules. Keep the optimized result keyed to the same content identity as the
  existing thumbnail.

**Gate:** The architecture preserves current scheduling, cache ownership, UI
responsiveness, and fallback behavior without an unsupported cross-thread D2D
assumption. If not, revise the proposal or stop; do not broaden into a renderer
rewrite to force A6 through.

## Phase 4: Implement the Smallest Proven Slice

Only after a go decision:

1. Add the D2D image-effect scaling path for the measured A6 thumbnail case,
   using the measured interpolation and linear-gamma settings.
2. Keep it behind a reversible internal capability/selection boundary while
   validating. Do not add a user-facing preference unless product requirements
   call for one.
3. Preserve the current WIC route as the fallback for unavailable hardware,
   device loss, unsupported input, and GPU-path errors. Fallback must not blank
   a valid thumbnail or block the UI.
4. Preserve asynchronous scheduling, cancellation, stale-result rejection,
   cache accounting, memory-pressure behavior, and exact output sizing.
5. Add focused tests for output dimensions and pixel properties, orientation,
   cancellation/stale completion, cache invalidation, hardware-unavailable
   fallback, and device-loss/error fallback. Use WARP for deterministic
   correctness coverage only when it exercises the intended code safely; do
   not count it as a performance pass.
6. Re-run the paired benchmark on the same hardware and configuration used for
   the baseline. Compare raw samples and distributions, not just a single
   before/after number.

Do not add A5 pooling, cache-format rewrites, generic GPU abstractions, or
unrelated decoder cleanup unless the measured implementation cannot work
without a narrowly justified change.

## Phase 5: Integrate, Document, and Close

- Keep ordinary CI hardware-independent. Add compile and deterministic
  correctness/fallback checks to the existing matrix where supported. Add a
  hardware performance job only if the repository has a stable, available GPU
  runner; otherwise provide an explicit opt-in local command and retain its
  artifacts without presenting it as a hosted gate.
- Preserve existing absolute and calibrated D3/A8 gates. A6 performance
  thresholds must be specific to a compatible GPU profile and supported by
  repeated measurements; do not mix them into CPU/WIC profiles.
- Update the benchmark scenario matrix, benchmark usage documentation, roadmap
  status, and report metadata with the accepted hardware scope, threshold,
  dataset checksums, limitations, and source artifact identities.
- Retain all per-run JSON and logs, including failed and rejected experiments.
  Keep baseline updates manual and reviewable.

## Completion Criteria

A6 is complete only when the prerequisite D3/A8 gates are reviewed, the A6
paired benchmark and its limitations are documented, the go decision is
supported by retained repeated evidence, the production GPU path passes
correctness and fallback tests, and the same-hardware before/after run meets
the 256 px target without unacceptable end-to-end or resource regressions.

A well-supported no-go is a valid result of this prompt, but it is not an
implemented A6 feature. In that case, leave A6 deferred and preserve the
benchmark evidence and reason for not proceeding.
