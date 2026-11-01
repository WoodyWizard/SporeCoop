---
id: BUG-0348
title: Cell -> Creature, 4 players: guest heap corruption found when a Cell pool is freed on leaving (GeneralAllocator_FreeInternal under SimUpdate+0x750), then freeze
status: fixed
kind: crash
stage: Cell
reported: 2026-10-31
source: lab 20261031-120025 campaign#1
role: guest
versions: 0.7.3.1+main 242a7bd
fixed_in: 0.7.3.2
branch: fix/w22-cell
commits: 21b178c, 90f091b, 27245c9
tests: cell_pause
signature: dc04aee5:GeneralAllocator_FreeInternal+0x13D / +0x64 from cObjectPool::_dtor in the Cell teardown 0xE7F9B0 (returns 0xE7FAF0 / 0xE7FA8D, named SimUpdate+0x750 / +0x6ED), then 0x00DDA358 (data 0x18)
docs: docs/re/cell.md#cell-sap-pending, docs/06-testing.md#testing-cell-4p, docs/known-issues.md#issue-cell-sap-broken-order
related: BUG-0337, BUG-0308, BUG-0289, BUG-0295
---

## Report
Lab 20261031-120025 campaign#1 (lane 2, four players, main 242a7bd): on Cell -> Creature (creature/follow,
game clock 13:10:23) the guest Bob faulted at `GeneralAllocator_FreeInternal+0x13D` (0x0092739D, data
0x5C738BD8), calls `00927979 00F47234 00B72206 00E7FAF0 00E81BE0 00E81CDF`, then 270 faults, a freeze
and a hang report. Before it: a Cell desync (c 2.5) and, from 13:08:38, BUG-0337's new warning "N
broadphase objects left open by the sweep" (198, 782, 827, 660; endpoints 2597 for 904 objects).

## Root cause
Observed (Ghidra + runtime): the cell broadphase's AddObject (0xBBB290) appends each new cell's
endpoints to pending arrays of 2 x 4096 entries with no bound check; only the sweep in SimUpdate
empties them, and RemoveObject does not shrink the count. In the pause menu SimUpdate does not run,
but a guest keeps creating mirrors from the host's states: on lane 8 the pending count reached
17424-20538 entries (2.1-2.5 x the arrays) while paused, the order was broken after Resume (466-477
objects left open) and leaving Cell for the galaxy menu faulted in `GeneralAllocator_FreeInternal`
freeing a Cell pool in the teardown 0xE7F9B0 (BUG-0308's exact signature), then 0x00DDA358 as in the
campaign. In the campaign Bob's position stood still for a minute (13:07:36-13:08:36) while his mirrors
churned (a pause menu left open, BUG-0289), then the left-open warnings started (194 of the first
198 with his avatar's object id). Not new since fix/w20-cell (its hook only reads the arrays; the
same fault is in lab 20261030-104149, BUG-0308); w20's warning is what showed it. Inferred: the
campaign guest's heap was damaged this way (the same steps reproduce the same faults).

## Fix
`coop.fix cell.sap_pending` (modes.cpp `cell_sap_room()`, unsafe off side): `cell_create` (mirrors) and
`cell_spawn` (puppets) create nothing while `pending + 2 + 512 > 2 x capacity`; the mirror comes with
a later state once the simulation runs again. `coop.pool sap` prints `held=`. A paused guest's mirrors
that were removed meanwhile come back after Resume.

## Verification
FAIL: `SPORE_FIX_OFF=cell.sap_pending` cell_pause, lab 20261031-123620-fix-w22-cell (lane 8: pending
20538 of 8192, 466 objects left open after Resume, 91 faults on the guest leaving Cell, first
FreeInternal+0x64 from the pool dtor at 0xE7FA8D); earlier 20261031-123042-fix-w22-cell (pending 17514).
PASS: cell_pause 2/2, lab 20261031-123625-fix-w22-cell (lane 9: pending capped at 7680, order intact,
mirrors back, the guest reaches the galaxy menu, no fault); also 20261031-123047-fix-w22-cell 2/2.

## History
- 2026-10-31: opened.
- 2026-10-31: fix/w22-cell: root cause (pending arrays of the cell broadphase overflow while the guest is paused), cell.sap_pending, cell_pause; fixed (unreleased).
- 2026-11-01: released in 0.7.3.2.
