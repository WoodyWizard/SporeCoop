---
id: BUG-0798
title: Cell: a downed cell is held at its pre-rescale position across a level change, so the partner cannot revive it
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-cell
commits: 3e554da8
tests: cell_ko_scale
signature:
docs: docs/re/cell.md#cell-world-scale, docs/re/cell.md#cell-c1-0713
related: BUG-0519 BUG-0662
---

## Report
No player report. A player knocked out while the partner eats past a level line is pinned far out in the pool, away from the partner and the food; no revive is possible and it always ends in the small respawn, the camera jumps there.

## Root cause
Observed (lab 20261112-065614, `cell.down_scale` off): the guest knocked out 15 m from the host, the host's food
crossed the level line 1 -> 2 (world scale 0.5 -> 1.5) 1.7 s later; the guest's game held its cell at (115.7 101.1)
while the world around it was divided by 3 (the host's cell at (33 33) in the guest's game): 102 m from its place in
the world, 106 m from the host, no revive possible. Cause (code, as in the audit): downed_tick holds the cell at
g_down_pos, stored once at the knockout in that moment's metres; the rescale 0xE6DB70 changes the metres
(pos x G+0x514C is what it keeps, docs/re/cell.md#cell-world-scale).

## Fix
cellsync.cpp `cell.down_scale`: the knockout keeps the world scale of its moment (g_down_scale) and the held spot is
`g_down_pos x g_down_scale / world_scale()` every frame (down_pos(), also for the respawn spot without a partner).
Switch `coop.fix cell.down_scale off` (0.7.12).

## Fix idea
Keep g_down_pos in world-scale units (position x cCellGame+0x514C at the knockout) and convert it with the current scale each frame, as the mirrors do (cell.world_scale); switch cell.down_scale.

## How to test
Guest `coop.pool selfhit` (knocked out), host `coop.pool food <next level line>` within 2 s. Assert the guest's own position (`coop.pool own`) is within revive range of the host's partner position (`coop.pool scalepos`) after the rescale, and that the host held next to it revives it. Main: tens of metres apart, no revive.

## Verification
`coop_cell_ko_scale.py` (suite `cell_ko_scale`, cell fixture, ~1 min): the guest knocked out 15 m from the host
(`coop.pool selfhit 50`), the host's food crosses a level line; in the guest's own game its position x world scale
must stay (under 1.5 m), the host's cell must stay near, then the host next to it revives it.
* FAIL with `SPORE_FIX_OFF=cell.down_scale`: lab 20261112-065614 (102 m off its place, the host 106 m away).
* PASS: lab 20261112-064328 (0.05 m, revived), 20261112-062128 (the 6 m variant: 0.00 m, revived).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-cell): fixed (cell.down_scale), FAIL and PASS runs in Verification.
