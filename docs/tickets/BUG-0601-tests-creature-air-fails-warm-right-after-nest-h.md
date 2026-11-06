---
id: BUG-0601
title: Tests: creature_air fails warm right after nest_heal (the NPC never strikes at the guest in the air)
status: fixed
kind: test
stage: Creature
reported: 2026-11-05
source: rel/0.7.8 lab 20261105-121717, fix/w44-guest-player2 20261105-121010
role: guest
versions: 0.7.8-dev
fixed_in: 0.7.8
branch: fix/w44-tests
commits: 598d85a7 2e1860c0 aefb8de5
tests: creature_air (coop_creature_air.py)
signature:
docs: docs/06-testing.md#testing-harness-g2
related: BUG-0555, BUG-0437
switch: none
---

## Report
creature_air fails on its precondition "the NPC kept hitting at the guest's creature in the air in the host's
game (0)" whenever it runs warm right after nest_heal: rel/0.7.8 (no P2 code) out/lab/20261105-121717 (also a
sync-check n apart 93.9 m), fix/w44-guest-player2 20261105-121010 and two earlier runs of that branch. It passes cold
(20261105-114931) and in other orders. Likely state nest_heal leaves (the NPCs near the nest, a heal or a herd
state) that the warm reset does not undo. Reproduce: `tools/lab.py run nest_heal,creature_air --lane N`.

## Root cause
Kind: **test setup** (the NPC the test relies on to strike at the airborne guest), not the game or the mod.
* Observed (lab 20261105-124455 and -124913 on lane 9, nest_heal then creature_air warm; the test now prints the NPC's
  line every round): the NPC bit the guest on the ground (3 bites in 8 s) and then, with the guest in the air, walked
  off: #52 (species 065CBF12, 6 hp) 100 m from the host after round 1 (speed 6.8 m/s), dead in round 2 from the
  guest's 0.1 hits. The host's air rule saw no melee choice (`dropped=0`): the NPC never came in reach.
* Observed: it is not nest_heal's state. Cold runs fail the same way with another NPC first (20261105-125326,
  -125634, -130042: #7, species 065CB8BD, 30-120 m away from the guest per round), and it is not the species: a
  065CB8AB (the cold list's usual #20, which strikes at once in 20261105-125928) stood 12-14 m off for 3 rounds in
  20261105-130326, while a 065CBF12 struck 9 times there. Which NPC the test took was the first in `coop.npc list`,
  i.e. the mod's numbering, which a warm reset after nest_heal shifts (#49-#54 instead of #20/#21).
* Inferred, not shown: the walking off is the game's own (a hurt animal fleeing its attacker, or one that does not
  approach a target it cannot reach with melee: docs/re/creature.md#creature-player-rules, the ability choice allows
  only ranged types at an airborne target 1 m or more away).
* Disproved: my first guess (a weak NPC drained by the 4 hits a second); sorting by health picked an 8 hp animal that
  walked off just the same (20261105-125326), with one hit every 1.5 s too (20261105-125634).

## Fix
tools/tests/coop_creature_air.py (no mod change): every 5 s of an air round the guest is put beside its NPC when it is
3-15 m off; an NPC farther than 15 m after a round, or without a strike in 3 rounds, gives way to the next candidate
(up to 8; the reasons are printed). The checks are unchanged (an NPC must strike at the airborne guest in the host's
game, no bite may reach the guest in the air). The suite's 4 minutes stay (6 made the lab run it before nest_heal in
one call: it orders tests by their minutes).

## Verification
* FAIL of the old test (warm after nest_heal): out/lab/20261105-124455, 20261105-124913 (lane 9), and the report's
  rel/0.7.8 20261105-121717, fix/w44-guest-player2 20261105-121010.
* PASS (nest_heal then creature_air warm in one lab call, lane 9): out/lab/20261105-145929 (#54 walked off 115 m, #52
  struck 8 times), 20261105-150235 (the guest put beside #54 twice, 1 strike), 20261105-150510 (5 strikes); earlier
  versions of the change: 20261105-130326, -131124 OK, -130745 FAIL (with 5 candidates: two walked off, three never
  bit on the ground; now 8). Cold: 20261105-131943, -132225, -132502 OK.
* The twin still catches BUG-0555: `SPORE_FIX_OFF=creature.air_immune` out/lab/20261105-131430 FAIL (3 bites in the air).

## History
- 2026-11-05: opened.
- 2026-11-05 (fix/w44-tests, G2): the NPC walks out of reach of the airborne guest; the test keeps it in reach or takes the next one.
- 2026-11-05: released in 0.7.8.
