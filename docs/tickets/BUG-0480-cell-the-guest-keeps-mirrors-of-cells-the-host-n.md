---
id: BUG-0480
title: Cell: the guest keeps mirrors of cells the host no longer has (stale mirrors) and misses some the host has near it
status: fixed
kind: desync
stage: Cell
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in: 0.7.9
branch: fix/w40-cell-evo, fix/w45-leftovers
commits: 34b41821 f68e3316 2191c794
tests: cell_gone, cell_gone_probe, cell_gone_play (extended), coop_cell_gone.py
signature:
docs: docs/06-testing.md#testing-world-lod, docs/re/cell.md#cell-stale-mirrors, docs/re/cell.md#cell-short-lived
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
Observed (fix/w45-leftovers, 2026-11-06, lane 7): the 28-141 "stale" mirrors at once were not stale. Reproduced with
the old oracle in 3 of 8 cold cell_gone_play runs (out/lab/20261106-111736 #2, 20261106-113202 #2 #4). A new host-side
record of each sent index (`coop.pool cell <key>` on the host: last / first send, sends) showed every such cell sent
0.12-0.28 s before the question, for 0.9-1.7 s in all, its slot free again: the host's game made and destroyed them
between its two dumps, and the guest's mirror was 0.1-0.2 s behind (one or two state rounds). The destroyer was the
game's own cull: DestroyCell called from 0xE7C3E6 in CellPhysicsStep 0xE7C080 (ShouldCullCell 0xE5F8C0: the parent
tile +0x36C deleted and IsTileActive refusing the cell's own tile), 470 cells per run less than 2 s after their
first send: the game's streaming at the edge of the gameplay range when the view moves (the test teleports both
cells 20 m a round), in single player too. The mirror lifecycle is right; the oracle was wrong.
The other half (host cells near the guest without a mirror): 0 within 25 m in 150 rounds of 15 runs.

## Fix
Detector: the Cell compare dumps the host before and after the guest (tools/tests/worlddiff.py CELL_BRACKET,
SPORE_DETECTOR_BRACKET=0 = the old single dump): a proxy is `guest_only` only when the host had the index in
neither dump. No mod change: the lifecycle path (rounds, 1 s fallback) was not shown wrong. Test commands
`coop.pool statehold <s>` (guest), `coop.pool cull <r> <n>` (host), `coop.pool cell <key>` now shows a mirror's
age and the foreground flag; a one-off check in the host's send (removed again) counted sent cells cell_at did not find: none.
fix/w45-leftovers: no mod behaviour change (nothing wrong in the mod). The test oracle: a mirror in neither host dump
is stale only when the host stopped sending its index >= 1 s before the guest's dump (the host's last send); the
play case also fails on host cells within 25 m of the guest (in both host dumps) without a mirror. Diagnostics: the
host's `coop.pool cell <key>` for a cell it does not have names its last / first send and the callers of DestroyCell
for cells destroyed < 2 s after their first send. Symbols CellPhysicsStep, ShouldCullCell.

## Verification
cell_gone PASS out/lab/20261103-154618; cell_gone_probe (the probe sees mirrors kept while the state is lost:
its FAIL case) PASS out/lab/20261103-155103; cell_gone_play FAIL out/lab/20261103-161952 (#3), PASS #1 #2 and
out/lab/20261103-155835 (3 of 3). Detector over 10 rounds of cell_gone_play: one host dump 2 x guest_only:cell,
bracketed 0 (out/lab/20261103-155433).
fix/w45-leftovers: old oracle FAIL out/lab/20261106-111736 (#2, 29 "stale"), 20261106-113202 (#2 32, #4 141, the
host's last-send record on them); new oracle PASS out/lab/20261106-115611 (4 of 4, the same events read "143 / 28
mirrors in neither host dump: 0 stale, sent up to 0.06 / 0.16 s before the guest's dump") and 20261106-114009
(cell_gone_play 3 of 3, cell_gone 3 of 3, cell_gone_probe 3 of 3: the probe still catches mirrors kept while the
state is lost, so the new oracle can fail). Detector: play_cell 0 Cell findings in 27 comparisons
(out/lab/20261106-114009-2).

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-cell-evo: lifecycle checked under loss (no hole found), detector bracketed; the 28-49 mirror events left open.
- 2026-11-06: fix/w45-leftovers: the remaining events were cells the game culls ~1 s after making them (0xE7C3E6), the mirrors 0.1-0.2 s behind; the oracle uses the host's last send; no host cell near the guest unmirrored; fixed (test).
- 2026-11-07: released in 0.7.9.
