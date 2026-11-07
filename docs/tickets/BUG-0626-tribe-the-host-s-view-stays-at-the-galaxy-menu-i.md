---
id: BUG-0626
title: Tribe: the host's view stays at the galaxy menu intro's camera after a quick load (minimap and clicks do nothing; play_tribe host minimap calibration)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: lab rel/0.7.9 20261106-105201 play_tribe; also fix/w45-village-ground 20261106-050921, fix/w43-transitions 20261104-225940
role: host
versions: 0.7.8, 0.7.9
fixed_in: 0.7.9
branch: fix/w45-int3
commits: 38b9771c, 30b2aa04, 10e66552
tests: tribe_intro_camera, play_tribe
signature:
docs: docs/re/tribe.md#tribe-intro-camera
related: BUG-0607, BUG-0586, BUG-0481
---

## Report
rel/0.7.9 integration run 20261106-105201, play_tribe: `FAIL host: the minimap takes the view where it points`
(`host: minimap calibration: o (0.0, -471.29, 185.84) a (...) b (...) (busy=0, game time paused=False)`: the view
did not move on three minimap clicks) and `FAIL host: gave orders through real clicks (0)`. The same signature
(the same stuck point) in fix/w45-village-ground 20261106-050921 and fix/w43-transitions 20261104-225940.

## Root cause
Observed (72 play_tribe host logs, docs/re/tribe.md#tribe-intro-camera): the galaxy menu's intro "GGE_intro"
starts ~16 s after the menu came up even though the lab's quick load left the menu after 4-5 s, so it starts in
the Tribe stage, during the share save for the joining guest. The 3 failures are all among the 12 runs where the
intro was ended ("still playing in a stage: ended it") before the "game saved" box was confirmed; none of the 48
runs with the other order failed. An earlier fix that ended the intro in the same PlayCinematic call still had the
stuck view when the intro came 0.05 s before the box (lab 20261106-124414); not starting it at all, with the same
timing, did not (lab 20261106-125505 #1). Inferred: the box, open while the intro owns the camera, hands that
camera back on its OK. Not a 0.7.9 regression (first seen 2026-11-04, 0.7.8 code). Related to BUG-0607 (a Creature
guest's view from space after the same intro was ended in the stage).

## Fix
`world.intro_at_once` (world.cpp, 38b9771c then 30b2aa04): a hook on cCinematicManager::PlayCinematic does not
start "GGE_intro" outside the galaxy menu mode (its only caller ignores the result). `coop.status` counts "menu
intro ended N (not started M)". Off: the intro plays and menu_intro_tick ends it after 1 s (0.7.8).

## Verification
FAIL (0.7.8 behaviour, the switch's off side; the switch cannot be off before a cold start's intro, it is applied
after the join): play_tribe lab 20261106-105201, fix/w45-village-ground 20261106-050921, fix/w43-transitions
20261104-225940 (3 of 12 runs with the intro ended before the box was confirmed), tribe_intro_camera 20261106-124414
(the first fix: ended in the same call, stuck). PASS: tribe_intro_camera 20261106-124712, 20261106-124833 (2),
20261106-125051 (2), 20261106-125505 (2): the start's own intro "not started" (125505 #1 with the timing of 124414),
the view follows the minimap. The by-hand intro of the test with the switch off did not reproduce the stuck view
(125051, 125505), so the test makes no switch-off claim. play_tribe after the fix: lab 20261106-125914 (#1 cold: the start's intro "not started", both
minimaps OK; #2 warm OK), 20261106-132710 (cold on lane 2, intro not started at 1 s into the session, OK).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-int3): fixed (world.intro_at_once); BUG-0607 is likely the same intro on a Creature guest (not re-tested).
- 2026-11-07: released in 0.7.9.
