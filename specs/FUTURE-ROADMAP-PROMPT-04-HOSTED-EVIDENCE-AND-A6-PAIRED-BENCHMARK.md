# Long-Horizon Prompt 04: Publish Hosted Evidence, Calibrate, Then Measure A6

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Execute the evidence sequence needed before A6 production work:

1. Publish the reviewed D1-D3/A8 evidence workflow to an approved remote ref.
2. Collect and review at least ten independent successful hosted workflow
   artifacts for each Release decode-path profile.
3. Calibrate and review the D3/A8 baselines from the retained raw samples.
4. Build and run a benchmark-only, paired WIC-versus-Direct2D thumbnail-scale
   comparison on a verified hardware GPU.
5. Publish an evidence-based A6 go/no-go recommendation and stop before
   changing production thumbnail scaling.

Prompt 02 defines the shared evidence system. Prompt 03 defines A6's
measurement, architecture, and production acceptance contract. This prompt
coordinates their execution in the required order. Do not replace, weaken, or
claim completion of either contract.

## Current Execution Hold

As of 2026-09-26, the user has opted out of cloud-hosted GitHub Actions. The
repository's only workflow, `HyperBrowse CI`, is manually disabled; run
`36270914454` was cancelled, and no runs remain active or queued. Do not enable
the workflow, dispatch it, or trigger a cloud run through a push or pull request
without fresh explicit user authorization.

The hosted D3/A8 collection gate is paused. `docs/perf/baseline.json` remains
at `collecting` with `workflowSampleCount: 0` for both profiles; cancelled,
failed, local, or otherwise incomplete runs do not count toward the hosted
sample requirement. Before resuming, obtain approval either for hosted Actions
or for a revised no-cloud evidence policy. Local measurements must not be
described as hosted-runner samples or used to calibrate the existing
`windows-2022` profiles without an explicitly reviewed change to the evidence
contract.

## Hard Scope Boundary

This task may publish and run the current evidence workflow, calibrate the
existing startup/cache profiles, and add the isolated benchmark-only A6
scenario needed to measure both paths. It must not route production thumbnail
decodes through the GPU candidate, change the normal WIC decoder behavior,
change the thumbnail cache representation, or implement A5 pooling. Stop after
the paired A6 report and recommendation. Production A6 work requires a separate
explicit implementation decision after the go/no-go review.

## Publication and Worktree Safety

Publishing code and dispatching GitHub Actions are remote-visible actions.

- Inspect the current branch, worktree, upstream, current remote SHA, and all
  changed/untracked files before acting. Preserve user changes and generated
  evidence. Never reset, clean, stash, or revert unrelated content.
- Separate the reviewed files required for the evidence workflow from unrelated
  user changes. Do not silently include unrelated files in a commit or push.
- Follow the repository's current commit and branch instructions. Obtain
  explicit user authorization before any commit or push if the invocation does
  not already explicitly authorize publication. Never force-push, bypass branch
  protection, or push directly to a protected branch contrary to repository
  policy. If publication is blocked, finish local review and report the exact
  files/ref/approval needed; do not pretend remote runs used local changes.
- Dispatch only a ref that contains the reviewed workflow, runner, dataset, and
  baseline schema changes. Verify the resulting run's `headSha` before counting
  any artifact.
- Do not expose GitHub tokens or other credentials in logs or reports.

## Phase 0: Revalidate Current State

Before publishing or adding a benchmark scenario, inspect the live repository
and remote state. Starting values below are not assumptions to preserve:

- `.github/workflows/ci.yml`, `tools/RunBenchmarks.ps1`, the dataset generator,
  `docs/perf/baseline.json`, Prompt 02, Prompt 03, `docs/testing.md`,
  `docs/perf/scenario-matrix.md`, and the A6 roadmap entry.
- The CI matrix, workflow triggers, concurrency group, artifact names,
  `retention-days`, per-scenario repetition count, benchmark exit behavior, and
  `if: always()` artifact upload behavior.
- The actual baseline schema and comparison code. Confirm what
  `workflowSampleCount` counts and which raw values the report retains.
- The current remote's workflow runs and artifacts, including their ref, head
  SHA, run ID, attempt, matrix leg, conclusion, creation time, and contents.
  Older runs or artifacts from another benchmark schema do not count.
- The exact Release executable/build tree and the state of D1/D2/D3/A8.
- C++ implementation instructions and the owning WIC decoder, D2D renderer,
  thumbnail cache, scheduler, benchmark executable, tests, and CMake targets
  before editing benchmark behavior.

Record this inventory in the final evidence note. Do not edit the calibrated
baseline until the collection and review gates below pass.

