---
id: BUG-0551
title: Creature: a chat line typed while the host is paused saving the world for a returning guest is lost (the mod takes the typing, the line stays empty)
status: open
kind: bug
stage: Creature
reported: 2026-11-04
source: final check out/lab/20261104-135838 play_creature
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0482
---

## Report
play_creature in the 0.7.6 final check (out/lab/20261104-135838): `the chat line takes the typing while the creature lives (1 lines lost; while dying: 0)`. The host's line at 14:21:33 was lost while the host was paused (`busy=paused`) saving and sharing the world for the guest that came back from the main menu (14:21:26 to 14:21:30); the host log has `a click on a co-op text line while the game's input is locked (a caption): the mod takes the typing` twice, the line stayed empty. Reproduce: play_creature, a chat line typed by a bot during the share save after the guest's exit_to_menu.

## Root cause
Not analysed yet. Inferred: the share save's pause and its "game saved" box (confirmed by the mod at 14:21:28) end while the mod takes the typing; the line is reset when the box closes or the game resumes. Same class as BUG-0482 (caption, modal box).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): G1: read coop_ui.cpp's locked-edit path (locked_edit_tick / locked_edit_apply); not reproduced, not changed.
