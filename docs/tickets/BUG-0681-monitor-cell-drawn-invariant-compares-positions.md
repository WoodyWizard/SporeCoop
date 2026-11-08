---
id: BUG-0681
title: Monitor: Cell drawn invariant compares positions in two world scales during a level change (cell_levelup_view gate)
status: fixed
kind: test
stage: Cell
reported: 2026-11-07
source: fix/w46-detector-scale lab 20261107-143741 cell_levelup_view#1
role: both
versions: 0.7.10-dev
fixed_in: unreleased
branch: fix/w46-k7-findings
commits: 839499be
tests: cell_levelup_view, gates_selftest monitor (f_drawn_scale*)
signature: MONITOR-VIOLATION drawn guest: guest draws host 18.0 m from where host's own game has it (limit 17.0 m, Cell) during a level change
docs: docs/06-testing.md#testing-monitor-cell-scale
related: BUG-0662, BUG-0519, BUG-0606
---

## Report
cell_levelup_view (food +-150, six world scale tier changes) failed the lab gate once in 4 runs on fix/w46-detector-scale
(lab 20261107-143741 #1, lane 9): `MONITOR-VIOLATION drawn guest: guest draws host 18.0 m from where host's own game
has it (limit 17.0 m, Cell) on 2 lines from s 60`, right after a level change (change 5, up). The same kind of line
was in the run BUG-0662 came from (lab 20261106-223318: `drawn host: host draws guest 20.1 m`, then not a gate).
The other runs of the day (20261107-134532 x2, 20261107-141059 x2, 20261107-143741 #2) had no violation.

## Root cause
Observed (the PASS runs below, with the new `wscale=` field): the drawn invariant compared the puppet as the viewer's game draws it (in the viewer's Cell world scale) with the partner's own `pos=` in the partner's scale. At a level change each game rescales its world at its own moment (WorldScaleStep, docs/re/cell.md#cell-world-scale): in lab 20261108-003018 every run had 4-6 trace lines whose two games stood in two scales (k 0.53 / 0.61, 1.42 / 1.50, ...), raw distances 0.7-20.3 m, the same pairs 0.0-1.0 m apart in one scale. The original failing line of lab 20261107-143741 #1 (fix/w46-detector-scale) fits it: host pos (87.0 89.7) drawn by the guest at (74.4 76.8), and at s 60 (94.6 94.4) drawn at (101.7 101.4): both coordinates off by one factor (0.855 / 1.075), the scale ratio. The same class as BUG-0662 (worlddiff) and BUG-0606 (sync-check). Both positions of a line are read at the same 10 s mark (monitor_frame_start / trace_tick of one frame on each game).

## Fix
The own trace line in Cell ends with the game's world scale (`wscale=` = cellsync::scale_now(), G+0x514C, as the census `me` line of BUG-0662). tools/tests/monitor.py (Drawn.in_scale) converts the partner's own position into the viewer's scale (pos x own scale / viewer scale: pos x scale is the same for a cell at rest through a change) when both own lines of that moment carry a scale and they differ; a log without `wscale=` is compared raw as before. Switch: `SPORE_MONITOR='{"drawn_scale": 0}'` (raw positions, the rule before the fix).

## Verification
- FAIL (the rule before, drawn_scale 0): the original run lab 20261107-143741 #1 (fix/w46-detector-scale, lane 9): `drawn guest: guest draws host 18.0 m (limit 17.0 m, Cell) on 2 lines from s 60`; and a replay of the new logs of lab 20261108-003018 #4 with drawn_scale 0: `host draws guest 20.3 m (limit 19.0 m)` and `guest draws host 18.9 m (limit 16.2 m)` on 2 lines from s 30 (with the conversion: 0.6 / 0.7 m, no violation). A lab run with `SPORE_MONITOR='{"drawn_scale": 0}'` (lab 20261108-004130, 4 runs) passed: its scale pairs fell on single lines (19-32 m raw on one line, the gate needs 2 in a row), as 3 of 4 runs before the fix did.
- PASS: lab 20261108-003018 cell_levelup_view 4/4 (lane 9), no drawn violation, 24 lines in two scales all within 1.0 m in one scale.
- Selftest: gates_selftest `monitor: drawn: Cell positions in two world scales are compared in one` FAILS on the old monitor.py (1 check), the new one passes all; a real 8 m offset in one scale and a log without `wscale=` are still violations.

## History
- 2026-11-07: opened.
- 2026-11-08 (fix/w46-k7-findings): fixed: the own Cell trace line carries `wscale=`, the monitor's drawn rule compares in one scale.
