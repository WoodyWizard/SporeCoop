---
id: BUG-1397
title: saved_box_twice: the second save is refused with the box reason (the mod sees the closed box up to 0.2 s after the UI), the test wants only the still-finishing reason
status: open
kind: test
stage: All
reported: 2026-11-21
source: final check 20261121-003202
role: host
versions: 0.7.16
fixed_in:
branch: fix/w62-saves
commits:
tests: saved_box_twice, saved_box_twice_off
signature: saved_box_twice: FAIL the second save is refused for now with its reason (waiting for the previous save's "game saved" box to close)
docs: docs/re/creature.md#creature-save-dropped
related: BUG-1365, BUG-0846
---

## Report
Third final check of 0.7.16 (lab 20261121-003202, lane 3, and the new-test run 2-new.txt): the new BUG-1365 case
`coop_saved_box.py --case twice` failed one check: `second save 0.02 s after the box closed: waiting for the previous
save's "game saved" box to close; saved_box=1 hold=0 may_start=0 too_early=0`, then `FAIL the second save is refused for
now with its reason (...)`. Every other check passed: the save was taken 1.61 s after the box closed and written.

## Root cause
* Observed (the test's own line): 0.02 s after the test saw the box's window gone (`uifind`), the mod's flag of the
  box (world.cpp `g_saved_box_visible`, read by `save_game` for BUG-0846) was still set: the mod looks at the box every
  0.2 s outside the frame (`box_tick` / `box_task`), so it learns the close up to ~0.2-0.4 s after the window is gone.
  `save_game` checks that flag before the BUG-1365 test (`save_may_start`), so the refusal named the older reason. Both
  are "not now" refusals and the retry was taken at 1.61 s (inside the 5 s), as the BUG-1365 fix wants.
* A bug of the test, not of the mod and not a regression: the check asked for one reason where the order of the two
  "not now" checks in `save_game` gives the other in the first 0.2 s. `may_start=0` in the same line shows the
  BUG-1365 condition was also true (the game would have dropped the save).

## Fix
Test only (coop_saved_box.py case_twice): after the box's window closed, the test waits until the mod's own view of
the box is closed too (`coop.save box` answers `saved_box=0`, set in the same look that clears the flag `save_game`
reads, at most 5 s), then asks for the second save at once (still inside the game's 1.5 s). The check of the reason is
unchanged (it must be the BUG-1365 one), so it now tests what it was meant to test. The twin `saved_box_twice_off`
(`SPORE_FIX_OFF=world.save_allowed`) is unchanged: the second save says "saved" and nothing is written.

## Verification
Open, waiting for the lab run (no lab runs today, owner rule). FAIL run of the old check: 20261121-003202
(saved_box_twice). Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run saved_box_twice saved_box_twice_off --lane N
--retry 0 --no-build`: saved_box_twice must PASS, saved_box_twice_off must FAIL ("saved" and nothing written).

## History
- 2026-11-21: opened (fix/w62-saves): cause from the test's log line, test waits for the mod's view of the box.
