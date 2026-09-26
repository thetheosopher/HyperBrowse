# Long-Horizon Prompt 02: Close the Performance Evidence Loop

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Complete roadmap items D1-D3 and the remaining A8 work as one coherent
performance-evidence system. Provide formal, reproducible benchmark datasets;
a repeatable runner that produces per-run and aggregate JSON; retained results;
and baselines whose budgets are based on measured variance rather than a single
local run or convenient round numbers.

This work is a prerequisite for further A5 WIC-controlled scale-buffer pooling
and for A6 GPU-accelerated thumbnail scaling. Do not implement or expand either
optimization in this task. Finish the evidence system first, use it to identify
whether either optimization addresses a measured bottleneck, and record the
recommendation for the next implementation step.

## Current Evidence

Verify each point before relying on it; these are starting observations, not
permission to preserve a flawed implementation:

- Startup diagnostics emit a JSON snapshot through `--bench-startup`, and
  `tools/TestStartupBenchmark.ps1` runs the Release app against a folder using
  an isolated settings registry key.
- CI currently applies fixed startup budgets of 2500 ms to first-window
  visibility, 2500 ms from visibility to first thumbnail, and 5000 ms overall.
- `HyperBrowsePerformanceBenchmark` and
  `tools/TestPersistentCacheBenchmark.ps1` cover persistent-cache hit/store/
  compaction/queue-delay measures and synthetic hidden-browser scroll dispatch.
- CI uploads startup and persistent-cache snapshots, but the current checks do
  not compare those measurements to a checked-in, measured baseline.
- CI uses the small checked-in `assets` directory as its deterministic startup
  fixture. It is not a substitute for the standard A-E benchmark datasets in
  `specs/archive/05-benchmarking-plan.md`.
- The archived plan calls for at least five runs per scenario and recording
  environment, dataset, configuration, cache state, distributions, and
  variance.

## Operating Rules

- Work directly in the current workspace. Read `.github/copilot-instructions.md`,
  applicable implementation instructions, `docs/architecture.md`,
  `docs/testing.md`, `specs/FUTURE-ROADMAP.md`, the archived benchmark plan,
  `tools/TestStartupBenchmark.ps1`, `tools/TestPersistentCacheBenchmark.ps1`,
  and `.github/workflows/ci.yml` before changing behavior.
- Preserve unrelated user changes. Never reset, revert, stash, or overwrite
  them. Do not commit or create a branch.
- Keep this task to datasets, benchmark instrumentation needed to produce
  trustworthy measurements, runner/reporting, CI retention, baselines, and
  documentation. Do not change production WIC pooling, thumbnail scaling, or
  GPU behavior.
- Reuse the existing startup and persistent-cache benchmark paths where they
  provide valid measurements. Do not replace a useful focused gate merely to
  make the new runner appear unified.
- Keep benchmarks opt-in outside CI where they require interactive actions or
  hardware unavailable on hosted runners. Document those scenarios and retain
  their JSON, but do not represent a manual measurement as an automated CI gate.
- Do not use personal photo libraries, unlicensed media, network downloads, or
  machine-specific paths as checked-in benchmark inputs. Prefer deterministic
  generated fixtures. Keep any user-supplied real RAW files local and describe
  their required properties without redistributing them.
- Distinguish process-cold from OS file-cache-cold runs. Do not claim a cold
  disk/cache state unless the runner can create and verify it safely.
- Never loosen a budget solely to make CI green. If the environment cannot
  provide enough independent measurements to calibrate a threshold, record the
  gap and leave the baseline explicitly provisional rather than claiming
  completion.
- After every substantive edit, run the cheapest focused validation for that
  slice before continuing. Preserve benchmark JSON and logs on failed runs too.

## Source Anchors

- Roadmap contracts and A5/A6 dependencies:
  `specs/FUTURE-ROADMAP.md`, Themes A and D.
- Dataset definitions and original scenario methodology:
  `specs/archive/05-benchmarking-plan.md`.
