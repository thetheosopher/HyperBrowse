# HyperBrowse — Bugs / Feature Requests

A running list of things found while using HyperBrowse as a daily driver, kept in one place
instead of scattered across emails.

**On the fork.** I run a private fork of HyperBrowse so I can patch things for my own use while
you get to them in your own time. It is not published and I am not sending patches — I would
rather you fix these your own way, and enjoy doing it, than work from someone else's diff. I keep
pulling your changes into my copy and resolving conflicts as they come, so anything patched on my
side is a stopgap that gets thrown away the moment your fix lands. Nothing here is a deadline.

This list deliberately says nothing about implementations, mine or proposed. It is what I saw and
how to see it again.

**Numbering is stable and permanent.** Items 1–11 keep the numbers they had in the *HyperBrowse
2.0 — Feedback* document sent 2026-08-23, so anything already discussed can still be referred to
by number. New findings are appended. Numbers are never reused or renumbered — if we talk about
"number 12", it means the same thing forever.

**Conventions**

- Items are ~~struck through~~ once fixed upstream, with the version they landed in. They stay in
  place rather than moving, so the numbering never shifts.
- Each item is tagged **Bug** or **Request**. The line between them is blurry.
- Items describe what was observed and how to see it again, nothing more.

Last updated: 2026-09-09

---

## 1. Settings have outgrown the menu — Request

The settings surface is now large enough that the menu is working against it:

- Clicking a checkbox toggles it *and* closes the menu, so changing several options means
  reopening the menu each time.
- There is no single place to see what is configurable, so any setting not already known about is
  effectively invisible.

This would also cover half of item 9, since a settings dialog documents itself.

---

## 2. Arrow-key panning is inverted on both axes — Bug

Keyboard panning moves content-drag style: `Up` moves the image down, as though the image had
been grabbed and pulled. Same on the horizontal.

Scrollbars, `Page Up`/`Page Down` and the mouse wheel all move the viewport instead, as does
IrfanView, so the arrow keys are inconsistent with both the rest of the app and the platform
convention.

---

## 3. `H` changes more than the zoom mode — Bug

Expected rule: `H` should only change zoom/fit mode, and should not touch fullscreen state or
window geometry. Two violations:

- **In fullscreen:** `H` always exits fullscreen, whether zoomed or not.
- **In windowed mode:** `H` leaves the existing zoom applied, and the window itself becomes
  smaller and shifts left.

---

## 4. `File > Clear All Favorite Destinations` has no confirmation — Bug

The command executes immediately. One misclick clears every favourite, and rebuilding them is
entirely manual.

---

## 5. `F7`/`F8` do not remember the last destination — Request

The destination dialog highlights the top row every time. When filing a run of images into the
same folder, every operation costs as much as the first. IrfanView pre-selects the last
destination used, making repeats `F8`, `Enter`.

Per-session memory would be sufficient; persisting across restarts is fine but not needed.

---

## 6. The same feature has two names, and neither is quite right — Bug

The right-hand pane says *Quick Send*; the menus and dialogs say *Favorite Destinations*. Same
feature, two names.

Since first reporting this: the preferred name is **Quick Actions**, used everywhere. Neither
existing name describes it well — it is not only about sending, and not only about favouriting —
and having two of them makes the feature harder to talk about than it needs to be.

---

## 7. No favourite toggle on the folder tree context menu — ~~Request~~ **Withdrawn — already implemented**

Original request: `File > Add Current Folder to Favorite Destinations` takes several clicks and
acts on the *current* folder rather than the one being pointed at. A toggle on the tree's
right-click menu would allow favouriting any folder without navigating to it first, and should
read "Remove from ..." when the folder is already a favourite.

**Withdrawn 2026-08-27 — this already exists in 2.0 and does exactly that.** Right-clicking any
folder in the tree offers the toggle, it acts on the folder clicked rather than the current one,
and the label already flips between add and remove.

The reason it was asked for is worth more than the request was: the menu item is labelled
**"Add to Favorites"**. That is a *third* name for the feature the pane calls "Quick Send" and
the File menu calls "Favorite Destinations". Looking for either of those two names, the existing
command does not read as the same feature, so that confused me. So this is a request for a rename. 😊

---

## 8. `Ctrl+I` image information dialog ignores the theme — Bug

The dialog renders in light mode regardless of the selected theme. Minor.

**Amended 2026-08-27 — I under-reported this, and in two directions.**

First, the `Ctrl+I` dialog is a Windows *task dialog*. Task dialogs are drawn by the common
control library from the system theme, and there is no supported way to make one follow an app's
own dark mode. It cannot be fixed by changing a colour; it would have to stop being a task dialog.

Second, and the part I had wrong: it is not just this dialog. Every custom dialog in the app
answers `WM_CTLCOLOR*` with `GetSysColorBrush(COLOR_WINDOW)` and `GetSysColor(COLOR_WINDOWTEXT)`
— system colours, which stay light whatever the app's theme is. That is thirteen sites across six
dialog procedures, so rename, batch rename, slideshow settings, performance settings, file
associations and the text prompts are all in the same position. The reason I only reported
`Ctrl+I` is presumably that it is the one I open most.

So this is a larger job than "minor" suggested, and it splits in two: the ordinary dialogs need to
paint from the app palette instead of the system one, and `Ctrl+I` separately needs to become an
ordinary dialog before that can help it. Still not urgent — nothing is unreadable, it just looks
wrong next to a dark main window.

---

## 9. Keyboard shortcuts are not documented in the README — Request

Repeat from the previous round, and growing as keys are added — `H` is new in 2.0 and is not in
the README. A shortcut table for the browser and viewer would help; every undocumented key is a
feature a new user will never find.

Added 2026-08-27: it goes deeper than the README. The shortcut catalogue in the source — the one
that feeds the in-app shortcut listing — has 17 viewer entries, and the viewer's key handler
answers to considerably more than that. Missing from the catalogue: `H`, `W`, `L`, `R`, `C`, `X`,
`Tab`, `Space`, `F11`, `Page Up`, `Page Down`, `Enter`, and `+`/`-`. So a user who goes looking
in the app rather than the README still will not find them. Worth fixing at the catalogue, since
then both the app listing and any generated documentation pick them up at once.

---

## 10. (Removed; not a bug)

---

## 11. "Copy Prompt" button in the details pane for AI-generated images — Request (wishlist)

The details pane is genuinely useful for AI-generated images. Where generation data is present in
the image metadata, a "Copy Prompt" button at the top of the pane that puts the prompt on the
clipboard would save a lot of squinting and selecting. The button would appear only when a prompt
is detected, so it costs nothing for ordinary photos.

Metadata layout is not standardised — A1111 writes a text blob into the PNG `parameters` chunk,
ComfyUI writes JSON into `prompt` and `workflow` chunks. Whatever subset is convenient is fine. If
the full workflow JSON is available, a second button for that would also be handy, since that is
what gets pasted back into ComfyUI to reproduce an image.

Lowest priority of anything on this list.

---

## 12. Viewer navigation wraps to the first image after exactly 16 files — Bug

Found 2026-08-27.

Open an image by double-clicking it in Explorer, in a folder holding well over 16 images. Page
forward through the folder. After 16 images the viewer reports "Wrapped to first image" and
returns to the start, as though the folder contained only those 16.

Reproduced consistently. The count is 16 every time.

**Repro**

1. Pick a folder with, say, 100 images.
2. Double-click one of them in Explorer (do not open HyperBrowse first).
3. Page forward with the arrow keys.
4. On the 16th image, navigation wraps to the first instead of continuing.

**The asymmetry that makes it interesting:** close the viewer, reopen HyperBrowse, browse to the
same folder and page through from the browser — navigation now goes past 16 and covers the whole
folder correctly. Only the double-click-from-Explorer path shows it.

---

## 13. Some JPGs thumbnail correctly but will not open — Bug

Found 2026-08-27. Metadata for one failing file sent separately.

The browser shows a correct thumbnail for the file, so it is being decoded at least once, but
opening it in the viewer fails.

First noticed on JPGs rotated 270 degrees, which suggested an orientation issue — but further
files have since failed that do not share that rotation, so the 270 degree link may be a
coincidence rather than the cause.

**Repro**

1. Browse to a folder containing one of the affected JPGs.
2. Confirm the thumbnail renders.
3. Open the file in the viewer — it fails to display.

Sample files and full metadata available on request.

Added 2026-08-27, from reading the decode paths rather than the files. The thumbnail is not an
embedded EXIF preview or a cached copy — it is a real decode of frame 0, taking the same decoder,
the same frame and the same orientation transform as opening does. The one thing that differs is
that the thumbnail is decoded to a scaled target while opening decodes at full resolution, so
whatever fails appears to fail on size rather than on the file being unreadable.

That narrows it to the tail of the full-resolution path — the format conversion to 32bpp PBGRA,
the destination bitmap allocation, or the pixel copy. Usefully, each of those reports a different
message, and the viewer displays it under "Unable to Open Image":

- "Failed to open the selected image for decode."
- "Failed to apply image orientation."
- "Failed to scale the decoded image."
- "Failed to convert the decoded image into the viewer pixel format."
- "Failed to allocate the destination bitmap."
- "Failed to copy decoded pixels into the destination bitmap."

Whichever one appears identifies the failing step without needing the file.

One caveat on my own report, which may matter more than the rest of it: **the thumbnail is not
proof the file still decodes.** Thumbnails are looked up in the persistent disk cache before any
decoding is attempted, so a file that was thumbnailed successfully at some earlier point keeps
showing that cached image indefinitely, whether or not it can be decoded now. I had assumed
"thumbnails but will not open" meant two code paths disagreeing about the same file; it may
instead be one working decode from the past and one failing decode today.

Easy to separate: turn off Persistent Thumbnail Cache (or clear it from the cache cleanup
dialog) and revisit the folder. If the thumbnail disappears too, the file simply does not decode
any more and the interesting question is what changed — not why two paths differ.

Ruled out since, by driving the full-resolution decode directly rather than through the UI: all
eight EXIF orientations come back correctly oriented, and so do awkward shapes — 1x1, 3x4097,
4097x3, 1023x767 — which is where an assumption about row stride or buffer size would show up if
there were one. So it is not orientation handling, and not a size or stride assumption in the
full-resolution path. Whatever it is appears specific to these particular files rather than to a
class of them.

