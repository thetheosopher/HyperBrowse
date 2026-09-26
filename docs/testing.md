# HyperBrowse Testing Guide

## Test layers

### Build validation

The normal development configuration is the `vs2026-x64` CMake preset. It uses the Visual Studio 2026 generator, x64 architecture, and builds tests by default.

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug --target HyperBrowse
cmake --build --preset debug --target HyperBrowseTests HyperBrowsePerformanceBenchmark
```

Use the Release preset for release-sensitive changes:

```powershell
cmake --build --preset release --target HyperBrowse
```

The CI workflow also builds with warnings as errors and validates both Debug and Release configurations. It runs the build/test/benchmark job twice: once with optional nvJPEG enabled and once with `HYPERBROWSE_ENABLE_NVJPEG=OFF` to exercise the WIC fallback path. The repository's local VS Code build task configures with `--fresh`, so expect a reconfigure when using that task.

### Smoke and integration tests

Run the CTest presets:

```powershell
ctest --preset debug-tests
ctest --preset release-tests
```

The current test target registers:

- `HyperBrowseSmoke`
- `HyperBrowseFolderHistorySmoke`
- `HyperBrowseFileOperationMediaCacheSmoke`
- `HyperBrowseViewerFitSmoke`
- `HyperBrowseViewerInteractionSmoke`
- `HyperBrowseThumbnailPersistenceSmoke`
- `HyperBrowseThumbnailPathSafetySmoke`
- `HyperBrowseThumbnailMaintenanceSmoke`
- `HyperBrowseThumbnailStaleCompletionSmoke`
- `HyperBrowseThumbnailFailureSmoke`
- `HyperBrowseFileRenameSmoke`
- `HyperBrowseAppTextSizeSmoke`
- `HyperBrowseAccessibilitySmoke`
- `HyperBrowseDialogGeometrySmoke`
- `HyperBrowseSettingsSmoke`
- `HyperBrowseMultiViewerSettingsSmoke`
- `HyperBrowseItemNumberNavigationSmoke`
- `HyperBrowseUserMetadataSmoke`
- `HyperBrowseExternalDropTargetSmoke`
- `HyperBrowseMenuMetricsSmoke`
- `HyperBrowseResponsivePanelSmoke`
- `HyperBrowsePerformanceBenchmark`

All 22 tests above are enabled. `HyperBrowsePerformanceBenchmark` writes a JSON snapshot but does not enforce hosted-runner budgets by itself. When `HYPERBROWSE_BUILD_FUZZ_TESTS=ON`, CMake also registers `HyperBrowsePersistentCacheFuzz` and `HyperBrowseRawHelperProtocolFuzz`; these optional boundary tests are absent from normal builds rather than registered as disabled tests.

The `vs2026-x64` development preset explicitly sets `HYPERBROWSE_BUILD_FUZZ_TESTS=OFF`. An opt-in configuration may set it to `ON`; building `HyperBrowseTests` then builds `HyperBrowseBoundaryFuzz` before the two boundary tests are run.

On a machine with a supported NVIDIA GPU, prove that the configured runtime performs an actual decode, rather than only compiling the nvJPEG path, with:

```powershell
.\build-release-package\tests\Release\HyperBrowseTests.exe --nvjpeg-hardware C:\path\to\fixture.jpg
```

Run this command from a CUDA-bundled build tree so `cudart64_12.dll` and `nvjpeg64_12.dll` are beside the test executable. A successful exit means an nvJPEG decode completed on the available CUDA device; the normal CTest matrix continues to allow WIC fallback on hosts without suitable hardware.

The tests cover model/service behavior and selected application/viewer state without requiring every workflow to be driven through a live desktop session. The full smoke also exercises persistent-cache sharding, atomic entry replacement, bounded legacy flat-layout migration, restart loading, malformed index paths, corruption cleanup, source-missing maintenance, asynchronous statistics/compact/purge callbacks, per-shard statistics, adjacent invalidation coalescing, and pressure-mode store suppression. Add focused coverage to `tests/smoke.cpp` when a change can be exercised deterministically there.

### Startup and performance checks

The standard datasets are defined in `tests/benchmark-datasets/manifest.json`.
Generate and verify the synthetic datasets with:

```powershell
.\tools\GenerateBenchmarkDatasets.ps1 -ProjectRoot $PWD -DatasetId A,B,C,E
.\tools\GenerateBenchmarkDatasets.ps1 -ProjectRoot $PWD -DatasetId A,B,C,E -VerifyOnly
```

Generated image files are placed under `build/benchmark-datasets/` and are not
checked in. Dataset D is intentionally local: copy
`tests/benchmark-datasets/raw-manifest.template.json` beside legitimate NEF/NRW
inputs, fill in the camera, dimensions, case ID, and thumbnail path, then pass
that directory through `-RawSourceDirectory` when generating Dataset D. Verify
it separately with `-DatasetId D -VerifyOnly -RawSourceDirectory <path>`. Without
local RAW inputs D is reported as unavailable, not as a passing decode test.

Run the repeatable startup and persistent-cache scenarios with five samples:

```powershell
.\tools\RunBenchmarks.ps1 -ProjectRoot $PWD -BuildDir .\build `
    -Configuration Release -Scenario All -DatasetId A -Runs 5
```