- Startup measurements and isolated launch settings:
  `tools/TestStartupBenchmark.ps1` and the startup diagnostics implementation
  found by tracing `--bench-startup`.
- Persistent-cache and hidden-browser benchmark metrics:
  `tools/TestPersistentCacheBenchmark.ps1` and
  `HyperBrowsePerformanceBenchmark` in the test sources/build configuration.
- Existing Release/Debug matrix, fixed thresholds, decode-path variants, and
  artifact upload behavior: `.github/workflows/ci.yml`.
- Benchmark instructions and the claims currently made to contributors:
  `docs/testing.md`.

## Autonomous Implementation Queue

### A. Inventory the evidence that exists

1. Record the worktree state, active build tree, exact Release executable, CI
   matrix dimensions, and current benchmark output schema.
2. Run the existing startup and persistent-cache gates once without changing
   their thresholds. Save the raw JSON and logs outside tracked generated
   output; record the executable path and source revision.
3. Inspect every reported metric: identify its owner, unit, sampling window,
   cache assumptions, and whether it measures user-visible work or a synthetic
   proxy. Find any roadmap metric that currently has no trustworthy producer.
4. Search for existing dataset fixtures and baselines before creating new
   ones. Reconcile what is actually present with D1-D3 and A8.

Gate: the inventory distinguishes implemented measurements, synthetic
proxies, missing scenarios, and current fixed-budget checks. No number is
treated as a calibrated baseline merely because it passed once.

### B. Define and generate standard datasets

Implement the D1 A-E set from the archived plan under the repository's
benchmark-dataset convention. Keep generated bytes reproducible and generated
output out of source control unless existing repository policy explicitly
requires checked-in fixtures.

- **A, small mixed folder:** 100-250 files across supported common formats,
  including representative JPEG, PNG, GIF, and TIFF inputs.
- **B, large JPEG-heavy folder:** a deterministic 2,000-10,000-image fixture
  for thumbnail throughput and the optional nvJPEG/WIC comparison.
- **C, mixed-format production-like folder:** thousands of files with declared
  format proportions and known dimensions. Generate the common-format content
  deterministically and label it synthetic; any real RAW subset must use the
  local staging rules for Dataset D rather than implying generated files are
  production samples.
- **D, RAW-focused folder:** define a manifest for representative NEF/NRW
  samples, distinguishing embedded-preview and full-decode cases. Use
  redistributable fixtures only when their provenance permits it; otherwise
  provide a local staging manifest and exact validation requirements.
- **E, large-resolution stress folder:** deterministic high-resolution
  landscape and portrait images, with bounded total generated size and an
  explicit memory-pressure procedure.

For each dataset, record a stable ID and version, generator version/seed,
format and dimension counts, expected file count and byte size, checksums, and
known prerequisites. The manifest must make incomplete generation detectable.
Generation must be repeatable and safe to rerun; it must not overwrite user
data or pull content from the network. Reuse existing supported image-writing
dependencies or Windows facilities instead of adding a runtime dependency to
the product.

Gate: two independent generations from the same manifest produce identical
dataset inventories and checksums. Dataset verification rejects missing,
unexpected, or corrupt files with an actionable error. RAW staging reports
which optional cases are unavailable without silently counting them as passes.

### C. Establish scenario and metric contracts

Map each scenario in the archived plan to an existing measurement or an
explicitly scoped instrumentation change. At minimum, preserve the current
startup and persistent-cache gates and associate them with formal dataset IDs.
Classify the remaining scenarios as automated, benchmark-executable-only, or
manual/hardware-dependent. Do not report an unsupported metric as zero or
infer it from unrelated timings.

For every automated measurement, document:

- exact start and end events, unit, aggregation method, and whether lower or
  higher is better;
- dataset ID/version and checksum, build configuration, decode path, thumbnail
  size, relevant settings, and cold/warm state;