Reproduced 2026-08-27 with a sample file, and **most of the reasoning above turns out to be
wrong** — including my conclusion that orientation was ruled out. Recorded here in full because
the wrong turns are the useful part.

It is not a decode failure. The decode never finishes, so nothing is ever handed to the viewer
and the window stays black. No error message is ever produced, which is why the list of six
candidate messages above was never going to identify anything.

The sample is an ordinary 4032x3024 phone photo, 1.7 MB, EXIF orientation 6. Driving the
full-resolution decode directly: **114 seconds of CPU and still running when killed.** A fresh
256-pixel thumbnail of the same file: **47 seconds.** Neither returned.

The mechanism is the orientation transform, but through cost rather than correctness. The
decode chain is `IWICBitmapFrameDecode` → `IWICBitmapFlipRotator` → `IWICFormatConverter`,
finished by a single `CopyPixels` over the whole image. The flip rotator is a streaming source
that holds nothing, so producing one output row of a 90 or 270 degree rotation requires a
*column* of the frame beneath it — and a JPEG frame decodes forwards only. The work therefore
scales with the number of output rows requested: about 3000 when opening the file, about 250 for
a thumbnail. That ratio is the whole of "thumbnails fine, will not open", and it also explains
why the grid still shows a picture — that thumbnail is the disk-cached copy described above, not
a live decode.

So the 270 degree observation that started this report was correct, and dismissing it was my
error. The reason the eight-orientation test above passed is that it used a 65x33 image, where
several thousand redundant passes over a tiny frame cost nothing measurable. Any test image
small enough to be convenient is small enough to hide this.

**Repro**

1. Take any JPEG at real camera dimensions (4000x3000 or so) with EXIF orientation 6 or 8.
2. Decode it at full resolution through a flip rotator layered directly over the frame decoder.
3. Time it, and compare against the same file with orientation 1.

---

## 14. Quick Send favourites are not written to disk until exit, so the last instance to close wins — Bug

Found 2026-08-27.

Favouites appear to be held in memory and persisted only when HyperBrowse exits. With two
instances running, each holds its own copy from startup, and whichever closes last overwrites the
other's additions. Entries added in the first window are lost.

**Repro**

1. In HyperBrowse window #1, add one or more new Quick Send (favourite) entries.
2. Double-click a picture in Explorer to open a second HyperBrowse.
3. Check Quick Send in window #2 — the entries added in window #1 are absent.
   *(First symptom: they had not been written to disk.)*
4. Close window #1. This does write its favourites to disk.
5. Close window #2. This overwrites them with window #2's older list, and the entries added in
   step 1 are gone.

Related to item 10 — single-instance handling would make the two-window case unreachable, but the
"not saved until exit" behaviour stands on its own.

---

## 15. Quick Send shortcut keys are restricted to letters and digits — Bug

Found 2026-08-27.

Assigning a shortcut to a Quick Send destination only accepts letters and digits. Trying to use
`-` or `=` is rejected.

The ask is to allow any key, with modifiers, rather than a fixed alphabet — including shifted
combinations such as `Shift+8` and `Shift+9`. Function keys are the reasonable exception, since
those are already spoken for.

With a limited set of destinations this is not obvious, but the letters and digits get used up,
and the keys that would be most comfortable to reach are not necessarily in that set.

**Repro**

1. Add a Quick Send destination.
2. Attempt to assign `-` or `=` as its shortcut.
3. The key is not accepted.

---

## 16. The window opens white in dark theme — Bug

Found 2026-08-27.

Starting HyperBrowse with the dark theme selected shows a large white window before the theme is
applied. The theme is not honoured early in startup.

**Repro**

1. Set the dark theme and close HyperBrowse.
2. Start it again.
3. The window appears white first, rather than dark from the moment it is visible.

---

## 17. The test suite crashes for anyone with "show hidden files" enabled — Bug

Found 2026-08-27, building and running the suite locally.

`HyperBrowseSmoke` dies with an access violation rather than reporting a failure, so `ctest`
shows a bare SEGFAULT with no message. It reproduces every run on this machine.

The folder-tree enumeration scenario checks the hidden-folder result *after* the shared result
object has been reset for the next query, so it indexes position 3 of a vector that is empty by
then. The branch only runs when Windows is configured to show hidden files and folders, which is
presumably why it has not shown up elsewhere.

Two separate consequences worth noting: the read is out of bounds rather than a failed
expectation, so it takes the whole suite down instead of naming the problem; and because the
process dies, all buffered test output is lost, which makes it look like a crash on startup
rather than one two thirds of the way in.

**Repro**

1. In Explorer, turn on View > Hidden items.
2. Build and run `ctest -C Release`.
3. `HyperBrowseSmoke` exits with an access violation and no diagnostic.

(Found with an AddressSanitizer build — `/fsanitize=address` — which points straight at the line.)

---

## 18. Viewer fit-mode tests fail on a wide monitor — Bug

Found 2026-08-27, running the suite locally on a 3440x1440 ultrawide.

Both `HyperBrowseSmoke` and `HyperBrowseViewerFitSmoke` fail with:

```
Viewer FitWidth navigation did not resize from the next image aspect ratio (client=3424x1353, index=2)
```

The scenario puts the viewer in FitWidth, navigates to a 40x16 test image, and requires the
client area to match that 2.5 aspect within 0.02. On this display the client comes back
3424x1353, an aspect of 2.531 — outside the tolerance by about 0.03.

The numbers say the window is doing the sensible thing rather than the wrong thing. FitWidth
spans the work-area width, so 3424 wide at 40:16 would need about 1370px of client height, and
the work area on a 1440p display only leaves about 1353 once the frame is subtracted. The height
appears to be limited so the window does not extend past the bottom of the screen — which is
correct behaviour, and means the resulting aspect legitimately is not the image aspect.

That makes it display-dependent rather than intermittent: on a 1920x1080 panel the same image
needs roughly 762px against about 993 available, so nothing limits it and the check passes. It
should reproduce on any display wide enough that work-area width divided by 2.5 exceeds work-area
height minus the window frame.

**Repro**

1. Run the suite on a monitor of roughly 21:9 or wider (3440x1440 reproduces it every time).
2. `HyperBrowseSmoke` and `HyperBrowseViewerFitSmoke` both fail on the FitWidth navigation check.
3. On a 16:9 monitor, both pass.

---

## 19. Duplicate access keys in the File menu — Bug

Found 2026-08-27.

Several File menu entries share the same underlined access key, so pressing that letter cycles
the highlight between them instead of invoking a command. Two pairs are visible:

- `Open Folder...` and `Open` both use **O**.
- `Add Current Folder to Favorite Destinations` and `Clear All Favorite Destinations` both use
  **D** (the ampersand sits before "Destinations" in both).
- `Open Recent Folder` and `Properties` both use **R**.

**Repro**

1. Open the File menu with `Alt+F`.
2. Press `O`. The highlight moves between the two Open entries rather than opening anything.
3. Same with `D`, and with `R`.

Cosmetic, and only noticeable to keyboard users — but the second pair is the one that bites,
since the two commands next to each other do very different things and one of them clears the
whole favourites list.

---

## 20. `HyperBrowseSmoke` FitWidth navigation fails intermittently — Bug

Found 2026-08-27, on a 3440x1440 display.

One run in three failed with:

```
Viewer next-image navigation failed while in FitWidth
```

The same binary passed the two runs either side with no changes between them, so it is timing- or
state-dependent rather than a straightforward break. The step waits up to five seconds for the
index to advance, so a slow decode does not explain it on its own.

A guess at where to look, offered only as a starting point: in FitWidth the viewer spans the
work-area width, which should leave the image exactly as wide as the client area and therefore
not horizontally pannable — and the arrow keys navigate only when the image does not overflow. If
that comparison can land a fraction the wrong side of equal, `Right` would pan by a hair instead
of changing image, and the wait would time out. That would also explain why it shows up here and
on a wide display, where the fitted width is largest.

Related to item 18, which is the same corner of the same feature on the same class of monitor.

**Repro**

Run `ctest -C Release` several times on a wide display. It is not reliable — it took three runs
to see once.

---

## 21. `HyperBrowseSmoke` fails on GitHub-hosted runners: a rename does not report the created path — Bug

Found 2026-08-27, in your own CI rather than ours.

`HyperBrowseSmoke` fails on GitHub-hosted Windows runners at `RunFileRenameOperationScenario`:

```
HyperBrowse smoke test failed: File rename operation did not report the created path
```

The three assertions before it all pass, so the rename itself works: nothing is reported failed,
`before.jpg` is gone, and `after.jpg` exists. Only the reporting of the new path is missing —
`fileOperationResult.update.createdPaths` comes back empty where the test expects one entry.

That list is filled in the progress sink from `PathFromShellItem(psiNewlyCreated)`, using the
item the shell hands to `PostRenameItem`. If the shell passes nothing there — or an item with no
filesystem path — the rename still completes and the list stays empty, which is exactly the shape
of what the runner shows. Session 0, no interactive desktop, and a stripped-down shell are all
plausible reasons for that argument to arrive differently on a hosted runner.

**It fails both CI jobs, and the second one is misleading.** `build-test-benchmark` reports it
directly as a Debug test failure. The `package` job then runs the same suite through
`tools/PackageRelease.ps1`, which throws `Run Release smoke tests failed with exit code 8`, and
that surfaces as `error MSB8066: Custom build for '...HyperBrowseReleaseArtifacts.rule' exited
with code 1`. The packaging step itself is fine — it never gets to run. Worth knowing before
anyone goes looking for a packaging bug, and worth noting it fails in Release there too, so this
is not Debug-specific.

Seen on run 32672722726 (`thetheosopher/HyperBrowse`, master, `65be7bc`), both jobs.

**Not reproducible on a desktop.** The same scenario passed 25+ consecutive runs here today
across Debug, Release and an AddressSanitizer build, on Windows 11.

**Repro**

1. Push to a branch that runs the CI workflow, or run the suite on a GitHub-hosted
   `windows-latest` runner.
2. `HyperBrowseSmoke` fails in `RunFileRenameOperationScenario`; the `package` job fails
   afterwards for the same reason wearing an MSB8066 hat.

---

## 22. The `Tab` overlay toggle is documented nowhere a user would look — Bug

