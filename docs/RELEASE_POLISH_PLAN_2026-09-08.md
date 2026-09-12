# HyperBrowse 2.2 release polish plan

Audit date: **2026-09-08**. Candidate: **2.2.0**, based on commit **9ea1245** plus the existing working-tree changes to `CMakeLists.txt`, `README.md`, and `src/ui/MainWindow.{cpp,h}`.

**Implementation status (2026-09-08): all P1 findings are fixed and locally verified; hold publication until the remaining manual and hosted release gates below are signed off.** Debug, Release, WIC-only, sanitizer, CUDA-bundled packaging, startup, and hardware nvJPEG checks now pass. The remaining hold is for screen-reader/high-contrast/DPI walkthroughs, slow-media soak, installer scope/upgrade checks, hosted CI, signing, and final human release sign-off.

This document began as the assessment and execution plan and now serves as the persistent implementation ledger. The pre-existing version bump and Quick Actions toast changes were preserved. Implementation is in the uncommitted working tree based on `9ea1245`; no commits were created during this pass. Statuses and remaining boundaries are recorded below.

## Scope and evidence

Reviewed the browser/model and selection paths; viewer navigation and settings; metadata persistence; copy/move/rename/undo; batch conversion and cancellation; background execution, cache and decode boundaries; custom accessibility and rendering; shortcuts and offline help; CMake, tests, CI, and installer/portable packaging. Existing architecture and polish documents were used as context and checked against current source rather than copied as current findings.

The visual review used the checked-in [main-window screenshot](MainWindow.PNG) and current layout/painting code. **No interactive native desktop walkthrough, screen-reader session, mixed-DPI session, installer run, or hosted CI run was performed.** Existing smoke tests exercise selected native windows; they do not replace those manual gates.

Evidence labels:

- **Reproduced:** observed in this audit's test run or isolated production-code probe.
- **Source-confirmed:** a concrete code path supports the finding; the complete desktop reproduction remains to be run.
- **Verification gap:** missing or disabled coverage; not a claim that the feature necessarily fails.
- **Polish:** an observable consistency or usability issue requiring a focused design/code change.

P1 means resolve before release because of data integrity, lifetime safety, or an unresolved release gate. P2 means address during release polish, or explicitly record a narrow deferral and its user impact. P3 work is listed separately and should not expand this release into a redesign. Effort estimates are rough engineering sizes: S = less than a day, M = 1-3 days, L = several days including regression coverage; they are not commitments.

## Validation ledger

| Check | Result on this candidate | Evidence / boundary |
| --- | --- | --- |
| Debug warnings-as-errors build and CTest | **Passed** | Fresh `build-polish`, MSVC 19.51 / VS 2026, CMake 4.2.3; 16/16 enabled tests in 50.33 seconds. |
| Release warnings-as-errors build and CTest | **Passed** | Fresh `build-polish`; 16/16 enabled tests in 47.78 seconds. No tests are disabled. |
| WIC-only Release build, tests, and package | **Passed** | Fresh `build-polish-wic`, LibRaw/nvJPEG/CUDA redist OFF; final-source rerun passed 14/14 in 47.72 seconds. Capability validation accepted the intentionally absent helper/DLLs/notices. |
| Metadata persistence stress | **Passed** | `HyperBrowseUserMetadataSmoke` passed five consecutive Debug runs in 15.12 seconds after bounded retry of transient Windows publish sharing errors. |
| Multi-viewer Settings stress | **Passed** | `HyperBrowseMultiViewerSettingsSmoke` is enabled and passed five consecutive Debug runs; control creation is synchronized before interaction. |
| AddressSanitizer boundary tests | **Passed** | Fresh `build-polish-asan`; final-source rerun of persistent-cache and RAW-helper protocol fuzz tests passed 2/2 in 11.02 seconds. |
| CUDA-bundled shipping target | **Passed** | Fresh `build-polish-shipping`; static runtime, LibRaw/nvJPEG/CUDA redist ON, warnings-as-errors ON; release target ran 14/14 tests in 48.33 seconds, staged both layouts, verified versions/contents/notices/hashes, and compiled Inno Setup. |
| Startup budgets, exact staged shipping executable | **Passed** | Default isolated key: 525.91 ms process-to-window, 241.37 ms window-to-thumbnail, 767.28 ms total. Custom path-with-spaces run: 497.04 / 253.51 / 750.55 ms. Both are below 2,500 / 2,500 / 5,000 ms. |
| Benchmark failure/isolation behavior | **Passed** | Pre-existing registry value and caller environment restored; output path with spaces passed; fixture exit 7 and forced-timeout cases both failed the script as required. |
| WIC staged runtime fallback | **Passed** | WIC-only portable candidate launched without helper/CUDA files and recorded `thumbnail.decode.jpeg.cpu` (6.01 ms); startup was 546.31 / 262.61 / 808.91 ms. |
| Actual nvJPEG hardware decode | **Passed** | CUDA-bundled `HyperBrowseTests.exe --nvjpeg-hardware` decoded the JPEG fixture on an NVIDIA RTX A1000 6GB Laptop GPU, driver 596.58. |
| Shipping artifact hashes | **Recorded** | ZIP `2231727ea4e9c0785ecf1c100b8c858f0333e9757e86821b6e78e572247b4564`; installer `542e97fe53e5d92e9fbf87f003ac29c05d015d4723b323ab34b75f30a0c88474`. Layout and artifact `SHA256SUMS.txt` files are generated automatically. |
| Native visual/accessibility/installer/hosted matrix | **Pending release gate** | Exact candidate screenshot refreshed and inspected in light mode. Screen reader, both high-contrast schemes, mixed DPI/largest text, slow/network/removable media, install/upgrade/uninstall under both scopes, and hosted CI remain human/external checks. |

