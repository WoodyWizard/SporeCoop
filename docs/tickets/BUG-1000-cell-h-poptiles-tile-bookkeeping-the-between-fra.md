---
id: BUG-1000
title: Cell: h_PopTiles tile bookkeeping: the between-frames check runs before the rebuild cleanup, and a flush hands shadow tiles to partner lists flushed next
status: fixed
kind: bug
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-21
role: host
versions: 
fixed_in: unreleased
branch: fix/w49-cell
commits: 23b91609, 64795658
tests: cell_flush_handover
signature:
docs: docs/06-testing.md#testing-cell-c1-0714
related: BUG-1066
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-21. Not seen at runtime, found by code reading (main efd2e0ba). Two
bookkeeping slips in the host's `h_PopTiles` partner tile path (Cell, a host with partners):
1. the debug check `tiles_check("the frame between")` runs before the loop that forgets partner lists whose tiles a
   rebuild (RebuildLevel) freed, so after every rebuild it logs a false "dead entries" warning and uses up the 20
   warnings `tiles_check` allows per session (real mismatches later are not logged any more);
2. on a flush of every partner list (`all_go`: a rebuild, a level change, a short pool), each deleted tile calls
   `hand_over`, which populates a shadow tile of the same key in ANOTHER partner list, often one the same loop flushes
   next: cells created for a tile deleted a moment later.

## Root cause
Observed in code (mods/coop/cellsync.cpp on main): 2679 `tiles_check("the frame between", level)` before the
cleanup at 2681-2685 (`!tile_alive` -> `tiles_forget`); 2728 `all_go`, 2730-2735 `tiles_delete(pt.list, false)`
for each list in turn; `tiles_delete` 2359-2360 calls `hand_over(e[0], l)`, which (2284-2319) tries the host's list,
then every `g_ptiles` list but `from`, and calls `o_PopTile` on a shadow there.
Inferred: (2) gives extra cells in the pool at a level change (they belong to a deleted tile: whether the game
deletes them with the tile or they stay as strays was not checked) and wasted work; the host's own list is the only
list that should get hand-overs during a flush. Severity: low (the support log and some extra cells).

## Fix idea
(1) move the check after the cleanup loop (or skip it after `all`); (2) during `all_go` pass a flag so `hand_over`
only tries the host's list (or flush every list first, then hand over to the host's list). Switch:
`cell.flush_handover`.

## How to test
Extend tools/tests/coop_cell_* with a host + 2 guests far apart and a forced level change (`coop.pool food` to a
level line, as cell_levelup_view): count `g_tiles_handed` during flushes into lists flushed in the same pass (a new
counter in `coop.pool`) and the `tiles: check after the frame between` warnings after a rebuild; main: > 0, fixed: 0.

## Fix
cellsync.cpp: (2) during the flush of every partner list (`all_go`) `hand_over` tries only the host's list (`g_flushing`);
a hand-over into a list the same pass flushes (populated, or waiting for room under the cell budget) is counted as
`handed_flushed` (`coop.pool tilestat`). (1) the between-frames `tiles_check` runs after the lists a rebuild freed are
forgotten; its warning counter counts only warnings with dead entries. One switch for the ticket:
`coop.fix cell.flush_handover off`. Test command `coop.pool rebuild` (host: G+0x51D8, the game's RebuildLevel next frame).

## Verification
Test `cell_flush_handover` (3 players, the two guests on one spot 30 m from the host: one list populated, the other its
shadows; four level changes, two rebuilds).
FAIL (SPORE_FIX_OFF=cell.flush_handover): out/lab/20261113-025004 (8 hand-overs into a list flushed in the same pass).
PASS: out/lab/20261113-025004-2, out/lab/20261113-025224.
Part (1) (the log order) had no FAIL of its own: the check's 20 warnings were used up by BUG-1066's mismatch in the
first two changes, before the rebuilds (4 list resets, 0 dead-entry warnings with the switch off too); fixed by code.
Earlier runs with the guests 12 m apart showed no shadow hand-over either way (20261113-024334 off, 20261113-023819 and
20261113-024127-2 on).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-cell (package C1); BUG-1066 opened (the tile pool mismatch seen in its runs).