`RunBenchmarks.ps1` calls the existing focused scripts rather than duplicating
their timing logic. Each run retains the child snapshot, a metadata envelope,
the per-run application-log delta, and runner output under
`build/bench/<git-sha>/<run-id>/`; `report.json` includes all
successful raw samples, median, nearest-rank p95 when at least five samples
exist, min/max/mean, source/build identity, environment, and dataset inventory
checksums. `docs/perf/latest.md` is the generated Markdown summary. Process
state is recorded explicitly; OS file-cache state is uncontrolled, so the
runner does not claim a cold-disk result. `-CacheState repeat-process` performs
one unmeasured launch before measured launches, while each sample still uses a
new process.

### A6 GPU scaling evidence

`RunBenchmarks.ps1` does not currently measure WIC-versus-GPU thumbnail scaling.
Its startup and persistent-cache results are not evidence for A6. The paired
scenario, hardware requirements, and go/no-go criteria are defined in
[the A6 execution prompt](../specs/FUTURE-ROADMAP-PROMPT-03-A6-GPU-THUMBNAIL-SCALING.md).
Do not enable a production GPU path until the D3/A8 baselines are reviewed and
the paired benchmark has run through completed output on a verified hardware
GPU; WARP results are correctness evidence only, not GPU performance evidence.

The current Release CI absolute guardrails remain 2500 ms to first-window
visibility, 2500 ms from visibility to first thumbnail, 5000 ms process to
first thumbnail, 10 ms average disk hit, 100 stores/second, 5 seconds for
compaction, 500 ms maximum cache-worker queue delay, and 250 synthetic scroll
messages/second. These are safety ceilings/floors, not calibrated claims about
all machines. The hidden-window scroll measure is scheduling/presentation
overhead, not physical input-device smoothness.

CI invokes the aggregate runner on both Release decode paths with five
repetitions and uploads raw JSON, aggregate reports, summaries, logs, and the
Dataset A inputs as 90-day artifacts, including on failure. Relative baseline
comparison is fail-closed once calibrated. The checked-in
`docs/perf/baseline.json` profiles are currently collecting hosted-runner
evidence; during this bootstrap, CI uses `-AllowProvisionalBaseline` and
continues enforcing the absolute limits above. Do not remove that flag until
each profile has at least ten successful independent workflow artifacts.

To collect or compare results locally:

```powershell
.\tools\RunBenchmarks.ps1 -ProjectRoot $PWD -BuildDir .\build `
    -Configuration Release -Scenario All -DatasetId A -Runs 5
