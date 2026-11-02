---
id: BUG-0361
title: Creature: a running NPC's proxy (npc.kinematic) is teleported every frame and shows no walk gait (slides); Tribe: animal proxies move in steps
status: fixed
kind: bug
stage: Creature, Tribe
reported: 2026-11-01
source: found on fix/w29-puppet-move (BUG-0360 measurement)
role: guest
versions: 0.7.3.2, 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-npc-walk
commits: cc489d4, 0436a20, a738cb0, 5cd211d, fccd6f1, 5d14f1f
tests: npc_walk, npc_walk_tribe (tools/tests/coop_npc_walk.py)
signature:
docs: docs/re/creature.md#creature-npc-walk, docs/06-testing.md#testing-npc-track
related: BUG-0360, BUG-0281, BUG-0305, BUG-0329, BUG-0363, BUG-0387
switch: npc.walk, npc.walk_tribe
---

## Report
Found while measuring BUG-0360: a creature teleported every frame (creature spatial vt 0x44,
`game::teleport`) keeps the animation's walk gait state (`+0xB54 -> +0x17C -> +0x15B8`) at 0 in
97-100 % of its moving frames and its main animation stays the plain idle 0x2481DE5: it slides
over the ground without walking. `npc.kinematic` (BUG-0281) places the proxy of a running host NPC
on a guest exactly like that every frame, so running NPCs probably slide on guests (inferred, not
measured on proxies; BUG-0281's note "stills show running poses" is from screenshots).

How to check: extend `coop.track` (coop.cpp) to NPC proxies (or sample one with `coop.npc`), run
`creature_npc_dash` and read the gait state of the running proxies.

## Root cause
Observed (fix/w30-npc-walk, `coop.npctrack`: every proxy on the guest every frame at the position the game
drew, `coop_npc_walk.py`: the host's game walks NPCs around the guest with MoveTo at 2-6 m/s, then hits two):

1. Creature: `npc.kinematic` teleported a moving NPC's proxy (vt 0x44, with the teleport mark +0xB5D) onto
   the host's path every frame. It was on the path (0-1 % standing 0.1 s windows, drawn error p50
   0.01-0.03 m) but its walk gait was on in 3-5 % of its moving frames on the guest's screen (the host's
   own NPCs at the same time: 94-99 %) and its main animation stayed the idle 0x2481DE5: it slid in the
   standing pose. Variants: a MoveTo ahead but still the teleport with the mark: gait 6 %; placed without
   the mark but without a MoveTo: 68 %; MoveTo ahead + speed + `game::place`: 99-100 %.
2. Tribe: npc.kinematic is Creature only; the animals' proxies were driven by `drive::step` (a MoveTo
   again every 0.1 s, pulls, stall / trail teleports, BUG-0281's problem): walk gait 91-98 %, but at the
   path's speed in only 70-77 % of the 0.1 s windows, 4-7 % standing, drawn error p50 0.27-0.33 m.
