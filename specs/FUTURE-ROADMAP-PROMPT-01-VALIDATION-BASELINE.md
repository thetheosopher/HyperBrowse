# Long-Horizon Prompt 01: Restore a Trustworthy Validation Baseline

Use this prompt with a coding agent operating in the HyperBrowse repository.

## Mission

Restore a trustworthy Debug and Release validation baseline before starting new
roadmap feature work. Do not treat a green build as sufficient: the normal
CTest matrix must be green, optional fuzz-test configuration must be
self-consistent, and failures must be attributable to the current test rather
than to a user's running installation or stale build output.

This is roadmap recommendation 1 from the 2026-09-26 assessment.

## Current Evidence

The current repository has these known validation problems:

- `HyperBrowseSmoke` can fail while an installed
  `C:\Program Files\HyperBrowse\bin\HyperBrowse.exe` is already running. Its
  IPC scenario launches a process and searches for the matching main window.
- `HyperBrowseThumbnailStaleCompletionSmoke` fails at the persistent-cache
  assertion in `tests/smoke.cpp`. The scenario calls
  `ThumbnailScheduler::SetDiskCacheEnabled(false)`, then expects a stale
  successful decode to be persisted to disk. The recent A3 cache worker path
  uses the disk-cache state when deciding whether to enqueue stores.
- A local build configured with `HYPERBROWSE_BUILD_FUZZ_TESTS=ON` can register
  `HyperBrowsePersistentCacheFuzz` and `HyperBrowseRawHelperProtocolFuzz`
  without having built `HyperBrowseBoundaryFuzz.exe`.

Treat these as hypotheses to verify, not as permission to make superficial test
changes. Preserve the intended stale-result, cache, single-instance, and
shutdown contracts.

## Operating Rules

- Work directly in the current workspace.
- Read `.github/copilot-instructions.md`, the applicable C++ instructions,
  `docs/architecture.md`, and `docs/testing.md` before editing.
- Preserve unrelated user changes. Never reset, revert, stash, or overwrite
  them.
- Do not commit or create a branch.
- Use the repository CMake presets and CMake Tools build/test integration where
  available.
- Use `apply_patch` for edits and keep changes narrowly scoped.
- Do not kill, alter, or depend on a user's installed HyperBrowse process during
  test setup.
- Keep UI-thread, worker-lifetime, cancellation, stale-result, and persistent
  cache ownership rules intact.
- After every substantive edit, run the cheapest focused validation for that
  slice before continuing.
- Continue through all slices without pausing after the first successful fix.
- Stop only for a genuine blocker, a destructive decision, or a validation
  failure whose correct resolution requires user input.

## Source Anchors

Start from these owning surfaces:

- IPC/single-instance smoke setup: `tests/smoke.cpp`, especially the process
  launch and main-window discovery path in `RunSingleInstanceScenario`.
- Thumbnail stale completion contract: `tests/smoke.cpp`,
  `RunThumbnailStaleCompletionScenario`.
- Thumbnail scheduling and persistence ownership:
  `src/services/ThumbnailScheduler.cpp/.h`.
- Persistent cache storage and worker serialization:
  `src/cache/DiskThumbnailCache.cpp/.h`.
- Optional fuzz registration: `tests/CMakeLists.txt` and the
  `HYPERBROWSE_BUILD_FUZZ_TESTS` option in the top-level CMake configuration.
- CI validation matrix: `.github/workflows/ci.yml`.

## Autonomous Implementation Queue

### A. Establish the baseline

1. Record `git status --short --branch` and the active CMake configuration.
2. Confirm the exact Debug executable and test executable being used.
3. Run the normal Debug test matrix and record every failure by test name and
   message.
4. Reproduce the two known failures individually before editing.
5. Check for running HyperBrowse processes and determine whether the IPC test
   can be made isolated without interfering with an installed application.

Gate: the baseline is recorded, the failing paths are reproducible, and no
failure is attributed to the wrong subsystem.

### B. Make IPC smoke coverage isolated and deterministic

1. Read the application single-instance mutex, pipe, launch-path, and shutdown
   ownership code before changing the test.
2. Determine whether the test should use a unique test identity, an isolated
   settings/IPC namespace, or another existing application seam.
3. Make the smallest change that lets the test launch its own process and
   identify its own window without killing or depending on an unrelated
   installed process.
4. Preserve coverage of the real single-instance forwarding and shutdown path.
5. Add or update a focused assertion for the isolation behavior if the seam is
   deterministic.

Gate: `HyperBrowseSmoke` passes repeatedly with the normal installed
application absent, and the test does not damage or terminate a separately
running user process.

### C. Repair the stale-completion/cache contract at the root

1. Trace the scenario from `Schedule` through decode completion, memory-cache
   insertion, stale notification suppression, disk-store enqueue, persistence
   worker shutdown, and a fresh `DiskThumbnailCache::TryLoad`.
2. Decide from the existing public behavior and A3 documentation whether
   `SetDiskCacheEnabled(false)` means:
   - disable both persistent lookup and persistent store, or
   - disable persistent lookup while still allowing completion persistence.
3. Align the test setup and implementation with that documented contract.
   Do not weaken stale-result rejection: a stale completion must remain
   cacheable if that is the intended policy, but must not post a stale UI-ready
   update.
4. Preserve worker-owned disk I/O and shutdown flushing.
5. Add focused coverage for both the stale decode and stale disk-lookup retry
   paths if the existing scenario does not make the distinction explicit.

Gate: `HyperBrowseThumbnailStaleCompletionSmoke` passes repeatedly, the
persistent entry is observable after scheduler shutdown, no stale UI update is
posted, and no disk-cache method is moved onto the UI thread.

### D. Make optional fuzz configuration self-consistent

1. Inspect the CMake option, target definition, test registration, presets, and
   CI ASAN job.
2. Ensure the normal build does not register fuzz tests when the option is off.
3. Ensure the opt-in fuzz configuration builds `HyperBrowseBoundaryFuzz` before
   registering/running its two CTest cases.
4. Keep fuzz tests separate from the normal shipping matrix as documented.
5. Update `docs/testing.md` only if the actual CMake behavior or invocation
   changes.

Gate: normal Debug/Release CTest discovery has no missing fuzz executables; the
opt-in boundary configuration builds and runs both fuzz tests successfully.

### E. Revalidate the complete baseline

Run the exact newly built executables and configurations:

```powershell
cmake --build --preset debug --target HyperBrowse HyperBrowseTests
cmake --build --preset release --target HyperBrowse HyperBrowseTests
ctest --preset debug-tests --output-on-failure
ctest --preset release-tests --output-on-failure
```

Then configure a separate opt-in fuzz build, build
`HyperBrowseBoundaryFuzz`, and run only the two fuzz CTest cases. Also run:

```powershell
git diff --check
```

Verify that the Debug and Release executables are newer than the source files
changed during this task and that the test output names the exact build tree.

## Completion Criteria

This prompt is complete only when all of the following are true:

- Normal Debug and Release CTest matrices pass without missing executables.
- The IPC smoke is isolated from unrelated installed/running processes.
- The stale-completion smoke passes and documents the intended cache semantics.
- The opt-in fuzz configuration builds and runs both boundary tests.
- No UI-thread persistent-cache I/O, stale-result notification, or shutdown
  ordering regression is introduced.
- Any changed documentation matches the actual CMake and runtime behavior.
- The final report lists the root cause, files changed, validation commands, and
  any remaining environment-specific limitations.

Do not begin B2 compare, B5 saved searches, A6 GPU thumbnail scaling, or other
new roadmap feature work until this completion gate is green or a clearly
recorded blocker has been approved.