The initial audit evidence below is retained as historical reproduction context. The implementation evidence above supersedes its failed/open results. Builds needed access to the installed Windows SDK outside the filesystem sandbox; native tests needed their isolated HKCU settings and single-instance pipe.

**Original audit evidence (superseded by the clean implementation runs above):** the earlier shared Release tree emitted MSBuild **MSB8028** warnings and had warnings-as-errors disabled. That tree is not used as implementation evidence.

The first validation runs wrote local, ignored evidence under `build/polish-audit/`: `release-ctest.log`, `release-ctest-unrestricted.log`, `release-settings-repeat.log`, `MetadataProbe.cpp`, and `metadata-results.txt`. **That directory was subsequently regenerated and those earlier files disappeared**, along with the Release executable and both test executables. Their relevant observed results are preserved in this document. The later `debug-ctest.log` and `release-settings-idle.log` record missing binaries, not executed test failures. The audit did not request a build-tree deletion. Use a separate stable validation tree for follow-up; do not depend on these scratch files surviving another build. Promote the probes into normal tests when implementing the fixes.

The metadata probe compiled the **production** `UserMetadataStore.cpp`, substituting only its known-folder lookup with a directory under `build/polish-audit/fixtures`. It did not read or write the user's metadata store. Each restart case destroyed the store to join its save worker, then created a new store.

```text
ascii_rating_after_restart=4             expected 4
unicode_rating_after_restart=0           expected 5
unicode_tags_length_after_restart=0      expected 2
case_rename_rating=0                     expected 4
folder_rename_rating=0                   expected 4
```

## Implementation ledger

Implementation/review date: **2026-09-08**. Implementation reviewer: **Codex**. Human release reviewer: **pending**. Unless a row says otherwise, the implementation is complete in the uncommitted working tree based on `9ea1245`, and its focused coverage is included in the 14-test Debug/Release results above.

| ID | Status | Implementation and verification | Accepted limit / owner / target |
| --- | --- | --- | --- |
| POL-01 | **Complete** | Added an injectable stable physical-memory snapshot for Settings policy tests; the application-owned Experimental Settings Apply/OK/Cancel paths pass in Debug and Release. | Live-memory policy remains covered by integration behavior; no release exception. |
| POL-02 | **Complete** | Replaced locale-dependent streams with versioned UTF-8 TSV, legacy ACP fallback, durable atomic publication, visible error state, retained dirty mutations, and an injectable data root. Unicode, escapes, failure/retry, last-good-file, and restart coverage passes. | No release exception. |
| POL-03 | **Complete** | Equal normalized keys are handled safely and remaps no longer retain invalidatable iterators. Case-only and map-growth rename coverage passes. | No release exception. |
| POL-04 | **Complete** | Folder copy/move/rename/delete now remap descendants with path-boundary checks; nested, sibling, and Unicode cases pass. | External filesystem renames remain outside the app-operation contract; documented behavior, no release exception. |
| POL-05 | **Complete** | Worker startup/shutdown ordering is explicit, exceptions are contained, and transient atomic-publish access/sharing errors retry for a bounded two seconds. Repeated lifecycle stress passed 5/5. | Permanent save errors still surface and retain dirty state by design. |
| POL-06 | **Complete** | Rename dialog inputs are snapshotted before the modal loop and the source is revalidated afterward. Rename regressions pass. | No release exception. |
| POL-07 | **Complete** | Journal entries retain actual mappings and identity/change state; undo/redo revalidate targets, update paths across cycles, and drop unsafe history. Overwrite results without a backup are not journaled. | No release exception. |
| POL-08 | **Complete** | Batch result accounting uses explicit success/failure/completed invariants and cannot underflow or double-count. Focused cancellation/failure tests pass. | No release exception. |
| POL-09 | **Complete** | Cancelling remains an active state until worker completion; close drains the service through explicit shutdown. Cancel/close tests pass. | No release exception. |
| POL-10 | **Complete** | Conversion writes an owned temporary file in the destination directory and publishes without overwrite only after encoder commit. Collision, 1,000-name exhaustion, cancel, failure cleanup, and success tests pass. | Export metadata/color/animation/alpha limits are documented in the offline guide. |
| POL-11 | **Partial** | Added bounded `ImageCommandService` execution for uncached info, copy pixels, and JPEG orientation; completion returns to the owning UI and close drains safely. Async info/copy/orientation tests pass. | Manual slow RAW/network, many-JPEG, origin-close, and selection-switch responsiveness remains. Owner: release maintainer. Target: candidate manual pass before sign-off. |
| POL-12 | **Complete** | Settings applies a new interval to every active slideshow without starting stopped slideshows. Enabled multi-viewer Apply/Cancel/full-screen coverage passed 5/5 Debug stress runs. | No release exception. |
| POL-13 | **Complete** | An interprocess lock plus per-key pending-mutation merge prevents stale whole-file replacement. Two-process independent edits and exit-order coverage passes with isolated data roots. | No release exception. |
| POL-14 | **Partial** | Default Settings exposes semantic MSAA tabs and controls with roles, names, values, states, shortcuts, focus, and actions. Automated Settings accessibility assertions pass. | Inspect plus NVDA/JAWS and nonvisual browser/viewer file-action confirmation remain. Owner: release maintainer. Target: accessibility manual pass before sign-off. |
| POL-15 | **Partial** | Main, browser, viewer, diagnostics, and default Settings use Windows system colors in high contrast and refresh on setting/theme/color changes. | Both Windows high-contrast schemes, disabled/selected/focus/link contrast, and largest text require manual measurement. Owner: release maintainer. Target: accessibility manual pass before sign-off. |
| POL-16 | **Partial** | Main/viewer confirmations use DPI/text-scaled bounded two-line layouts, matching DirectWrite/GDI trimming and clipping, and an accessible full status alert. Visible terminology is Quick Actions. | 100/150/200% DPI, largest text, narrow resize, rapid operations, UNC/Unicode, and partial-pair presentation remain manual. Owner: release maintainer. Target: UI matrix before sign-off. |
| POL-17 | **Complete** | README/offline guide now agree on Escape, structured filters, Quick Actions, conversion limits, CMake requirements, test inventory, 2.2 changes/limits, and installer scope. `MainWindow.PNG` was recaptured from the exact 2.2 Release UI. | Historical documents retain historical Quick Send references intentionally. |
| POL-18 | **Complete** | Packaging requirements derive from generated capabilities; versions, staged layouts, ZIP contents, optional presence/absence, layout hashes, and final artifact hashes are verified. CUDA-bundled and WIC-only packages pass; staged WIC fallback and an actual RTX A1000 nvJPEG decode are recorded. | Hosted rerun remains a separate final gate. |
| POL-19 | **Complete** | Both layouts include the project license, NanoSVG license, LibRaw COPYRIGHT/CDDL/LGPL files when enabled, and a reviewed version/revision/linkage inventory. Package manifests require them. | Review owner/date are recorded in `THIRD-PARTY-NOTICES.txt`; repeat review when dependencies change. |
| POL-20 | **Partial** | Inno `HKA` registration follows selected install scope; uninstall deletes only installer-owned registration and retains per-user preferences. Installer compiles successfully. | Per-user/all-users with different admin, second user, upgrade, portable coexistence, and uninstall/reinstall remain manual. Owner: release maintainer. Target: clean-machine distribution pass before sign-off. |
| POL-21 | **Complete** | Benchmark default is an isolated subkey, the matching child environment is set/restored, Windows arguments are quoted, pre-existing values survive, and nonzero/forced exits fail. Default/custom keys and space-containing paths pass. | No release exception. |
| POL-22 | **Complete** | Removed the disabled gate and synchronized dialog/control readiness. Multi-viewer Settings is one of 16 enabled cases and passed five consecutive Debug runs plus full Debug and Release suites. | No release exception. |

