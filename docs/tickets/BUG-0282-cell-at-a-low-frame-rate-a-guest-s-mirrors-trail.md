---
id: BUG-0282
title: cell: at a low frame rate a guest's mirrors trail the host's moving cells by ~0.65 s (2 m at 3 m/s, 15 fps)
status: open
kind: desync
stage: cell
reported: 2026-10-29
source: fix/w7-sync BUG-0275 measurement
role: 
versions: 
fixed_in:
branch:
commits:
tests:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened.
