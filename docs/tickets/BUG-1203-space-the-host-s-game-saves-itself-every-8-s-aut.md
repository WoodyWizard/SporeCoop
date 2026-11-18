---
id: BUG-1203
title: Space: the host's game saves itself every 8 s (autosave 'a colony founded' after each of its own saves)
status: fixed
kind: bug
stage: Space
reported: 2026-11-16
source: final check 20261116-025154
role: host
versions: unreleased
fixed_in: 0.7.15
branch: fix/w51-t3
commits: 8655eaa9, 88466c8c, 780d561e
tests: autosave_milestone, space_allies_near, space_allies, space_map, space_free, store_tests (make test)
signature:
docs: docs/re/space.md#space-save-planet-gap, docs/06-testing.md#testing-space-autosave
related: BUG-0864, BUG-1144, BUG-1149
---

## Report
0.7.15 final check (lab 20261116-025154): in Space the host's game wrote a save every ~8 s for the whole session
(`autosave: the host's game saves (a colony founded)` 16 times in space_allies, 9 in space_free, with nobody founding
a colony), each with a 0.7-0.8 s save hitch and the "game saved" box. Tests that use real input in Space failed
around it (space_map: the Esc for the pause menu landed in a save; space_free: a galaxy click lost) and a guest's
allies mirror lost its star for a moment after each save (space_allies, space_allies_near).

## Root cause
* Observed (diagnostic build, lab 20261116-084330 lane 7, space_free and space_allies_near): about 1 s after each
  save of the host's game, `space_map.cpp` roster_tick's E2 milestone saw the host's own colony count go up again:
  `n=2 s_mine=1` (space_free: colonies 01460035, 00460014), `n=1 s_mine=0` (space_allies_near: 0046000C). The read
  right after the save (read_local, cEmpire +0x88 stars -> +0x84 planets) missed one colony; the next one had it
  again. Every count increase was "a colony founded": milestone, the save 5 s later, the next miss, and so on.
  The guest path (kept_roster: own colonies more than kept) fired the same way once.
* Why the read misses the colony right after a save is not settled (inferred: the planet records of a star system
  other than the current one are not read back until something asks for them); the E2 milestone code (0.7.15) made
  the miss visible.

## Fix
`autosave::NewIds` (autosave.h, unit test `autosave new colony ids` in tests/store_tests.cpp): "a colony founded" is a
colony id never seen in this Space world (one set for the host's own and its guests' own colonies; the first read of a
world is what it has). A colony missed for a moment and seen again is no milestone. Switch
`coop.fix world.autosave_colony_ids off`: the count (0.7.15 before the fix).
Tests: coop_autosave.py (autosave_milestone) checks one save per colony (no milestone save in the next 30 s);
harness.autosave_settled(); coop_space_map.py waits for the host's autosave (the guest's colony is a real
milestone, saved when the host leaves the planet: just before its Esc) before the pause menu step.

## Verification
FAIL (rel/0.7.15, no change): the final check 20261116-025154 (space_allies 16 autosaves "a colony founded", space_free 9,
space_allies_near "the game lists the mirror's star", space_map: the Esc landed in the save); rel/0.7.15 on lane 6:
out/lab/20261116-082811 (space_allies_near FAIL, 9 / 4 / 3 autosaves in space_allies / space_free / space_allies_near).
Diagnostic build (the read and the milestones logged): out/lab/20261116-084330 (lane 7) and -085747 (lane 6): after
every save `n=2 s_mine=1` / `n=1 s_mine=0`, star 0046000C `planets=0 count=3`.
FAIL (twins): `world.autosave_colony_ids` fix alone still FAILs space_allies_near (out/lab/20261116-085357, one legit save
and the gap); `SPORE_LAB_FIXOFF=space_allies_near=space.roster_planet_gap` FAIL out/lab/20261116-091525; both switches off
FAIL out/lab/20261116-102402 ("the host's colony made at most one autosave (milestones 3, saves 2)" and the near list).
PASS (both fixes): out/lab/20261116-090711 (space_allies_near, space_map, lane 7), out/lab/20261116-094912 (lane 7, 2 of
2 each: space_map, space_allies, space_allies_near, space_free, autosave_milestone; 12 of 12), autosave_timer 2 of 2 in
out/lab/20261116-100807. Unit test `autosave new colony ids` (make test).

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t3 (roster read over the post-save planet gap; milestone per colony id; tests).
- 2026-11-18: released in 0.7.15.