The remaining partial P2 items affect responsiveness proof and manual accessibility/installer confidence rather than known data loss. They are not approved for silent deferral: the named release maintainer must record the manual result or an explicit release exception in this document before publication.

## Prioritized work queue

| ID | Priority | Finding | Evidence | Area / effort |
| --- | --- | --- | --- | --- |
| POL-01 | P1 | Settings smoke fails on automatic cache values | Reproduced; cause unconfirmed | Settings/test harness / M |
| POL-02 | P1 | Unicode metadata does not persist; save failures are silent | Reproduced | Metadata storage / M |
| POL-03 | P1 | Metadata remapping loses case-only renames and retains an invalidatable iterator | Reproduced + source-confirmed | Metadata storage / S |
| POL-04 | P1 | Folder operations do not remap descendant ratings/tags | Reproduced | Metadata/file workflows / M |
| POL-05 | P1 | Metadata worker starts before its control fields are initialized | Source-confirmed | Lifetime safety / S |
| POL-06 | P1 | File rename retains model references across a modal message loop | Source-confirmed | Rename/browser lifetime / M |
| POL-07 | P1 | Undo/redo does not preserve complete file identity and path transitions | Source-confirmed | File journal / M-L |
| POL-08 | P2 | Batch failures can double-count and underflow the success total | Source-confirmed | Batch conversion / S |
| POL-09 | P2 | Cancel marks batch conversion idle before the worker finishes | Source-confirmed | Batch/shutdown / M |
| POL-10 | P2 | Batch encoder writes directly into the final output path | Source-confirmed | Export integrity / M |
| POL-11 | P2 | Several image commands still perform blocking work on the UI thread | Source-confirmed | Command responsiveness / M |
| POL-12 | P2 | Changing slideshow duration updates only the primary viewer | Source-confirmed | Multi-viewer settings / S |
| POL-13 | P2 | Independent instances can overwrite each other's metadata edits | Source-confirmed | Metadata consistency / M |
| POL-14 | P2 | Custom Settings/browser/viewer semantics remain incomplete for assistive technology | Verification gap + source | Accessibility / M-L |
| POL-15 | P2 | Custom themes do not follow Windows high-contrast state | Source-confirmed | Accessibility/rendering / M |
| POL-16 | P2 | New confirmation toast needs bounded text and DPI/text-size treatment | Source + visual verification gap | Quick Actions / S-M |
| POL-17 | P2 | Offline help, terminology, screenshot, and test inventory drift | Source-confirmed | Documentation / S |
| POL-18 | P2 | Package checks do not prove required optional payloads are present | Source-confirmed | Release packaging / M |
| POL-19 | P2 | Distribution omits project and vendored component notices | Source-confirmed | Release packaging / S |
| POL-20 | P2 | Installer cleanup removes shared settings; all-users association scope is unclear | Source-confirmed + verification gap | Installer / M |
| POL-21 | P2 | Startup benchmark registry isolation is not connected to the child process | Source-confirmed | Release tooling / S |
| POL-22 | P1 gate | Multi-viewer Settings coverage is explicitly disabled | Verification gap | Release validation / M |