**Gate:** If the remote workflow does not contain the reviewed current evidence
workflow, or the working tree contains changes that cannot safely be published,
stop at publication authorization. Local files and local benchmark results are
not hosted workflow artifacts.

## Phase 1: Publish and Validate the Evidence Workflow

Use the repository's approved publication path for the existing branch/ref.
The workflow must be available on the remote before collection starts. Confirm
that its trigger supports dispatch on the selected ref; otherwise use the
normal protected-branch/PR path rather than bypassing repository policy.

Before collecting calibration evidence, run the focused local dataset, runner,
PowerShell/JSON, and Release build/test checks required by Prompt 02. Verify
that the CI command still runs five Release samples per scenario for both
`windows-2022/nvjpeg` and `windows-2022/wic-fallback`, compares against the
provisional profile without suppressing absolute guardrails, and uploads reports,
per-run JSON, logs, and the summary on failure as well as success. Preserve the
existing 90-day retention unless repository policy requires a different value.

Use one immutable source SHA for the whole collection. Record the ref, SHA,
workflow file revision, dataset inventory checksums, runner version, and
baseline schema. If any benchmark code, dataset, or workflow semantics change,
stop and restart the collection on a single new SHA; do not combine revisions.

## Phase 2: Collect Ten Independent Artifacts per Profile

The current workflow is expected to produce one artifact per matrix leg, with a
name similar to
`performance-<decode-path>-<run-id>-<run-attempt>`. Verify the actual naming
and contents before relying on it.

- Collect at least ten successful artifacts for `windows-2022/nvjpeg` and ten
  for `windows-2022/wic-fallback`.
- Count a matrix-leg artifact only when it belongs to a distinct successful
  workflow `run_id`, has the exact approved `headSha`, and contains the
  expected compatible Release report. Do not count multiple attempts, duplicate
  downloads, or two matrix artifacts as two samples for the same profile.
- Require the workflow and benchmark job to conclude successfully, with five
  successful raw samples for every required scenario and no missing metrics,
  dataset mismatches, or partial reports. Retain failed runs for diagnosis but
  exclude them from the successful calibration count.
- Since one successful matrix workflow should yield one artifact for each
  decode path, prefer ten fully successful workflow runs, producing ten
  distinct artifacts per profile. Confirm both artifacts exist for each run.
- Run dispatches serially. The workflow's per-ref concurrency policy may cancel
  an earlier run when another starts on the same ref. Wait for completion and
  verify its conclusion/artifacts before dispatching the next run.
- Keep each downloaded artifact in a unique local directory keyed by run ID,
  attempt, and profile. Do not overwrite or merge raw files with the same names.
  Record artifact IDs/names and checksums in a calibration inventory.
- Keep collection within the artifact retention period. If an artifact expires,
  replace it with a new successful independent run; do not count a stale report
  that is no longer reviewable.

A dispatch may be initiated with the GitHub CLI only after the ref is published
and approved, for example:

```powershell
gh workflow run ci.yml --ref <approved-ref>
```

Capture the exact run ID created by that dispatch, wait for completion with
`gh run watch <run-id> --exit-status`, inspect its metadata, then download each
matrix artifact by its exact name. Do not use a broad `gh run list` result to
mistake an older push/PR run for the dispatch just started.

**Gate:** Each profile has ten distinct successful run IDs at one source SHA,
with compatible configuration, dataset checksums, schema, and complete raw
samples. Ten workflow artifacts with five repetitions each provide at least 50
raw observations per metric per profile. If either profile misses this gate,
continue collection or report the blocker; do not calibrate or proceed to the
A6 paired run.

## Phase 3: Review and Calibrate D3/A8

Review the actual downloaded JSON, not only each workflow conclusion or
Markdown summary. For every CI-gated metric and profile:

1. Validate report schema, scenario inventory, units, dataset ID/checksum,
   Release configuration, decode-path selection, source SHA, executable
   identity, environment, sample count, and per-run status.
2. Reconcile aggregate values with the retained per-run values. Detect missing,
   duplicated, non-finite, failed, or incompatible samples. Keep failures
   visible and document any exclusion using a rule established before looking
   at its effect on the result. Do not discard an inconvenient outlier merely
   to obtain a tighter threshold.
3. Analyze the pooled raw observations and per-workflow distributions. Report
   median, nearest-rank p95 where supported, min/max, and a robust measure of
   spread/noise. Do not treat ten workflow medians as ten raw repetitions or
   conflate `nvjpeg` build configuration with proof of actual nvJPEG hardware
   decoding on hosted Windows runners.
