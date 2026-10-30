---
id: BUG-0307
title: Cell, 4 players: a guest's own cell goes NaN again and again (26 times), its mirrors churn, fps falls to 1-3 and its address space drains
status: open
kind: desync
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign#2
role: guest
versions: 0.7.3.1
fixed_in:
branch: fix/w12-cell-fault
commits:
tests: cell_nan_all, cell_churn, campaign, cell_4p, cell_nan_drain, cell_jumps
signature:
docs: docs/known-issues.md#issue-cell-own-nan
related: BUG-0299, BUG-0292, BUG-0306, BUG-0314
---

## Report
Lab 20261030-104149 campaign#2 (main 813f8cd, lane 4), guest Carol (instance 24, the 3rd player, 900
mirrors, 160-250 m from the host): `our own cell's position is not finite (BUG-0292): 900 mirrors, 0
of them with a pose that is not finite` 26 times between 11:45:54 and 11:52:14 (game clock), healed
by BUG-0299's guard each time. Its mirror count swung 900 -> 307 -> 43 -> 900 ... between the 10 s
lines, the campaign's coverage for Carol fell to 57% and then 1% (9 mirrors), its fps went from 26
(avg) to 3, 2, 1, and its free address space from 3502 MB to 1427 MB in 6 minutes (the other three
games stayed at ~3500 MB). The first NaN came at fps ~26, before the slowdown.

## Root cause
Why it repeated (reproduced): BUG-0299's heal repaired the position, the previous position, the
particles' positions and velocities (and stop_at the velocity and acceleration), but not the
rotation (+0x5C) and shape (+0x24) matrices, the size, the mass, or the particles' rest offsets of
the shape matching (`parts+0x11944`). With any of those NaN the next soft-body step makes the
position NaN again: `coop.pool nanself all` (every field NaN) with `SPORE_FIX_OFF=cell.nan_heal_all`
stays NaN although the heal runs at every frame (1155 heals in 15 s, lab 20261030-122306). The heal
also started only on a NaN position, not on a NaN in the other fields.

What made the first NaN: still not known. Observed: no NaN mirror (BUG-0292's check), the guard heals the position, the NaN comes
back within seconds; the mirror churn follows (the host sends the cells around the guest's
reported position). Address space, negative results: re-creating all the guest's mirrors 40 times (28 049 creations,
`cell_churn`, lab 20261030-121142) cost 0 MB (3638 -> 3638 MB free); 15 s with the own cell NaN in
every field and the heal failing at each frame cost 0 MB (3647 -> 3647 MB, lab 20261030-122306). So
neither the churn nor the NaN state itself drains it; what does is not known (candidates not
checked: model / resource loads for the new cells around a jumping centre, something that grows
with the frame time at fps 1-3). The
host's fault loop BUG-0306 ran in the same minutes (from 11:47:35) and froze part of the host's
pool, which may have worsened the coverage. Source of the NaN: docs/known-issues.md#issue-cell-own-nan.

## Fix
Partial (the repetition, not the source): `coop.fix cell.nan_heal_all` (cellsync.cpp heal_own): any
non-finite field of the own cell (position, previous position, velocity, acceleration, rotation and
shape matrices, size, mass, the particles' positions, velocities and rest offsets) starts the heal,
and each of them is repaired (matrices to identity, size and mass to the last finite ones, rest
offsets on the ring of the particles). Diagnostics for the source: `own_probe` logs the first probe
of a frame that sees a non-finite field (cell step start, before / after each own collision pair and
the own soft-body step, our guest tick, the mirror moves) with the last probe that saw it finite,
the cells near, non-finite cells and shared particle indices; `coop.pool` prints `own_nf=`,
`shared_parts=`, `vm_free_mb=`.

## Verification
The repetition: FAIL `SPORE_FIX_OFF=cell.nan_heal_all` cell_nan_all, lab 20261030-121742 and
20261030-122306 (position NaN in 8-9 of 15 samples, after the level-ups too). PASS cell_nan_all, lab
20261030-121908 (one heal, 0 of 15 non-finite; cell and cell_nan of the same run passed, no heal in
the plain cell test). The natural first NaN was not reproduced: no own_probe line in campaign_short x2 + cell_4p x2 (lab
20261030-120947, c2fcc91), cell_4p + campaign_short (20261030-122501, 918f075), campaign_short x2
(20261030-123430, 918f075, lanes 4/7 while lanes 2/3 ran campaign_short + campaign; fps 35-54, not
the 1-3 of the original run).

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w13-cell): the coverage loss explained: the NaN heals made the own cell jump
  100-200 m, every jump left the background decoration cells of the place in the pool, and the pool
  reached the guest's mirror cap (1500): no mirror was created any more. Reproduced with jumps alone
  and fixed as BUG-0314 (`cell.bg_cull`). Still open: the first NaN (no own_probe line in 4-player
  runs with level-ups, a guest at 3 fps and 80 jumps, nor in coop_sync_multi at 15 / 30 fps) and the
  address space drain. Negative: 3 minutes of a NaN own cell with the heal off, level-ups and
  swimming kept 3632 MB free (lab 20261030-133256, cell_nan_drain); 4 players with Carol jumping at
  3 fps (28 538 mirrors created) kept 3613-3620 MB. docs/re/cell.md#cell-bg-leftovers.
