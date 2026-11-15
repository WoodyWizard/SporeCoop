---
id: BUG-0432
title: class: fixed-size game pools overrun by co-op object counts (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content
commits: e81a56d9 ad15c36c
tests: cell_merge, pool_stress_cell, pool_stress_creature, pool_stress_tribe, pool_stress_civ, pool_stress_space
signature:
docs: docs/05-re-notes.md#re-notes-fixed-pools
related: BUG-0718, BUG-0719, BUG-0676, BUG-0295, BUG-0337, BUG-0338, BUG-0348, BUG-0355
---

## Report
Fixed per pool in Cell: BUG-0295-0297, 0337, 0338, 0348, 0355. Not covered: the game's MergeAxis 4096-endpoint buffer guarded only by soft budgets at mod spawn points (cellsync.cpp:2208); objects created while the guest's sim is paused bounded only for cells; no inventory of fixed arrays in Creature / Tribe / Civ / Space pools.
General fix: inventory of fixed game arrays per stage; hard guards at the consumer; mirror budget near the camera; "same caught EIP every frame" detector. Outcome check: 4-player stress per stage with fault counters.
Source: docs/generalisation-plan.md G16.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2b audit (0.7.11): the broadphase MergeAxis 4096-endpoint stack queue overflowed at ~2854 cells (fault 0xBBB07B, observed); no inventory of fixed game arrays existed outside Cell. The "same caught EIP every frame" detector exists since BUG-0676 (SporeKit storm_check).

## Fix
Shared method: an inventory of fixed game arrays per stage (docs/05-re-notes.md#re-notes-fixed-pools, `coop.pools`), a hard guard at the consumer where an overrun is possible (cell.sap_merge: the same merge with its queue on the heap past 4096 endpoints), and the per-stage 4-player stress test with fault counters (pool_stress_<stage>). Children: BUG-0718, 0719 fixed. Outside Cell everything the mod adds goes into growing vectors or the game's own slot searches; the Tribe physics pool peaked at 672 of 4096 bodies.

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2b final run on the merged branch out/lab/20261108-054842 (detect_* of all five stages, play_* of all five stages, every K2b test, SPORE_DETECTOR=20; report out of tools/detector_report.py): no signature of this class; the rows seen map to other tickets (BUG-0475, 0476, 0477, 0478, 0479, 0480, 0487, 0497, 0539, 0579, 0617, 0677, the BUG-0421 egg / guest-local rows, and Civ planet village / industry rows of K8's BUG-0659).

## History
- 2026-11-01: opened.
- 2026-11-08 (fix/w46-k2b-content, K2b of 0.7.11): every sibling item a closed child or covered; umbrella closed.
- 2026-11-08: released in 0.7.11.
