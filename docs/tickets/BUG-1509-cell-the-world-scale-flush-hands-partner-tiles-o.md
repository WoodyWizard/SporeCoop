---
id: BUG-1509
title: Cell: the world-scale flush hands partner tiles over while the scale moves (BUG-1000 incomplete) (CELL-7)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_flush_handover (coop_cell_flush_handover.py); twin cell_flush_all_lists_off
switch: cell.flush_all_lists
signature:
docs: docs/re/cell.md#cell-audit-0717
related: BUG-1000
---

## Report
External static audit of 0.7.15.5, item CELL-7 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (CELL-7, an incomplete fix of BUG-1000), confirmed in cellsync_tiles.cpp: only pop_tiles' flush set g_flushing / g_flush_lists; h_WorldScale's flush (a level change, scale_moving) and partner_tiles_reset(true) deleted every partner list without it, so hand_over populated a shadow of the NEXT partner list with a direct PopTile and the same loop deleted it at once (cells made for nothing; 3-4 players).

## Fix
`cell.flush_all_lists` (cellsync_tiles.cpp flush_lists_begin / _end): both flush loops note every partner list and set g_flushing, so hand_over gives a tile only to the host's list (BUG-1000's rule). The lists are noted with the switch off too and hand_over counts a hand-over into one of them (handed_flushed of `coop.pool tilestat`), the twin's measure. Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run (multi, 3 games): cell_flush_handover (PASS expected, unchanged test: no tile handed to a list flushed in the same pass), cell_flush_all_lists_off (SPORE_FIX_OFF=cell.flush_all_lists: FAIL expected through the level change's flush). If the twin passes (the scale flush finds no shadow of another list on the lanes' fixture), the run is the evidence of an unreachable path and the ticket closes with that run as `no-fail-run:`.
0.7.17 release lab: PASS: cell_flush_handover (lab 20261124-020506); FAIL (fix off): cell_flush_all_lists_off (lab 20261124-005839).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.flush_all_lists), waiting for the night run.
- 2026-11-24: released in 0.7.17.
