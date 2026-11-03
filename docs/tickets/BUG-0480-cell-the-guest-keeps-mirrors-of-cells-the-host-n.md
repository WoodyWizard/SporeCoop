---
id: BUG-0480
title: Cell: the guest keeps mirrors of cells the host no longer has (stale mirrors) and misses some the host has near it
status: open
kind: desync
stage: Cell
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in:
branch: fix/w40-cell-evo
commits:
tests: cell_gone, cell_gone_probe, cell_gone_play (extended), coop_cell_gone.py
signature:
docs: docs/06-testing.md#testing-world-lod, docs/re/cell.md#cell-stale-mirrors
related:
---

## Report
Detector: `Cell:guest_only:cell` in 16% of 242 Cell comparisons (a proxy with a host index the host has no cell for near the guest), `Cell:host_only:cell` 2% (up to 75 host cells 25 m from the guest without a proxy), `Cell:scale:cell` 1% (0.93 / 0.78). Class G5 (lifecycle and resync of unknown / gone ids).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (fix/w40-cell-evo, lane 4): the mirror lifecycle holds under loss. A guest that misses the host's state
for 4 s while the host destroys 8 cells near it has no mirror of them once the state flows again (coop_cell_gone.py
--case lost, 0 stale in 6 rounds, out/lab/20261103-154618); without loss, both swimming, 0 stale in 4 of 6 runs.
The detector's numbers were mostly its own timing: one host dump next to one guest dump, while Cell cells live
seconds (eaten food, killed prey) and a host level change replaces the pool between the two dumps (369 at once,
host 397 rows / guest 1050, out/lab/20261103-012624 play_cell#2); the purge then is 0.1 s (a complete round) or
1 s (a lost chunk) behind.
Observed, cause not found: in 3 of 9 play runs (cold starts, round 5 or 6) 28-49 mirrors at once (kinds meat /
rock / bubble, level 0-1, keepalive) whose host index the host had in neither dump nor in `coop.pool cell` a moment
later, although the guest's mirror entry had been refreshed by the host's state 0.1-0.2 s before and the host
never sent an index its cell_at did not find at the send (a one-off check, removed: out/lab/20261103-155103, 20261103-155433, 20261103-161952 #3).
Inferred, not verified: cells the host's game has only during part of its frame (the partner tiles populate for
the guest's view, 21 partner tiles in that log): sent from inside the frame, gone when the console looks.

## Fix
Detector: the Cell compare dumps the host before and after the guest (tools/tests/worlddiff.py CELL_BRACKET,
SPORE_DETECTOR_BRACKET=0 = the old single dump): a proxy is `guest_only` only when the host had the index in
neither dump. No mod change: the lifecycle path (rounds, 1 s fallback) was not shown wrong. Test commands
`coop.pool statehold <s>` (guest), `coop.pool cull <r> <n>` (host), `coop.pool cell <key>` now shows a mirror's
age and the foreground flag; a one-off check in the host's send (removed again) counted sent cells cell_at did not find: none.

## Verification
cell_gone PASS out/lab/20261103-154618; cell_gone_probe (the probe sees mirrors kept while the state is lost:
its FAIL case) PASS out/lab/20261103-155103; cell_gone_play FAIL out/lab/20261103-161952 (#3), PASS #1 #2 and
out/lab/20261103-155835 (3 of 3). Detector over 10 rounds of cell_gone_play: one host dump 2 x guest_only:cell,
bracketed 0 (out/lab/20261103-155433).

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-cell-evo: lifecycle checked under loss (no hole found), detector bracketed; the 28-49 mirror events left open.
