---
id: BUG-0349
title: Cell: after a world reload a guest keeps the world's cell while the host shows the guest's own cell (the own cell is applied once per session)
status: fixed
kind: desync
stage: Cell
reported: 2026-10-31
source: player reports 2026-10-31 (second batch: 11 zips, host and guest)
role: guest
versions: 0.7.3.1
fixed_in: 0.7.3.2
branch: fix/w22-reports
commits: c8f91ff
tests: cell_own_load (tools/tests/coop_cell_own_load.py)
signature:
docs: docs/re/creations.md (4.2 Cell stage), docs/re/cell.md#cell-rebuilds-in-session
related: BUG-0342, BUG-0345, BUG-0352
switch: creations.cell_reapply
---

## Report
Host on GOG 2024, guest on Steam (both Windows, 0.7.3.1), Cell, one world loaded several times
(the host's evolutions and a manual "share" each send the world again). Host mark (19:13:59 host
time): "Guest looks as his original self but he sees himself as his old self". Later the guest's
own cell editor produced a cell made from the host's species, carrying the host's species name.

## Root cause
Observed (guest log of the 19:21 zip, host log of the 19:31 zip):
* First load of the world (19:06:28 guest time): `own cell is now 29104A34 (level rebuilt)`: the
  guest's choices.ini cell is applied (and the ghost of BUG-0342 follows).
* Every later load of a world in the same session where the own key is unchanged: no rebuild,
  `own avatar cell model 29104A34 -> 291053A3: ... the game has 291053A3 (DIFFERENT)` (19:12:51) and
  `29105088 -> 291053A4 ... (DIFFERENT)` (19:19:04). The guest's game plays the world's cell (the
  host's species) for the rest of that load, while the host draws the guest's puppet from the
  guest's own creation (`puppet of 1: cell model 7C80307D`, later 7C3CB48C). Each machine shows the
  guest differently: the mark.
* Code (`creations.cpp apply_own_cell`): `if (... k == g_cell_set ...) return;` and `g_cell_set` is
  cleared only at the session end (`on_session_end`). A key applied once is never applied again
  after a load, although the loaded world's avatar has the world's model.
* Consequence (observed): the guest's own cell editor at 19:14:56 opened on the cell its game had
  (291053A3, the host's species), so `back from the editor, own cell 29105088` is an edit of the
  host's species with the host's species name; the guest's own design line is lost and choices.ini
  now names that cell. The previous session (BUG-0342 "load 2") shows the same `(DIFFERENT)` state.

Inferred: the same happens for every world reload of a guest with a chosen own cell (host evolution,
manual share, rejoin without a session end).

## Fix
`creations.cell_reapply` (on; off = 0.7.3.1): `apply_own_cell` forgets the applied key (and a rebuild
it waited for) when `game::stage_epoch(Cell)` changes (a load of a Cell world), so the own cell is
applied again after every load.

## Verification
`tools/tests/coop_cell_own_load.py` (suite `cell_own_load`), reload phase:
* FAIL with `SPORE_FIX_OFF=creations.cell_reapply`: lab 20261031-130510 (after the reload the guest
  plays 4DA56CA5, the world's cell, the host draws BAF1AD40).
* PASS: lab 20261031-130258 and 20261031-130756 (BAF1AD40 on both).

## History
- 2026-10-31: opened from the second batch of the 2026-10-31 report zips (host mark, guest log).
- 2026-10-31: fixed on fix/w22-reports (creations.cell_reapply).
- 2026-11-01: released in 0.7.3.2.
