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
Not analysed in depth. Observed: the nest that differs (-113 -491 -63) has generation 0 on both games; on the host its
species is a key the host's game made in the run (7C530B3A, 7C0BDF11: another key every run, the 7C... range of keys
made at runtime), on the guest the fixture's 065CBE70. Inferred (coop_nest_evolve.py): the test drives one nest extinct
and then gives the DNA; the host's game seeds a nest (most likely that extinct one, near the players) with a new species
of its own making, and wsync's herd_evo row (BUG-0668) carries the generation and a species the guest's game does not
have, so the guest's nest keeps the old one. A desync of the herd_evo row (wsync.cpp, not an NPC table), seen in 2 of 9
runs before (flaky ledger) and in all 4 runs of 2026-11-15. Not a regression of 0.7.15 (v0.7.14.1 fails the same) and not
of the NPC registry (the same with it off).

## Fix
None yet.

## Verification
no-fail-run: open.

## History
- 2026-11-15: opened (feat/w51-npc), evidence above; for the orchestrator / the next Creature package.