```

This produces a baseline candidate but does not compare it automatically. For
a calibrated compatible profile, add `-CompareBaseline` and set the same
`-RunnerImage`, `-DecodePath`, and `-ProfileId` used during calibration. The
current hosted profiles are still collecting and local runs are not compatible
with them. `-AllowProvisionalBaseline` is reserved for the CI calibration
period; it does not disable the absolute safety limits.

The supported runner scenarios and currently unmeasured archived-plan cases are
listed in [the performance scenario matrix](perf/scenario-matrix.md). To
promote a profile in `docs/perf/baseline.json`, combine the per-workflow
candidate distributions from at least ten successful artifacts for the same
runner image, decode path, Release configuration, and dataset inventory. Choose
median-based tolerances above the measured noise floor, record the source
artifact IDs and rationale, and submit the change for review. The runner never
updates the checked-in baseline automatically. If a profile is absent,
incompatible, or exceeds its calibrated threshold, baseline comparison fails.

The checked-in `assets` folder remains a small startup fixture for unrelated
manual checks; the performance runner uses generated Dataset A. Large-folder,
RAW, viewer-interaction, physical-scroll, and GPU/resource measurements remain
manual or uninstrumented as documented in the scenario matrix.

Do not use the release packaging target as a routine performance or correctness check. It can stage package contents and optional CUDA redistributables.

## Manual validation

For a UI change, build the exact configuration that will be launched and verify the executable timestamp before testing. The repository has multiple build trees, including `build` and `build-release-package`; stale-binary testing can make a correct source fix appear ineffective.

Use an isolated registry location for manual runs:

```powershell
$env:HYPERBROWSE_SETTINGS_REGISTRY_PATH = 'Software\HyperBrowse\ManualTest'
.\build\Debug\HyperBrowse.exe
```

Check the affected workflow and its neighboring state transitions. Depending on the change, include:

- folder navigation, early loading feedback, large-folder enumeration, and folder-watch changes;
- thumbnail priority, cache hits/misses, persistent cache maintenance, and shutdown;
- browser selection, drag/drop, copy/move/delete, undo/redo, paste, and focus restoration;
- viewer open, next/previous navigation, async decode, delete, zoom, fullscreen, comparison, and keyboard focus;
- settings Apply/OK/Cancel and persistence across restart;
- multi-monitor and high-DPI behavior for geometry or rendering changes;
- keyboard-only focus, Inspect/MSAA names and states, a screen reader, and both Windows high-contrast schemes for accessibility changes;
- RAW, WIC (including WebP where the Windows codec is available), and optional nvJPEG fallback paths for decoder changes.

## Diagnostics

Use the existing logging and timing utilities for timing-sensitive issues. The debug log is written to the process temp area; CI uploads it when available. Instrument the immediate action, worker completion, UI completion, and any delayed folder-watch or cache path before drawing conclusions about a perceived delay.

When investigating a failure:

1. Reproduce with the intended build directory.
2. Confirm the binary timestamp.
3. Capture the smallest useful log or diagnostic snapshot.
4. Identify whether the delay is on the UI thread or in a delayed worker completion.
5. Add a focused regression assertion or test when the behavior can be made deterministic.

### Sanitizer and fuzz policy

CI now runs a dedicated AddressSanitizer boundary-fuzz job with the dynamic
MSVC runtime. The opt-in `HYPERBROWSE_BUILD_FUZZ_TESTS` target uses fixed seeds
and bounded mutations, so it is deterministic and does not require a separate
fuzzing engine. `HyperBrowsePersistentCacheFuzz` mutates persistent-cache headers, version and
journal records, legacy and sharded index rows, orphan files, and
traversal-shaped paths, while `HyperBrowseRawHelperProtocolFuzz` mutates RAW
helper payload sizes, headers, and bytes. The job disables optional LibRaw and
nvJPEG dependencies because these tests exercise the protocol and cache
boundaries, not codec implementations.

The sanitizer job is intentionally separate from the shipping matrix: it uses
`HYPERBROWSE_STATIC_MSVC_RUNTIME=OFF` and `/fsanitize=address`, uploads the
CTest log and any Windows dump files, and does not replace the normal Debug,
Release, startup, or package gates. New binary input boundaries should add a
bounded seed mutation to this target as well as a focused deterministic smoke
assertion when a user-visible failure mode is involved.

## Test isolation and safety

Smoke tests use a dedicated registry subkey. Manual runs should use a different subkey when testing settings or destructive file workflows. Do not point test runs at a user's important image folder when a fixture or temporary directory is sufficient.

The single-instance smoke scenarios use a per-test-process mutex and named-pipe namespace, so they do not depend on or interfere with an installed or separately running HyperBrowse instance.

Keep generated test output and build trees out of source control. The repository `.gitignore` excludes the standard build, test, and log locations.
