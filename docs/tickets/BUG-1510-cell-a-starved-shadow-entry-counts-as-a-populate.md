---
id: BUG-1510
title: Cell: a starved shadow entry counts as a populated tile, so a shared tile can stay empty (3-4 players) (CELL-8)
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
tests: cell_shadow_empty (coop_cell_audit_0717.py --part shadow); twin cell_shadow_empty_off
switch: cell.shadow_empty
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-8 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Static reading (CELL-8), confirmed in cellsync_tiles.cpp: tile_elsewhere left out only starved entries, but a plain shadow (tiles_shared / tiles_bg) has no cells either; hand_over over the budget marked one shadow starved with player 0xFF and left the others, feed_starved then found another list's plain shadow, took the tile for populated and dropped the mark: nobody held it populated. The 0xFF player never matched a view, so between kPopCap and kPopCapScreen such a tile was never fed. Reaching it needs a populated tile deleted from a partner list while two other lists hold its shadow over the budget: the deletes keep a tile active in any player's box (g_union), so the window is narrow (3-4 players, >= 1100 cells).

## Fix
`cell.shadow_empty` (cellsync_tiles.cpp): no shadow entry counts as populated in tile_elsewhere (cell_logic::tile_entry_populated, unit-tested) and the starved mark names the partner whose list holds the tile. `coop.pool tilestat` prints shadows=, starved=, shadow_counted=. Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run (multi, 4 games): cell_shadow_empty (PASS expected: three guests together, the budget below the live cells while one swims away, then normal: the two that stayed see >= 10 mirrors and no tile waits starved), cell_shadow_empty_off. The path is narrow (Root cause): if the twin passes too, that run closes the ticket as `no-fail-run:` (the fix stays, covered by the unit test).
0.7.17 release lab: PASS: cell_shadow_empty (lab 20261124-020506); FAIL (fix off): cell_shadow_empty (lab 20261123-131931).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.shadow_empty), waiting for the night run.
- 2026-11-24: released in 0.7.17.
