---
id: BUG-1394
title: Cell: a level change moves the whole world of a game whose camera point is over 1000 m from the origin (the game recentres, the partners do not): campaign_short#2 sync-check p: 1 apart (p0 171.3)
status: open
kind: desync
stage: Cell
reported: 2026-11-21
source: lab
role: 
versions: 
fixed_in:
branch: fix/w62-core
commits: 472ea2de
tests:
signature: sync-check: MISMATCH with the host (Cell ...): p: 1 apart (p0 171.3) during the end of Cell
docs: docs/re/cell.md#cell-rescale-centre
related: BUG-0606, BUG-0519, BUG-0511
---

## Report
Third final check of 0.7.16, campaign_short x3 (lab 20261121-094225): #2 FAIL on the sync-check gate, "MISMATCH with
the host (Cell 9652F8AD, 4 objects compared): p: 1 apart (p0 171.3)" on Dave (extra2), "(p0 192.5)" on Bob, right
after the campaign's `food 1000` (the end of Cell). #1 and #3 PASS. This signature: once in 138 campaign_short runs.

## Root cause
Observed (logs) and Ghidra; old behaviour of the game, not a 0.7.16 or w61 change (no w61 commit touches Cell
positions or the sync-check's scale conversion; 0xE6DB70 has always worked this way).
- Ghidra: WorldScaleStep 0xE73E50 calls 0xE6DB70 with the camera's drawn point (cCellUI +0x48). Within 1000 m of the
  origin the centre is (0 0 0); farther out it is that point floored to a grid (0xE4F900), and every position, the
  tiles, the camera become (pos - C) / ratio: that game's world is moved by C, not only scaled
  (docs/re/cell.md#cell-rescale-centre).
- Logs: from s 269.9 (food 1000) the host's cell went (149 176) at scale 0.505 -> (492 -136) at 14829 -> (2724 -13403)
  at 15000 and stayed so for its last 12 s of Cell, while every guest's own cell went to about (0 0) (as all the
  cells in the passing runs #1 / #3). The guests drew the host's puppet at (472 38) -> (2711 -13298): the
  sync-check's 171 / 192 m were a real difference of the two worlds (the BUG-0606 conversion corrects the scales
  only, rightly), so the gate is right: not a gate or test bug.
- Not known: why the host's camera point was over 1000 m from the origin at the first step (its cell was 231 m out;
  the camera update keeps +0x48 in a box round the avatar and does not run during a cinematic). The logs have no
  camera point, and no game may be started in this task.

## Fix
Not fixed yet (no lab or game runs in this task; a fix needs the cause of the far camera point, or a co-op rule for
the recentre: every game moving by the host's C, or the host's game kept centred on the origin as the guests' are,
both need runtime checks of the game's precision and the tile shift). Done now: the mod logs the case once per scale
change on every game: `cell: the world scale change A -> B moves this game's world about the camera point (x y z), N m
from the origin (target +0x54, followed cell +0xE0, own cell, cinematic) (BUG-1394)` (cellsync.cpp
scale_centre_note), so the next run that shows it names the camera's state. Symbols Cell_RescaleAbout /
Cell_SnapToGrid and the CORRECTION of cell-world-scale recorded.

## Verification
Open, waiting for the lab run: campaign_short (and cell_end_sync, cell_levelup_view) with the new log line; a run that
shows it gives the camera state for the fix. no-fail-run: diagnostics only so far.

## History
- 2026-11-21: opened.
- 2026-11-21: fix/w62-core: cause traced to the game's recentre (0xE6DB70), diagnostics line added; open.
