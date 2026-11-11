---
id: BUG-0887
title: Cell: the partner's cell puppet still overshoots a stop in a latency spike (drive.puppet_fall is set for Creature only)
status: open
kind: desync
stage: Cell
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0680, BUG-0674, BUG-0408
---

## Report
Inferred: in Cell, when the partner's cell stops or turns during a latency spike, this game draws it going on along the old velocity for up to 0.25 s, then stepping back when the next state comes: the BUG-0680 / BUG-0674 shape in another stage.

Severity: low. Confidence: low.

## Root cause
* Observed (code): mods/coop/coop.cpp:901 `r.buf.interp_fall = g_mode == modes::kCreature;`; puppets of other stages extrapolate at full speed up to max_extrap 0.25 s (net/interp.h:533-552).
* Observed (code): mods/coop/modes.cpp:1337-1365 cell_drive puts the cell exactly at the sampled pose every frame (cell_move(c, s.pos)), so the overshoot is drawn.
* Inferred (low): the visible size depends on Cell swim speeds; not measured.

## Fix idea
Set interp_fall for the Cell puppet too (or for every puppet), with the same drive.puppet_fall switch.

## How to test
A Cell variant of puppet_stops (SPORE_NET=player): the walker's cell swims and stops 10+ times; assert the guest's drawn partner cell (the game's cell position) ends within a Cell-scaled tolerance of the walker's own stop. The net_tests puppet_stop_past case with Cell-like speeds is the deterministic FAIL with interp_fall off.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0680. Severity low, confidence low.
