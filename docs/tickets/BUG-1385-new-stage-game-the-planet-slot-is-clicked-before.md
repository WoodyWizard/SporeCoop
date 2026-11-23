---
id: BUG-1385
title: new_stage_game: the planet slot is clicked before the galaxy menu intro (it starts ~16 s after the menu), the stage chooser never opens (cell_far_guest)
status: fixed
kind: test
stage: Menu
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 cell_far_guest
role: 
versions: unreleased
fixed_in: unreleased
branch: fix/w61-cell
commits: 074bc674
tests: cell_far_guest, cell_new_game, join_new_game
signature:
docs:
related: BUG-0607, BUG-0626
---

## Report
0.7.16 second final check (lab 20261120-133703, lane 2, cold): cell_far_guest FAIL "the host started a new cell game
(46 s)", new_stage_game.py: "planet slot at 139 433 / FAILED: the stage chooser did not open". The same line in
fix/w13-cell 20261030-140816, fix/w9-cell-food 20261030-003607 (cell_far_guest) and feat/w51-retire 20261115-034753
(cell_new_game), slot 818 669.

## Root cause
Observed (host log): galaxy menu at 18:11:53.68, `world: the galaxy menu intro starts` at 18:12:09.53 (15.85 s later,
as BUG-0607 measured), the log ends there. new_stage_game.py waits for GalaxyGameEntry and 600 frames (~10 s), finds slot
0 and clicks 5 times: the clicks came before and during the intro (the intro moves the camera and hides the menu for
~10 s, docs/06-testing.md / coop_save_areas.py). Inferred: no click on a settled menu was ever made; the slot at the left
edge (139 433) may also sit under the menu's panel. A harness fault.

## Fix
new_stage_game.py: `wait_menu_intro` after the 600 frames: until `coop.status` showed "(menu intro)" and it ended, or 20
s after the menu with none playing (max 90 s). With --slot N, a slot that did not open the chooser three times gives way
to slot N+1; 6 tries.

## Verification
no-fail-run: harness-only change. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_far_guest,cell_new_game,join_new_game
--lane N --retry 0 --no-build` PASS. Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_far_guest, cell_new_game and join_new_game PASS in 20261122-194210 and 20261123-023643 (cell_far_guest and join_new_game also 20261121-003202). Closed.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the test on fix/w61-cell.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
