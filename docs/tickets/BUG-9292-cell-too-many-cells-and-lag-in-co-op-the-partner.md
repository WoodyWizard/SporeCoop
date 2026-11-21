---
id: BUG-9292
title: Cell: too many cells and lag in co-op: the partner areas fill the host pool up to 1100 cells against about 600 alone
status: open
kind: bug
stage: Cell, Creature
reported: 2026-11-20
source: owner 2026-11-20 (friends build 0.7.15.94, both games)
role: host
versions: 0.7.15.94
fixed_in:
branch: build/amigos
commits:
tests: pop_budget
signature:
docs:
related: BUG-0174, BUG-0295, BUG-9285, BUG-9287
---

## Report
The owner (friends build 0.7.15.94, both games on it, 2026-11-20): "too many creatures spawn and it lags a lot; remove
spawning many creatures". The host's log of that session (Cell, 25 min): 16 fps at ~450 live cells, 7-10 fps at
800-1000; the partner was often 50+ m away.

## Root cause
Counted on lane A (Steam build, host and guest on lane A, the game's own counts):
* Cell, level 5 (`coop.pool` cells=, the cell pool's live count, every 2 s): the host alone avg 576 max 804; two
  players apart (the guest 120 units away, the host populating both areas, cellsync_tiles.cpp partner passes, BUG-0174)
  avg 731 max 1104 with the budget of 0.7.15 (1100 cells, a partner's screen 1300). Two players together: no more than
  alone (avg 363). At level 8 both stayed below 650 either way.
* Creature (Boheus, `coop.pools` objects): the host's creatures 105 alone, 106 with the guest; the guest 98-102. The
  guest's game removed 44 of its own NPCs in the first 3 min after the join (its world's own population, made again as
  copies of the host's) and only 3 in the next 2 min: a one-time churn at the join, not a steady multiplication.
* No other per-player spawn found: lineage cells swap looks only (BUG-9285: only player-species spawns now), own nests
  keep the game's herd counts.
So the multiplier is the second populated area in Cell: up to 1100 live cells on the host where a game alone holds ~600.

## Fix
`cell.pop_budget_solo` (cellsync_tiles.cpp): the session's population budget is a single player's, 800 cells (a
partner's own screen up to 950) instead of 1100 / 1300; a partner's outer tiles wait while the pool is full, its screen
keeps food. Measured (level 5, apart): avg 608, max 710 (was 731 / 1104); the guest still had food on its screen.
`coop.pool budget` still sets the caps for the tests. Off: 1100 / 1300.

## Verification
Lane A (counts above, manual driver E:\Repositorio\SporeCoop-wt\agentA5\count.py / count2.py). The lab test
tools/tests/coop_cell_pop_budget.py (pop_budget, twin pop_budget_off) is left to the orchestrator's lab.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on build/amigos.
- 2026-11-20: built into 0.7.15.95 (the friends' package E:\SporeCoop-amigos-0.7.15.95).
