---
id: BUG-0827
title: Cell: a guest swallowing a host cell whole may get the food while the cell lives on in the host's game (suspected)
status: fixed
kind: desync
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-cell
commits: 3e554da8
tests: cell_swallow_gone
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0636 BUG-0632
---

## Report
No player report. Suspected: the guest gets the meal of a whole swallow but the host's cell lives on and can be eaten again.

## Root cause
Observed (lab 20261112-065805, `cell.swallow_gone` off): the guest's mouth swallowed three host creatures whole
(`coop.pool swallow c<key> 10`: the game's executor, StartEatSwallow -> event 1 -> SwallowWhole 0xE7D880); one of
them lived on in the host's game. SwallowWhole of a cell with a model plays "cell_death_eaten" and posts event 7,
whose handler destroys the cell (return address 0xE7B468) ~1.1 s later; h_Destroy counted a mirror's destroy as eaten
only within 1 s of the last touch, so a late one was taken for the game's own removal: no ACT_GONE, the host's
creature lived on (the meal still came, MSG_CELL_FOOD). Timing-dependent (2 of 3 just inside the second).

## Fix
cellsync.cpp `cell.swallow_gone`: SwallowWhole hooked (`Simulator::Cell::SwallowWhole`, verified: the only call
0xE7DBBF pushes 3 and adds esp,0xC); on a guest a mirror our avatar swallows is marked, and its destroy within 10 s
counts as eaten (ACT_GONE with the loot flag the game gave). Switch `coop.fix cell.swallow_gone off` (0.7.12).

## Fix idea
Hook SwallowWhole on the guest for mirrors and send ACT_GONE there.

## How to test
Guest carnivore two levels above a small host cell; host log must show 'gone ... applied' for every 'sent food' of a swallow (fix) vs 'no host cell with it' with no gone (main).

## Verification
`coop_cell_swallow_gone.py` (suite `cell_swallow_gone`, ~1 min): the guest swallows three host creatures next to
it; each must be gone in the host's game within 6 s.
* FAIL with `SPORE_FIX_OFF=cell.swallow_gone`: lab 20261112-065805 (2 of 3 gone).
* PASS: lab 20261112-062148, 20261112-063438 (3 of 3, gone after 1.1 s).

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-cell): fixed (cell.swallow_gone), FAIL and PASS runs in Verification.
- 2026-11-12: released in 0.7.13.
