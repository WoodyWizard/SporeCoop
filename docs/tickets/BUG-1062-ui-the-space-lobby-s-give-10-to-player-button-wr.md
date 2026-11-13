---
id: BUG-1062
title: UI: the Space lobby's "Give 10 to <player>" button wraps its caption on two lines (even at 170 px)
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: fix/w49-space key frame, lab 20261113-015206
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-give-caption-wrap
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
