---
id: BUG-1247
title: Cell: the guest's mirrors of the host's NPC cells spin (dance) in place
status: not-reproduced
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch: fix/w53-cellfx
commits:
tests: cell_mirror_turn, cell_mirror_turn_net, cell_mirror_turn_bare
signature:
docs: docs/re/cell.md#cell-mirror-turn, docs/archive/known-issues-2026.md#issue-cell-mirror-spin
related: BUG-0001, BUG-0355
---

## Report
A players' video: in the guest's game every NPC cell around spins/dances in place, while in the host's game at the same spot they swim evenly.

## Root cause
Not found. What a player sees of a cell is its soft body (docs/re/cell.md 10; a mirror's particles are turned to the
host's heading after every soft-body step by h_Soft, BUG-0001). `tools/tests/coop_cell_mirror_turn.py` samples on both
games at 10 Hz, for every NPC cell on the screen, the angle of its first soft-body particle about the particles' mean
(`coop.pool cloud`: host = its own cells, guest = its mirrors by host key; for a cell without a soft body its heading
right after the cell step) and compares the turn rates of the same cell on both screens. In every run the guest's
mirrors turned like the host's cells (guest rate within 0.01-0.07 rad/s of the host's, no cell over twice the host's
rate + 0.6 rad/s), and h_Soft's correction was over 0.5 rad in at most 2 samples of a cell:
* both in one place, the host swimming around: out/lab/20261118-124151 (3 cells), -125953 (4);
* SPORE_NET delay 250 ms, loss 5 % (round trip ~530 ms): -124151 (5 cells);
* the guest at 15 fps: -124151 (4), -125953;
* the guest 70 m from the host (mirrors of cells off the host's screen, against the host's heading as received): -125159
  (12 cells);
* after the host's evolution (mating call, vote, editor) and back in the pool: -125953 (4 cells);
* the guest's particle pool full and its mirrors made again (no soft body, BUG-0355's "bare" mirrors): -131028 (18 cells,
  all bare): their heading right after the cell step stayed the host's (the step does not turn a held mirror).
* The cell animation (+0x1B0, equal to the state +0x18C in nearly every sample) of the cells on screen (-131028): host
  0 in 98 %, then 1, 0x12, 0xB, 0x16; guest's mirrors 0 in 96 %, then 0x12, 0x32, 0x33, 0x30, 0x16. The guest's mirrors
  play 0x30 / 0x32 / 0x33 (the own cell's swim animations, #cell-partner-attack-anim) in ~1.5 % of the samples where the
  host's cells do not: its own game picks them for a mirror the mod moves. Not judged a spin (no turn), noted.
Not tried: a warm reset between tests, a guest at another world scale (level) than the host, more than two players,
a long session.

## Fix
None: not reproduced. A write-back of the host's rotation to the guest's bare mirrors right after the cell step (as the
puppets get) was written and taken out again: their heading there was already the host's (lab 20261118-131028).

## Verification
no-fail-run: not reproduced (runs above); the tests stay as guards.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellfx): not reproduced (one place, bad network, 15 fps, 70 m apart, after the evolution, bare mirrors).
