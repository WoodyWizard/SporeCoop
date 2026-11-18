---
id: BUG-9116
title: the editor depth cube pushes along the line from the spine, not straight in and out, and keeps one pixel size at every zoom
status: fixed
kind: bug
stage: Editor
reported: 2026-11-11
source: owner report on 0.7.8-lineages46-dev
role: all
versions: 0.7.8-lineages46-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: QA single instance, Creature editor (Samuth's creature), coop.depth nudge on every depth part, see Verification
signature: depth move not along the skin normal
docs: docs/re/creations.md#creations-depth-skin-normal
related: BUG-9103
switch: editor.depth_normal editor.depth_size
---

## Report
"The square nudge tool pulls and pushes from the trajectory between the object's origin and the square
itself. It should be pulling straight in or out. Also the size of the square isn't scaled properly with
the other arrow / ball tools."

## Root cause
* Direction: the cube (BUG-9103) moved a part along the line from the nearest spine segment through the
  part, and sat on that line. Where the skin is not round about the spine that line is slanted: a side
  detail's line was about 40 degrees off the skin's normal, a detail on the back moved straight along
  the body (0, 1, 0) instead of up and back out of it.
* Size: the cube was 9 pixels at every zoom; Spore's handle balls and arrows are objects in the scene
  and grow or shrink with the editor's zoom.

## Fix
* `editor.depth_normal`: the handle's line is the skin normal of the part's attach frame: EditorRigblock
  +0xA8 (3x3 rotation by rows), its second row negated (docs/re/creations.md#creations-depth-skin-normal).
  The limits are measured along that line from where the part first sat (out: as before; in: the
  inward room grows by 1/cos of the angle to the spine's line, so it still stops short of the spine).
  The mirrored twin moves along the mirrored normal.
* `editor.depth_size`: the cube has a world half size (0.028 units), at least 5 pixels on screen; the
  click area follows the drawn size.

## Verification
QA Steam copy, one instance, the Creature editor opened from the galaxy menu (`coop.depth open
291D2375`), `coop.depth nudge <part> 0.02` and back on all 9 depth parts: each moved 0.0200 along its
skin normal, 0.00000 sideways; 0 faults. The old line for comparison (printed as "spine line"): side
detail 0.775 -0.632 0.013 against the normal 0.914 -0.117 0.389, back detail 0 1 0 against 0 0.504 0.864.
Not checked on screen: the editor's 3D view did not render in the background QA window (blank, its
camera returned one screen point), so the cube's look and its size against the balls need the owner's
eye; 0.028 is a first value to tune.

## History
- 2026-11-11: reported and fixed on feat/lineages-47.
