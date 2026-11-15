---
id: BUG-0614
title: Net: a pose with NaN or Inf from another player reached game::teleport / set_position / place (no guard outside Cell)
status: fixed
kind: bug
stage: All
reported: 2026-11-06
source: BUG-0612 split (fix/w45-remote-bodies-2)
role: host
versions: 0.7.8
fixed_in: 0.7.9
branch: fix/w45-remote-bodies-2
commits: 33ae8911
tests: net_nan (tools/tests/coop_net_nan.py)
signature:
docs:
related: BUG-0612, BUG-0425, BUG-0299
---

## Report
Part of the BUG-0425 umbrella (G8 "NaN/Inf guard in game::teleport/set_position/place"), split from BUG-0612. A received pose was written into a body unchecked outside Cell: `PlayerState::read` takes any float, the NPC / Tribe / Civ snapshots the same; game::teleport, set_position, set_orientation, place and move_to passed them to the game's spatial object (BUG-0299 was a NaN own cell in Cell).

## Root cause
Observed in the code: no check between the wire and the game. Observed in the lab (20261106-043542, the guard off): a scripted guest's states with NaN / +Inf / -Inf in position, rotation and velocity for 4 s did NOT reach its drawn puppet on this path (host 980 rows, guest 990 rows of the puppet in coop.bodyrec, 0 not finite, no fault, both games ran on): the puppet driving and the game's spatial SetPosition tolerate it here. So no visible failure was reproduced; the guard is a defence for the other writers (NPC, Tribe, Civ snapshots, the raw velocity writes).

## Fix
`game.finite_pose` (game.cpp): teleport, place, set_position, set_orientation and move_to refuse a non-finite position / quaternion (counted, `nonfinite_refused=` in the air hold line of `coop.ground`/`coop.track air`); a player state with a non-finite position, rotation or velocity is dropped before the interpolation buffer (coop.cpp player_state; its velocity is written raw into kVelOff). Off: 0.7.8.

## Verification
no-fail-run: the bad states did not reach a body without the guard either (FAIL not reproducible on the puppet path). net_nan PASS with the guard out/lab/20261106-042950 (0 of 704 host / 717 guest puppet rows not finite), and PASS with it off out/lab/20261106-043542 (0 of 980 / 990): the test asserts the game's own positions on both machines and stays as a regression test.

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-remote-bodies-2, M2): opened from BUG-0612; guard added (game.finite_pose); the lab showed the old code tolerating NaN on the player path (no visible bug); kept as a defence with its test.
- 2026-11-07: released in 0.7.9.
- 2026-11-10 (plan/w47-closed-fixed): fixed-ticket review: the raw velocity writes from NPC and Tribe snapshots (npc.cpp:1939, driver.h:348/418) are still unguarded; new ticket BUG-0894.
