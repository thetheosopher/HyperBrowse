# Contributing to HyperBrowse

## Before changing code

1. Identify the owning component and read its header, implementation, and nearest tests.
2. Check `docs/architecture.md` for thread, lifetime, cache, and rendering boundaries.
3. Check `specs/` for product intent, but treat current source and tests as authoritative when a planning document is stale.
4. State the observable behavior being changed and the cheapest test that can falsify the proposed fix.

## Local workflow

Configure the normal development tree:

```powershell
cmake --preset vs2026-x64
```

Build a focused target:

```powershell
cmake --build --preset debug --target HyperBrowse
```

Run the smoke suite:

```powershell
ctest --preset debug-tests
```

Use the Release equivalents before a release-oriented change:

```powershell
cmake --build --preset release --target HyperBrowse
ctest --preset release-tests
```

For isolated manual runs, set `HYPERBROWSE_SETTINGS_REGISTRY_PATH` to a subkey relative to `HKCU`, such as `Software\HyperBrowse\ManualTest`. Do not use the packaging preset for routine development validation; it can stage dependencies and produce release artifacts.

## Implementation expectations

- Keep the UI thread free of blocking decode, enumeration, metadata, persistent-cache, and shell work.
- Preserve cancellation and object lifetime guarantees for asynchronous work.
- Use existing message IDs, path helpers, logging, diagnostics, and cache abstractions.
- Preserve visible viewer content while replacement images decode asynchronously.
- Invalidate index-keyed render caches when item removal can reuse an index.
- Distinguish operation origin from operation type when browser and viewer workflows share a service.
- Keep user-facing shortcuts, menu commands, README documentation, and the HTML user guide consistent.
- Avoid unrelated refactors, generated files, build-directory changes, and formatting churn.

## Tests and manual checks

A change should include focused smoke coverage when practical. For UI behavior, also manually check the affected workflow with the exact executable produced by the build just run. Confirm its `LastWriteTime` before diagnosing runtime behavior.

At minimum, check the relevant combination of:

- folder load, folder-watch update, and large-folder behavior;
- browser selection, drag/drop, copy/move/delete, undo/redo, and focus restoration;
- viewer navigation, async image replacement, delete, zoom, fullscreen, and keyboard focus;
- cache invalidation and shutdown;
- high-DPI or multi-monitor behavior when window geometry or rendering changes;
- Debug and Release builds when build configuration or resource ownership changes.

## Pull requests

Keep pull requests focused and describe the user-visible result, root cause, and validation performed. Include screenshots or a short reproduction/verification sequence for visual or interaction changes.

Before requesting review:

- [ ] The change has a focused scope and no unrelated generated artifacts.
- [ ] The affected target builds successfully.
- [ ] The relevant CTest preset passes.
- [ ] Manual UI verification used the newly built executable when applicable.
- [ ] New behavior has smoke coverage or the PR explains why it cannot be automated.
- [ ] README, user guide, specs, or architecture docs were updated when needed.
- [ ] Packaging was tested when installer or portable output changed.