4. Set relative tolerances above observed runner noise while retaining all
   current absolute safety guardrails. Explain the evidence and rationale for
   every threshold. Do not loosen an absolute limit to force a pass.
5. Update `docs/perf/baseline.json` and a reviewable calibration note with the
   source SHA, dataset checksums, date, profile, workflow run IDs, artifact
   names/IDs, sample counts, distributions, exclusions, and threshold rationale.
   Preserve the schema expected by the comparator and validate that absent,
   incompatible, and exceeded calibrated baselines fail closed.
6. Mark a profile `calibrated` only after its ten artifacts are reviewed and its
   relative metrics are populated. Remove `-AllowProvisionalBaseline` from CI
   only after both profiles are calibrated and reviewed. Run a confirmation CI
   workflow against that change and verify the calibrated comparisons pass.

Do not count the confirmation run toward the original ten unless it has the
same source SHA and benchmark semantics; ordinarily it will not. It can begin a
new evidence series if a later code change requires recalibration.

**Gate:** Both profiles are reviewed and calibrated from retained, compatible
raw evidence, with the CI comparison behavior validated. If measurements are
noisy, contradictory, or fail absolute budgets, stop and report the issue. Do
not start A6 benchmarking until this gate is satisfied.

## Phase 4: Implement an Opt-In A6 Benchmark Candidate

Build only the benchmark path necessary to compare WIC and Direct2D scaling.
The benchmark must not route production thumbnails through the GPU candidate.
Keep the existing default persistent-cache benchmark and ordinary CTest behavior
unchanged; A6 hardware measurements are opt-in and are not a hosted CI
requirement unless a stable, compatible GPU runner is explicitly available.

Before coding, document the benchmark boundary and resolve these facts from the
current source:

- `WicThumbnailDecoder` uses WIC scaling and returns an HBITMAP-backed
  `CachedThumbnail`; its existing `thumbnail.scale` timing is not automatically
  a clean source-decode-versus-scale split.
- The existing single-threaded D2D renderer/factory and UI render target must
  not be reused from decoder workers without a proven ownership model.
- The current GPU candidate must produce the same cache-ready output
  representation as the WIC baseline. Include GPU upload, synchronization,
  readback, allocation, and copying into the HBITMAP/cache-ready result in
  elapsed end-to-end measurements; queued effect submission alone is not a
  completed thumbnail.

Prefer an explicit opt-in mode on the existing performance executable or a
focused companion target, selected through the aggregate runner. The normal
cache benchmark invocation must remain unchanged. Implement a benchmark-owned
D3D/D2D device/context on a thread with explicit ownership; do not add a
production dependency on the UI-owned singleton. The candidate must request a
hardware adapter directly and report its DXGI vendor/device/LUID and driver.
If only WARP/software or an unexpected adapter is obtained, fail the hardware
performance mode as unavailable rather than reporting it as an A6 pass.

The paired scenario must:

- Use generated datasets B and C plus every image in E, or a fixed, documented,
  deterministic stratified subset from B/C and every image in E if full-folder
  repetitions exceed a practical bounded runtime. Include the selected file
  names/checksums, formats, dimensions, and selection rule in JSON. Never use
  private or network media. Dataset D is optional and only valid for legitimate
  local staged RAW inputs.
- Compare the current WIC path to a benchmark-only Direct2D effect path on the
  same decoded pixel input and exact output geometry. Also report the
  file-to-cache-ready path separately so decode/materialization is not
  mislabelled as pure scale time. Document lazy WIC work and define how the
  common source is materialized for scale-only timing.
- Exercise 256 px and the largest supported thumbnail preset. Record source
  dimensions/orientation and output dimensions for every sample.
- Interleave path order using a deterministic schedule, warm each path equally,
  and record cache state. Run at least five measured repetitions per path and
  scenario; prefer ten paired blocks when feasible. Keep raw per-image and
  per-repetition values.
- Report source preparation/decode time, scale CPU time, scale completion
  elapsed time, upload/readback/allocation/copy boundaries, end-to-end
  cache-ready latency, throughput, and p50/p95 where supported. Report peak
  memory and CPU/GPU telemetry only when obtained from a reliable sampler;
  otherwise encode an explicit unavailable reason, not zero.
- Compare output dimensions, orientation, premultiplied alpha, and pixel/color
  differences using a documented tolerance. Do not claim visual equivalence
  based only on matching dimensions.
- Store a versioned raw JSON report, executable/source identity, dataset and
  inventory checksums, build configuration, adapter/device proof, driver,
  Windows version, D2D device mode, path order, timings, failures, and logs under
  a unique `build/bench/a6-scale/<source-sha>/<run-id>/` directory.
