---
id: BUG-0307
title: Cell, 4 players: a guest's own cell goes NaN again and again (26 times), its mirrors churn, fps falls to 1-3 and its address space drains
status: fixed
kind: desync
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign#2
role: guest
versions: 0.7.3.1
fixed_in: 0.7.9
branch: fix/w12-cell-fault, fix/w45-cell-nan
commits: 251c7cc2, e5adadb5, 4075f89e
tests: cell_contact_nan, cell_levelups, cell_nan_all, cell_churn, campaign, cell_4p, cell_nan_drain, cell_jumps, cell_view_nan
signature:
docs: docs/re/cell.md#cell-contact-nan, docs/known-issues.md#issue-cell-own-nan
related: BUG-0621, BUG-0299, BUG-0292, BUG-0306, BUG-0314, BUG-0604
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

What made the first NaN (2026-11-06, fix/w45-cell-nan, docs/re/cell.md#cell-contact-nan): the game's collision
contact solver. Observed (Ghidra): after its pair loop Collisions 0xE7F260 runs ContactSolvePass 0xE654D0, 10 rounds
of sequential impulses (ContactSolve 0xE58C90, ApplyImpulse 0xE560D0) that write the cells' velocity +0x90 and every
particle's velocity, never the positions: exactly the mask 0x104 "at guest tick, finite at collision". The game's
sphere tests (0xE6AD80, 0xE73620, 0xE738F0, 0xE73BB0) leave the contact normal unset when two collision sphere
centres are closer than 0.004: 0xE73BB0 passes the first cell's centre coordinates, the others a stale stack slot.
The solver assumes |n| = 1 and overshoots by ~|n|^2 per round. Observed (runtime, host, cells on one centre 70-120 m
from the origin): normal (-70.16 -96.02 0), 2 cells at 884 m/s, 4 cells at inf (lab 20261106-111709). Inferred,
not reproduced on an own cell: the guest's case was such a contact of its own cell whose stale-slot normal held
coordinates (the graphics pool was full in that second, BUG-0355, so cells next to it took the stale-slot paths), and
it repeated for 17 frames because the heal put the own cell back on the same place next to the same cell each
frame. Not at the level-up (CORRECTION): the NaN frames were at food 96, the level change came at food 102 (s 95).
40 level changes on two games (cell_levelups, lab 20261106-110109 fix off, 20261106-112017 fix on) gave no
non-finite and no non-unit contact: the level change does not make it.
The address space drain is split off as BUG-0621.

Before (2026-10-30): Observed: no NaN mirror (BUG-0292's check), the guard heals the position, the NaN comes
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

Source (2026-11-06, fix/w45-cell-nan): `coop.fix cell.contact_finite` (cellsync.cpp h_ContactSolve, a hook on
ContactSolve 0xE58C90): a contact's normal is made a unit vector before it is solved (the unset one keeps its
direction, a zero or non-finite one becomes +x) and an impulse that is still not finite is not applied. Probes
`before the contact solver` / `contact solver` (h_ContactPass on 0xE654D0) and `contacts_unit=` / `contacts_bad=`
/ `contacts_bad_own=` / `contact_solves=` in `coop.pool`; the first 30 bad contacts are logged with both cells
(diagnostics on in every build, the log is capped).

## Verification
The source: cell_contact_nan (`coop.pool stack2 12 2|4` on the host: cells without a soft body on one centre far from
the origin, the game's own collision and solver, nothing of ours writes a velocity): FAIL with
SPORE_FIX_OFF=cell.contact_finite, lab 20261106-111709 (884 m/s for 2 cells, inf velocity mask 0x4 for 4); PASS lab
20261106-111835 (1.0 and 2.4 m/s, finite). The own-cell part of the same test (`coop.pool stack 20`: a cell's first
sphere on the own cell's, soft-soft) passes in both: its stale normal was a unit one.
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
- 2026-11-06 (fix/w45-cell, package C2): the first natural NaN of the own cell seen since: lab 20261105-142923
  cell_new_game#1 (fix/w44-tests, 2 players, guest), at the guest's level-up (s 93, size 0.82 -> 0.48): mask 0x104
  (the velocity +0x90 and the particles' velocities, positions finite), 17 frames in 0.24 s, each "at guest tick,
  finite at collision", healed each frame by cell.nan_heal_all, then never again. Its visible damage was a NaN
  camera for the rest of the session (BUG-0604, fixed: cell.camera_heal, test cell_view_nan with the same mask
  injected inside the cell step, `coop.pool nanself vel`). Narrowed (Ghidra, SimUpdate's order,
  docs/re/cell.md#cell-view-nan): the NaN came after the collisions 0xE7F260; 0xE5D5B0, 0xE78D10 and 0xE697E0 do
  not write a cell's +0x90, 0xE73F60 only reads it; not read: RebuildLevel 0xE7FD00 (when G+0x51D8), 0xE53AE0,
  0xE6C9F0 and our own code between the game's step and guest_tick. Candidate not shown: ReTierSoftBody 0xE51A20
  reads old particles by index without a check (a -1 entry of a full pool). Not reproduced: cell_new_game again
  with two guest level-ups (lab 20261106-003243), no own_probe line. Still open: the writer of the NaN velocity,
  the address space drain. Status stays open.
- 2026-11-06 (fix/w45-cell-nan): the writer of the NaN found (the contact solver fed the game's unset normal of two
  cells on one centre) and fixed (`cell.contact_finite`, test cell_contact_nan); the address space drain split off as
  BUG-0621. Status fixed.
- 2026-11-07: released in 0.7.9.
