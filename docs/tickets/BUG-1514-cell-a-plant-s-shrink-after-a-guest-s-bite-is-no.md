---
id: BUG-1514
title: Cell: a plant's shrink after a guest's bite is not sent (the plant stays full for the others) (CELL-14)
status: open
kind: desync
stage: Cell
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w81-cell
commits: 17bc2248, e89fe984
tests: cell_plant_shrink (coop_cell_audit_0717.py --part shrink); twin cell_plant_shrink_off
switch: cell.plant_shrink
signature:
docs: docs/re/cell.md#cell-audit-0717
related: 
---

## Report
External static audit of 0.7.15.5, item CELL-14 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-uifx-cell.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-L1 (feat/w81-cell). A behaviour change gets a switch and a twin.

## Root cause
Observed in the disassembly (objdump 2026-11-23, GOG 2024): EatFood 0xE7DF40 after OnEat shrinks the plant in place when the level below is in the view's range (0xE56F20): +0xB4 target size *= TileSizes[L-1] / TileSizes[L], +0x358 = L-1, +0x1F0 cleared (0xE7DFDB); else DestroyCell(food, 1, 0, 0). On a guest the shrink of a mirror sent nothing (only DestroyCell becomes ACT_GONE); the host's plant stayed whole and the guest's mirror kept its lower level (the state writes size, never the level).

## Fix
`cell.plant_shrink` (cellsync_combat.cpp h_EatFood, shrink_plant; cell_logic::plant_shrink): a guest's bite that shrank a plant mirror sends MSG_CELL_EAT kind ACT_SHRINK (3) with the new level and the bite's points, only to a host of cell 1.2 or newer (an older host took any kind but "gone" for a kill: counted shrinks_old_host, nothing sent); the host shrinks its plant the game's way and adds the points as ACT_GONE does (eaten first: taken back). TileSizes' sym entry now names the shrink. Off: 0.7.16.1.

## Verification
Open, waiting for the lab run: pending night run: cell_plant_shrink (PASS expected: the guest's one bite shrinks a host plant on its game: within 3 s the host's plant is one level lower and smaller), cell_plant_shrink_off (FAIL expected: the host's plant stays whole). Unit test: audit_0717 (plant_shrink).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-L1), branch feat/w81-cell.
- 2026-11-23: implemented on feat/w81-cell with its lab test and twin (switch cell.plant_shrink), waiting for the night run.
