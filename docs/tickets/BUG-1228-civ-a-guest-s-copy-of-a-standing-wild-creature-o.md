---
id: BUG-1228
title: Civ: a guest's copy of a standing wild creature on a steep slope of the guest's ground is put back every 2.5 s (trail teleports, 20 m/s)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-17
source: BUG-1216 analysis (final check 0.7.15, lab 20261116-025154 campaign_tc2)
role: guest
versions: rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-slope
commits: d7fd7a09, c7502dea
tests: civ_npc_slope
signature: "sync-check:   nNNN here: ... own=19-22 ... trail=N" growing on a guest while the host's creature stands
docs: docs/re/civ.md#civ-npc-ground, docs/re/civ.md#civ-npc-slope, docs/06-testing.md#testing-npc-slope
related: BUG-1216, BUG-0305, BUG-0402
---

## Report
Found in BUG-1216's analysis. In the 0.7.15 final check (lab 20261116-025154 campaign_tc2, s 1363-1397 of the run, a
live Tribe -> Civ campaign) a herd of species 067A147A stood still on the host (spd 0, agl 0.00). On every guest the
ground at that spot lies 9.2-10.5 m higher (`want_agl=-9.24 / -10.47`) and slopes steeply: Carol's copies n404 / n406
stood 2 m beside their target on their own ground (agl 0.00) but 1.3-1.8 m lower than the ground at the target, moved
at 19-22 m/s (`own=`) and were trail-teleported back every ~2.5 s (`trail=6 -> 20`, `tp=7 -> 20` in 33 s, `nudge`
pulls in ~97 % of the frames). A player on such a guest would see the animals slide and pop back in place.

## Root cause
Observed (runtime, civ_npc, labs 20261117-142058 with `coop.npctrack` per frame, 20261117-142930): a guest's NPC copy (a
puppet: no AI) on ground of 56-68 degrees (the rise over 3 m to each side) is slid down by its own game: velocity
19-28 m/s while its remote stands (the final check's `own=19-22`), the model 0.2-0.5 m under the terrain, grounded byte
0. The mod's driving of a standing copy (drive::step) then fought the slide: over 0.8 m off it walked it (MoveTo) and
nudged it every frame, the trail rule (BUG-0305: over 1.5 m for 0.5 s) teleported it onto its target and the nudge put it
there every frame for the next second (kTrailQuiet, k = 1): drawn 0.4-1.1 m off in every other frame, then a 2-3.5 m
slide, then the next trail teleport, every ~1.5 s, for as long as the remote stood (7 trail teleports and 45-53 m of
movement in 12 s). A copy put on 39-53 degrees stays, or slides 0.4-0.8 m once and rests (under the walk's 0.8 m nothing
drives it again). Nothing held a standing copy against its game's slide: the mod released it to the game's physics
between its pulls.
Inferred: in the campaign the copies' target lay on the guest's ground, 10 m higher than the host's there (BUG-1216,
docs/re/civ.md#civ-npc-ground), on a slope that steep, while the host's creature stood on its own, flatter, ground. Why
the two terrains differ there is still not known (no terrain stamp within 250 m).

## Fix
`npc.slope_hold` (npc.cpp, every NPC proxy driven by drive::step, every stage): a standing copy (its remote under 0.3
m/s) that its game moved more than 0.1 m between two frames (where the last frame's driving left it vs where the game drew
it) is held from then on while its remote stands: game::place_hold with the new `still` mode (the game's next creature
update and physics update may move it towards its target, drive::State::step_to, not away; put back with game::place,
no teleport mark, velocity cleared) and its velocity cleared each frame. The hold ends (game::place_release) when the
remote walks, the copy is placed, put elsewhere (npc.civ_jump) or dies. Off: the game slides it (0.7.14).
`coop.npc status`: `slope_slides=` (copies found moved so, with the switch off too), `still_kept=`.
Test commands: `coop.ground steep`, `coop.npctrack put <id> x y z [secs]`, `coop.npctrack start <r> x y z`.

## Verification
* Test civ_npc_slope (civ_npc): the host holds a creature on up to 3 bare spots of 55-68 degrees near a known slippery
  one, then 1.5 m further up the slope; 12 s of its copy on the guest; FAIL over 2 teleports or 3 m of movement on a spot.
* FAIL with `SPORE_FIX_OFF=npc.slope_hold`: 20261117-150616:civ_npc_slope (the test as committed: 63 and 68 degrees,
  7 trail teleports and 50 / 39 m each in 12 s); earlier versions of the test 20261117-142930 (63 / 68 degrees),
  20261117-150055 (63 degrees).
* PASS (fix on): 20261117-150636:civ_npc_slope and 20261117-150855:civ_npc_slope (the same 3 spots measured, 0
  teleports, at most 0.1 m); earlier versions 20261117-142950, -150115, -150307.
* Creature / Tribe (the same driving): the test's state could not be made there (docs/re/civ.md#civ-npc-slope: a held
  animal next to the avatars was thrown and held 10-13 m up on both games, others left the game's list; no such ground
  near the tribe fixture's village). Regression (fix on, lanes 5 + 9): 20261117-151225 PASS 20/20 (civ_wild_sync,
  civ_proxy_air, civ_wild_pooled, civ_wild_reused, civ_wild_moved, npc_walk, npc_walk_slope, npc_walk_tribe, npc_walk_slow,
  npc_walk_slow_creature, npc_walk_novel, npc_walk_fps30, npc_walk_player, tribe_proxy_held, proxy_identity,
  proxy_identity_tribe, proxy_ghost, creature_stun_proxy, tribe_npc_dash, creature_npc_dash); play_civ and play_creature
  --quick: 20261117-152623 PASS 2/2.
* The campaign itself (live Tribe -> Civ, campaign_tc2) was not rerun (the final check's job).

## History
- 2026-11-17: opened.
- 2026-11-17: fix/w51-slope: reproduced on civ_npc (slope + driving cycle), fixed with npc.slope_hold.
- 2026-11-18: released in 0.7.15.