`Tab` in the viewer is the master switch for everything drawn over the image: the corner
overlays and the full metadata panel together. It is one of the most useful keys in the
application — it is the only way to get an unobstructed look at a photo without leaving the
viewer — and I used HyperBrowse daily for weeks without knowing it existed.

Searching for it afterwards, here is everywhere it appears in 2.0:

- `README.md`, in a version-history bullet under **2.0.0**, as a subordinate clause about a
  different feature: "...that hides and shows with the existing `Tab` overlay toggle." That
  wording assumes the reader already knows the toggle, which is exactly backwards for the one
  place it is written down.

That is the complete list. Specifically, it is **not** in:

- `docs/user-guide.html`, the guide `F1` opens. The string `Tab` does not occur in that file at
  all.
- **Help &gt; Keyboard Shortcuts**, the in-app shortcut list. `Tab` is not among its viewer
  entries. That overlaps item 9, but is worth separating: most of the other keys missing from
  that list have a menu item or a toolbar button that reveals them anyway, and this one has
  neither.
- The **Settings &gt; Viewer &gt; Show Detail Overlays** menu item, which is the toggle's own
  command and shows no accelerator text. Every other menu item that has a key shows it there, so
  a blank one reads as "this has no key".

So the feature is discoverable only by pressing `Tab` on the off chance, or by reading a
changelog entry for something else.

**Repro**

1. Open an image in the viewer.
2. Look for a way to hide the overlays: the **Settings &gt; Viewer** menu, **Help &gt; Keyboard
   Shortcuts**, and `F1` for the user guide.
3. None of them mentions a key. Press `Tab` and the overlays hide.

Any one of the three places above would have been enough for me to find it.

---

## 23. Combo boxes render light in dark mode — Bug

Every drop-down in the application draws itself as a white field with black text and a light
drop-button, whatever the theme is set to. In a dark window it is the brightest thing on screen.
It is not new and not confined to one dialog; it is how combo boxes have always looked here in
dark mode.

**Repro**

1. Set the theme to Dark.
2. Open any window with a drop-down in it.
3. The field is white and the text is black, on a dark background.

## 24. Escape in the viewer closes it outright, with no way to just leave full screen — Request

Escape is the key I reach for to back out of full screen, and in the viewer it closes the viewer
altogether. The image is then gone and has to be found and opened again. F11 does leave full
screen, but Escape is the reflex, and every time it costs the image.

What I would like is for Escape to step down one level rather than out: leave full screen if it
is full screen, and close only when already windowed. If you wanted to go further, landing in a
height-fitted window would match what I actually do next about nine times in ten — Escape then
`H` is a fixed habit for me.

**Repro**

1. Open an image and press F11 for full screen.
2. Press Escape to leave full screen.
3. The viewer closes.

## 25. Full screen keeps the previous zoom instead of fitting the image — Bug

Going full screen makes the window fill the screen but leaves the image at whatever scale it
already had. If the zoom was custom, full screen shows a corner of the picture at the old
magnification, which is not what full screen means to anyone. It happens on F11 and on
double-clicking an image from Explorer alike.

**Repro**

1. Open an image and zoom in a few steps, so the zoom is neither fit nor actual size.
2. Press F11.
3. The window fills the screen and the image is still at the old zoom, showing part of itself.

## 26. A double-clicked image opens behind the Explorer window that launched it — Bug

Double-clicking an image in Explorer starts HyperBrowse, which comes up full screen — underneath
the Explorer window I just double-clicked in. The picture is open and on screen and I cannot see
it until I click the taskbar or move Explorer aside. It is consistent, not occasional.

**Repro**

1. In Explorer, in a folder of images, double-click one.
2. HyperBrowse opens the image full screen.
3. The Explorer window is still in front of it.

## 27. (Removed; not a bug)

## 28. A second window opens exactly on top of the first — Bug

When I do get two windows up, the second restores the same saved position and size as the first
and lands on it pixel for pixel. It looks as though nothing happened — the only way to tell there
are two is to drag the top one aside. Every other application I use offsets a second window down
and to the right.

**Repro**

1. Get two HyperBrowse windows open.
2. The second is exactly over the first, and until it is moved there is no visible sign of it.

## 29. Escape minimises the main window — Request

With "close the main window with Esc" turned off, Escape minimises the window instead. Escape is what I press to dismiss something *inside* a window,
and having the whole window drop to the taskbar is a surprise every time. I would rather it did
nothing at all when the close option is off. Minimising has its own menu item and its own key,
and I will use those when I want them. Or this could be a user setting. 😊

**Repro**

1. Turn "close the main window with Esc" off.
2. Press Escape in the main window.
3. The window minimises.

## 30. Full metadata is one setting for both windowed and full-screen viewing — Request

The metadata panel is welcome next to a windowed image and is the last thing I want across a
full-screen one, and there is a single switch for both. I end up toggling it every time I change
mode. Two settings, one per mode, would suit how it is actually used.

**Repro**

1. Turn full metadata on and view an image in a window. It is useful there.
2. Go full screen. The same panel is now over the picture.
3. Turning it off for full screen turns it off for windowed viewing too.

## 31. Running the test suite changes the user's own settings — Bug

The smoke tests operate on `HKCU\Software\HyperBrowse` — the same key the installed application
keeps its preferences in. Running them from a working copy therefore reads and writes the
settings of whoever is logged in, and scenarios that construct real windows write whatever those
windows persist as a matter of course, not only the values a scenario set out to change.

Individual values are guarded with scoped backup helpers, and that only covers what a scenario
knows it touches; it does nothing about writes made by the application code underneath it, and
nothing at all if the run is interrupted, since the restore is on the way out.

I lost preferences repeatedly before working out that running the tests was what did it. A key of
the suite's own, or an environment override for the settings location, would make the tests safe
to run on a machine someone actually uses.

**Repro**

1. Note your current preferences — window position, recent folders, favourite destinations.
2. Run the smoke tests from a working copy.
3. Compare. Values the run touched have moved; interrupt it part-way and more are left changed.

## 32. No key for full screen within reach of the home position — Request

F11 is the only key for full screen and it is the most-used thing in the viewer for me. Reaching
for the function row breaks the rhythm of going through a folder, where everything else —
arrows, `Enter` is under the hand already. A modified key next to those would fix
it; `Ctrl+Enter` would be my choice, since plain Enter is already the zoom toggle and the two
read as a pair.

**Repro**

Not a defect; there is simply no full-screen key other than F11.

## 33. In full screen, no way to tell an image is already in the destination folder — Request (wishlist)

Filing a folder of photos, my loop is F8 then Enter over and over into the same destination. What
I cannot see while doing it is whether the image on screen is one I have already filed there, so
I either send it twice and answer an overwrite prompt, or skip something because I am not sure.

A line in full screen saying the current image is already in the default destination — matched on
name and size, so a same-named different picture does not read as a duplicate — would remove the
guessing. Something like `File already exists in Quick Action [9] D:\Sorted\Keep`, with the
shortcut key shown, so I know which destination is meant without leaving the image.

**Repro**

Not a defect; there is no such indication today.

---

# Part Two

Items 1–33 were sent on 2026-08-28. Everything below was found afterwards. Same conventions,
same permanent numbering — this is a continuation of the list, not a replacement for it.

---

## 34. Copying a file makes the next image stop appearing instantly — Bug

Filing images in the viewer, the flip to the next image is instant — until I copy one. Press
`F8` to copy the displayed image to a favourite destination, then press the arrow key for the
next image, and the flip is no longer instant: there is a visible hitch before the picture
appears. The flip after that one is instant again, and stays instant until the next copy.

A copy does not change the image it copied, does not change any other image in the folder, and
writes into a folder I am not looking at. Nothing that is on screen, or about to be, is different
because of it. From the outside it looks as though the work already done for the folder is
discarded when a file operation completes, and has to be redone on the next navigation.

Moving a file behaves the same way. There the moved file itself really has gone from the folder,
but the other images in it have not, and they show the same effect.

**Repro**

1. Open a folder of large images and page through it in the viewer until the flips are instant.
2. Press `F8` and copy the current image to a favourite destination outside the current folder.
3. Press the arrow key for the next image. The flip hitches instead of being instant.
4. Keep going. The remaining flips are instant again until the next `F8`.

The effect is easier to see with larger images.

## 35. (Removed; not worth reporting — the number is retired)

## 36. Every completed copy walks the whole folder tree, twice — Bug (performance)

Filing images with `F8` into the same destination over and over, the flip to the next image is
not quite instant afterwards. The delay is not the copy — that runs on a worker — and it is not
the image; it lands on the *next* keypress, and it scales with how much of the folder tree is
expanded. Collapse the tree and it shrinks. Expand a drive with a lot of directories and it
grows.

**Repro**

1. Expand a good deal of the folder tree — a drive with many directories, several levels open.
2. Open a folder of large images in the viewer.
3. `F8` the current image into a favourite destination, then press the arrow key for the next.
4. The flip hesitates. Collapse the tree back down, repeat, and the hesitation shrinks with it.

**Where it comes from — and this item breaks the list's usual rule**

Everything else here deliberately says nothing about implementation. This one is the exception,
because the behaviour on its own gives you almost nothing to go on and the cause is a two-line
observation:

`InsertFolderTreeFolderIfParentLoaded` runs on every completed copy and move, and calls
`FindFolderTreeItemByPath` twice. That function is a full recursive walk of the tree control, and
for every node it passes it sends a `TVM_GETITEM` to the control and calls `FolderPathsEqual`,
which normalises two paths. So each completed file operation costs two whole-tree traversals,
with a window message and two string normalisations per node, on the UI thread, landing in the
message queue right where the next keystroke is.

Three fixes, smallest first:

- **One walk instead of two.** A folder can only be inserted under its own parent, so find the
  parent first and then scan only that parent's children to answer "is it already there?". Same
  answer, no second traversal, no change in behaviour.
- **Descend instead of scanning.** The tree mirrors the filesystem, so a path can be found by
  walking down from the matching root through its components — O(depth x siblings) rather than
  O(every node). It needs care where a favourite root and a drive root both cover a path.
- **Index it.** A `path -> HTREEITEM` map maintained in `InsertFolderTreeItem` and the two delete
  sites makes every lookup O(1). It is the most work, because deleting a node has to retire its
  whole subtree from the map, but `FindFolderTreeItemByPath` is called from eight places —
  including per event in the folder-watch handler, where a burst of folder changes currently
  costs one full traversal each — so it is the one that pays off everywhere.

