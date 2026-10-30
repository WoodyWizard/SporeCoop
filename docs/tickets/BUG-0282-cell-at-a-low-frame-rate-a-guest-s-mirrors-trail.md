---
id: BUG-0282
title: cell: at a low frame rate a guest's mirrors trail the host's moving cells by ~0.65 s (2 m at 3 m/s, 15 fps)
status: not-reproduced
kind: desync
stage: cell
reported: 2026-10-29
source: fix/w7-sync BUG-0275 measurement
role: 
versions: 
fixed_in:
branch: fix/w13-cell
commits:
tests: cell_lowfps
signature:
docs:
related:
---

## Report
Found while measuring BUG-0275 (lane 6, 2026-10-29): 4 players in the cell fixture, play_cell bots on
every machine, 25 checks of every guest (host path over 0.25 s before the guest's sample). Carol
forced to 15 fps (`frame.limit 66`), Dave to 30 fps (`frame.limit 33`), Bob free (~65 fps):

| guest | fps | cells compared | cells > 1.2 m | p90 median / max |
|---|---|---|---|---|
| Bob | 65 | 13261 | 1 | 0.00 / 0.50 m |
| Dave | 31 | 13346 | 2 | 0.00 / 0.53 m |
| Carol | 15 | 13165 | 283 | 0.01 / 1.72 m |

The 283 far cells were 283 different host cells moving at ~2.4 m/s (median), all off by ~2 m in
the same direction (behind their way): error / speed ~0.66 s. At 65 fps the same play gave no cell
over 1.2 m (p90 0.01 m). Cause (inferred, not verified): cellsync.cpp guest_tick steers a mirror by
velocity (`v = host vel + (want - cur) * 2`, a jump only beyond 3 m), and the game's cell physics
moves a cell less per second at a low frame rate, so the mirror settles behind. Players at a low
frame rate (a weak PC, 4 games on one PC) see the host's cells ~2 m behind.
Reproduce: the measurement script of BUG-0275 (see its Verification) with FPSLIM=Carol:66.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not reproduced on main 5d7f20e (2026-10-30, fix/w13-cell). Measured with the steering unchanged:
2 players, guest at 15 fps (`frame.limit 66`), both with play_cell bots, 30 checks: 0 of 16 616
compared cells over 1.2 m, worst 0.08 m (lab 20261030-133013, lane 2); 4 players (Carol 15 fps, Dave
30 fps) after `coop.pool food 200`, 30 checks each: p90 median 0.05 / 0.04 m, largest 0.39 / 0.45 m,
0 checks over the limit (coop_sync_multi, lane 3). What changed since the 2026-10-29 measurement is
not known (inferred candidates: the BUG-0295 budget, fewer rigid mirrors).

## Fix
None. A follow step (rigid mirrors more than 0.2 m off moved towards their place each frame) was
written and reverted: with no failing run it could not be shown to matter. Guard test
`coop_cell_lowfps.py` (suite cell_lowfps).

## Verification
no-fail-run: not reproducible on 5d7f20e (lab 20261030-133013 cell_lowfps passed with the old steering).

## History
- 2026-10-29: opened.
- 2026-10-30: fix/w9-cell-food: not fixed (not part of the partner-tiles change). Related finding
  (BUG-0289): the same steering (`v = host vel + (want - cur) * 2`, a jump only beyond 3 m) gives a
  0-3 m sawtooth when the guest's game runs no simulation at all (pause menu open). A fix idea
  for both: write the mirror's position along the host's extrapolated path every frame when it is
  off by more than ~0.5 m (through the particles for a soft body), instead of relying on the game's
  integration at the guest's frame rate; it needs the 15 fps measurement of BUG-0275 to confirm.
- 2026-10-30 (fix/w13-cell): not reproduced at 15 fps with 2 and 4 players; guard test cell_lowfps.
