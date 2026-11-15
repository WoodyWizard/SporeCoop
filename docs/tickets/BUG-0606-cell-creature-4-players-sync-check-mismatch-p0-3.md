---
id: BUG-0606
title: Cell -> Creature, 4 players: sync-check MISMATCH p0 359-740 apart on every guest during the campaign's Cell advance (food 1000 shortcut)
status: fixed
kind: desync
stage: Cell
reported: 2026-11-05
source: lab fix/w45-crashes 20261105-233139 campaign_ct#1
role: guest
versions: 0.7.8
fixed_in: 0.7.9
branch: fix/w45-cell
commits: e6e8164b, 0d9e7d10
tests: campaign_ct, cell_end_sync
signature: sync-check: MISMATCH with the host (Cell ...): p: 1 apart (p0 359-740) right after `coop.pool food 1000` in the Cell advance
docs: docs/re/cell.md#cell-sync-scale
related: BUG-0393, BUG-0377, BUG-0511, BUG-0519, BUG-0378
---

## Report
Found on fix/w45-crashes (0.7.8 + the BUG-0403 hook writer), lab 20261105-233139 campaign_ct#1 (lane 5,
4 players, SPORE_DETECTOR=20): the lab's sync-check gate failed. At 23:35:15 the campaign's Cell advance gave
the host food 1000 (`coop.pool food 1000`, the shortcut); the host's cell grew (size 0.82 -> 2.29 -> 2.68),
its pool went from 1108 to 20 to 389 cells, and every guest's sync-check found the host's player cell p0 apart:
Carol 739.9 (4 objects compared) at 23:35:24, Dave 370.8 and Bob 358.8 (155-161 objects) at 23:35:27 / 31.
The host's trace line showed Dave at d=4482. The advance went on (captions ok, the host played Creature 38 s
later). The same test passed on lane 5 at 23:19 (20261105-231922) and campaign_tribe twice; no other campaign
log of the lab history has a Cell `p0` MISMATCH in the hundreds (searched all `campaign*.game.log`).

## Root cause
A sync-check artifact, not a desync players see. Observed (re-read of the run): food 1000 started the end of
Cell; the host's position went 42 -> 3512 -> 42301 at ~2800 m/s "in the game's numbers" while each guest drew it
(drawn=(3219.6 2606.9) at s 200 for host pos 3512.7 2784.7) and the guests' own cells sat at ~0. Observed at runtime
(coop_cell_end_sync.py, 2 players, cell fixture): the end of Cell changes the world scale 0.5 -> 15000 in ~11 s on
each game at its own moment (host 3.78 / guest 1.93 at one sample); the guest draws the host's cell 0.04-0.07 m
(p90) from where it is, but the sync-check compared the guest's raw positions (in its scale) with the host's (in
its own): `p: 2 apart (p0 27.6 p1 26.6)`, `c: 87 apart`. The same class as BUG-0511 / BUG-0519 (positions of two
games in two world scales), in the check. Inferred: campaign_ct's 359-740 m are the same comparison at the much
larger numbers of that run (why the host's numbers grew there and shrank in the fixture was not found).

## Fix
`coop.fix diag.cell_scale` (diag_sync.cpp, net/diagmsg.h): the digest carries the sender's Cell world scale (an
appended float; an older reader stops before it, an older digest reads 0 = not converted); the host converts the
guest's Cell positions by guest scale / host scale before comparing. Off: raw positions (0.7.8).

## Verification
cell_end_sync: FAIL with SPORE_FIX_OFF=diag.cell_scale, lab 20261106-005031 (8 of 30 checks with positions
apart: p0 / p1 10-29 m, cells 26-35 m); PASS lab 20261106-005151 (0 apart in 29 checks; the guest's drawing p90
0.04 m in both runs). Not re-run: campaign_ct with 4 players.
cell_end_sync PASS again in the regression run lab 20261106-015141 (23/23; play_cell with SPORE_DETECTOR=20: 37 Cell comparisons, no finding).

## History
- 2026-11-05: opened.
- 2026-11-06 (fix/w45-cell): a sync-check artifact (world scales), fix diag.cell_scale, test cell_end_sync.
- 2026-11-07: released in 0.7.9.