## 37. Quick Send eats the next keystroke, because the shell's progress dialog owns the viewer — Bug

Filing images in the viewer, my loop is `F8`, `Enter`, `Page Down`, over and over, as fast as I
can press them. The `Page Down` frequently does nothing for about a second — the image does not
change, then it does. It reads as though the copy were blocking the main thread. It took me a
while to find a way to show it reliably; the timing below is that way.

**Repro — and the timing is the whole point**

1. Open a folder of images in the viewer.
2. `F8` the current image to a favourite destination, press `Enter` to accept it, and then press
   `Page Down` **immediately**, the way you would if you were filing a folder of photos at speed.
   The flip hesitates for roughly the length of the copy.
3. Now do exactly the same thing, but **wait about a second** after the copy before pressing
   `Page Down`. It is instant.

That difference is the tell. If it were decoding, or cache work, or the image itself, waiting
would not change anything — the work would just happen later. It only makes sense if the viewer
is not accepting the keystroke at all while the copy is running.

**Where it comes from**

Same exception as item 36 — the behaviour alone would send you looking in the wrong place, so:

`FileOperationService` calls `IFileOperation::SetOwnerWindow(ownerWindow)` and builds its
operation flags **without `FOF_SILENT`**. The shell therefore puts up its own progress dialog,
owned by whichever window started the operation — the viewer. An owned shell dialog disables its
owner and takes the foreground for as long as it is up, so keystrokes aimed at the viewer go
nowhere until the copy finishes and it closes. Waiting a second means the dialog has already
gone, which is why the delay disappears.

Adding `FOF_SILENT` to the flags fixes it. It suppresses only the shell's *progress* window —
confirmations, error dialogs and the elevation prompt all still appear — and nothing is lost,
because the service already `Advise`s its own `IFileOperationProgressSink` and the application
draws its own progress from it.

One corroborating detail: `ApplyCompletedFileOperation` already carries a `SetForegroundWindow`
call whose comment says "a viewer-initiated delete leaves the main window active and the viewer
stops responding to the keyboard until clicked" — which looks like the same dialog seen from
another angle.

## 38. A key to resume filing where I left off — Request

This one is about a workflow, and I suspect it is yours too, so it may be worth more than it
looks.

When I sort a big folder I go through it start to finish, filing as I go: look at the image,
`F8` it to a destination or `F7` it away, `Page Down`, repeat. A folder is 1147 images and I am at 27. Then I stop for the day, or the machine restarts, or I close the app, and the position is
gone. What I do now is note roughly which number I was on, reopen the folder, and navigate back to
it by hand.

What would fix it is a single key (maybe F4?) that jumps me back to where I was filing:

- If the last thing I did was **copy** a file to a destination, jump me to **that file**. It is
  still there, and it is the last one I dealt with.
- If the last thing I did was **move** a file, that file is gone, so jump me to the file
  **immediately before it** in the sequence. Landing one early is fine.

The important part is that it survives the app closing, since that is the case it is for. So it
would want to be written out with the rest of the settings rather than held for the session.

**One position per folder, rather than one for the application.** I usually have more than one
folder in flight at a time — one being sorted, another being reviewed — and with a single
position the key resumes whichever folder I touched last, which is the wrong one in every other
folder. Opening a folder and pressing the key should go to the last file I acted on *in that
folder*; a folder I have never filed from would say so rather than send me elsewhere.

It would also want to survive the folder being renamed or moved, since that happens as part of
sorting: a folder I am filing from often gets its final name once I can see what is in it.

**`F4` would be my suggestion for the key.** It is unused, and it is close to `F7`/`F8`, so it
falls in the same hand position as the rest of the filing loop. A menu item alongside it would
help as well, mostly so the key is discoverable.

This is a different thing from the selected-image path the app already remembers on exit. That
one follows wherever I last *looked*, and while filing I often page past several images without
touching them, or go back to compare something. The position I would want to resume from is the
last one I **acted on**.

**Repro**

Not a defect; there is no such key today.

## 39. Panning immediately after a wheel zoom jitters and snaps back — Bug

Consistently reproducible, in the gesture of zooming into part of an image and then dragging to
look around it.

Full screen, wheel-zoom in with the pointer near the top of the picture. Then, straight away,
click and hold and drag upward to pan down into the image. It jitters, and then returns to where
it was at the top. Do the identical thing but pause for a couple of seconds after zooming before
pressing the mouse, and it pans as expected.

That "wait a moment and it works" is what gave it away: the zoom is animated on a timer, and the
gesture only misbehaves while that animation is still running.

**Repro**

1. Open an image full screen.
2. Put the pointer near the top of the picture and wheel-zoom in a couple of notches.
3. **Immediately** — do not pause — press and hold the left button and drag upward to pan down.
   The image jitters and then jumps back up to where the zoom left it.
4. Repeat, but wait two or three seconds after the last wheel notch before pressing the button.
   It pans normally.

**Where it comes from**

Same exception as items 36 and 37; the timing is the whole clue and it points straight at the
cause.

The wheel zoom runs a smooth-zoom timer, and each tick eases *both* the scale and the pan toward
the zoom's target:

```
panOffsetX_ += (smoothZoomTargetPanX_ - panOffsetX_) * 0.22;
panOffsetY_ += (smoothZoomTargetPanY_ - panOffsetY_) * 0.22;
```

So a drag in progress is fighting the timer: the mouse moves the pan, and 16 ms later the timer
takes 22% of that back. That is the jitter. When the animation converges it then *assigns*
`panOffsetX_ = smoothZoomTargetPanX_` outright, throwing the drag away entirely — the snap back.
Waiting lets the animation finish first, which is why waiting works.

Taking hold of the image should end the animation: jump the scale and pan to the values it was
heading for, stop the timer, and let the drag proceed from there. That is the same state waiting
would have produced, so the two routes then behave identically and the gesture stops depending on
how fast the user is.

## 40. The slideshow transition setting also applies to manual browsing — Bug

There is one transition checkbox, on the **Slideshow** tab, reading **"Use a transition between
slides"**, with a style and a duration beside it — crossfade at 350 ms by default. Everything
about where it sits and how it is worded says it governs slideshows.

It governs ordinary browsing as well. Turn it on for slideshows and every arrow key and every
`Page Down` cross-fades too, with no slideshow running. There is no second setting, nothing in
the label that hints at it, and no way to have a transition in a slideshow while browsing stays a
cut.

**It should only affect slideshow mode.** Browsing a folder and watching a slideshow are
different activities, and one setting covering both means you cannot have a transition in the
slideshow and a cut while browsing.

For context: I do not have it enabled, so this is not something I have been running into. I found
it while looking at something else. Worth noting that the duration applies as well, so at the
350 ms default a transition would sit in front of each image while browsing.

**Repro**

1. Settings ▸ Slideshow ▸ tick "Use a transition between slides", leave the style on Crossfade.
2. Close the dialog, open an image, and press `Page Down` a few times — no slideshow running.
3. Every flip cross-fades.
4. Untick it and browse again. The flips are cuts.

**Where it comes from**

The main window passes the slideshow checkbox straight into the viewer's *manual* transition
switch — `SetManualTransitionEnabled(useSlideshowTransition_)` — so the viewer treats a manual
navigation exactly like a slideshow step.

One related detail: the viewer's own default for that switch is `true`, so before any setting has
been applied it is already set to fade manual navigation. Whether that is reachable depends on
initialisation order.

---

## 41. The information panel stops keeping up while `Page Down` is held — Bug

Holding `Page Down` in the viewer navigates faster than the images can be decoded and drawn,
which is expected — the pictures cannot keep up and there is no reason they should. The top-left
information panel does not keep up either. The `N / M` count and the file name change roughly
once every second or two rather than with the key, so the only way to see where the selection has
reached is to let go of the key and wait for it to settle.

That panel is the thing being used while the key is held: the point of holding `Page Down` is
usually to get back to a known place in a long folder, and the count is what says when to stop.

**Repro**

1. Open a folder of several hundred images and open one of them in the viewer.
2. Hold `Page Down` down for five seconds or so.
3. Watch the top-left panel, not the image.

The count sits on one number for a stretch and then jumps forward by a large amount. Same in a
window and in full screen.

**Where it comes from**

Two things, both in `ViewerWindow`:

- When navigation outruns the prefetch, `PrepareForImageChange(keepDisplayedImage = true)`
  returns early, before the `RequestRepaint()` at the end of the function. That branch is the one
  taken for every navigation that has to decode, so nothing invalidates the window and the panel
  is next redrawn only when a decode happens to land.
- `WM_PAINT` is synthesised only when the message queue is empty, so an invalidate on its own
  competes with the key repeat that caused it.

The panel's file name also comes from `DisplayedImageIndex()` while the count beside it comes
from `currentIndex_`, so during a fast run the two describe different files.

---

## 42. No way to go to a file by its number — Request

The viewer's information panel counts `N / M` and the browser's status line counts the same way,
but there is nothing that takes a number and goes there. Returning to a known position in a large
folder means holding `Page Down` or `Page Up` and watching the count go past — and past the target
if the key is not released in time.

A shortcut that asks for a number and jumps to it would cover it. `Ctrl+G` is the usual key for
this in other applications, and is unused in both the browser and the viewer. Out of range could
beep and restate the range rather than silently doing nothing.

Both places would benefit: the viewer, where the count is on screen already, and the browser.

---

## 43. The viewer's `Image Information` leaves full screen, and the key it advertises does nothing — Bug

Two halves of the same thing, in the viewer.

**The menu item takes you out of full screen.** Right-click ▸ `Image Information` opens the main
window's image information dialog. From full screen, the viewer drops out of full screen when it
does: the dialog appears in front of the main window, and after closing it, `F11` is needed to get
back to where the reading started.

The viewer already displays metadata without a dialog: `Settings ▸ Viewer ▸ always show full
metadata` puts a panel in the border beside the image, and `Tab` brings it up with the rest of the
overlays. **Expected: asking for the metadata in full screen shows it and stays in full screen.**

