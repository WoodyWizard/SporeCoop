---
id: BUG-1477
title: Cell: players' My Creations cells as NPC species (off-by-default cell.lineage_cells, a guest's cells only with its consent; idea from PR #211 by IltonBJSilva)
status: open
kind: feature
stage: Cell
reported: 2026-11-23
source: plan, PR #211
role: 
versions: 0.7.17
fixed_in:
branch: feat/w81-cell
commits:
tests:
signature:
docs:
related: BUG-1257, BUG-1333, BUG-0975
---

## Report
About 17 % of native spawns from the players' own cells, one chain with our kin filter hook (GetModelKeyForCellResource 0xE65640); creations 1.2 MSG_CREATION kind 12 (reserved in WP-0); the cell pool is not saved (check by a save parse). docs/plan-0.7.17.md item 9. Credit IltonBJSilva (PR #211).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 9 (WP-L2), branch feat/w81-cell.
