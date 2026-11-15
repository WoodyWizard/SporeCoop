---
id: BUG-0719
title: Fixed-size game arrays filled by co-op objects in Creature, Tribe, Civ and Space, and objects made while a guest's simulation is paused
status: fixed
kind: bug
stage: All
reported: 2026-11-08
source: BUG-0432 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-a
commits: ad15c36c
tests: pool_stress_cell, pool_stress_creature, pool_stress_tribe, pool_stress_civ, pool_stress_space, cell_merge
signature:
docs: docs/05-re-notes.md#re-notes-fixed-pools
related: BUG-0432, BUG-0718, BUG-0295, BUG-0337, BUG-0338, BUG-0348, BUG-0355, BUG-0449, BUG-0676
---

## Report
From the BUG-0432 audit (K2b, 2026-11-08): objects made while a guest's simulation is paused were bounded only in Cell (cell.sap_pending); no inventory of the game's fixed arrays existed for Creature, Tribe, Civ and Space; the class asks for a 4-player stress run per stage with fault counters.

## Root cause
The inventory (docs/05-re-notes.md#re-notes-fixed-pools, from the docs, the tickets, the code and Ghidra): the fixed arrays co-op objects fill are Cell's (the cell pool 4096, the broadphase's merge queue, sweep list and pending arrays, the soft-body particles 1500, the cell graphics pool 256, the tile pool), each with a hard guard or a budget (the merge queue had none: BUG-0718), and in the 3D stages the physics world's body pool (4096) and contact pool (2048); the rest the mod adds to (vehicles, UFOs, tribe members, herds) are vectors that grow, or slot searches of the game's own. Only Cell creates objects in an array that the running simulation empties (BUG-0348); elsewhere a guest's pause does not hold objects back. Measured: the physics pools are used only in Tribe (at most 672 of 4096 bodies on a guest with four players); Creature, Civ and Space leave them at 0.

## Fix
The class test `coop_pool_stress.py` per stage (pool_stress_<stage>, four players, three in Civ: a rejoin, the guests in their pause menus) with `coop.pools` (gamefixes.cpp `pools_report`: the physics pools' count / high water / capacity and the world's objects), failing at 75 % of a capacity or any caught fault; the hard guard where an overrun was possible (the merge, BUG-0718). No guard is needed elsewhere: no pool came near its capacity. The "same caught EIP every frame" detector is SporeKit's storm_check (BUG-0676).

## Verification
no-fail-run: an inventory with a stress test per stage and no overrun to reproduce outside Cell (the Cell one is BUG-0718 with its FAIL / PASS runs). PASS: pool_stress_cell out/lab/20261108-050903, pool_stress_creature out/lab/20261108-051355, pool_stress_tribe out/lab/20261108-051700, pool_stress_civ out/lab/20261108-052001, pool_stress_space out/lab/20261108-052247 (no caught fault on any game; peaks in docs/05-re-notes.md#re-notes-fixed-pools).

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: inventory, coop.pools, the per-stage stress test; the merge guard is BUG-0718.
- 2026-11-08: released in 0.7.11.