**The key it advertises does nothing.** That same menu item is labelled `Image &Information	Ctrl+I`,
but `Ctrl+I` has no effect while the viewer has focus. The viewer's key handler does not answer to
it, and `MainWindow::TranslateAcceleratorMessage` returns early for any message belonging to the
viewer window or its children — deliberately, so the browser pane's accelerators cannot pre-empt
the viewer's own keys — so the main window's `Ctrl+I` accelerator never fires either. The menu is
naming a shortcut that is not connected to anything.

Related to item 9: `Ctrl+I` is in the shortcut catalogue for the main window but not for the
viewer, so the in-app shortcut listing does not claim it. Only the context menu does.

**Repro**

1. Open an image in the viewer and go full screen (`F11`).
2. Press `Ctrl+I`. Nothing happens.
3. Right-click ▸ `Image Information`.

The dialog opens and the viewer is no longer full screen.

**Where it comes from**

`ViewerWindow::DispatchContextMenuCommand` posts `kContextMenuImageInformation` to the owner, and
`MainWindow::ShowImageInformationForPath` opens a dialog owned by the main window. Activating it
puts the main window in front of the full-screen viewer.

---

## 44. `New Folder` only exists on the folder tree — Request

Making a folder while sorting images means leaving HyperBrowse for Explorer, or noticing that the
one place offering it is the folder tree on the left.

`New Folder...` is on the tree's right-click menu, and only there. It is not on the `File` menu,
and it is not on the right-click menu of the browsing pane where the images are — which is where
the work is happening and where the reflex is to right-click. There is no keyboard route to it at
all; `Ctrl+Shift+N` does nothing.

The tree menu also creates inside the folder that was clicked, which is the right behaviour for
that menu but means retargeting the tree first when the folder wanted is simply the one being
browsed.

**Expected:** `File ▸ New Folder...`, and `New Folder...` on the browsing pane's context menu,
both creating inside the folder currently open. `Ctrl+Shift+N` is the usual key for this
elsewhere in Windows and is unused here.

Landing the selection on the folder just made would finish it — after creating one, the next
thing is usually to drag files into it.

---

## 45. (Removed; withdrawn — the number is retired)

## 46. The shortcut key a new Quick Send favourite is given starts at the far end of the number row — Request

Quick Send favourites are given a shortcut key automatically when they are added, and the keys
are handed out from `0` upward: the first favourite gets `0`, the second `1`, and so on through
the digits and then the letters.

The keys are pressed as part of the `F7`/`F8` gesture, so the hand is already at the top right of
the keyboard when the destination popup appears. Counting from `0` puts the first favourite
added — which for me is the one used most — at the opposite end of the number row from `F8`, and
the keys nearest `F8` are the ones handed out last. Working from `F8` outward, the order under
the fingers is `7 8 9 0 - =` and then back round to `1 2 3 4 5 6`.

Two parts to this, and the first is useful without the second:

- **Hand the keys out in an order that starts near the function keys**, rather than at `0`.
- **Let the order be typed in**, as a run of keys in a settings field — `7890-=123456` — since
  which keys fall under the hand depends on the keyboard and on who is using it. Keys left out of
  the field would still want to be assignable, so that a short order does not put a ceiling on
  how many favourites can be added.

Either part only affects the key a *new* favourite is given. Changing the shortcut a favourite
already holds is what the shortcut field in the destination list is for, and re-keying existing
favourites to match a new order would move keys that are already in muscle memory.

This is separate from item 15, which is about which characters can be assigned at all rather than
about the order they are handed out in.

**Repro**

1. Start with no Quick Send favourites.
2. Add a favourite destination folder.
3. Open the destination popup with `F8`. The new favourite is listed against `0`.
4. Add five more. They are listed against `1`, `2`, `3`, `4`, `5`.

---

## 47. One viewer window per instance, so two images cannot be compared side by side — Request

Opening an image in the viewer reuses the same viewer window each time, so a HyperBrowse instance
shows one image at a time. Going back to the browser and opening a second image replaces the first
rather than adding to it.

A lot of what I do with a folder of images is comparing them: two frames of the same subject, a
before and an after, a crop against the original. Compare mode covers the case where the two are
adjacent in the folder and I want them beside each other in one window. What it does not cover is
having three or four images up at once, each in its own window, arranged where I want them on the
screen, and switching between them while the browser stays available.

What I am after is being able to open an image into a *new* viewer window rather than into the
existing one — as many as I care to have open — and to switch between them the way I would
between any other windows.

Two smaller things fall out of it, and either would be useful on its own:

- **A way to ask for a new viewer window explicitly**, so that opening an image can mean "add a
  window" rather than "replace what is in the one I have". The existing Open would keep its
  current behaviour.
- **The keys that act on the displayed file acting on the window they were pressed in.** `Del`,
  the right-click menu, `F7`/`F8` and the rest currently address "the viewer", which is
  unambiguous while there is only one.

Running a second copy of HyperBrowse gets images onto the screen together, but each copy browses
its own folder and keeps its own Quick Send destinations, so it is a different thing from having
several views onto one browsing session.

**Repro**

1. Open a folder with several images.
2. Double-click an image. The viewer opens showing it.
3. Return to the main window and double-click a different image.
4. The same viewer window is reused and now shows the second image. The first is no longer on
   screen.

---

## 48. Thumbnails decode one at a time however many workers are configured — Bug (performance)

Opening a folder that has not been thumbnailed before, the cells fill in one after another rather
than several at once, and the machine stays close to idle while they do. Raising the decode worker
count in the performance settings does not change the rate.

**Repro**

1. Open a folder of several hundred images that have not been thumbnailed before.
2. Watch the grid fill, and watch CPU use at the same time.
3. Raise the worker count in Settings and open another folder in the same state. The rate is the
   same.

**Where it comes from**

Same exception as items 36, 37 and 39 — the behaviour on its own does not point anywhere useful.

`DiskThumbnailCache::Store` takes a single process-wide mutex and holds it across all of its file
I/O. Under that lock it re-reads and rewrites the entire index for every thumbnail stored, ending
with a `MOVEFILE_WRITE_THROUGH` rename that forces a flush to disk. `TryLoad` takes the same lock
across its own read. Every decode worker therefore queues behind one lock waiting on the disk,
whatever the pool size is set to.

Measured here, 8 threads storing into a 1,000-entry cache: **59.9 stores/sec**. With the lock
scoped to the index alone and the index appended to rather than rewritten, the same measurement
gives **1,989/sec**. On the current code 8 threads are slower than 1 — 116 stores/sec
single-threaded — which is the contention showing up directly in the numbers.

A cache with real content in it costs more rather than less: at 4,167 entries the index file is
742 KB, so each stored thumbnail carries roughly 1.5 MB of serialized I/O plus a disk flush.

---

## 49. Dragging the scrollbar thumb loads no thumbnails — Bug

Dragging the browser's scrollbar thumb through a long folder shows empty cells the whole way
down. Nothing decodes until the button is released.

Scrolling by wheel, by `Page Down`, and by clicking in the scrollbar trough all load as they go.
It is only the thumb drag.

**Repro**

1. Open a folder of several hundred images.
2. Drag the scrollbar thumb slowly from the top of the folder to the bottom.
3. The cells stay empty as they pass. Release the button and the visible ones fill in.

---

## 50. (Not a bug; retired.)

---

## 51. A viewer setting changed while the viewer is closed is discarded — Bug

The viewer's overlay preferences take effect only if the viewer window happens to be open at the
moment the setting is changed. Change one with the viewer closed, then open an image: the viewer
comes up with the previous behaviour.

The settings dialog then shows the previous value as well, so there is nothing on screen to say
the change did not take.

**Repro**

1. With the viewer closed, change one of the **Settings ▸ Viewer** options.
2. Open an image in the viewer. The setting has not been applied.
3. Reopen the settings dialog. It shows the value from before the change.

**Where it comes from**

Each viewer preference is applied inside `if (viewerWindow_ && viewerWindow_->IsOpen())`, and the
viewer reads its five overlay preferences once, at construction. The dialog reads the current
value back off the live viewer, so with the viewer closed the write is skipped and the read
reports the stale one.

Related to item 14. That item is favourites held in memory and written only at exit; the same
shape reaches further than favourites do.

---

## 52. `-` and `=` cannot be typed into any text box in the main window — Bug

Neither `-` nor `=` can be entered into any edit field belonging to the main window — the toolbar
filter box, an in-place folder rename, or the shortcut assignment field. `_` and `+` behave the
same way.

The filter box is where it shows: its own cue banner advertises `rating:>=3`, and the `=` cannot
be typed. In Thumbnails mode the keystroke visibly resizes the grid instead; in Details mode
nothing happens at all.

**Repro**

1. Click into the toolbar filter box.
2. Type `rating:>=3`.
3. The `=` does not arrive. In Thumbnails mode the grid resizes as the key is pressed.

**Where it comes from**

`-` and `=` are `VK_OEM_MINUS` and `VK_OEM_PLUS`, bound unmodified to the thumbnail-size commands,
with their shifted forms `_` and `+` bound as well. Accelerators are translated before the focused
control sees the message, so all four are taken from every edit control in the window.

---

## 53. `PackageRelease.ps1` cannot find the CMake that Visual Studio ships — Bug

Packaging stops immediately on a machine whose only CMake is the one bundled with Visual Studio

or the Build Tools:

```

Failed to locate cmake.exe. Install CMake or add it to PATH.

```

CMake *is* installed, and it is the same copy the project builds with. It simply is not on PATH,

and it does not live in any of the places the script looks — it sits under the Visual Studio

install, in `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`. The Build Tools put it

there and keep it off PATH, so on a machine set up with nothing but the Build Tools — which is the

documented way to build this — the script stops before it starts.

**Repro**

1. On a machine with the Visual Studio Build Tools (which include CMake) and no separate CMake

   installation, confirm `cmake` is not on `PATH`.

2. Run `powershell -File tools\PackageRelease.ps1`.

3. It stops with `Failed to locate cmake.exe. Install CMake or add it to PATH.`

Same for `ctest.exe`, which is resolved the same way and sits in the same directory.

## 54. Panning with the arrow keys immediately after a wheel zoom jitters and snaps back — Bug

Item 39 with the keyboard instead of the mouse. The same gesture, the same timing dependence,
and the same "wait a couple of seconds and it works" tell.

