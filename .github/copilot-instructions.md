# HyperBrowse Contribution Guidance

## Project

HyperBrowse is a native Windows image browser and viewer implemented in C++20 with CMake. The application uses a Win32 shell, Direct2D/DirectWrite for the browser and viewer rendering paths, WIC for common image formats, optional LibRaw for RAW formats, and optional nvJPEG acceleration.

Read the nearest owning implementation and its tests before changing behavior. Prefer existing helpers, message IDs, service abstractions, cache APIs, and RAII patterns over introducing parallel mechanisms.

## Build and validation

- Use the repository CMake presets. Configure with `cmake --preset vs2026-x64`.
- Build focused targets with `cmake --build --preset debug --target HyperBrowse` or `cmake --build --preset release --target HyperBrowse`.
- Run focused tests with `ctest --preset debug-tests` or `ctest --preset release-tests`.
- Use `HYPERBROWSE_BUILD_TESTS=ON` for normal development. Keep CUDA bundling disabled unless packaging is the task.
- The release packaging target has side effects and may download or stage dependencies; do not use it as a routine validation command.
- After UI or executable changes, verify that the launched executable is newer than the edited source and that the exact binary being run matches the build directory just built.
- Add or update focused smoke coverage for behavior changes when the behavior can be exercised without a real interactive desktop.

## Threading and lifetime

- Keep the UI thread responsive. Do not perform decode, enumeration, metadata extraction, persistent cache I/O, file-operation waits, or other potentially blocking work in window procedures or UI callbacks.
- Worker threads communicate results back through the established message or callback paths. Confirm cancellation, stale-result rejection, shutdown ordering, and HWND lifetime before adding a callback.
- Do not call `DiskThumbnailCache` from the UI thread. Persistent index and cache-file work belongs behind the scheduler's worker path.
- Keep destruction safe: stop or cancel producers before destroying the objects that receive their results, and make worker loops exit through the existing shutdown mechanism.
- Capture window activation state at the beginning of shell operations when completion can occur after a shell dialog changes foreground focus.

## UI, rendering, and state

- Preserve visible content during asynchronous image transitions unless a blank/loading state is explicitly required. Do not eagerly clear a valid viewer image before its replacement is ready.
- Invalidate render caches by content identity, not only by a numeric item index. Removing an item can reuse the same index for a different file.
- Treat folder-watch events and file-operation completion as separate sources of truth. Do not infer whether an operation came from the viewer or browser from an operation type alone.
- Preserve keyboard focus, foreground activation, selection, and post-operation focus behavior when changing file workflows.
- Keep user-visible shortcuts and menu labels synchronized with the README and user guide.
- Respect the existing Direct2D/DirectWrite and GDI ownership split unless the task explicitly changes the rendering architecture.

## Change discipline

- Make the smallest change that fixes the root cause. Avoid unrelated formatting, generated build output, or broad refactors.
- Keep public APIs and message contracts stable unless the task requires a contract change.
- Use ASCII for new text unless the existing file clearly requires another encoding.
- Add comments only for non-obvious invariants or control flow; do not narrate straightforward code.
- Update documentation and tests when behavior, build requirements, shortcuts, or package output changes.
- Never commit changes, reset the worktree, or revert unrelated user changes unless explicitly asked.
