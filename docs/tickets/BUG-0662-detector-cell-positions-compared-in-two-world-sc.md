---
id: BUG-0662
title: Detector: Cell positions compared in two world scales during a level change (Cell:pos:cell rows)
status: open
kind: test
stage: Cell
reported: 2026-11-06
source: fix/w46-cell-p0 lab 20261106-224058, 20261106-223318
role: both
versions: 0.7.9-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0480, BUG-0606, BUG-0511
---

## Report
With SPORE_DETECTOR=20, cell_levelup_view (food +-150: a world scale tier change) gets one `Cell:pos:cell` row with
80-552 cells 7-14 m apart (host (146 76), guest (133 70)) in the comparison that falls into a level change (lab
20261106-223318, 20261106-224058 #1). detector_report.py files it under BUG-0480.

## Root cause
Inferred (not verified): the same as BUG-0606 for the sync-check: during the ~3 s world rescale (WorldScaleStep,
BUG-0511) each game's positions are in its own scale and the two games change at different moments; worlddiff
compares the raw positions. The guest draws the host's cells where they are (cell.world_scale). The sync-check's
fix (diag.cell_scale: compare after scaling by guest scale / host scale) is not in worlddiff.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