Full screen, wheel-zoom in a couple of notches, then straight away press `Down` to pan into the
image. The image jitters and then returns to where the zoom left it. Pause two or three seconds
after the last wheel notch and the arrow keys pan normally.

**Repro**

1. Open an image full screen.
2. Put the pointer near the top of the picture and wheel-zoom in a couple of notches, far enough
   that the image is taller than the window.
3. **Immediately** — do not pause — press `Down` a few times. The image jitters and then jumps
   back to where the zoom left it.
4. Repeat, but wait two or three seconds after the last wheel notch before pressing `Down`. It
   pans normally.

**Where it comes from**

The same smooth-zoom timer described in item 39. Each tick eases both the scale and the pan
toward the zoom's target, and assigns the pan outright when it converges, so a pan applied from
anywhere while the animation is running is taken back 22% at a time and then discarded.

The arrow keys reach `panOffsetY_` directly:

```
panOffsetY_ += wParam == VK_UP ? panStep : -panStep;
```

with nothing that ends the animation first, so they are in the same position a drag is.

There is a second effect here that a drag does not have. The arrow keys choose between panning
and navigating to the next image by asking whether the image is currently larger than the
window:

```
case VK_UP:
case VK_DOWN:
    if (CanPanVertically()) { ...pan... } else { Navigate(...); }
```

`CanPanVertically()` measures against the scale in force at that moment, and mid-animation that
scale is still climbing. So an arrow key pressed early enough during the zoom can be answered
with "this image does not need panning" and navigate to the next image instead — a different
outcome from the one asked for, rather than a visual artefact.

Ending the animation before the key is acted on addresses both: it settles the scale and pan to
the values the zoom was heading for, which is the state waiting produces, and the pan-or-navigate
decision and the step size are then measured against the scale the user actually asked for.

## 55. A Quick Send that succeeds gives no confirmation — Request

`F7` and `F8` file the displayed image, or the browser selection, into a chosen destination. When
that works, nothing on screen says so: the image is still there for a copy, and for a move the
viewer advances to the next image, which is also what it does for an ordinary key press.

The result is that a Quick Send that worked and a Quick Send that did not register look the same
at a glance. Checking means leaving what you are doing and opening the destination folder, which
is the trip the feature exists to avoid.

**Repro**

1. Add a favourite destination and give it a shortcut key.
2. Open an image in the viewer.
3. Press `F8` and choose the destination.
4. Nothing is shown. The image is unchanged and there is no indication of where it went, or that
   anything happened.
5. Press `F8` again but dismiss the chooser with `Escape`. From the screen, the two outcomes are
   not distinguishable.

**What would help**

A short confirmation naming the file, whether it was copied or moved, and which destination it
went to — enough to read at a glance without leaving the image. In the viewer it would need to be
drawn in the window rather than shown in a dialog, since the viewer is usually full screen.

The destination's shortcut key would be worth including if it has one: the folder paths are long,
the key is what the user actually pressed, and it identifies the destination in one character.

## 56. Menu bar menus do not follow the pointer once one is open — Bug

With a menu bar menu open, moving the pointer sideways onto another title does nothing. The open
menu stays open and the title under the pointer does not react, so reaching a neighbouring menu
takes a second click: one to dismiss, one to open.

Standard Windows menu bars behave the other way round. The first click opens a menu and puts the
bar into a tracking state; from then on the menu under the pointer opens as the pointer reaches
it, and the previous one closes. That is how the menu bar in Explorer, Notepad and every common
control based application behaves, and it is what the muscle memory of scanning along a menu bar
expects.

**Repro**

1. Click **File** on the menu bar. Its menu opens.
2. Without clicking, move the pointer sideways onto **Edit**, then **View**.
3. Nothing happens: the File menu stays open and the other titles do not highlight.
4. Click **Edit**. It opens, and the same applies from there.

**Where it comes from**

The application draws its own command bar and detaches the real menu bar
(`SetMenu(hwnd_, nullptr)`), so the menu titles are owner-drawn buttons and each popup is shown
with `TrackPopupMenuEx`. That call runs its own modal message loop, which knows nothing about the
command bar it was launched from — so while a popup is up, no code is watching the other titles,
and the pointer moving over them is not seen by anything.

A real menu bar gets this behaviour from the common controls implementation. An owner-drawn one
has to provide it: while the popup is up, watch for the pointer entering a different title, end
the current menu, and open that one instead. Because `WM_TIMER` messages are dispatched by the
menu's own modal loop, a short timer started before `TrackPopupMenuEx` is enough to do the
watching without installing a `WH_MSGFILTER` hook.

## 57. A windowed viewer in a fit mode returns to the middle of the screen on every image change — Bug

With `H` (fit the window to the screen height) active and the viewer windowed, moving the window
somewhere else and then changing image puts it back where `H` originally placed it. It happens on
every image change, so a window that has been positioned deliberately cannot be kept there while
browsing.

`W` behaves the same way against its own placement: the window returns to the left edge of the
work area.

**Repro**

1. Open an image and leave full screen, so the viewer is windowed.
2. Press `H`. The window is sized to the screen height and centred horizontally.
3. Drag the window well to the left, or to a second monitor.
4. Press `Page Down`. The window jumps back to the position `H` gave it.
5. Every subsequent image change does the same.

**Where it comes from**

A window fit mode sizes the window around the image, so it is correctly re-applied when the image
changes: the next image has its own aspect ratio and the window has to follow it. The re-apply
goes through the same code as the original key press, and that code computes a POSITION as well
as a size, from the monitor work area:

```
const LONG windowLeft = mode == WindowFitMode::Height
    ? monitorInfo.rcWork.left + (workWidth - windowWidth) / 2
    : monitorInfo.rcWork.left;
```

so the position the user chose is overwritten with a monitor-derived one each time.

The two cases are different in intent. Pressing `H` or `W` is a request to place the window;
re-applying the fit to the next image is not, and there the window's existing position is the
answer. Passing `SWP_NOMOVE` on the re-apply path leaves the size behaviour exactly as it is and
stops the window moving.

---

## 58. Paging in the viewer can reach a subfolder and stop on "Unable to Open Image" — Bug

Paging through a folder of images with `Up`/`Down` (or `Page Up`/`Page Down`) can arrive at a
position that holds one of the folder's own subfolders. The viewer tries to decode it and shows
"Unable to Open Image", with the message listing the formats the build supports.

Opening a viewer is not enough to see it on its own: the list a viewer is given when it opens has
the subfolder rows removed. They arrive later, when something re-syncs the already-open viewer
against the browser's list, so the same folder can be paged through many times without it
happening.

**Repro**

1. Turn on **View > Show Subfolders in Browser**, and use a folder that contains images
   and at least one subfolder. The setting is the precondition: with it off there are no
   subfolder rows in the browser at all.
2. Open one of the images in the viewer.
3. Cause a re-sync of the open viewer. Any of these does it:
   - opening the image at step 2 from Explorer or the command line instead of from the
     browser: that path re-syncs the viewer in the same pass that opened it, so the subfolder
     is in the list straight away;
   - opening the viewer while the folder is still enumerating, so a later enumeration batch
     lands;
   - completing a file operation from the browser (a move, a copy, a rename);
   - toggling the RAW/JPEG display preference from the View menu.
4. Page through the folder with `Up`/`Down`. One position in the sequence is the subfolder, and
   it shows "Unable to Open Image". With the default sort putting folders first, it is the
   position immediately before the first image.

**Where it comes from**

`MainWindow::OpenItemsInViewer` filters `isDirectory` items out of the list before the viewer
receives it, and adjusts the selected index to match. `MainWindow::SyncViewerToBrowserModel`
builds its replacement list from `browserModel_->Items()` in `OrderedModelIndicesSnapshot()`
order and has no equivalent filter, so every row the browser shows is copied into the viewer's
list. `ViewerWindow::ReplaceItems` takes the list as given, and `ViewerWindow::Navigate` steps
through it by index and decodes whatever it lands on.

---

## 59. Batch Convert counts subfolders as images and reports one failure each — Bug

With **View > Show Subfolders in Browser** on, **File > Batch Convert > Folder** includes the
folder's subfolders in the work it does. Each one is handed to the decoder, fails, and is
counted, so the completion summary reads e.g. `Converted 12 of 15 image(s). Failures: 3.` for a
folder holding 12 images and 3 subfolders, and the warning dialog it raises on any failure is
shown. The 12 images convert correctly; it is the total and the failure count that include the
subfolders.

Batch Convert Selection does the same for a selection that includes a folder row.

**Repro**

1. Turn on **View > Show Subfolders in Browser**.
2. Open a folder that holds both images and at least one subfolder.
3. **File > Batch Convert > Folder**, pick any format and an output folder.
4. The summary counts the subfolders in its total and reports one failure per subfolder.

**Where it comes from**

`MainWindow::CollectItemsForScope` copies rows out of the browser model for both scopes without
testing `isDirectory`, and it is the starting point for the viewer, the slideshow, compare,
Batch Convert and the JPEG orientation adjust. The viewer paths drop folder rows further down
in `OpenItemsInViewer`, and `AdjustSelectedJpegOrientation` skips anything whose file type is
not JPEG, so the folder rows are harmless there. `BatchConvertService` iterates the list it is
given and calls `decode::DecodeFullImage` on each entry, which is where a folder becomes a
counted failure.

Related to item 58, which is the same folder rows reaching a different consumer.

## 60. A test run leaves a subkey inside the application's own settings key — Bug

2.1.0 moves the smoke tests off the application's settings values. They set
`HYPERBROWSE_SETTINGS_REGISTRY_PATH` to a per-process path before anything reads or writes, so a
run no longer overwrites window geometry, recent folders or favourites. That part of item 31 is
addressed.

The path chosen is `HKCU\Software\HyperBrowse\SmokeTests\<pid>`, which sits inside the
application's own key rather than beside it, and nothing removes it when the run ends. Each run
adds a subkey named after that process's id and leaves it in place. The README describes the
arrangement as "so they do not read or modify the installed application's preferences", which
holds for the preference values themselves.

**Repro**

1. Open `regedit` at `HKCU\Software\HyperBrowse` and note which subkeys are present.
2. Run `HyperBrowseTests.exe`, or `ctest`, from a build.
3. Refresh. `SmokeTests\<pid>` is there, and is still there after the process has exited.
4. Run again. A second numbered subkey is added beside the first.

**Where it comes from**

