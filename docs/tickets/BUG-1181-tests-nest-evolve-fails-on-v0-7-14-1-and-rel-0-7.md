---
id: BUG-1181
title: Tests: nest_evolve fails on v0.7.14.1 and rel/0.7.15: the player's home nest shows the host's evolved species on the host only
status: fixed
kind: test
stage: Creature
reported: 2026-11-15
source: feat/w51-npc lab 20261115-165219
role: 
versions: v0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-nest
commits: 94cbeab2, d8b058e1
tests: nest_evolve, nest_evolve_own
signature:
docs: docs/06-testing.md#testing-w51-nest, docs/re/creature.md#creature-nest-early-dna
related: BUG-0668, BUG-1140, BUG-1176, BUG-1178
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

## Root cause (fix/w51-nest)
Observed: the nest that differs is no nest. `coop.nest evolve` lists every herd of the game's object list, also the
herds of our own (game::create_herd for the partner's puppet and the NPC proxies: no nest +0x160, no archetype +0x88);
it now prints `nest= arch= mkey=`. With `--own` (the guest plays its own creature, the lane state of every failed run:
a worlds.ini record, BUG-1176) the same difference came back at the same place, and both sides were herds of our own
(lab 20261115-214417: "host gen 0 7CBC3253 (ours)", "guest gen 0 065CBE70 (ours)"): on the host the herd of the guest's
puppet, whose species is the host's copy of the guest's creation (a key the host's game makes in the run: 7C...), on
the guest the herd of the host's puppet (the world's species); both stand where the avatars stand, so the rounded
homes matched. The failed runs' guest logs say the same ("puppet of 1 'Bob': species 7C0BDF11 here; its creation here
7C0BDF11", "own=29020CC4 'Judge' from this world's record (worlds.ini)"). wsync's herd_evo row never sends or applies
them (npc_herds wants a nest). Not a desync of what the guest sees: the game's 133 nests had the host's generation and
species in every sample, and the live creatures of the nests of other species were of the same species on both games.
Inferred (checked for the four runs of 2026-11-15 only, not run by run for the 2 of 9 before): the failures followed
the lanes' worlds.ini records (BUG-1176 fixed that: the cold start resets worlds.ini; nest_evolve_own keeps the case).

## Fix
Test only (coop_nest_evolve.py): the herds of our own (no nest, no archetype) are not compared; a new check asserts that
the live creatures of every game nest of another species than the players' are of the same species on both games;
`--own` (suite: nest_evolve_own) lets the guest play its own creature first. `NEST_EVOLVE_OUR_HERDS=1`: every herd
compared (the twin). `coop.nest evolve` prints each herd's nest, archetype and its live members' species key.

## Verification
FAIL: 20261115-214417 (twin NEST_EVOLVE_OUR_HERDS=1, nest_evolve_own, lane 2: the puppets' herds differ).
PASS: 20261115-214437 (lane 3, nest_evolve_own and nest_evolve), 20261115-220653 (lane 2), 20261115-222140 (lane 2, final), 20261115-224806 (lane 3, final).

## History
- 2026-11-15: opened (feat/w51-npc), evidence above; for the orchestrator / the next Creature package.
- 2026-11-15 (fix/w51-nest): cause found (the test compared the herds of the puppets), test fixed; FAIL/PASS twin above.
- 2026-11-18: released in 0.7.15.