## Findings and acceptance criteria

### POL-01 — Resolve the repeatable Settings test failure

**Evidence:** [tests/smoke.cpp](../tests/smoke.cpp#L3898) compares the automatic cache edit text with freshly resolved capacities. The unrestricted full Release suite and focused repeat both reported `Experimental Settings did not populate automatic cache values from the selected profile`. The population path is [UpdateExperimentalSettingsCacheValues](../src/ui/MainWindow.cpp#L5692).

**Impact:** the default Settings surface currently has no passing release evidence for Follow profile cache values and later assertions in that scenario are not reached. Do not yet classify this as a proven UI defect: the expected values are recomputed from live available memory, so test determinism is also suspect. These first two runs overlapped a Debug compilation. A third, intended idle repeat could not run because the Release test executable had disappeared.

**Action / done:** capture actual versus expected values, selected profile, automatic flags, and the memory snapshot at each calculation. Repeat once without build contention. Share/inject a stable sizing snapshot in deterministic tests where appropriate, while retaining an integration assertion that the displayed values match the applied policy. The application-owned Experimental Settings route must pass Apply, OK, Cancel, profile switching, and persistence checks. Do not disable or weaken the test to make the release green.

### POL-02 — Persist all Unicode metadata and report failed saves

**Evidence:** [SaveEntries](../src/services/UserMetadataStore.cpp#L184) uses `std::wofstream` without an explicit Unicode encoding; [LoadLocked](../src/services/UserMetadataStore.cpp#L363) similarly uses `std::wifstream`. There is no application-wide locale setup correcting this. The probe's ASCII rating survived restart, while `C:\fixtures\日本.jpg` and a Japanese tag did not. Stream/open/replace failures return silently; [SaveWorkerLoop](../src/services/UserMetadataStore.cpp#L413) marks the generation completed regardless.

**Impact:** ratings/tags appear successful in memory and disappear on restart. A non-encodable entry can prevent the entire snapshot from being saved, including unrelated ASCII edits. Filesystem write failures have the same invisible failure contract.

**Action / done:** use an explicit versioned encoding such as UTF-8, migrate existing readable data, return a structured persistence result, retain dirty state on failure, and surface a recoverable save warning. Add an injectable storage directory for tests. Cover ASCII, accented text, CJK, surrogate pairs, escaped tabs/newlines, mixed entries, read-only destinations, failed atomic replacement, and restart. A failed save must preserve the last good file and must not be acknowledged as durable.

### POL-03 — Make metadata remapping safe for equal keys and rehash

**Evidence:** [ApplyFileOperationUpdate](../src/services/UserMetadataStore.cpp#L327) finds `source`, inserts with `entries_[createdKey]`, then calls `entries_.erase(source)`. A case-only rename normalizes both names to one key and erases that same entry; the probe returned rating 0 instead of 4. Insertion can also rehash the unordered map and invalidate the saved iterator before `erase`.

**Action / done:** explicitly handle equal normalized keys; copy/extract the source value and use a safe key-based erase or equivalent mutation order. Cover case-only file/folder renames, a new destination at a forced rehash boundary, existing destination metadata, copy versus move, and repeated undo/redo. Run iterator-debug validation for the rehash case. The case-only loss is reproduced; a rehash-triggered crash was not reproduced in this audit.

### POL-04 — Preserve descendant metadata during folder operations

**Evidence:** [ApplyFileOperationUpdate](../src/services/UserMetadataStore.cpp#L309) updates exact path keys only. [MainWindow's completion path](../src/ui/MainWindow.cpp#L19312) forwards the shell's source/destination mapping. A successful folder rename reports the folder mapping, while ratings are keyed by image paths below it. The probe rated `C:\old\photo.jpg`, applied `C:\old` to `C:\new`, and found no rating at the new image path.

**Action / done:** make directory operations explicit and apply boundary-aware prefix remapping to descendant entries for rename/move/copy; handle deletion consistently. Do not mistake `C:\old2` for a child of `C:\old`. Test nested folders, unrelated siblings, Unicode paths, folder overwrite conflicts, partial shell results, and inverse operations. The acceptance scope is folder operations initiated through HyperBrowse; external rename tracking should be documented separately.

### POL-05 — Initialize metadata worker state before starting the thread

**Evidence:** the [constructor](../src/services/UserMetadataStore.cpp#L236) starts `saveWorker_` in its initializer. In the [member declaration order](../src/services/UserMetadataStore.h#L49), the worker precedes `requestedSaveGeneration_`, `completedSaveGeneration_`, and `saveShuttingDown_`. Its [wait predicate](../src/services/UserMetadataStore.cpp#L420) can read these members before their initialization finishes.

**Impact:** an early-running worker has an initialization race, including the possibility of exiting or processing a save before its control state exists. A runtime failure was not forced in this audit.

**Action / done:** start the worker in the constructor body after all members are initialized, or place all worker-observed state before the thread. Keep destructor join ordering explicit. Cover repeated create/save/destroy cycles and add exception containment around snapshot creation/save so a background exception cannot terminate the application.

### POL-06 — Snapshot rename inputs before opening the dialog

**Evidence:** [StartRenameSelectedImage](../src/ui/MainWindow.cpp#L17417) holds a `BrowserItem&`, passes `item.fileName` by reference to the dialog, and uses `item.filePath` after the dialog returns. The [dialog pumps all thread messages](../src/ui/MainWindowDialogs.cpp#L1260), and [BrowserModel::AppendItems](../src/browser/BrowserModel.cpp#L87) can reallocate its vector. Folder-watch reload/removal can also invalidate those references while the owner is disabled for input.

**Reproduction to run:** open F2 while a large folder is still enumerating, or externally add/remove files while F2 is open, then confirm or cancel. Disabling a window does not stop its asynchronous messages.

**Action / done:** copy the item/path/name before entering the modal loop, revalidate the source before dispatch, and audit neighboring prompts for borrowed model references. Cover enumeration growth, watch removal/rename, navigation via forwarded launch requests, Cancel, and an unchanged filename. Use a deterministic message-driven regression rather than a timing-only test.

### POL-07 — Preserve undo identity, original names, and new result mappings

**Evidence:** [journal entries](../src/ui/FileOperationJournal.h#L12) contain paths but no file identity/version or overwrite-recovery information. [Undo move](../src/ui/MainWindow.cpp#L18757) supplies no original target leaf names. A conflict-renamed move from `A\photo.jpg` to `B\photo.1.jpg` therefore returns as `A\photo.1.jpg`, while Redo still expects `A\photo.jpg`. [Journal completion](../src/ui/FileOperationJournal.cpp) transfers the original entry unchanged, so conflict renames during inverse operations can also leave stale paths. Undo-copy blindly recycles the recorded destination path even if another program replaced its contents after the copy.

**Action / done:** record and verify suitable file identity/change information before inverse operations; preserve original leaf names; update the journal with actual successful inverse mappings. Make overwrite behavior explicit: either retain enough information to restore the previous destination or decline to advertise that operation as fully undoable. Cover copy/move/rename collisions, overwrite, externally replaced destinations, partial undo, undo-conflict rename, and two undo/redo cycles. Never delete or move a newly substituted file solely because its pathname matches an old journal record.

### POL-08 — Keep batch totals valid on every failure path

**Evidence:** [filename exhaustion](../src/services/BatchConvertService.cpp#L435) increments `failedCount` and still calls `EncodeImage` with an empty path, incrementing failure again. Exception handlers report `completedCount = 0` with `failedCount = items.size()`. [Completion UI](../src/ui/MainWindow.cpp#L21293) subtracts unsigned `failedCount` from `completedCount`, producing an enormous success count when failures exceed completed work.

**Action / done:** skip encoding when there is no output path; maintain separate attempted/succeeded/failed counts with explicit invariants; make the UI safe for any service result. Cover all 1,000 candidate names occupied, unavailable output directory, decoder failure, encoder failure, thrown work, cancellation after some work, and queue rejection. The displayed converted count must stay between zero and the request total, with each input classified once.

### POL-09 — Keep a cancelling batch active until completion is acknowledged

**Evidence:** the [Cancel Batch Convert handler](../src/ui/MainWindow.cpp#L9744) calls `Cancel()` and immediately clears `batchConvertActive_`. The [close gate](../src/ui/MainWindow.cpp#L23825) relies on that flag, but decode/encode may still be running. `BatchConvertService` suppresses completion posts only when its destructor sets shutdown; `WM_DESTROY` merely calls `Cancel()`.

**Impact:** cancelling enables another operation prematurely and allows immediate close to bypass the responsive wait state. Destruction can subsequently block while joining the still-running worker, after the window has disappeared.

**Action / done:** distinguish Running, Cancelling, and Finished; keep progress/cancel feedback visible until the matching final update, and provide a pre-destruction shutdown/post-suppression boundary. Validate Cancel then immediate Close during a slow decode and encode, repeated Cancel, attempted restart, and completion racing close. Retain the owner and message payload lifetime until work has drained.

### POL-10 — Commit converted outputs atomically

**Evidence:** [MakeUniqueOutputPath](../src/services/BatchConvertService.cpp#L278) checks for existence, then [EncodeImage](../src/services/BatchConvertService.cpp#L214) opens the final path for writing. Error returns do not remove an incomplete output. The existence check and subsequent open also leave a collision window with other processes.

**Action / done:** encode to an exclusively created temporary file in the destination directory, commit the encoder successfully, then publish without overwriting an unexpected new destination. Remove owned temporary files on failure/cancel. Test encode failure after file creation, cancellation, full/unavailable output storage, and a destination created between name selection and publish. Preserve originals and unrelated files; explain export metadata/alpha behavior in help rather than implying a lossless archival copy.

### POL-11 — Remove remaining synchronous image work from commands

**Evidence:** [Image Information](../src/ui/MainWindow.cpp#L17335) extracts metadata synchronously on a cache miss; [Copy Image](../src/ui/MainWindow.cpp#L18250) decodes the full image synchronously; [JPEG orientation adjustment](../src/ui/MainWindow.cpp#L18621) processes the selected files in a UI-thread loop. Metadata extraction can invoke shell/property handlers, and RAW decode can wait on a helper.

**Action / done:** snapshot inputs, use the existing bounded executors/services, provide progress/cancel feedback, and complete clipboard/dialog updates on the owning UI thread. Cover a large RAW, uncached network image, many JPEGs, closing the originating viewer, and switching selection during work. The window must continue painting and accepting cancellation. Measure these command latencies separately from startup.

### POL-12 — Apply slideshow timing changes to every relevant viewer

**Evidence:** Experimental Settings gathers `OpenViewerWindows()` for overlays, but [duration application](../src/ui/MainWindow.cpp#L20656) restarts only `viewerWindow_`. A secondary viewer running a slideshow retains its prior interval.

**Action / done:** apply the new interval to each active slideshow using the gathered viewer collection, without starting stopped slideshows or changing the displayed image unnecessarily. Test two independent viewers, one windowed and one full-screen, Apply/Cancel, and close one viewer while Settings is open. Include interval assertions in the multi-viewer regression gate.

### POL-13 — Define metadata consistency across separate application instances

**Evidence:** single-instance behavior is user-configurable in Settings. Each `UserMetadataStore` [loads once](../src/services/UserMetadataStore.cpp#L352) and [replaces the entire same metadata file](../src/services/UserMetadataStore.cpp#L224); there is no interprocess merge or lock. Atomic replacement prevents a torn file but does not prevent a stale instance from overwriting another instance's edits. Registry isolation also does not isolate this metadata file.

**Action / done:** serialize and merge persisted mutations across processes, or establish a single metadata owner with an explicit supported multi-instance contract. Test two independent processes that load the same initial data, edit different files, and exit in both orders. Do not mistake multiple viewer windows in one process for this case. Give all persistence tests a separate data root as well as a separate registry key.

### POL-14 — Finish accessibility exposure beyond the main-window shell

**Evidence:** the existing [MainWindowAccessibility](../src/ui/MainWindowAccessibility.cpp) bridge and active accessibility smoke are valuable. The only custom `WM_GETOBJECT` dispatch is in MainWindow; custom Settings tabs/toggles/footer actions do not expose an equivalent provider, and a named browser surface is not item-level thumbnail/selection semantics. The [keyboard-accessibility plan](keyboard-accessibility-plan.md) already records dialog and manual screen-reader work as incomplete.

**Action / done:** extend the established semantic model to default Settings and relevant browser/viewer actions. Verify names, roles, checked/selected/disabled states, focus order, dynamic updates, and nonvisual confirmation of file actions using Inspect plus NVDA or JAWS. Preserve the custom UI; replacing it with native controls is not required. Keep keyboard-only navigation and screen-reader exposure as separate checks.

### POL-15 — Respect high contrast and system setting changes

**Evidence:** the [default Settings painter](../src/ui/MainWindow.cpp#L5742) and other custom surfaces choose fixed light/dark colors. There is no `SPI_GETHIGHCONTRAST` handling in `src`. [Main-window WM_SETTINGCHANGE](../src/ui/MainWindow.cpp#L23896) handles shell refresh rather than accessibility/theme changes.

**Action / done:** derive a high-contrast palette from Windows system colors when enabled and refresh affected resources on the relevant setting changes. Check disabled controls, selected thumbnails, keyboard focus, links, metadata text, and toast text with both high-contrast schemes, light/dark themes, and larger text. Do not claim contrast compliance from the screenshot alone; measure after the palette change.

### POL-16 — Complete the new Quick Actions toast's visual contract

**Evidence:** the existing working-tree toast [layout](../src/ui/MainWindow.cpp#L12142) caps width at 720 physical pixels and height at 50. Its [DirectWrite path](../src/ui/MainWindow.cpp#L12235) disables wrapping and supplies neither trimming nor explicit text clipping, while the GDI fallback uses ellipsis. Confirmation text includes a filename, destination leaf, full path, and optional shortcut, so long messages readily exceed the available width.

**Action / done:** measure text against the actual font and DPI; choose a bounded one/two-line layout or predictable ellipsis, with full destination available through an accessible status/detail surface. Align D2D/GDI behavior and consistent Quick Actions naming. Check long UNC/Unicode paths, 100/150/200% DPI, largest app text, narrow window, resize during display, rapid repeated operations, partial paired-file success, and both viewer/main-window confirmations. Verify click-through and nonactivation remain intact. This audit did not visually run the new toast.

### POL-17 — Reconcile the shipped help and terminology

**Evidence:** the [HTML guide's Escape row](user-guide.html#shortcuts) says minimize or close, while the [current handler](../src/ui/MainWindow.cpp#L9647) and README say do nothing or close. The guide explains filename-only filtering although [rating/tag parsing](../src/browser/BrowserPane.cpp#L360) ships. Settings/status still say Quick Send in places while the main UI and README say Quick Actions. [docs/testing.md](testing.md) lists five tests instead of the thirteen registered cases. The checked-in screenshot shows older menu/tab labels than the current command catalog.

**Action / done:** audit the shortcut catalog against README, Help > Keyboard Shortcuts, menus, and HTML; correct Escape, describe supported structured filters, standardize the visible product term, refresh the screenshot, and list enabled versus disabled tests accurately. Add a concise 2.2 change/known-limitations section and clarify CMake 3.23 manual-build versus 4.2 preset requirements. Keep historical 2.0/2.1 plans historical and link this plan from the current documentation entry point.

### POL-18 — Validate the actual shipping payload, not just a minimal layout

**Evidence:** [PackageRelease.ps1](../tools/PackageRelease.ps1) only requires the RAW helper if it already finds one in the portable layout; both layouts can omit it without that check failing. Required manifests do not demand the configured NVIDIA DLLs or their notices. [CI's package job](../.github/workflows/ci.yml#L83) sets `HYPERBROWSE_BUNDLE_CUDA_REDIST=OFF`, unlike the shipping package preset's ON setting. The nvJPEG-on build matrix also does not establish that GPU acceleration actually ran on a suitable GPU.

**Action / done:** derive required files from the configured capabilities, verify file/product versions, validate the archive contents, and preserve a hash manifest. Add evidence for the bundled shipping configuration, helper present/missing behavior, runtime absent fallback, and actual GPU use on suitable hardware. Do not label a compile-time ON matrix as proof of accelerated decoding. Test the staged executable in a clean environment rather than only the build-tree executable.

### POL-19 — Include project and vendored component notices in distributions

**Evidence:** [CMake install rules](../CMakeLists.txt#L314) stage NVIDIA notices, but do not stage the root `LICENSE`, `external/nanosvg/LICENSE.txt`, or LibRaw's `COPYRIGHT`/license files. The [runtime manifest](../cmake/RuntimeDependencies.txt.in) describes LibRaw as statically linked without providing its notice/version inventory.

**Action / done:** inventory bundled components, the selected LibRaw licensing/distribution arrangement, upstream versions/revisions, and applicable notices; stage the appropriate files in both outputs and require them in manifest validation. This is a concrete distribution-completeness finding, not a legal conclusion about the current package. Record the dependency review date and owner before publishing.

### POL-20 — Make installer scope and settings cleanup intentional

**Evidence:** the installer supports an all-users mode, but [all application/association entries are HKCU](../cmake/HyperBrowseInstaller.iss.in#L63). Its `Software\HyperBrowse` root uses `uninsdeletekey`, the same root used by installed and portable application settings, so uninstall removes saved preferences and destinations under that key.

**Action / done:** define whether uninstall retains preferences by default or offers an explicit removal option; remove registration-owned keys without unexpectedly erasing shared settings. Verify per-user install, elevated all-users install with a different administrator account, a second standard user's Open With registration, upgrade, uninstall/reinstall, and portable use alongside an installation. Do not claim that all-users file placement also registers associations for every user without proving it.

### POL-21 — Make startup measurements isolated and trustworthy

**Evidence:** [TestStartupBenchmark.ps1](../tools/TestStartupBenchmark.ps1#L102) writes the supplied `-RegistryPath`, but does not set `HYPERBROWSE_SETTINGS_REGISTRY_PATH` for the child. The application reads that environment variable through [SettingsRegistry.h](../src/util/SettingsRegistry.h), otherwise using the default user key. Passing only a custom registry argument can therefore benchmark the wrong folder. The script also prints the child's exit code without requiring success and passes an unquoted output path through `Start-Process -ArgumentList`.

**Action / done:** connect the isolated registry key to the child's environment, restore any prior environment in `finally`, quote arguments correctly for paths containing spaces, and fail on a nonzero/forced process exit. Test default and custom paths, an output directory with spaces, pre-existing settings, timeout, and a nonzero exit after snapshot creation. For interim runs, explicitly set both the environment variable and matching `-RegistryPath` and report that workaround.

### POL-22 — Resolve the disabled multi-viewer test gate

**Evidence:** [tests/CMakeLists.txt](../tests/CMakeLists.txt#L55) registers `HyperBrowseMultiViewerSettingsSmoke` then sets `DISABLED TRUE`. The [keyboard plan](keyboard-accessibility-plan.md) explicitly acknowledges this policy. This audit did not enable or bypass the disabled test. Current multi-viewer shipping claims therefore need separate current evidence.

**Action / done:** identify the reason for the disablement, capture a bounded reproduction, then repair/replace the scenario before re-enabling it through the normal workflow. Until then, record an explicit release exception with owner, rationale, manual matrix, and expiry. One enabled single-viewer Settings test cannot stand in for two-viewer settings, focus, close-during-dialog, and independent slideshow behavior.

## Execution sequence

1. **Establish a trustworthy baseline:** investigate POL-01, agree the POL-22 gate, use fresh build directories, and capture current Debug/Release results. Do not edit the existing toast/version work as part of a test-environment repair.
2. **Protect user data and lifetime:** POL-02 through POL-07; add isolated metadata storage tests first, then rename/dialog and undo regressions. Include POL-13 in the same persistence design so multi-instance behavior is explicit.
3. **Finish operation feedback and cancellation:** POL-08 through POL-12; ensure all long operations have consistent Running/Cancelling/Finished behavior and that successful result paths are authoritative.
4. **Complete visible polish:** POL-14 through POL-17; verify default Settings, browser/viewer, toast, help, and keyboard/screen-reader behavior against the exact newly built executable.
5. **Prove distribution:** POL-18 through POL-21, then run the final matrix. Freeze the version only after package metadata, contents, help, and release notes agree.

For each completed item, record implementation commit(s), exact checks/results, any accepted limitation, and reviewer/date in this document. Leave an item Open or Partial if only code or only tests are complete.

## Manual release matrix

Use disposable image fixtures, a unique settings key, and an isolated metadata/cache root where supported. Settings-key isolation alone does not isolate ratings/tags. Keep a backup of the fixture corpus and compare contents after file-operation checks.

| Surface | Cases to exercise | Acceptance |
| --- | --- | --- |
| Startup / restore | First run; remembered deleted/offline folder; folder/image command line; second launch; path containing spaces/Unicode; prior monitor removed | Useful loading/error state, valid visible bounds, correct target, responsive close. |
| Browser | Empty folder; unsupported-only folder; no filter matches; malformed image; incremental large-folder load; recursive view; rapid back/forward; folder watch add/remove/rename | States are distinguishable; selection/focus follows identity; counts settle correctly; no old thumbnails on reused indices. |
| Keyboard / layout | Tab/Shift+Tab; text edits versus global shortcuts; F2; Apps/Shift+F10; light/dark/high contrast; 100/150/200% DPI; largest text; narrow and restored windows | Every visible action is reachable; text entry does not trigger file commands; focus is visible; controls/text do not clip. |
| Viewer | Two unrelated viewers; window/full-screen transitions; zoom/pan/compare; slow or failed replacement decode; rapid next/previous; close while loading | Correct originating window receives actions; visible content/error state agrees with the active file; memory/work remain bounded. |
| Settings | Experimental Settings; every page; Apply then Cancel; restart; Follow profile; explicit overrides; two viewers; close viewer while dialog is open | Committed values persist, uncommitted values do not; automatic values are coherent; all relevant viewers update. |
| Ratings / tags | Unicode filenames/tags; case-only rename; folder rename/move/copy; overwritten destination; two processes; restart; failed save | Ratings/tags follow the supported operations and survive restart; save failures are visible without losing last good data. |
| File workflows | Browser/viewer/tree/drag/clipboard; paired RAW+JPEG; recycle/permanent delete; conflict prompt; rename incoming; partial success; external replacement; undo/redo twice | Only intended files change; original names and identities are respected; summaries describe actual partial outcomes. |
| Quick Actions | Long/duplicate-leaf destinations; Unicode/UNC; edited/reordered shortcuts; no destinations; rapid F7/F8; partial pair failure; toast resize/DPI | Correct destination is identifiable; focus stays with origin; confirmation is readable and accessible; partial failure is not presented as complete success. |
| Conversion / slow commands | Mixed valid/corrupt formats; 1,000 occupied output names; read-only/full/offline output; cancellation during decode/encode; Cancel then Close; image info/copy pixels/rotate | Totals are bounded; no partial final files or unintended overwrites; UI remains responsive; close drains safely. |
| Cache / devices | Cold/warm cache; disabled/purge/compact; corrupt entries; memory pressure; display reset; sleep/resume; removable/network disconnect | Recoverable failures have useful diagnostics; no stale-image substitution, unbounded work, or post-destroy completion. |
| Codecs | JPEG/PNG/GIF/TIFF; checked-in NEF/NRW fixtures; supported RAW collection; helper missing; NVIDIA DLLs missing; supported GPU available | Clear decode failure/fallback, no crash; actual fallback/acceleration path is recorded; first-frame/page limitations match help. |
| Distribution | Fresh Windows 10/11 standard user; portable; per-user/all-users install; upgrade from supported prior version; second user; Open With; offline F1; uninstall/reinstall | No developer-tool dependency; version/notices/payload match; documented settings/association scope; no surprising preference loss. |

## Final release gates

- [x] All P1 findings are fixed and locally verified. Unicode/save-failure/remap/multi-process metadata coverage and the enabled multi-viewer gate pass; no P1 exception is used.
- [x] Fresh Debug and Release builds pass with warnings treated as errors; all 16 enabled CTest cases pass. There are no disabled tests; two sanitizer boundary tests remain opt-in by configuration.
- [ ] Hosted nvJPEG-on and WIC-fallback matrix passes on the release commit; current sanitizer boundary tests pass. Local sanitizer tests pass 2/2 and local WIC/CUDA shipping matrices pass; attach hosted run links after pushing the candidate.
- [x] Startup budgets pass with the deterministic `assets` fixture: process-to-window <= 2,500 ms; window-to-thumbnail <= 2,500 ms; total <= 5,000 ms. Exact staged shipping Release result: 525.91 / 241.37 / 767.28 ms using the default isolated benchmark key. JSON: `build-polish-shipping/benchmark evidence/startup default isolation.json`. This warm local small-fixture result does not establish large-folder performance.
- [ ] Slow local/network/removable-media and close-during-operation cases are exercised; include a sustained browse/view/convert session and memory/handle observations.
- [ ] Experimental Settings, multiple viewers, keyboard, screen-reader, high contrast, DPI/larger text, and new toast are manually checked on the exact candidate binary.
- [ ] Shipping portable ZIP and installer are generated from the same clean candidate, payload/version/notices are validated, and clean-machine install/upgrade/uninstall checks pass. Generation and validation pass locally; the clean-machine installer matrix remains.
- [x] README, offline guide, screenshot, shortcut catalog, version metadata, release notes, and known limitations agree. Signing decision: local candidates are unsigned and must not be represented as signed; owner is the release maintainer, with signing or an explicit unsigned-release approval required before publication. Artifact hashes are recorded in the validation ledger and generated `SHA256SUMS.txt`.
- [x] Every deferred P2 item has an owner, user-impact statement, and follow-up target in the implementation ledger. Publication still requires the outstanding manual/hosted results and release sign-off here.

## Deliberate follow-up work

Continue small ownership extractions from the roughly 24,000-line `MainWindow.cpp` only when they make one of the above fixes safer. Do not make wholesale decomposition a prerequisite for this polish pass. Likewise, defer new formats, animated playback, color-management expansion, saved searches, extra compare layouts, and new transition effects unless separately selected for this release.

Existing strengths to retain include bounded service executors, cache/protocol corruption tests, folder-watch policy tests, transactional journal-stack transitions, deferred shell-operation close handling, the main-window MSAA bridge, and the committed Windows CI/package workflow. This plan targets gaps in those contracts rather than reopening completed work without evidence.
