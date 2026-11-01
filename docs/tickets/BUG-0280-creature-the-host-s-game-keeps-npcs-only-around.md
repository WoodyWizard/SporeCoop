---
id: BUG-0280
title: Creature: the host's game keeps NPCs only around the host's avatar; a guest far from it sees nest members appear and vanish every few seconds
status: fixed
kind: desync
stage: creature
reported: 2026-10-29
source: fix/w7-npc BUG-0274 analysis
role: 
versions: 0.7.3.0
fixed_in: 0.7.3.2
branch: fix/w9-creature
commits: f7eb82e 8c950ac b6a3830 355bca5 20f36a8 4b555a7 e4f980c 83e9c20 bbdeb14 f430ef5
tests: tools/tests/coop_npc_far.py (suite npc_far), tools/tests/coop_tribe_far.py (suite tribe_far)
signature: creature NPCs pooled outside the host camera sphere (Relevance_Update 0xACE4E0)
docs: docs/re/creature.md#creature-view-centres, docs/re/creature.md#creature-view-centres-tribe, docs/06-testing.md#testing-w9-creature
related: BUG-0274
---

## Report
Seen while analysing BUG-0274 (lane 4, creature fixture, 2 players, 2026-10-29): the guest stays at
the nest, the host walks 300 m away (`coop.npc gopos` in 20 steps). On the host every NPC farther
than about 150-200 m from its avatar goes back to the creature pool (`coop.npc creatures`: herd
00000000, pos (0 0 0), no updates), also the ones right next to the guest. The guest then has no
NPCs around it at all (17 proxies -> 0), and the nest keeps spawning one missing family member
that the game pools again 1-3 s later: on the guest a family member appears at the nest and vanishes
every 3-6 s, each time with a new net id (c128, c129, c131 ... c134 in 40 s).

Reproduce: `SPORE_LANE=N python3 tools/tests/coop_npc_pooled.py` prints it (matched proxies drop to
none while the host stands far away); or by hand as above.

## Root cause
Observed: the host's game simulates creatures only around the host's own avatar (the guests'
puppets do not keep them); the cull distance and the function were not looked up. Inferred: the
same in the 4-player campaign (BUG-0274: the host 190-300 m away from the nest, the guests near it).

Found (2026-10-30, fix/w9-creature; disassembly, runtime on lane 9): `SporeCoop::Relevance_Tick`
0xAD2300 pushes one sphere per frame (the local camera's view point, enter 100 m, leave 150 m at
runtime) into the vector mgr+0x20 and runs `SporeCoop::Relevance_Update` 0xACE4E0 (every 5 s):
creatures outside every sphere's leave radius go back to the pool (vt 0xC8(1)), herds and spawn
points are enabled / disabled by the same spheres. Only the host's camera is in that list
(docs/re/creature.md#creature-view-centres).

## Fix
The host keeps a list of the view centres of all players (`mods/coop/view_centres.cpp`: a partner's
position whenever its puppet is shown in the host's place, any stage) and a hook on
Relevance_Update adds one sphere per partner (the game's radii) to the game's list for that call
(a buffer of ours lent to the vector, the game's pointers restored right after). Creature stage
and (2026-10-30, `npc.view_centres_tribe`) Tribe, where a partner's centre is its pointer. Bounded cost: one sphere per partner, the game's 10-herds-per-pass limit. The same
list is meant for Rivals (docs/11-rivals.md 11.3.2) and the Cell tiles (BUG-0174). Switch
`coop.fix npc.view_centres off`; `coop.views` prints the centres and the game's own sphere.

## Verification
`coop_npc_far.py`: the guest at the nest, the host 450 m away, 40 s of censuses of both games.
* FAIL without the fix (`SPORE_FIX_OFF=npc.view_centres`, lane 9): out/lab/20261030-002600 (the
  guest kept 8 of its 20 NPCs; host NPCs near the guest 8). Earlier runs with a looser check
  (20261030-001557, 001845) lost 8 of 20 too and passed it: the check was tightened to 85 %.
* PASS with the fix: out/lab/20261030-002333, out/lab/20261030-002821, out/lab/20261030-011525 (20 of 20 in every sample).
Tribe (`coop_tribe_far.py`, the guest's pointer 332 m from the host's camera at a herd of the game):
* FAIL without it (`SPORE_FIX_OFF=npc.view_centres_tribe`): out/lab/20261030-013630 (no herd enabled,
  no animal near the guest's point).
* PASS: out/lab/20261030-013436, out/lab/20261030-014003 (3 herds enabled, 18 animals there).
* Not seen in these runs: the nest flicker (new ids every 3-6 s), with or without the fix.

## History
- 2026-10-29: opened.
- 2026-10-30: fixed on fix/w9-creature (npc.view_centres), the cull found (Relevance_Update).
- 2026-10-30: extended to the Tribe stage (npc.view_centres_tribe).
- 2026-11-01: released in 0.7.3.2.