Found on the way (observed): a host NPC thrown back by a hit (9.4 m/s, slowing) is not kept by a place
without the mark (the proxy's locomotion moved it back each frame by as much as it was placed); at a stop
the interpolated target can jump back up to 1 m in a frame; Tribe herd animals report their speed while
pushed slower by their mates; far creatures off the screen are updated in bursts.

## Fix
`npc.walk` (Creature) and `npc.walk_tribe` (Tribe): while the host's NPC moves, `drive::walk_place`
(driver.h, the method of `creature.puppet_walk`, BUG-0360) walks the proxy (SetDesiredSpeed = the host
path's speed, a MoveTo 12 m ahead given again only when the direction turns) and `game::place` puts it on
the interpolated path every frame (Tribe: at the guest's own ground height, BUG-0329); an error is closed
at 3 m/s faster than the NPC. When the game does not keep the place (its error grows for 0.15 s over
0.75 m, a knockback), it is moved by teleports with the mark at the same speed until it is on the path.
The last 0.1 s of a walk goes on at the path's speed (capped at the walk's speed) to the stop point, then
a MoveTo there; standing, the driving is unchanged.
`coop.fix npc.walk off`: the teleport of npc.kinematic (0.7.3.3); `coop.fix npc.walk_tribe off`: the driving.
Diagnostics: `coop.npctrack` (also the host's own NPCs, `walk`, `variant`, `objects`).

## Verification
`coop_npc_walk.py` (suites `npc_walk`, `npc_walk_tribe`): 15 s wandering, 30 s of up to 4 NPCs walked by
the host's game near the guest, 15 s after two hits; checks no jump on the guest's screen, <= 5 %
standing, >= 85 % (Tribe 75 %) of the 0.1 s windows at 0.6-1.4x the path's speed, walk gait >= 80 % of the
moving frames on screen; Tribe also drawn error p50 <= 0.1 m.
* Creature FAIL with `SPORE_FIX_OFF=npc.walk,npc.walk_tribe`: out/lab/20261101-083826-fix-w30-npc-walk
  (npc_walk 3 of 3 FAIL: gait 3-4 % on screen; matched 91-97 %). Earlier: 20261101-080452 (3 of 3, gait 3-4 %).
* Creature PASS: out/lab/20261101-083834-fix-w30-npc-walk (npc_walk 3 of 3: gait 100 %, matched 92-98 %,
  standing 0-1 %, no jumps, drawn error p50 0.02-0.03 m). Also by hand at 15 fps (frame.limit 66) and at 30 fps
  over `SPORE_NET=mobile`: gait 99-100 %, matched 93-98 %.
* Tribe FAIL with `SPORE_FIX_OFF=npc.walk_tribe`: out/lab/20261101-085037-fix-w30-npc-walk (3 of 3: drawn
  error p50 0.27-0.29 m, matched 70-75 %, standing 4-6 %).
* Tribe PASS: out/lab/20261101-085045-fix-w30-npc-walk (3 of 3: error p50 0.01-0.02 m, matched 89-96 %,
  standing 0-2 %, gait 99-100 %).
* Not explained: in 1 of 9 Creature runs with the fix (out/lab/20261101-080500 npc_walk#1, before the
  knockback handling) every walked proxy had gait 0 for the whole run; not seen again in 12 later runs; the
  test prints the walked proxies' animation state now (docs/re/creature.md#creature-npc-walk).
* Final code (5cd211d, the 2.5 m snap and the walk at the path's speed added): npc_walk PASS in
  out/lab/20261101-095149-fix-w30-npc-walk (gait 99 %, matched 88 %, on screen 90 %), npc_walk_tribe PASS in
  20261101-094111. One npc_walk run there had no measuring centre on either game (0 rows; the test now waits
  for it and says so).
* Regression (lanes 4, 5): 20261101-090121 creature, creature_desync, sync, puppet_walk, npc_run_hp, team ok,
  creature_npc_dash FAIL (BUG-0363: fails in half the runs with npc.walk on and off alike, 4 of 8 each:
  20261101-092700 / 093400 on, 092708 / 093351 off); 20261101-094111 creature, creature_desync, team, npc_run_hp
  ok, sync FAIL once on civ/vehicles (n=1, not NPCs), puppet_walk FAIL at a 25 fps observer (BUG-0387, the
  puppet driving is unchanged here); 20261101-095149 sync FAIL once on tribe/NPC creatures p90 1.51 m (limit
  1.5; 0.11 and 0.65 m in the two runs before), puppet_walk FAIL at 37 fps (BUG-0387); 20261101-095826
  puppet_walk 1 of 2 (27 fps observer FAIL, 55 fps 98-99 %). Smoke: out/lab/20261101-100220-fix-w30-npc-walk 14 of 14 (lanes 4, 5). sync again: 20261101-101853 tribe/NPC
  creatures p90 0.65 / 0.68 m (ok both), one civ/vehicles FAIL (n=1, the Civ module).

## History
- 2026-11-01: opened.
- 2026-11-01: measured and fixed on fix/w30-npc-walk (npc.walk, npc.walk_tribe).
- 2026-11-02: released in 0.7.3.4.
