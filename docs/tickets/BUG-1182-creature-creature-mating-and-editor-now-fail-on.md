---
id: BUG-1182
title: Tests: creature_mating and editor_now fail since the lab's condition start-up waits (A9): the host's game picks no mate within 30 s
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: feat/w51-npc lab 20261115-194833, 20261115-202054
role: 
versions: rel/0.7.15 96e0fd4c, feat/w51-npc
fixed_in: 0.7.15
branch: feat/w51-npc
commits: 8841da2b
tests: creature_mating, editor_now
signature:
docs: 
related: BUG-1112, BUG-1178, BUG-0516, BUG-1085
---

## Report
creature_mating and editor_now fail at their first step on feat/w51-npc's final tree (20261115-194833 lane 2) and on
rel/0.7.15 96e0fd4c (creature_mating, 20261115-202054 lane 3): "the host's game picked a mate near the nest", "the mate
is one of the host's NPCs (None)" (`coop.drama mate`: `mate 00000000 flags=0`). npc_editor_hold hit the same once
(20261115-193906) and opens the editor through `coop.vote evolve` since.

## Root cause
Observed: 9362bcdd (before feat/w51-core2) fails the same (20261115-203931 lane 3, both tests); the final tree with
`SPORE_START_FIXED=1` (the lab's old fixed start sleeps) PASS (20261115-204220). As BUG-1178: A9's condition waits
(BUG-1112) start the test some 7 s sooner after the load, and coop_creature_mating.mate_click gave the host's game 30 s
to pick a mate (the game's own choice near the nest, `coop.drama mate`), which it had not done by then (mate 00000000).
Not a mod bug: the same build passes with the old start, and with a longer wait the game picks the mate (flags 0x10).

## Fix
coop_creature_mating.py: mate_click waits up to `MATE_WAIT_S` = 60 s (`MATING_WAIT_S=30`: the old wait). Used by
creature_mating, editor_now and npc_editor_hold. Test-only.

## Verification
FAIL (30 s): 20261115-194833 (lane 2), 20261115-202054 (rel/0.7.15 96e0fd4c, lane 3), 20261115-203931 (9362bcdd, lane 3).
PASS (60 s): 20261115-204504 (creature_mating, editor_now, lane 3).

## History
- 2026-11-15: opened (feat/w51-npc).
- 2026-11-15 (feat/w51-npc): the A9 start-up timing found (old sleeps pass), the wait lengthened.
- 2026-11-18: released in 0.7.15.
