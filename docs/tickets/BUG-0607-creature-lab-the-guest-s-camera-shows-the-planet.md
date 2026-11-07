---
id: BUG-0607
title: Creature (lab): the guest's camera shows the planet from space after the join (the intro cinematic 8915F9BB ended by the mod in a stage)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-06
source: lab, fix/w45-creature-reports (creature_ufo_mirror shots)
role: guest
versions: 0.7.8
fixed_in: 0.7.9
branch: fix/w45-intro-camera (fix: fix/w45-int3, world.intro_at_once)
commits: 38b9771c, 30b2aa04 (the fix, BUG-0626), 35d60ac6, c4bd21dd (test, log, docs)
tests: creature_intro_camera
signature:
docs: docs/re/creature.md#creature-intro-camera, docs/re/tribe.md#tribe-intro-camera
related: BUG-0626
---

## Report
Seen in the lab (creature fixture, lane 6) while testing BUG-0558: two of the guest's screenshots taken 0.5-5 s
after its join (out/lab/20261105-230837's run family: 20261105-231804, 20261105-233051, out/lane6/shots/ufo_mirror_guest)
show the planet from space (the planet in the middle of a star field, the Creature HUD on), the others at the same
moment the creature at its nest. The guest's log of those runs: `world: cinematic 8915F9BB still playing in a stage:
ended it`; in the runs with a normal view: `... still playing at the load: ended it`.

## Root cause
Observed (docs/re/creature.md#creature-intro-camera): the galaxy menu's intro "GGE_intro" starts 15.8-15.9 s after
the guest's menu came up. A load requested 0.2-0.7 s before that holds the frame (~0.65 s); the intro then starts in
the next frame, already in the Loading mode 01654C08, plays through the Creature setup and is ended by the mod 1 s
later; the guest's camera (App::cViewer) stays at a default point (0, -1000, 500), ~800 m from its avatar, for the
session (the planet from space). Both reported runs had this timing (load requests 15.8 and 15.77 s after the menu,
the menu left 16.4 s after it came up). Reproduced in lab 20261106-141833 #5 with world.intro_at_once off (0.7.8
code). An intro that starts after the Creature setup (by hand, or 0.19 s into Creature) does not leave it stuck.
Inferred: the creature camera is set up while the intro owns the camera and is not given its target when the intro
ends. No intro was seen starting in the menu mode between the request and the mode change (16 timed joins), so the
mode check alone covers it.

## Fix
BUG-0626's `world.intro_at_once` (fix/w45-int3, world.cpp): the hook on cCinematicManager::PlayCinematic does not
start "GGE_intro" outside the galaxy menu mode, the Loading mode included. Nothing new in the game code here: the
intro start now logs its mode (`world: the galaxy menu intro starts (mode ...)`), and twoinst.start(join_at=) times a
guest's join from its menu's log line (SPORE_FIX_OFF goes to the guest before the join).

## Verification
Test creature_intro_camera (the guest joins 12.5 s after its menu; ~1 in 3 starts hits the Loading-mode race;
asserts the game's camera within 80 m of the guest's avatar after the join, then twice after an intro started by hand
in Creature). FAIL (SPORE_FIX_OFF=world.intro_at_once on the guest before its join): lab 20261106-141833 #5 (intro in
mode 01654C08, camera 808.8 m from the avatar after the join and 1357 m after each by-hand intro; 5 of 6 starts missed
the race). PASS: lab 20261106-140930 #1, #3 (the same race, the intro "not started" in mode 01654C08, camera 10.2 m),
20261106-142441 (6 of 6, the final code; #1 blocked at the Creature entry), 20261106-140508 (4 of 4).
Regression: smoke 20261106-143141 (14/14), play_creature (SPORE_DETECTOR=20) and tribe_intro_camera 20261106-150119
(2/2; detector rows only of known classes: BUG-0487, BUG-0473, BUG-0421).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-creature-reports): opened, not analysed.
- 2026-11-06 (fix/w45-int3): likely the same intro as BUG-0626 (the galaxy menu intro started in a stage after a quick load and ended there); world.intro_at_once no longer starts it outside the menu. Not re-tested on the Creature guest.
- 2026-11-06 (fix/w45-intro-camera): reproduced (Loading-mode intro, camera at (0, -1000, 500)); fixed by world.intro_at_once; test creature_intro_camera.
- 2026-11-07: released in 0.7.9.
