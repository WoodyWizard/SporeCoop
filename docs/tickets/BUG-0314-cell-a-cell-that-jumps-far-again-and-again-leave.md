---
id: BUG-0314
title: Cell: a cell that jumps far again and again leaves background cells behind; a guest's pool reaches its mirror cap and it mirrors nothing
status: fixed
kind: desync
stage: Cell
reported: 2026-10-30
source: fix/w13-cell BUG-0307 hunt
role: all
versions: 
fixed_in: 0.7.3.2
branch: fix/w13-cell
commits: 62a880d
tests: cell_jumps, cell_jumps_host
signature:
docs: docs/re/cell.md#cell-bg-leftovers
related: BUG-0307, BUG-0295, BUG-0299
---

## Report
Found hunting BUG-0307 (fix/w13-cell, lane 2): four players in Cell, Carol (a guest) made to jump
to a random place 150-250 m from the host every 3 s (`coop_cell_4p.py --jumps 240`). Her pool's
live count rose to 1899-1952 while her mirrors fell to 0; her game created no mirror any more
(coverage 0%, nothing of the host's world on her screen) and stayed so after the jumps ended. The
same picture as BUG-0307's campaign run, where the own cell's NaN heals made it jump 100-200 m at a
time and the guest's coverage fell to 57% and then 1%.

## Root cause
Observed (`coop.pool census`): the cells that piled up are background decoration (query `+0x35C` =
`G+0x4100`, levels 6 and 7 at level 4), 1099 on a guest and 1075 on the host after 40 jumps (37 /
35 before), most of them over 150 m from the own cell. The game frees background cells only on a
level change (RemoveCellsOfOldScale); its cull of cells outside the active tiles (0xE5F8C0) runs per
foreground cell only. Both roles, at 3 fps and at 65 fps: a game behaviour that our jumps (NaN heals,
`tp`) expose. On a guest the pool reaches the mirror cap (1500 live cells, BUG-0295) and it creates
no mirror any more.

## Fix
`coop.fix cell.bg_cull` (cellsync.cpp `bg_cull`, both roles, in a Cell session): every 2 s, not during a
level change, background cells (not keep-alive) farther than 150 m from the own cell are destroyed
with DestroyCell (the game's own removal, as RemoveCellsOfOldScale does for cells outside the view),
at most 300 a pass. Test command `coop.pool census` (every used pool slot by kind).

## Verification
`tools/tests/coop_cell_jumps.py [--who host]` (suites cell_jumps, cell_jumps_host): 40 jumps, then at
most 600 background cells and (guest) the host's cells on its screen again.
* FAIL `SPORE_FIX_OFF=cell.bg_cull`: out/lab/20261030-135947 (lane 2): guest 1121 background cells
  (36 before, pool at the 1500 cap), host 1129 (36 before).
* PASS: out/lab/20261030-135952 (lane 3): guest 39 (1084 culled, 607 mirrors), host 36 (1050 culled).
* PASS again with the Cell regression set: out/lab/20261030-140816 (lanes 2+3: cell_jumps, cell_jumps_host,
  cell_coop, cell_sync, cell_meat, cell_leave, cell_4p, cell_nan, cell_nan_all, cell_stale,
  cell_new_game, cell_lowfps OK; cell_far_guest failed in the harness before the game started (stage
  chooser did not open) and passed in out/lab/20261030-142603).
* Hunt runs without the switch (out/hunt of the worktree): 4 players, Carol at 3 fps and at 65 fps,
  1899 / 1952 live cells, 0 mirrors.

## History
- 2026-10-30: opened (fix/w13-cell, found hunting BUG-0307).
- 2026-10-30: fixed by cell.bg_cull (fix/w13-cell).
- 2026-11-01: released in 0.7.3.2.
