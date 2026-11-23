---
id: BUG-1389
title: new_stage_game: a chooser that opens after the 150-frame check takes the next click on slot 818 669 as the Space icon (798 646), a Space wizard opens (cell_new_game)
status: fixed
kind: test
stage: Menu
reported: 2026-11-21
source: lab rel/0.7.16 20261121-003202 cell_new_game
role: 
versions: unreleased
fixed_in: 0.7.16
branch: fix/w62-flake-cc
commits: 372de2c3
tests: cell_new_game, cell_far_guest, join_new_game
signature:
docs:
related: BUG-1385
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 6, cold): cell_new_game FAIL "the host started a new cell game
(54 s)", new_stage_game.py: "planet slot at 818 669 / planet slot 0 did not open the chooser: slot 1 / FAILED: the stage
chooser did not open". The same slot (818 669) failed in feat/w51-retire 20261115-034753 (cell_new_game, BUG-1385's
report). Known flake: 3 of 8 failed on rel/main, 16 of 41 on all branches; the other two recent FAIL runs failed later
(the meat check), not here.

## Root cause
Observed: host log: galaxy menu at 03:34:31.6, `world: the galaxy menu intro starts` at 03:34:47.45 (15.85 s, BUG-0607);
the test log ends at 03:35:17.6, which fits BUG-1385's wait for the intro (~10 s of intro) plus six tries of 165 frames
each AFTER the intro. So BUG-1385's wait did its job and the clicks came on a settled menu. The failure screenshot
(out/lane6/shots/cell_new_game_wizard.png on the rel/0.7.16 worktree, taken at the FAIL) shows no galaxy menu: the
planet ring sits at the screen's center with the Space icon (rocket) and the wizard page "Choose or Create Your
Creature": a Space wizard was open. The stage chooser ring opens around the screen's center; its Space icon is at
798 646 (STAGE_ICON["space"]), 20 px left and 23 px up of slot 0 at 818 669. Inferred: one click opened the chooser
later than the 150 frames new_stage_game waited before `chooser_open`; the next try clicked the slot point again,
which now is the open ring's Space icon, and the Space wizard replaced the chooser, so every later check (slot 0 and 1)
failed. A harness fault, old (before 0.7.16); BUG-1385 did not make it worse (it only moved the clicks after the
intro). Other slot positions survive a late chooser because a second click there hits no icon.

## Fix
new_stage_game.py `wait_chooser`: after a click, wait 150 frames as before, then poll `chooser_open` every 30 frames up
to 600 frames before the slot is clicked again. A chooser that opens late is now seen before any second click.

## Verification
no-fail-run: harness-only change; the late chooser cannot be forced on purpose. Lab: `SPORE_DETECTOR=20 python3
tools/lab.py run cell_new_game,cell_far_guest,join_new_game --lane N --retry 0 --no-build` PASS. Open, waiting for the
lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_new_game, cell_far_guest and join_new_game PASS in 20261122-194210 and 20261123-023643 (FAIL before: 20261121-003202 cell_new_game, "planet slot 0 did not open the chooser"). Closed.

## History
- 2026-11-21: opened from the third final check of 0.7.16 (fix/w62-flake-cc); fixed in the test.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
