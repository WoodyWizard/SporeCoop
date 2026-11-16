---
id: BUG-1181
title: Tests: nest_evolve fails on v0.7.14.1 and rel/0.7.15: the player's home nest shows the host's evolved species on the host only
status: open
kind: test
stage: Creature
reported: 2026-11-15
source: feat/w51-npc lab 20261115-165219
role: 
versions: v0.7.14.1, rel/0.7.15
fixed_in:
branch: feat/w51-npc
commits:
tests: nest_evolve
signature:
docs: 
related: BUG-0668, BUG-1140
---

## Report
nest_evolve failed in feat/w51-npc's stage run (20261115-165219 lane 3) and in every rerun: lane 3 npc.registry off
(20261115-192704), lane 2 on (20261115-192709), v0.7.14.1 on lane 3 (20261115-193814): "every guest nest has the host's
generation and species in every sample (0/10-11 samples equal; first difference ... (-113, -491, -63), 'host gen 0
7C530B3A' / '7C0BDF11', 'guest gen 0 065CBE70')". The other checks pass (the host's game evolved 21 nests, the threshold,
the extinct nest).

## Root cause
Not analysed. Observed: the nest that differs is at the players' home (-113 -491 -63); on the host its species is a
creation key (7C...: an evolved species, different per lane: 7C530B3A lane 3, 7C0BDF11 lane 2), on the guest the
world's 065CBE70. Inferred: the lanes' creature fixtures (or their profiles) hold an evolved player species on the host,
and the guest's nest keeps the world's; the test compares the players' own nest too. Not a regression of 0.7.15
(v0.7.14.1 fails the same) and not of the NPC registry (the same with it off).

## Fix
None yet.

## Verification
no-fail-run: open.

## History
- 2026-11-15: opened (feat/w51-npc), evidence above; for the orchestrator / the next Creature package.