`ConfigureSmokeSettingsRegistry` in `tests/smoke.cpp` composes the path from
`Software\HyperBrowse\SmokeTests\` and `GetCurrentProcessId()`, and sets it as the environment
override; `main` calls it once at startup. There is no matching delete — neither `RegDeleteTree`
nor `RegDeleteKey` appears anywhere in `tests/smoke.cpp`.

Related to item 31.

---

## 61. The Settings dialog is a fixed pixel size and does not follow display scaling — Bug

HyperBrowse declares itself per-monitor DPI aware, so Windows hands it physical pixels and leaves
the scaling to the application. The Settings dialog is sized and drawn in fixed pixel units, so on
a display scaled above 100% it comes up at its 100% physical size while the rest of the desktop
does not. Its native child controls do scale — they take a font sized from the screen DPI — so at
150% or 200% the combo boxes and edit fields carry text sized for the display inside a dialog and
tab strip sized for 96 DPI.

Moving the dialog to a monitor with a different scale factor does not resize it either.

**Repro**

1. Set the display to 150% or 200% scaling and sign back in.
2. **View > Consolidated Settings**.
3. The dialog frame, the tab strip and the text drawn on them are at their 96-DPI size; the text
   inside the combo boxes and edit fields is at the display's size.

**Where it comes from**

`WinMain.cpp` calls `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`.
`D2DRenderer::CreateHwndRenderTarget` sets `rtProps.dpiX` and `rtProps.dpiY` to `96.0f`, and
`ResizeRenderTarget` re-applies `SetDpi(96.0f, 96.0f)` after every resize, so one DIP is one
physical pixel on every Direct2D surface. The dialog's window rect comes straight from
`kConsolidatedSettingsDialogWidth`/`Height` and `kExperimentalSettingsDialogWidth`/`Height` with no
`MulDiv(..., dpi, 96)`; the shortcut reference window, built at the same layer, does apply that
conversion. `CreateDialogUiFont`, which the child controls use, scales its point size by
`GetDeviceCaps(LOGPIXELSY)`.

A related detail in the same helper: `CreateDialogUiFont` reads that DPI from the primary
monitor's device context rather than from the window it is sizing a font for, so on a multi-monitor
setup with mixed scale factors a dialog opened on the secondary monitor takes the primary's font
size.

This one is read from the source rather than seen — the machine these notes come from runs at
100%, where the difference does not show.

---

## 62. Decode failure messages report `HRESULT 0x00000000` — Bug

2.1.0 attaches an HRESULT to every decode failure message and surfaces the text in the browser
tooltip as a `Decode detail:` line. Eight of those sites report `0x00000000` — success — for the
failure they are describing.

They share one shape: a WIC object created and then initialised inside a single condition.

```
result = factory->CreateBitmapScaler(&scaler);
if (FAILED(result) || FAILED(scaler->Initialize(...)))
{
    wic::SetError(errorMessage, L"Failed to scale the decoded image.", result);
```

`result` holds the outcome of the create. When it is the `Initialize` that fails — the call that
validates the source, the target size and the pixel format — its HRESULT is tested by `FAILED` and
then discarded, and `result` is still `S_OK`. The message reads:

```
Failed to scale the decoded image. (HRESULT 0x00000000)
```

`HResultName` returns no name for `S_OK`, so nothing follows the number to mark it as not the real
status.

The sites are four in `src/decode/ImageDecoder.cpp` — the pre-rotation scaler, the flip rotator,
the scaler and the format converter — and the same four in `src/decode/WicThumbnailDecoder.cpp`.
Separately, the `CreateDIBSection` failure path in both files reports a hardcoded `E_OUTOFMEMORY`
rather than the `GetLastError()` that is available at that point.

**Repro**

Any decode that fails at one of those four stages, rather than at open or frame-read time: hover
the thumbnail and the `Decode detail:` line carries `0x00000000`. Those stages are not
straightforward to fail on demand with an ordinary file, so this is easier to see in the source
than to provoke — the four pairs are adjacent and identical in each of the two files.

Found while reading 2c310f3, the commit that introduced the HRESULT in these messages.

---

## 63. The Settings dialog acts on `Esc`, `Enter` and `Ctrl+Tab` pressed in the viewer window — Bug

While the Settings dialog is up the main window is disabled, but a viewer window is not: it stays
open, clickable, and receiving keys. The dialog claims three keys without regard to which window
they were aimed at, so pressing them in the viewer drives the dialog instead. `Esc` in the viewer
closes the Settings dialog rather than doing what `Esc` does in the viewer; `Enter` commits the
dialog with OK; `Ctrl+Tab` pages the dialog to its next tab.

**Repro**

1. Open a folder and open an image in the viewer.
2. Return to the main window and choose **View > Consolidated Settings**.
3. Give the viewer window the focus.
4. Press `Esc`. The Settings dialog closes. `Enter` and `Ctrl+Tab` behave the same way.

Observed by posting `WM_KEYDOWN` with `VK_ESCAPE` to the viewer's own window handle while the
Settings dialog was open: the dialog's window was gone immediately afterwards.

**Where it comes from**

The dialog runs its own modal loop —

```
while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
```

— which retrieves messages for every window on the thread. It then tests
`WM_KEYDOWN`/`WM_SYSKEYDOWN` for `VK_ESCAPE`, `VK_RETURN` and `Ctrl+Tab` and acts on each without
looking at `message.hwnd`. The `EnableWindow(ownerWindow, FALSE)` above the loop disables only the
main window; the viewer is a separate top-level window and stays enabled. The `IsDialogMessageW`
call at the bottom of the same loop checks the target itself, so only the three hand-rolled cases
are affected.

---

# Part Three

Everything through item 63 has been sent. The items below were found afterwards. Same
conventions, same permanent numbering — this is a continuation of the list, not a replacement for
it.

---

## 64. Arrow keys pressed in a text box move the image selection instead of the caret — Bug

Right-click a folder in the folder tree and choose **Rename Folder...**. The tree opens an
in-place edit box on the folder's name. Pressing an arrow key does not move the caret in that box:
the image selection in the browser pane moves one item instead, and the rename box closes.

The toolbar filter box does the same thing. `Left`, `Right`, `Up`, `Down`, `Home`, `End`,
`Page Up` and `Page Down` all reach the browser pane rather than the text box that has the focus,
so the caret cannot be moved and text already typed cannot be gone back over.

Item 52 describes the same symptom for `-` and `=`. That one comes from the accelerator table;
this one comes from a hook that runs before it.

**Repro — the folder tree's rename box**

1. Open a folder with several images so the browser pane has a selection.
2. Right-click a folder in the folder tree and choose **Rename Folder...**.
3. Press `Home`, or `Left`.

The caret does not move. The image selection moves by one, and the in-place rename ends.

**Repro — the toolbar filter box**

1. Click in the toolbar filter box and type `abc`.
2. Press `Home`, or `Left`.

The caret stays where it was. The image selection moves by one.

**Where it comes from**

`MainWindow::TranslateAcceleratorMessage` offers each keyboard message to
`BrowserPane::HandleNavigationKey` before the focused control is given it:

```
if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
    && message->hwnd
    && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
    && browserPaneController_
    && browserPaneController_->HandleNavigationKey(message->message,
                                                    message->wParam,
                                                    message->lParam))
{
    return true;
}
```

`HandleNavigationKey` accepts `VK_LEFT`, `VK_RIGHT`, `VK_UP`, `VK_DOWN`, `VK_PRIOR`, `VK_NEXT`,
`VK_HOME` and `VK_END` and returns `true`, so the message is never dispatched. The window test is
`IsChild(hwnd_, message->hwnd)`, which is true for every text box the main window owns, including
the tree view's label-edit control and the toolbar filter box. The `IsTextInputControlWindow`
guard further down the same function lists `VK_BACK`, `VK_DELETE` and the `Ctrl` editing keys, but
it sits below this hook and does not list the navigation keys. In Thumbnails view
`HandleNavigationKey` also calls `SetFocus` on the browser pane, which is what ends the rename.

`TranslateAcceleratorMessage` and `BrowserPane::HandleNavigationKey` are unchanged in `master` at
011b29b.

---

## 65. `Alt+Left` and `Alt+Right` do not navigate the folder history — Bug

The shortcut reference lists `Alt+Left` as *Navigate to the previous folder* and `Alt+Right` as
*Navigate to the next folder*. In the main window neither does that. The image selection moves one
item left or right, the same as an unmodified arrow key, and the folder does not change.

**Repro**

1. Open a folder, then open a second folder, so there is a history to walk back through.
2. Give the main window the focus and select an image other than the first.
3. Press `Alt+Left`.

The selection moves one image to the left. The folder stays where it is.

**Where it comes from**

The same hook as item 64. It runs for `WM_SYSKEYDOWN` as well as `WM_KEYDOWN`, and an `Alt`
combination arrives as `WM_SYSKEYDOWN`. `HandleNavigationKey` matches on the virtual key alone and
does not look at the modifiers, so it consumes the message and returns `true` before
`TranslateAcceleratorW` is reached — and the `ID_VIEW_NAVIGATE_BACK_FOLDER` and
`ID_VIEW_NAVIGATE_FORWARD_FOLDER` accelerators the two combinations are bound to never fire.

`TranslateAcceleratorMessage` and `BrowserPane::HandleNavigationKey` are unchanged in `master` at
011b29b.

---

## 66. Keep HyperBrowse loaded in the notification area when the window is closed — Request (wishlist)

**Entirely optional, and the least important thing on this list.** Minimising the window instead
of closing it has the same effect, and I can simply do that. It is here only because closing is
what I reach for without thinking about it.

Closing the main window ends the process, so opening an image from Explorer starts HyperBrowse
from cold every time. On this machine that is about a second between the double-click and the
image being on screen, with the window flashing as it comes up.

An icon already exists in the notification area: `MainWindow::EnsureTrayIcon` puts one there to
carry the balloon `NotifyLongOperationComplete` shows when a long copy finishes, and it is removed
when the window goes. It registers a callback message, `TheTheosopher.HyperBrowse.TrayIcon`, that
`HandleMessage` does not compare against, so a click on that icon reaches nothing.

**Expected:** an option — a sub-option of **Use a single application instance**, which it depends
on — that keeps HyperBrowse loaded when the window is closed.

- Closing the main window (the **X**, `Alt+F4`, or `Esc` where that is set to close it) hides the
  window and leaves the process running, with the icon in the notification area.
- Clicking or double-clicking the icon brings the window back where it was. Right-clicking it
  offers **Open** and **Exit**.
- Starting HyperBrowse again from a shortcut, carrying no file, brings that window back rather
  than opening a second copy. A launch carrying a file already reaches the running instance while
  **Use a single application instance** is on; this is the same thing for a launch carrying
  nothing.
- `File > Exit` ends the application outright, whatever the option is set to.

The dependency is a practical one: with **Use a single application instance** off, a launch starts
a separate process rather than reaching the resident one, so a window left in the notification
area could not be brought back from a shortcut.

`MainWindow::HandleMessage`'s `WM_CLOSE` case falls through to `DefWindowProc`, and
`EnsureTrayIcon` and `NotifyLongOperationComplete` are as described above, in `master` at cee1fa1.

---

## 67. A path with a character above U+00FF is written truncated into the thumbnail cache index — Bug

The persistent thumbnail cache writes `index.tsv` and `index.journal.tsv` through
`std::wofstream` and reads them through `std::wifstream`. Neither stream is imbued with a locale,
so both use the default `"C"` locale, whose `codecvt` facet cannot represent a `wchar_t` above
U+00FF. A row for a file whose path contains such a character — Korean, Han, Cyrillic, Greek,
emoji, and most non-Latin scripts — stops at that character.

`BuildIndexLine` puts the source path into the row verbatim: `EscapeField` rewrites only `\`,
tab, newline and carriage return, and passes every other character through unchanged.

The write stops mid-row, before the terminating `L'\n'`, so the next record is appended onto the
fragment and the two become one unparseable line. `LoadIndexLocked` discards what it cannot parse,
so the entry that was being written and the one after it are both absent after a restart. Those
files are decoded again on each launch rather than being served from the persistent cache.

The same conversion failure sets `badbit` on the whole-file rewrite in `SaveIndexLocked`, which
returns `false`. `CompactIndexLocked` returns early on that `false` and does not truncate the
journal, so while such an entry is present the journal is not compacted and grows.

**Repro**

1. Create a folder whose name or contents use characters above U+00FF, for example
   `C:\Pictures\테스트\사진.jpg`, `C:\Pictures\漢字\写真.jpg` or `C:\Pictures\фото\снимок.jpg`.
2. Browse to it in Thumbnails view and let the thumbnails finish.
3. Open the cache directory and look at `index.tsv` and `index.journal.tsv` in a byte-accurate
   editor.

The row for that file ends at the first character above U+00FF and has no trailing newline; the
following record continues on the same line.

4. Restart HyperBrowse and browse to the folder again.

The thumbnails are decoded from the image files rather than read back from the cache.

**Where it comes from**

Five stream sites, all in `src/cache/DiskThumbnailCache.cpp`:

```
AppendJournalRecordLocked   std::wofstream stream(journalPath, std::ios::app);
CompactIndexLocked          std::wofstream stream(journalPath, std::ios::trunc);
SaveIndexLocked             std::wofstream stream(temporaryPath, std::ios::trunc);
LoadIndexLocked             std::wifstream stream(indexPath);
LoadIndexLocked             std::wifstream journalStream(journalPath);
```

A pure-ASCII cache directory is unaffected: those rows convert one byte per character and are
written whole.

All five are as described in `master` at cee1fa1.

---

## 68. A thumbnail that has finished decoding is discarded if the viewport moved while it decoded — Bug

`ThumbnailScheduler::WorkerLoop` marks a completed job cancelled when its request epoch is no
longer the active one, and a cancelled job is neither inserted into the memory cache nor queued
for the persistent cache:

```
const bool stillRelevant = jobs[index].requestEpoch == activeRequestEpoch_
    || requestedKeys_.contains(jobs[index].workItem.cacheKey);
