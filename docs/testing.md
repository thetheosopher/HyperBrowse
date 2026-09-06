# HyperBrowse Testing Guide

## Test layers

### Build validation

The normal development configuration is the `vs2026-x64` CMake preset. It uses the Visual Studio 2026 generator, x64 architecture, and builds tests by default.

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug --target HyperBrowse
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
- `HyperBrowseViewerFitSmoke`
- `HyperBrowseAppTextSizeSmoke`
- `HyperBrowseSettingsSmoke`

The tests cover model/service behavior and selected application/viewer state without requiring every workflow to be driven through a live desktop session. Add focused coverage to `tests/smoke.cpp` when a change can be exercised deterministically there.

### Startup and performance checks

CI runs `tools/TestStartupBenchmark.ps1` against the Release build with budgets for first-window visibility and first-thumbnail presentation. Performance changes should use the repository benchmark tools and report before/after measurements rather than relying on subjective timing.

The CI startup dataset is the checked-in `assets` directory. It is intentionally
small and deterministic for a gate, while large-folder, removable-media, and
network-path measurements remain release investigations rather than hard CI
thresholds. Hosted Windows runners have variable CPU, storage, and desktop
startup latency, so a benchmark result is interpreted as a regression only when
the configured budget is exceeded; repeated-run distributions should be
captured before tightening those budgets. JSON snapshots and the debug log are
uploaded for every matrix leg, including failed runs.

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
- RAW, WIC, and optional nvJPEG fallback paths for decoder changes.

## Diagnostics

Use the existing logging and timing utilities for timing-sensitive issues. The debug log is written to the process temp area; CI uploads it when available. Instrument the immediate action, worker completion, UI completion, and any delayed folder-watch or cache path before drawing conclusions about a perceived delay.

When investigating a failure:

1. Reproduce with the intended build directory.
2. Confirm the binary timestamp.
3. Capture the smallest useful log or diagnostic snapshot.
4. Identify whether the delay is on the UI thread or in a delayed worker completion.
5. Add a focused regression assertion or test when the behavior can be made deterministic.

### Sanitizer and fuzz policy

The repository does not currently enable a sanitizer job. The production CMake
configuration uses the static MSVC runtime, while an AddressSanitizer build
requires a separately configured MSVC runtime/toolchain and has additional
linker and runtime constraints; enabling it blindly in the normal Visual
Studio preset would test a different configuration and can conflict with the
packaging assumptions. Visual Studio's native fuzzing support is likewise not
part of the current CMake/CTest toolchain, and no libFuzzer integration is
checked in.

The supported substitute is deterministic malformed-input coverage in the
smoke suite: cache corruption, folder-watch payload parsing, RAW helper
protocol validation, and decoder failure paths. New parsers or binary input
boundaries must add bounded malformed fixtures there. A future sanitizer/fuzz
job should use a dedicated dynamic-runtime preset, isolate generated corpus
artifacts, and upload crashes and minimized reproducers without changing the
shipping build or the normal CTest matrix.

## Test isolation and safety

Smoke tests use a dedicated registry subkey. Manual runs should use a different subkey when testing settings or destructive file workflows. Do not point test runs at a user's important image folder when a fixture or temporary directory is sufficient.

Keep generated test output and build trees out of source control. The repository `.gitignore` excludes the standard build, test, and log locations.
