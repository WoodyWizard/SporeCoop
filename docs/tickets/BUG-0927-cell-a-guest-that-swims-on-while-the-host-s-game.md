---
id: BUG-0927
title: Cell: a guest that swims on while the host's game stands (a caption, a cinematic, the pause menu) gets the cells around the place it was at, an empty sea around it
status: fixed
kind: desync
stage: Cell
reported: 2026-11-11
source: 0.7.12 final check, lab 20261111-044257 campaign (lane 2): FINDING Dave:coverage
role: guest
versions: 0.7.x
fixed_in: 0.7.12
branch: fix/w47-final12
commits: de35e50a
tests: cell_host_paused
signature:
docs: docs/re/cell.md#cell-send-centres
related: BUG-0174, BUG-0295, BUG-0348
---

## Report
The 0.7.12 final check's campaign (lab 20261111-044257, lane 2, four players) ended its Cell phase with
`FINDING [desync] Dave: too few of the host objects in cell (Dave:coverage)`: the fourth player had 12-13 % of the
host's 159-164 cells within 60 m of its own cell (138-144 missing) while its game held 850 mirrors; its screen
showed open water (the finding's screenshot; its log: `screen: plants=0 meat=0 creatures=0 nearest plant=52`).
For a player: the host reads a caption (or sits in the pause menu, or a cinematic plays) for a while, a guest swims
on and the sea around it is empty until the host plays again.

## Root cause
Observed:
* The host's game stood from 04:50:13 to the end of the phase (110 s): its trace has `busy=cinematic,input_lock`, its
  own cell never moved (-32.7 35.0), its screen counts stayed the same (a caption that waits for its check mark, at
  food 123). The `tiles:` line of the tile pass, logged every 10 s before, came once in those 110 s.
* The guest swam 100 m in that time ((44 -54) to (70 53)); its mirrors stayed 850-855 and none was near it.
* mods/coop/cellsync.cpp `host_tick` (10 times a second, also while the simulation stands) chose each guest's cells
  (within 120 m of the guest's centre, its nearest 900: BUG-0174, BUG-0295) with `g_views`, which only the
  PopulateTiles hook fills. Lab 20261111-063915 (two players, the host in its pause menu, the guest jumps from 150 m
  away to 20 m from the host): `tile_passes` 1855 -> 1855 over 8 s, the guest kept its 555 mirrors of the old place
  and had 10 % of the host's 592 cells around it; all of them 5 s after the host resumed.
* Not the fourth player, not a limit: the other two guests swam on as well, but the host had 4 and 1 cells within
  60 m of them (under 5: the check does not judge); around the fourth the host's world still held 159 cells.
* The 0.7.11 final check's campaign (rel/0.7.11 lab 20261108-114435) has no coverage finding: its host never stood
  that long in Cell. The code is the same since BUG-0174 (0.7.3.x): not a 0.7.12 regression.

## Fix
`host_tick` takes the view centres from the registry (`views::partners`, noted by coop.cpp for every partner shown,
in every frame) in every send tick; the tile pass keeps its own set. `coop.pool` prints
`cell_send_centres: fix= tile_pass_behind= max= tile_passes=` (how far the tile pass's centres are behind).
Switch: `coop.fix cell.send_centres off`.
The host still populates nothing new while it stands (the game's tile pass does that): a guest that swims past the
populated area finds no new cells until the host plays again; it does get every cell the host's world has there.

## Verification
Test `cell_host_paused` (tools/tests/coop_cell_host_paused.py, lane 5): the guest 150 m away, the host opens its
pause menu, the guest jumps back next to the host; the guest's coverage of the host's cells within 60 m, as the
campaign measures it.
* FAIL: out/lab/20261111-063915 (`SPORE_FIX_OFF=cell.send_centres`): 10 % of 592 in three measurements (and the lab's
  sync-check gate: `c: 530 missing on the guest`).
* PASS: out/lab/20261111-064059, out/lab/20261111-064230.

## History
- 2026-11-11: opened.
- 2026-11-11: root cause (stale view centres while the host stands), fixed on fix/w47-final12 (cell.send_centres).
- 2026-11-11: released in 0.7.12.