- Update the scenario matrix and `docs/testing.md` to identify A6 as an
  opt-in, hardware-dependent benchmark. Keep A6 GPU-profile results separate
  from the Windows hosted startup/cache baseline profiles.

Do not add the A6 benchmark to ordinary hosted CI by default. Do not silently
fall back to WARP, treat a missing GPU metric as zero, rewrite `CachedThumbnail`,
or add the production decoder branch as part of this phase.

**Gate:** The harness proves the selected adapter is hardware, both paths
consume equivalent source pixels and produce cache-ready outputs, correctness
checks pass, and every timing boundary is explicit. Otherwise, fix the harness
or stop with an inconclusive result; do not interpret performance numbers.

## Phase 5: Build, Verify Hardware, and Run the Pair

1. Generate and verify B/C/E using the repository dataset generator. Record
   their inventory checksums and ensure the executable is the exact Release
   build being measured.
2. Build the opt-in benchmark target using the repository CMake preset/build
   tree. Run focused deterministic correctness tests first. Confirm the normal
   cache benchmark and existing CTest targets still work.
3. Run the benchmark's hardware probe before timing. Verify that the requested
   RTX 5080 or another named adapter was selected by the benchmark-owned D3D
   device and that D2D was created from that hardware device. Cross-check the
   adapter identity and driver with an independent Windows/NVIDIA tool where
   available. Retain the probe JSON/log. If hardware cannot be proven, stop;
   WARP results may support correctness only.
4. Run the paired benchmark in Release at both target sizes over the declared
   B/C strata and all E cases. Keep the machine otherwise idle, run the two
   paths in controlled interleaved order, and retain every sample and failure.
5. Repeat the paired invocation enough to estimate local noise. A minimum of
   five repetitions per path/scenario is required; ten paired blocks are the
   preferred decision set. Record the exact command and report ID.
6. Do not upload private data or credentials. Publish a report artifact only
   through an approved repository workflow or attach it through the repository's
   normal review process. Keep generated measurements out of tracked source
   unless the review explicitly accepts a concise evidence summary.

A sample command shape, to be finalized by the implemented runner, is:

```powershell
.\tools\RunBenchmarks.ps1 -ProjectRoot $PWD -BuildDir .\build `
    -Configuration Release -Scenario A6Scale -DatasetId B,C,E `
    -Runs 10 -RequireHardwareGpu -AdapterVendorId 0x10DE
```

Do not claim this command exists until the focused scenario and parameters have
actually been implemented and validated. If the current runner cannot express
this safely, add the smallest opt-in command contract and document it.

## Phase 6: Review A6 and Stop at the Decision

Compare the paired distributions on the verified adapter. The roadmap target
of at least 30% lower thumbnail-scale CPU time at 256 px is necessary, not
sufficient. Require the improvement to exceed noise and hold across the
representative strata. Also verify no meaningful regression in end-to-end
cache-ready latency, throughput, memory, queue behavior, visual output, or
cancellation/shutdown behavior.

- **Go:** The report supports the target and tradeoffs; recommend a separate,
  reviewed production implementation task under Prompt 03. State the specific
  supported adapter/profile and limitations.
- **No-go:** The CPU reduction is below target, within noise, or erased by
  transfer/readback/cache costs. Retain the report and recommend keeping A6
  deferred.
- **Inconclusive:** Evidence is incomplete, hardware identity is uncertain,
  outputs fail parity, or the profile is too noisy. State the precise gap and
  do not proceed to production code.

Update the A6 scenario matrix, testing guidance, and roadmap with the report
identity and decision. Keep the shared D3/A8 baseline calibration and the A6
hardware result as distinct evidence products.

## Completion Criteria

This prompt is complete only when:

- The reviewed evidence workflow ran on one published immutable SHA.
- Ten distinct successful workflow artifacts were collected and reviewed for
  each decode-path profile, with the required five raw repetitions per
  scenario in every artifact.
- Both hosted profiles have reviewed, measured baseline distributions and
  tolerances, the provisional comparison mode is removed, and a confirmation
  CI run passes.
- The opt-in A6 paired benchmark was built and its selected hardware adapter
  was verified; WIC and GPU candidate paths ran on the prescribed data and
  sizes with complete raw JSON and logs.
- The report states a defensible go, no-go, or inconclusive recommendation and
  records the remaining uncertainty.
- No production GPU thumbnail scaling or A5 pooling was implemented.

If publication permission, hosted success, baseline calibration, a valid GPU,
or output correctness is unavailable, stop at that gate, preserve all evidence,
and report what is still required. Never mark A6 complete merely because the
prompt, harness, or one local run exists.
