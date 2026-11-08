---
id: BUG-0718
title: Cell: over 2048 live cells overrun the broadphase merge's stack buffer (MergeAxis 4096 endpoints), bounded only by soft budgets
status: fixed
kind: crash
stage: Cell
reported: 2026-11-08
source: BUG-0432 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2b-content-a
commits: e81a56d9
tests: cell_merge
signature: 0xC0000005 at 0x00BBB07B SweepPrune::MergeAxis+0x16B
docs: docs/re/cell.md#cell-sap-merge, docs/05-re-notes.md#re-notes-fixed-pools
related: BUG-0432, BUG-0295, BUG-0337, BUG-0348, BUG-0719
---

## Report
From the BUG-0432 audit (K2b, 2026-11-08): the game's broadphase merge (SweepPrune::MergeAxis 0xBBAF10) keeps the endpoints it displaces in a 4096-entry stack queue with no bound check; only the soft budgets at the mod's spawn points (cell.budget, BUG-0295) kept the live cells below ~2048. Any count they do not see (the game's loot and spawns, a missed budget, the host's own tiles) overran it.

## Root cause
Observed (runtime, lane 6): four players in Cell, the host's caps raised over the merge's limit (`coop.pool budget 2600 2800`): at 2854 live cells (5348 endpoints) the host faulted at `0x00BBB07B SweepPrune::MergeAxis+0x16B` and then 2287 times, its cell stopped (lab 20261108-033717). With the budgets off the same at 4096 cells (lab 20261108-031215). Static (Ghidra, GOG 2024): MergeAxis parks only entries of the sorted array, so a sorted array of at most 4096 entries cannot overrun its queue; above it can (docs/re/cell.md#cell-sap-merge).

## Fix
The hard guard at the consumer, `cell.sap_merge` (cellsync.cpp `merge_axis`, from `h_UpdatePairs`): an axis whose sorted array holds over 4096 entries is merged by the mod with the same result (removed entries dropped, the added ones stable-sorted by value and merged, each moved endpoint's index written to its object record, the count returned) and its queue on the heap; at most 2 x the object capacity entries (`merge_clipped=` counts the rest). At or below 4096 entries the game's own MergeAxis runs. `coop.pool sap` prints `merges_ours= merge_peak= merge_clipped= merge_fix=`. Tests: `coop.pool budget <pop> <screen>` moves the host's population caps. `coop.fix cell.sap_merge off` (unsafe): the game's MergeAxis at every size.

## Verification
Test `cell_merge` (tools/tests/coop_cell_merge.py: four players, the host's caps raised, over 2300 host cells, its cell swims for 40 s, no caught fault).
* FAIL with `SPORE_FIX_OFF=cell.sap_merge`: out/lab/20261108-033717 (peak 2854 cells, 2287 faults from 0xBBB07B, the host's cell stopped).
* PASS: out/lab/20261108-033944 (peak 2830 cells, 244 merges of ours, merge peak 5588 entries, nothing clipped, no fault), out/lab/20261108-040824
* Related PASS on the final code: cell_4p out/lab/20261108-045828 (four players, the budgets on)
* Also seen: with the budgets off and the fix on the merge held at 4096 cells (50 merges, no fault at 0xBBB07B) but the full cell pool broke the game elsewhere (lab 20261108-031701: 0xE86C1C, 0xE83E30): the pool's 4096 slots are a second fixed pool, bounded by cell.budget (docs/re/cell.md#cell-sap-merge).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-065723, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: reproduced at 2854 cells, fixed with the heap merge (cell.sap_merge), test cell_merge.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
