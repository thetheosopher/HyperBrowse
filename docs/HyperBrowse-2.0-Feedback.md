# HyperBrowse 2.0 — Feedback

**From:** Doug
**Version:** 2.0

---

HyperBrowse is my daily driver now. I've switched off IrfanView completely. Thanks for turning around so much of the last list so fast.

**No rush on any of this.** None of it is blocking me. I'm filing it because I'm using the app every day and noticing things, not because anything is broken enough to slow me down.

Things that landed well: the lightning bolt icon fits — the app really is that fast. `H` for fit-to-height is great. Wheel zoom anchoring at the cursor is exactly right. File associations register correctly; all fourteen types show up properly under one app entry in Windows Settings.

---

## 1. Settings dialog — this is the big one

The settings surface has outgrown the menu. Two problems stack:

- Clicking a checkbox toggles it and closes the menu, so setting several options means reopening the menu each time.
- There's no single place to see what's configurable. Settings I don't already know about are effectively invisible.

The ask is a real settings dialog with tabbed groups. You already have the pattern — the slideshow settings dialog (`Ctrl+Shift+T`) is exactly this, just narrower. Folding that in as a tab would give one place instead of two.

This also solves half of item 9. A settings dialog documents itself.

---

## 2. Arrow-key panning is inverted on both axes

Thanks for adding keyboard panning — it's something I use a lot. But the direction is content-drag style: `Up` moves the image down, as if I'd grabbed and pulled it. Same on the horizontal.

IrfanView moves the viewport instead, so `Down` scrolls down through the image. That's also what scrollbars, `Page Up`/`Page Down`, and the wheel all do, so viewport-style is the Windows convention, so the keys feel backwards to me.

So the ask is to either flip the default or make it a setting — I don't care which. 😊

---

## 3. `H` does more than it should

One rule: `H` should only change zoom/fit mode. It shouldn't touch fullscreen state or window geometry.

Two violations:

- **In fullscreen:** `H` always drops me out of fullscreen, zoomed or not. It should snap to fit-height and leave me in fullscreen.
- **In windowed:** `H` leaves the existing zoom applied, and the window itself gets smaller and shifts left. It should reset to fit-height at actual scale and not touch the window.

---

## 4. `File > Clear All Favorite Destinations` has no confirmation

It just does it. One misclick wipes everything and it's a lot of manual work to rebuild.

The ask is a confirmation prompt — ideally with the count, like "Remove all 12 favorite destinations?" so it's clear what's about to go.

---

## 5. `F7`/`F8` should remember the last destination

The dialog highlights the top row every time. When I'm filing a run of images into the same folder, every operation costs the same as the first.

Pre-select the last destination used, so repeat operations are `F8`, `Enter`. This is how IrfanView does it. (And I should have spelled this out up front before.)

Per-session is enough — no need to persist across restarts, though it's fine if it does. Whichever is less work.

---

## 6. "Quick Send" vs "Favorite Destinations"

The pane on the right says Quick Send. The menus and dialogs say Favorite Destinations. Same feature, two names — it's confusing. The ask is to pick one and use it everywhere. No preference which.

---

## 7. Favorite toggle on the folder tree right-click menu

`File > Add Current Folder to Favorite Destinations` works, but it's several clicks and it acts on the current folder rather than the one I'm pointing at.

The ask is to put it on the tree context menu so I can right-click any folder and favorite it without navigating there first. Should read "Remove from Favorite Destinations" when the folder is already a favorite.

---

## 8. `Ctrl+I` dialog doesn't follow the theme

The image information dialog renders in light mode regardless of theme. This is minor, though.

---

## 9. Keyboard shortcuts still aren't in the README

Repeat from last round, and it's getting worse as you add keys. `H` is new in 2.0 and is not documented in the readme. I had Claude check the source. 😊

The ask is for a shortcut table in the README, browser and viewer sections. Every undocumented key is a feature no new user will ever find.

---

## 10. Single-instance handling for shell activations

Double-clicking an image in Explorer opens a second HyperBrowse even when one is already up on that folder. Now I have two windows on the same directory.

The ask is for the file to get routed to the running instance instead — focus it, navigate to the image.

Probably worth always reusing the existing instance rather than only when the folders match, since a sometimes-reuses rule would be hard to predict. If someone wants two windows deliberately, a File > New Window item would cover that.

Minor, not a blocker.

---

## 11. Wishlist: "Copy Prompt" button in the details pane

Only if you run out of things to do. 🙂

The file details pane is great for AI-generated images. ComfyUI writes generation data into a `parameters` key in the image metadata with the prompt nested under it. The ask is that if that's detected, a "Copy Prompt" button appears at the top of the pane and puts the prompt on the clipboard — it'd save me a lot of squinting and selecting.

Button only appears when a prompt is present, so it costs nothing for regular photos.

Metadata layout isn't standardized — A1111 stuffs a text blob into the PNG `parameters` chunk, ComfyUI writes JSON into `prompt` and `workflow` chunks. You're already parsing this stuff so you'll know what you're actually getting. Whatever subset is easy is fine.

If the full workflow JSON is there too, a second button for that would be handy — that's what I'd paste back into ComfyUI to reproduce an image. Only if it falls out for free.