cancelled[index] = cancelled[index] || !stillRelevant;

if (thumbnail && !cancelled[index])
{
    cache_.Insert(jobs[index].workItem.cacheKey, thumbnail);
}
...
if (thumbnail && !cancelled[index] && allowDiskCacheStore)
{
    EnqueueDiskStore(jobs[index].workItem.cacheKey, thumbnail);
}
```

`Schedule` advances `activeRequestEpoch_` and repopulates `requestedKeys_` on every viewport
change, so a decode that is in flight across a scroll completes with a stale epoch. The decode has
already run to completion at that point; the bitmap is then released rather than stored, in memory
or on disk, so the same file is decoded again when those rows are next shown.

**Repro**

1. Open a folder of images large enough that decoding is visible, in Thumbnails view.
2. Scroll down several screens while thumbnails are still filling in, then scroll back up.

Rows that had finished decoding before the scroll are blank again and decode a second time.

**Note**

Before 36b846d the insert was unconditional for any decoded thumbnail, and the epoch was consulted
only to decide whether to post the ready notification to the UI.

`WorkerLoop` is as quoted in `master` at cee1fa1.

---

## 69. WebP files are not listed or opened — Request (wishlist)

`.webp` files do not appear in the browser pane, and opening one from Explorer or from the
command line does nothing.

`kSupportedFileTypes` in `src/decode/ImageDecoder.cpp` is the set of extensions the application
admits, and `webp` is not among them:

```
constexpr std::array<hyperbrowse::decode::SupportedFileType, 14> kSupportedFileTypes = {{
    {L"jpg", L"JPEG image", false},
    {L"jpeg", L"JPEG image", false},
    {L"png", L"PNG image", false},
    {L"gif", L"GIF image", false},
    {L"tif", L"TIFF image", false},
    {L"tiff", L"TIFF image", false},
    ...
```

`IsWicFileType` and `IsRawFileType` read that array, `browser::IsSupportedImageExtension` calls
both, and `FolderEnumerationService` uses it to decide which entries to enumerate, so a `.webp`
file is filtered out before it reaches the list. `MainWindow::ApplyStartupLaunchPathOverride`,
which handles a path passed on the command line, and `MainWindow::OpenViewerAtPath`, which opens
a file into a viewer, apply the same check and return without opening anything.

**Repro**

1. Put a `.webp` file in a folder alongside a `.jpg`.
2. Open that folder in HyperBrowse. Only the `.jpg` is listed.
3. With HyperBrowse set as the default application for `.webp`, double-click the `.webp` in
   Explorer. Nothing opens.

**Expected:** `.webp` files are listed, thumbnailed and opened like the other non-RAW formats.

**Note**

Windows decodes WebP through WIC when the Store package **Webp Image Extension**
(`Microsoft.WebpImageExtension`) is installed. On this machine it is, and
`IWICImagingFactory::CreateDecoderFromFilename` returns a decoder named "Microsoft Webp Decoder"
for a `.webp` file, so the existing generic WIC path used by the other non-RAW entries decodes
one as it stands.

The other formats that Windows decodes through the same Store extensions — HEIC/HEIF, AVIF and
JPEG XL — are likewise not in the table.

`kSupportedFileTypes` is as quoted in `master` at cee1fa1.

Rechecked 2026-09-09 with an already-running HyperBrowse: double-clicking a `.webp` in Explorer
activates HyperBrowse but leaves the browser showing the folder it was already in. No viewer opens
for the selected file, so the visible result is the remembered folder rather than the requested
image.

---

## 70. The viewer blinks repeatedly after an image is opened from Explorer — Bug

Found 2026-09-09.

Starting HyperBrowse by double-clicking a supported image in Explorer opens the requested image,
but the viewer then repeatedly blanks and redraws it during the first few seconds. The title bar
continues to identify the requested file while the image itself flashes out and back.

In one run with a PNG, the debug log recorded eight release/recreate/upload cycles after the image
had opened. The cycles were spaced about 400 ms apart.

**Repro**

1. Close HyperBrowse.
2. In Explorer, double-click a supported `.png` or `.jpg`.
3. Watch the viewer for the first four seconds after the image appears.

The requested image opens, then blinks out and returns repeatedly before settling. This is separate
from item 69: a WebP does not reach the viewer at all, while a supported file reaches the viewer
and then flashes.

**Direction:** the timing matches the display-change recovery path in `MainWindow`. A display
change starts a short series of recovery timer callbacks. If the viewer is created while that
series is still active, the later callbacks also reach the new viewer and its graphics surface is
released, recreated and uploaded again on each callback.

---

## 71. Drag-over handling retains a COM data-object pointer without owning a reference — Bug

Found in HyperBrowse 2.2.0 on 2026-09-09.

`ExternalDropTarget::DragEnter` saves the incoming `IDataObject` pointer for use by later
`DragOver` calls. The saved pointer is kept after `DragEnter` returns, but HyperBrowse does not
acquire its own COM reference. `DragOver` can therefore call the application's drag callback with
a pointer whose lifetime HyperBrowse does not control.

This is a lifetime error at the external drag-and-drop boundary. If the source releases its
call-scoped reference after `DragEnter`, a subsequent pointer movement can dereference an invalid
COM object. The visible result can range from incorrect drop feedback to a crash during an
external file drag.

**Relevant area:** `src/ui/ExternalDropTarget.h` and `src/ui/ExternalDropTarget.cpp`, specifically
the data object saved by `DragEnter`, consumed by `InvokeDragOver`, and cleared by `DragLeave` or
`Drop`.

**Expected:** the data object used throughout a drag remains valid until the drag leaves the
window or completes.

---

## 72. Clipboard file-transfer failures can leak the preferred-effect allocation — Bug

Found in HyperBrowse 2.2.0 on 2026-09-09.

`WriteClipboardFilePaths` allocates one global-memory block for `CF_HDROP` and a second block for
the preferred copy-or-move effect. If `EmptyClipboard` fails, or if publishing the `CF_HDROP`
block fails, the function frees the file-list block but leaves the preferred-effect block
allocated.

The leak occurs on clipboard error paths, so repeated copy or cut attempts while another process
or clipboard listener is interfering can accumulate unreleased global-memory allocations in the
HyperBrowse process.

**Relevant area:** `src/ui/ClipboardFileTransfer.cpp`, in the cleanup paths of
`WriteClipboardFilePaths` after the two clipboard memory blocks have been allocated.

**Expected:** a failed clipboard write releases every allocation whose ownership was not
successfully transferred to the clipboard.
