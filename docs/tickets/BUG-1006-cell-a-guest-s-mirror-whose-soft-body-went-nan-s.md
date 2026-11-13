---
id: BUG-1006
title: Cell: a guest's mirror whose soft body went NaN stays NaN (cell_move and the h_Soft turn carry the NaN into every particle)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-19
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-cell
commits: 23b91609
tests: cell_mirror_nan
signature:
docs: docs/known-issues.md#issue-cell-mirror-nan, docs/re/cell.md#cell-loot-alert, docs/06-testing.md#testing-cell-c1
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-19. Not seen at runtime, found by code reading (main efd2e0ba). On a guest
in Cell, a mirror (a copy of a host cell) whose own soft body went NaN in the guest's cell step stays NaN for good:
the guest's tick moves its particles by `want - pos` where `pos` (the particles' mean) is NaN, so every particle
gets `+= NaN`, and the guest's turn after the soft-body step (`h_Soft`) computes the angle from the NaN matrix and
turns every particle by a NaN sine and cosine. The received pose is checked (BUG-0614 style, 3721), the local one is
not. A NaN mirror is what made the own cell NaN in BUG-0292 (the own soft body touching it), which the BUG-0299 /
BUG-0307 heal then has to repair again and again.

## Root cause
Observed in code: mods/coop/modes.cpp 1054-1076 `cell_move`: `d = p - pos` (1057), `q += d` for every particle
(1067), then `pos = p` (so the centre looks finite while the particles are NaN). mods/coop/cellsync.cpp 3984 calls it
for every soft mirror each tick; 3160-3215 `h_Soft`: `d = atan2(want) - atan2(m[1], m[0])` (3186) from the mirror's
own matrix after `o_Soft`, then `turn` on positions, velocities and goal offsets. No `cell_nonfinite(c)` check for
mirrors anywhere (the heal is for the own cell only, 3820). Inferred: how often a mirror's soft body goes NaN by
itself (the own cell did 26 times in 6 minutes in BUG-0307's 4-player run). Audit detail: the NaN goes into the
particles, not into `c + kRot` (guest_tick writes the host's finite matrix there at 4002). Severity: medium in 3-4
player Cell sessions (a mirror that drifts or vanishes, and a second source of own-cell NaN), low otherwise.

## Fix idea
In guest_tick before `cell_move`: if `cell_nonfinite(c)` (or the particles' mean is not finite), rebuild the
particles on a ring at `want` as `heal_own` does (shared helper `heal_soft(c, pos, size)`), velocities 0, goal
offsets reset when not finite; in `h_Soft` skip the turn when `d` (or `cs`, `sn`) is not finite. Switch:
`cell.mirror_nan_heal`.

## How to test
New tools/tests/coop_cell_mirror_nan.py (cell fixture, host + guest): a guest test command writes NaN into one
mirror's particle positions (like `nan_velocity` does for the own cell, BUG-0604); after 1 s assert on the guest's
game that the mirror's centre and particles are finite and within 2 m of the host's pose of that cell (`coop.pool`
mirror listing). Main: NaN for good (FAIL), fixed: PASS.

## Fix
cellsync.cpp `heal_mirror` (guest_tick, before the soft mirror's cell_move): a mirror with any non-finite field
(`cell_nonfinite`: centre, velocity, matrices, size, mass, particles, goal offsets) gets its size / mass / matrices
back (the host's size, identity matrices that the host's matrix replaces right after) and its particles on a ring
at the host's pose (`soft_ring`, now shared with the own cell's heal of BUG-0299 / BUG-0307), at rest. `h_Soft`
skips the mirror turn when the angle's sine or cosine is not finite. Switch `coop.fix cell.mirror_nan_heal off`.
Test commands `coop.pool nanmirror [c<key>]`, `coop.pool mirrornf c<key>` (guest).

## Verification
Test `cell_mirror_nan` (the guest's nearest soft mirror's particles NaN; 1 s and 3 s later finite and within 2 m of
the host's pose of that cell).
FAIL (SPORE_FIX_OFF=cell.mirror_nan_heal): out/lab/20261113-022529 (non-finite fields 0x284 after 1 s, 0x385 after 3 s).
PASS: out/lab/20261113-022529-2, out/lab/20261113-023438-2.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-cell (package C1).
