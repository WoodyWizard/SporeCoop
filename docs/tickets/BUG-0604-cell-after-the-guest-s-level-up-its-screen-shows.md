---
id: BUG-0604
title: Cell: after the guest's level-up its screen shows only water: no cell of any kind on screen for 7 min (cells 1-10 m from its avatar)
status: fixed
kind: bug
stage: Cell
reported: 2026-11-05
source: lab fix/w44-tests 20261105-142923 cell_new_game#1 (lane 9)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w45-cell
commits: 2aa5f9fc, 0d9e7d10
tests: cell_new_game, cell_view_nan
signature:
docs: docs/re/cell.md#cell-view-nan
related: BUG-0589, BUG-0174, BUG-0307, BUG-0343
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
Observed in the report's log (re-read 2026-11-06): at the level-up (s 93.36) the guest's own cell had a NaN velocity
and NaN particle velocities (own_probe mask 0x104, 17 frames in 0.24 s, "at guest tick, finite at collision"),
healed each frame. Observed in the game's code (Ghidra, 0xE600A0): the cell camera eases its drawn point
cCellUI+0x48 with factors computed from the avatar's speed; one NaN speed makes +0x48 NaN and every later frame
keeps it NaN (its box clamp compares a NaN distance and never runs), and the view boxes are built around it.
Observed at runtime with the same fault injected (`coop.pool nanself vel 17`, cell.camera_heal off): the guest's
`cam=(nan nan)` with a finite target, 0 cells on its screen 3 s later and after swimming 15 m (the report's plain
water). Inferred: the report's guest had that NaN camera (0.7.8 did not log the camera). What made the velocity
NaN at the level-up is BUG-0307's open first NaN (docs/re/cell.md#cell-view-nan).

## Fix
`coop.fix cell.camera_heal` (cellsync.cpp `camera_heal`): every frame in Cell (in a session or solo) a camera
point (+0x48 / +0x54 / +0x60) that is not finite goes back onto the own cell, as the game's own jumps set them.
Off: 0.7.8 (the view stays lost until the next load). Test command `coop.pool nanself vel [n]`; `coop.pool cam`
prints `heals=`.

## Verification
cell_view_nan (tools/tests/coop_cell_view_nan.py): FAIL with SPORE_FIX_OFF=cell.camera_heal, lab 20261106-002913
(guest camera nan, 0 cells on screen); PASS lab 20261106-003015 (17 heals, camera 0.3 m from the cell, 32 / 53
cells on screen). cell_new_game again (lab 20261106-003243): two guest level-ups, no NaN this time; it failed on
its meat probes (host 0/6, guest 2/6, BUG-0589's flaky probe, not this).
cell_view_nan PASS again in the regression run lab 20261106-015141 (with play_cell SPORE_DETECTOR=20, smoke, cell_end_sync, cell_grow, cell_puppet_scale, cell_sync_check, cell_camera, cell_nan, cell_nan_all: 23/23).

## History
- 2026-11-05: opened.
- 2026-11-06 (fix/w45-cell): root cause (the NaN camera after a NaN velocity), fix cell.camera_heal, test cell_view_nan.
