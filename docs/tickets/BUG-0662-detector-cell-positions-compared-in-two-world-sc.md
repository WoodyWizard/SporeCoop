---
id: BUG-0662
title: Detector: Cell positions compared in two world scales during a level change (Cell:pos:cell rows)
status: fixed
kind: test
stage: Cell
reported: 2026-11-06
source: fix/w46-cell-p0 lab 20261106-224058, 20261106-223318
role: both
versions: 0.7.9-dev
fixed_in: unreleased
branch: fix/w46-detector-scale
commits: 5e39a0ce, 334a248f, 0243b621
tests: cell_levelup_view, play_cell, tools_selftest worldstate
signature: DETECTOR Cell:pos:cell (and Cell:scale:cell) with many cells 6-14 m apart / sizes off by one factor in the comparison that falls into a level change
docs: docs/06-testing.md#testing-detector-cell-scale
related: BUG-0480, BUG-0606, BUG-0511, BUG-0681
---

## Report
With SPORE_DETECTOR=20, cell_levelup_view (food +-150: a world scale tier change) gets one `Cell:pos:cell` row with
80-552 cells 7-14 m apart (host (146 76), guest (133 70)) in the comparison that falls into a level change (lab
20261106-223318, 20261106-224058 #1). detector_report.py files it under BUG-0480.

## Root cause
Inferred, confirmed by the A/B runs below: during the ~3.5 s world rescale (WorldScaleStep 0xE73E50 / 0xE6DB70,
BUG-0511, docs/re/cell.md#cell-world-scale ✔) each game's cell positions `+0x4C` and sizes `+0x58` are divided by
the ratio at its own moment (the guest 1-4 frames after the host), so for ~3 s the two dumps are in two scales;
worlddiff compared the raw numbers. `coop.census all` did not even report the scale. The rows: cells off by one
factor about the origin (host (137 78) guest (131 74); host (106 96) guest (100 90)); with positions converted
only, the sizes gave the same class as `Cell:scale:cell` (191 cells 0.91 host / 0.80 guest, lab 20261107-141059).

## Fix
`coop.census all` ends the Cell `me` line with the game's world scale (`wscale=` G+0x514C, cellsync::scale_now).
worlddiff parses it (`World.wscale`) and, when both Cell dumps give a scale and they differ, compares a copy of the
guest's world with every position (rows and `me`) and every cell's size times guest scale / host scale, as the
sync-check's diag.cell_scale (BUG-0606) does. A real offset is kept (it is the same in both scales). A dump without
`wscale` is compared raw. Switch: `SPORE_DETECTOR_CELL_SCALE=0` (raw positions and sizes, 0.7.9).

## Verification
Lab, lane 9 (free 10), SPORE_DETECTOR=20, cell_levelup_view x2 + play_cell x2 each:
- FAIL (SPORE_DETECTOR_CELL_SCALE=0): lab 20261107-134532: cell_levelup_view #1 `Cell:pos:cell` n=28 (6 m), #2 n=127
  (8 m): 2 of 2 runs. play_cell 0 of 2 (26 / 25 samples; its samples did not fall into a level change).
- PASS, positions only converted: lab 20261107-141059: no `Cell:pos:cell` in 4 runs, but `Cell:scale:cell` n=191
  (cell_levelup_view #2), hence the size conversion (commit 0243b621).
- PASS (final): lab 20261107-143741: no `Cell:pos:cell`, no level-change `Cell:scale:cell` in 4 runs (55 samples).
  Still reported there: `Cell:scale:cell` n=3 (0.11 host / 0.06 guest, play_cell #2), the small-cell rows also seen in
  the FAIL run (BUG-0480 class): the detector still finds rows that are not the scale.
- Test outcomes unrelated to the detector (it is not a gate): 20261107-141059 play_cell#1 FAIL `the host's NPCs hunt
  the guest too` (passed in the 5 other play_cell runs); 20261107-143741 cell_levelup_view#1 FAIL monitor drawn 18.0 m
  (limit 17) after a level change: new ticket BUG-0681 (the monitor's drawn invariant, likely the same class).
- Selftest: `tools_selftest.py worldstate` (BUG-0662 case): a cell off by the scale only (k 1.15) is no row, a cell
  10 m off and a cell of another size still are; a dump without `wscale` is compared raw. On the old worlddiff
  (`SELFTEST_WORLDDIFF=<rel/0.7.10>/tools/tests`) 2 checks FAIL; on the new one all pass.

## History
- 2026-11-06: opened.
- 2026-11-07: fixed on fix/w46-detector-scale (census wscale, worlddiff converts the guest into the host scale).