- machine/runner image, Windows version, CPU, GPU, RAM, and nvJPEG availability
  when available from the environment;
- repetitions, raw samples, median, p95 where sample count supports it, min/max,
  and any discarded-run reason;
- limitations, including synthetic metrics such as hidden-window scroll
  dispatch and unavailable physical input or GPU utilization measurements.

Keep timing boundaries stable across revisions. Add instrumentation only at
the owning boundary and only where it can be validated against the actual
user-visible event. Avoid high-volume logging in timed regions.

Gate: a reviewer can determine exactly what each JSON metric means and can
reproduce the same scenario from the manifest and runner arguments.

### D. Build a repeatable runner and JSON report

Implement or extend `tools/RunBenchmarks.ps1` as the public entry point. It
should orchestrate the valid existing benchmark scripts and supported new
scenarios without duplicating their measurement logic.

The runner must:

1. Accept explicit build/executable, configuration, dataset, scenario, run
   count, output root, and cold/warm options; validate inputs before launching.
2. Run scenarios serially unless a scenario explicitly supports concurrency.
   Use unique settings/IPC identities and deterministic cleanup so a user's
   running application or settings cannot contaminate a result.
3. Preserve one raw JSON file per run, then emit a versioned aggregate
   `report.json` containing every raw sample and the calculated statistics.
4. Include source revision, dirty-worktree indication, executable path and
   timestamp, dataset manifest/checksum, environment, configuration, scenario
   settings, runner version, start time, and exit status.
5. Keep output under a commit/build-specific location such as
   `build/bench/<git-sha>/`; generate `docs/perf/latest.md` as a concise
   human-readable view without making generated run output a source artifact.
6. Write a failure record and retain partial output when a child process,
   scenario, timeout, JSON parse, or validation fails. Return nonzero after
   preserving evidence; never silently omit failed runs.
7. Validate its own output as JSON and reject schema, unit, dataset, or
   executable mismatches before baseline comparison.

Start with at least five repetitions per automated scenario, as required by
the archived plan. Make repetition count configurable so calibration can use
more samples without editing scripts. The aggregate must retain raw values;
summary statistics alone are insufficient evidence.

Gate: two invocations using the same build, manifest, and options produce
schema-valid reports with the same scenario inventory and metadata shape;
expected machine timings may vary, but no sample is lost. A deliberately
failing child run still leaves inspectable JSON and a failing exit status.

### E. Calibrate baselines and regression policy

Calibrate each CI-gated scenario using repeated Release measurements on the
same hosted runner image and the same decode-path matrix. Use enough independent
workflow samples to estimate runner variance; target at least ten successful
workflow samples per matrix leg in addition to the per-invocation repetitions.
If that evidence cannot be collected in the current environment, finish the
tooling and report calibration as blocked/provisional.

Store a versioned, reviewed baseline at `docs/perf/baseline.json`. Keep it
separate by runner image and decode-path configuration where those conditions
change the result. Record source dataset/version/checksum, sample count,
distribution, date/revision, and the reasoning for every threshold. Do not
compare unlike datasets, hardware paths, or schema versions.

Use robust statistics and the observed noise floor to set regression limits.
Keep absolute safety ceilings where they protect user experience, but separate
those ceilings from relative baseline-regression limits. A baseline update
must be reviewable and intentional; CI must not rewrite or bless a baseline
automatically after a regression. Explain how to capture and propose an
updated baseline.

Gate: every CI comparison is against a compatible measured baseline and fails
clearly when the baseline is absent, incompatible, or exceeded. Thresholds are
supported by retained repeated-run evidence, not just by historical fixed
constants. The startup limits are revisited using the measured distributions;
do not tighten them beyond runner noise or widen them without evidence.

### F. Retain results and wire the CI gate

Update `.github/workflows/ci.yml` to run the standard automated dataset suite
on the existing Release decode-path matrix. Reuse current build outputs. Keep
the normal smoke/test gates and existing focused benchmark assertions.

