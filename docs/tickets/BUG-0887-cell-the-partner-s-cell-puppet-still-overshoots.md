---
id: BUG-0887
title: Cell: the partner's cell puppet still overshoots a stop in a latency spike (drive.puppet_fall is set for Creature only)
status: not-reproduced
kind: desync
stage: Cell
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch: fix/w48-cell
commits: f6d5a6b2, e272f8c5
tests: cell_puppet_stops
signature:
docs: docs/re/cell.md#cell-c1-0713
related: BUG-0680, BUG-0674, BUG-0408
---

## Report
Inferred: in Cell, when the partner's cell stops or turns during a latency spike, this game draws it going on along the old velocity for up to 0.25 s, then stepping back when the next state comes: the BUG-0680 / BUG-0674 shape in another stage.

Severity: low. Confidence: low.

## Root cause
Not reproduced: over the player network (SPORE_NET=player: 250 ms spikes every 7 s) the guest's cell swam back and
forth 30 times (a stop and a turn every leg); both games recorded the players' cells (`coop.bodyrec start kinds p`).
The host's copy was never ahead of the guest's own cell along its way: at most 0.03 m in 1351 rows on the code
without a fall-off in Cell (lab 20261112-065254) and 0.01 m with Creature's fall-off in Cell too (20261112-065045);
p50 0.22 m behind (the 0.1 s delay). A cell slows down over ~1 s, and the state's velocity is the cell's +0x90
(inferred small for the soft-body avatar: not read), so the 0.25 s of extrapolation adds nothing visible.

## Fix idea
Set interp_fall for the Cell puppet too (or for every puppet), with the same drive.puppet_fall switch.

## How to test
A Cell variant of puppet_stops (SPORE_NET=player): the walker's cell swims and stops 10+ times; assert the guest's drawn partner cell (the game's cell position) ends within a Cell-scaled tolerance of the walker's own stop. The net_tests puppet_stop_past case with Cell-like speeds is the deterministic FAIL with interp_fall off.

## Fix
None. The switch tried (`drive.cell_fall`: interp_fall in Cell, commit f6d5a6b2) was taken out (e272f8c5).

## Verification
no-fail-run: not reproducible; `coop_cell_puppet_stops.py` (suite `cell_puppet_stops`, extended) stays as a
regression test (passes without a fix, lab 20261112-065254).

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0680. Severity low, confidence low.
- 2026-11-12 (fix/w48-cell): not reproduced (never ahead after turns in spikes), the tried switch taken out.
