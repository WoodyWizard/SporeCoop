---
id: BUG-0658
title: Creature: after a far teleport of the guest, the host draws its puppet 12-36 m over the host's ground for a few seconds
status: wontfix
kind: bug
stage: Creature
reported: 2026-11-06
source: lab 20261106-204954 mem_census_tour_guest_10 (fix/w46-addr-space)
role: host
versions: 0.7.9 (rel/0.7.9 a6523625)
fixed_in:
branch: fix/w46-k8-open
commits:
tests: puppet_far_jump, puppet_far_jump_caption, mem_census_tour_guest, mem_census_tour_guest_10, mem_census_tour_guest_places
signature:
docs: docs/re/creature.md#creature-far-goto
related: BUG-0631, BUG-0499
---

## Report
The address space tours of BUG-0631 (coop_mem_census.py --tour guest: the guest's avatar goes 500-1000 m to a new place
of the planet every 20 s with `coop.abilities goto`, which puts it 0.05 m over this game's ground radius there) failed
the log gates in every tour run: the invariant monitor's `drawn` and `ground` rules ("host draws guest 15.9 m from
where guest's own game has it", "partner 1's puppet 15.96 m over the ground") and a sync-check "p: 1 apart (p1 15.7)",
each on 2 consecutive lines. Runs: 20261106-202803 (61 hops: 4 drawn, 6 MISMATCH lines), 20261106-204949 (31 hops: 5 / 6),
20261106-204954 (31 hops, npc.view_centres off: 5 / 6), 20261106-210804 (72 hops over 12 places: 7 / 4). Offsets seen:
11.9, 15.9, 29.0, 35.7 m; at s 50, 110, 230, 370 (a few of the hops, not every one). None in the runs of the same
sessions without teleports (20261106-200638 play bots 20 min, 20261106-211958 60 puppet respawns, 20261106-221554 smoke /
play_creature / play_tribe).

## Root cause
Observed: the drawn offset equals the puppet's height over the HOST's ground at that spot, so the host draws the
puppet where the guest says it is and the host's ground there is 12-36 m lower than the guest's: the two games' ground
heights differ for a few seconds after a jump into an area neither has streamed.
Inferred, not checked: the guest's `goto` takes its ground radius from a low terrain LOD of an area just entered (the
avatar then stands or falls on it in its own game), the host's terrain there is at another LOD. Players do not teleport
500-1000 m; the nearest player case is a respawn at a far nest after a death (long_creature's death transition shows no
such violation). Test artefact or a real two-LOD case: not decided. The tour tests run with the sync-check and monitor
gates off (suite.py, their own teleports).

## Root cause (2026-11-08, fix/w46-k8-open): a test artefact
Observed (puppet_far_jump, coop_puppet_far_jump.py: the tour's seed, `coop.abilities goto`, the host's `coop.track`):
* At hop 2 of the seed the host draws the puppet 5.2-5.4 m over its GetHeightAt for the whole stay, not jumping
  (flags 0x1). The driver put it onto the target every frame (134-317 teleports in 6 s, `p` r 493.84) and the host's
  game put it back to r 499.2 by the next frame. GetHeightAt and SurfacePoint are 493.89 on both games, no census object
  within 12 m; the host's OWN avatar sent there by the same command stood at r 499.3 a second later: the game's
  collision there lies 5.3 m over GetHeightAt (inferred: the terrain's collision mesh over the height field; a cliff).
* CORRECTION of the report's "the two games' ground heights differ": they do not (493.89 on both); the 12-36 m of the
  tours are this case at other places.
* The guest's game kept its avatar inside that collision because a caption held its game the whole time
  (`coop.uistate` input_lock=1 modal=1; the tours' guest trace busy=cinematic,input_lock,paused from s 40): game time
  paused, its avatar not simulated (it did not move under W). With the captions closed (Enter, as a player does) the
  guest's game moves its own avatar and the puppet is on the host's ground at every place (8 of 8 hops).
* Players never teleport 500-1000 m with a caption up; the nearest real case (a respawn at a far nest) has no such
  violation (long_creature, creature_respawn, mem_census_respawn).

## Fix
None in the mod (test artefact). puppet_far_jump closes the guest's captions as its player does; the tours keep their
monitor / sync-check gates off for their own teleports (suite.py). A `debug.goto_surface` attempt (goto onto
SurfacePoint) was dropped: SurfacePoint gave the same 493.89.

## Verification
no-fail-run: not a mod bug. Evidence, lane 5: with the guest's caption up (the tours' state) FAIL at hop 2, 5.2-5.4 m:
out/lab/20261108-005553, 20261108-010128, 20261108-012225, 20261108-013911 (puppet_far_jump_caption #1); with the
captions closed PASS 8 of 8 hops: out/lab/20261108-013911 (puppet_far_jump #1, #2).

## History
- 2026-11-06: opened (fix/w46-addr-space): seen only in the teleport tours of the address space census; evidence above. For 0.7.10 to judge (a ground-LOD case of far jumps, or the test command's ground radius).
- 2026-11-08: closed (fix/w46-k8-open): a caption held the guest's game, its avatar teleported inside a collision 5 m over GetHeightAt was not simulated; the host's game lifted its copy.
