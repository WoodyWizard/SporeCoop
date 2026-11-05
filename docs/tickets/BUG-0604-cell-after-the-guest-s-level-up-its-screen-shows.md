---
id: BUG-0604
title: Cell: after the guest's level-up its screen shows only water: no cell of any kind on screen for 7 min (cells 1-10 m from its avatar)
status: open
kind: bug
stage: Cell
reported: 2026-11-05
source: lab fix/w44-tests 20261105-142923 cell_new_game#1 (lane 9)
role: guest
versions: 
fixed_in:
branch:
commits:
tests: cell_new_game
signature:
docs:
related: BUG-0589, BUG-0174
---

## Report
cell_new_game (`--diet carn --early --minutes 3`, two new players, lane 9, out/lab/20261105-142923 #1, build of
fix/w44-tests on rel/0.7.8 cd004dca): the guest's cell grew to the next level at s ~95 of its session (size 0.82 ->
0.48, the guest's `cell:` trace lines). From the next line (s 100) to the end (s 560) every line says `screen:
plants=0 meat=0 creatures=0` while `nearest plant=1-8 meat=1-10` (m from its avatar, pool 200-900 mirrors), and the
guest's screenshot at s ~190 (copied to out/lab/20261105-142923/cell_new_game_1_guest_s190.png, the host's _host_s190.png) shows
plain blue water: no cell, not its own, no food, only the HUD and the co-op caption. The host's screenshot of the same
moment shows its cell among food. The guest's game kept running (68 fps, its cell moved, it took 2 bites), its
probe found no food and no creature on its screen in 12 tries of 20 s (0/6, the test's FAIL). The guest's perf line
says `Cell, cinematic` at s 239 and s 541, `Cell` at s 118 / 179. Both games at world_scale 1.5 at the end.
Not seen in the run before it (20261105-132739 #1: both at level 3, world_scale 5.0, both ate). Not analysed: whether
the camera stayed at the old level's place or scale, or the level-up cinematic did not end on the guest.
Reproduce: `tools/lab.py run cell_new_game --lane N` (a guest that levels up during the play).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: not fixed (found by G2, fix/w44-tests, in a regression run of cell_new_game).

## History
- 2026-11-05: opened.