- Upload all per-run JSON, aggregate `report.json`, generated summary, and
  relevant logs on both success and failure.
- Set and document an explicit artifact retention period consistent with the
  repository/organization policy. The reviewed baseline remains versioned in
  source control; uploaded run artifacts preserve the evidence used to assess
  it.
- Include the dataset manifest/checksum and baseline identity in each report
  so an artifact remains interpretable after code changes.
- Ensure a failed benchmark still uploads whatever evidence was produced.
- Make manual/hardware-dependent scenarios available through documented local
  runner arguments, without making ordinary hosted CI require optional
  hardware or private media.

Gate: a CI run produces retrievable JSON for every matrix leg, and a deliberate
threshold violation fails the job while retaining its report. The gate cannot
pass by skipping a missing dataset, metric, or baseline.

### G. Use the evidence to decide A5/A6 readiness

Do not implement either optimization in this prompt. Once D1-D3/A8 gates are
green, publish a short evidence-based decision in `docs/perf/latest.md` or the
reviewed benchmark report:

- For remaining A5 WIC scale-intermediate pooling, identify allocation counts
  or allocation cost on the WIC decode/scale path and compare throughput,
  first-visible-thumbnail latency, memory, and tail latency. Pool only if the
  measured allocation pressure is material and the benchmark can observe the
  effect without changing unrelated paths.
- For A6 GPU scaling, establish that CPU scaling is a material bottleneck on
  dataset E and the relevant thumbnail sizes. Define a paired CPU/WIC versus
  GPU comparison, correctness checks, memory impact, and hardware fallback
  behavior. Preserve the roadmap's target of at least 30% thumbnail-scale CPU
  reduction at 256 px as a hypothesis to measure, not a promised result.
- If the evidence does not support an optimization, recommend deferring it.
  Do not interpret lack of instrumentation or unavailable hardware as proof of
  benefit.

Gate: the decision cites reproducible JSON report IDs and states the measured
benefit, variance, tradeoffs, and remaining uncertainty. A5/A6 work starts only
after this evidence gate is reviewed.

### H. Documentation and final validation

Update `docs/testing.md` with dataset generation/verification, runner commands,
JSON schema, retention policy, calibration/baseline update procedure, and
hardware-dependent limitations. Update `specs/FUTURE-ROADMAP.md` to distinguish
completed D1-D3/A8 work from any remaining manual scenarios, and leave A5/A6
explicitly evidence-gated. Do not claim full coverage for scenarios that remain
manual or unmeasured.

Run focused dataset and runner checks first, then the Release smoke and
benchmark gates using the repository's CMake presets. Run the Debug tests if
source/test behavior changed. Finish with `git diff --check`, verify all
referenced files exist, and confirm the exact executable under test is newer
than the changed sources.

## Completion Criteria

This prompt is complete only when:

- Datasets A-E have formal manifests and deterministic generation/verification;
  restricted RAW inputs have a safe, explicit local-staging contract.
- The repeatable runner invokes supported scenarios, retains every raw sample,
  emits versioned per-run and aggregate JSON, and preserves failures.
- Startup and persistent-cache scenarios use formal dataset identities; other
  scenarios are honestly classified as automated, executable-only, or manual.
- CI compares compatible Release measurements to a reviewed checked-in
  baseline, reports a clear regression, and retains JSON on failure.
- Baseline thresholds are grounded in repeated measurements and runner
  variance. Any uncollected calibration evidence is called out as a blocker,
  not presented as complete.
- `docs/testing.md` and the roadmap match the shipped runner, datasets, gates,
  and retention behavior.
- No A5 WIC-pooling or A6 GPU-scaling implementation was started. The final
  evidence report makes a reproducible recommendation on whether to proceed.

The final report lists dataset IDs/checksums, report and baseline locations,
calibration sample counts and environment, CI retention behavior, validation
commands/results, the A5/A6 recommendation, and any remaining limitations.
