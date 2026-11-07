---
id: BUG-0681
title: Monitor: Cell drawn invariant compares positions in two world scales during a level change (cell_levelup_view gate)
status: open
kind: test
stage: Cell
reported: 2026-11-07
source: fix/w46-detector-scale lab 20261107-143741 cell_levelup_view#1
role: both
versions: 0.7.10-dev
fixed_in:
branch:
commits:
tests: cell_levelup_view
signature: MONITOR-VIOLATION drawn guest: guest draws host 18.0 m from where host's own game has it (limit 17.0 m, Cell) during a level change
docs:
related: BUG-0662, BUG-0519, BUG-0606
---

## Report
cell_levelup_view (food +-150, six world scale tier changes) failed the lab gate once in 4 runs on fix/w46-detector-scale
(lab 20261107-143741 #1, lane 9): `MONITOR-VIOLATION drawn guest: guest draws host 18.0 m from where host's own game
has it (limit 17.0 m, Cell) on 2 lines from s 60`, right after a level change (change 5, up). The same kind of line
was in the run BUG-0662 came from (lab 20261106-223318: `drawn host: host draws guest 20.1 m`, then not a gate).
The other runs of the day (20261107-134532 x2, 20261107-141059 x2, 20261107-143741 #2) had no violation.

## Root cause
Inferred (not verified): the drawn invariant (tools/tests/monitor.py, `drawn=` of the partner line) compares the
puppet as one game draws it (in that game's world scale: cell.puppet_scale, BUG-0519, converts it) with the
partner's own position in the partner's scale; the partner line carries no scale, and for ~3 s of a level change the
two scales differ (WorldScaleStep, docs/re/cell.md#cell-world-scale). The same class as BUG-0662 (worlddiff) and
BUG-0606 (sync-check). Not looked at: whether the line's positions are both read at the same moment.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
