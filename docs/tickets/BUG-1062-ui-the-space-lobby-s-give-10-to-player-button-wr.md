---
id: BUG-1062
title: UI: the Space lobby's "Give 10 to <player>" button wraps its caption on two lines (even at 170 px)
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: fix/w49-space key frame, lab 20261113-015206
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-space2
commits: daac928c, 98058c5c
tests: space_give_pick
signature:
docs: docs/08-ui.md#ui-give-fit, docs/known-issues.md#issue-give-caption-wrap
related: BUG-1050
---

## Report
Seen in the key frames of space_give_pick (fix/w49-space, labs 20261113-014859-2 and 20261113-015206): the co-op
window's Space row shows "Give 10 to / Carol" on two lines, at the old width (570..720, 150 px) and at the new one
(520..690, 170 px, BUG-1050). The caption is cut by the button's frame at the top and bottom. Cosmetic, also with 2
players ("Give 10 to Alice").

## Root cause
Not analysed. Inferred: the sui button wraps its caption by a width other than the button's (a fixed text box, or
the font's width estimate of coop_ui fit()); "Give to <name>" in Tribe / Civ may do the same with long names.
Reproduce: SPORE_LANE=2 python3 tools/tests/coop_space_give_pick.py, look at frames/space_give_pick/*_guest.jpg.
Observed at runtime (fix/w49-space2, lane 3): the button's text borders (`coop.ui btn`, IButton vt +0x50) are
23 11 23 11: a caption wraps when its advance sum is over the button's width minus 46, so 150 / 170 px buttons fit at
most 104 / 124 px of text ("Give 10 to Carol" is 147, "Give 10 to Alice" 145). Not the font estimate: the glyph table
matches the ink.

## Fix
coop_ui.cpp refresh: the give button is as wide as its caption needs (caption_width + 48, at least its old width),
growing to the left from its right edge (720, or 690 left of Space's ">"); Tribe / Civ's ">" moves with it and Space's
jump button ends 5 px left of it (also sized to its caption). Switch `coop.fix core.ui_give_fit off` (the fixed
widths, 0.7.13).

## Verification
`coop_space_give_pick.py` (suite `space_give_pick`, a party of 3 in Space): the give row's buttons must have room
for their captions with the button's real text borders (23 px a side, `coop.ui btn`: width - 46) and pass
layout_problems; the key frame shows the caption on one line.
* FAIL (SPORE_FIX_OFF=core.ui_give_fit): out/lab/20261113-024426 ("Give 10 to Carol" needs 147 px, has 124), and
  out/lab/20261113-023651 (the first check, width - 30: has 140)
* PASS: out/lab/20261113-024531-2 (key frame: "Give 10 to Carol" on one line), out/lab/20261113-025113
* The first fix (+34) passed the old check but still wrapped in its key frame (out/lab/20261113-023651-2): the
  borders were measured then (docs/08-ui.md#ui-give-fit; the lenient check is BUG-1065).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space2 (the give row sized to its captions with the real 23 px text borders).
- 2026-11-14: released in 0.7.14.
