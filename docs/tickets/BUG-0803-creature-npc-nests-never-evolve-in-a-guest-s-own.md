---
id: BUG-0803
title: Creature: NPC nests never evolve in a guest's own world while it is connected
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-own-world
commits: dda0afe5, 946f4dd2
tests: own_creature
signature:
docs: docs/re/creature.md#creature-herd-evo-own-world, docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0668 BUG-0130 BUG-0427
---

## Report
No player report. A connected guest that plays its own Creature save sees no nest evolve; that save keeps nest evolution and extinction states that single-player would not give.

## Root cause
Observed at runtime (confirms the code reading): herd_evo_guard set CreatureGame_HerdEvolveOn = 0 for any connected guest in Creature. In a Creature world of its own, after `coop.dna add 250`, the guest's game evolved 0 of its 133 nests in 40 s (lab 20261112-031349).

## Fix
herd_evo_guard holds the evolution step only while the guest plays the host's world (not own_world::elsewhere()); the game's value comes back otherwise. Switch `coop.fix wsync.herd_evo_own off`.

## Fix idea
Switch the step off only while the guest plays the host's world (not guest_elsewhere()).

## How to test
Guest connected, loads its own Creature world, raises DNA past a nest's +0x94 threshold (debug DNA command). The herd's +0xF0 generation changes with the fix, stays on main (and changes with SPORE_FIX_OFF=wsync.herd_evo).

## Verification
Test `own_creature` step 5: in its own world the guest's DNA goes up 250; its own herds' generation +0xF0 (`coop.nest evolve`).
* FAIL: out/lab/20261112-031349 (`SPORE_FIX_OFF=wsync.herd_evo_own`, lane 4): 0 nests evolved in 40 s.
* PASS: out/lab/20261112-030351 (lane 5), out/lab/20261112-030546 (lane 4): 21 nests evolved within 5 s.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with wsync.herd_evo_own; FAIL 20261112-031349, PASS 20261112-030351 / 030546.
- 2026-11-12: released in 0.7.13.
