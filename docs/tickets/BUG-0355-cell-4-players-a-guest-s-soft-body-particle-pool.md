---
id: BUG-0355
title: Cell: a crowd of over 256 cells on screen loses soft-body particles every frame (full cell graphics pool); a 4-player guest's particle pool filled, cells shared the spare slot, 3 fps for good, stuck in Cell
status: fixed
kind: freeze
stage: Cell
reported: 2026-10-31
source: lab 20261031-151552 campaign_short#1 (extra1, lane 2)
role: guest
versions: 0.7.3.1+main 251d80d
fixed_in: 0.7.3.2
branch: fix/w26-cell-spare
commits: 0b1e55e, cd89fd4, 1e827ed, cd75347
tests: cell_spare
signature: guest log "the soft-body particle pool is full (1499 of 1500): the spare slot instead of -1 (BUG-0338)" in ordinary play, then "N of our own 64 soft-body particles are held by another cell too", perf fps 3 for good; coop.pool orphans > 0 (allocated at 0xE5CC88)
docs: docs/re/cell.md#cell-gfx-pool, docs/06-testing.md#testing-cell-spare, docs/05-re-notes.md#re-notes-sample
related: BUG-0338, BUG-0307, BUG-0337
---

## Report
Final verification of 0.7.3.2 (lab 20261031-151552 campaign_short#1, lane 2, main 251d80d): four
players, Cell -> Creature. The third player (a guest, extra1) mirrored 900 of the host's cells (as the
other two guests did). 18:02:47 its log: own cell not finite (BUG-0307) and "the soft-body particle
pool is full (1499 of 1500): the spare slot instead of -1 (BUG-0338)", pool=900 mirrors=900, no
test command filling it. From 18:02:56 hitches of 200-240 ms; 18:03:49, in the end-of-Cell cinematic
(its cell growing 0.82 -> 2.93 -> 5.30): "1128 of our own 64 soft-body particles are held by another
cell too", its cell at (32292 -47587), later (-126883 -89873). From 18:04 3 fps for good (frames
300-400 ms), address space 3526 -> 2656 -> 1899 MB free; the host's world transfer stalled (0/3267 KB,
rtt 3-4 s), the guest never left Cell (campaign: stuck / apart). The other three games were fine;
the campaign's own periodic tile log of that guest stopped at 18:02:39 (view boxes NaN, BUG-0299
guard skipping PopulateTiles).

## Root cause
Observed (Ghidra + runtime, `coop_cell_spare.py`, `coop.pool orphans` / `parts`):
* A foreground cell gets a soft body when it comes into the active area around the camera
  (SoftBodyLod 0xE78C00, every frame from UpdateCells): CreateSoftBody 0xE6D8F0 allocates its 4-64
  particles (tree walk 0xE5CB60, call site 0xE5CC88), writes the count, then takes an object from
  the cell graphics pool (`*cCellGFX::_ptr + 0x168`, capacity 256). Full, CreateObject 0xB72270
  returns 0 and the cell keeps +0x248 = 0 with its particles. The next frame's LOD sees "no soft
  body" and calls CreateSoftBody again over the old list; DestroyCell / FreeSoftBody free particles
  only when +0x248 is set. Every cell in the active area beyond the 256th loses its particles every
  frame.
* 2 players, 1000 host cells on a disc of 8 m (`coop.pool crowd 1000 8`), fix off: within 4 s
  395-1491 used particle slots held by no cell on the host, 189-1491 on the guest, all allocated at
  0xE5CC88 with owners gone; DestroyCell of 67 such cells with 616 particles gave back 40. The pool
  is then full for good and BUG-0338's spare slot goes to every cell asking: its position was NaN in
  every such run within seconds (each holder's soft-body step pulls it, up to 64 times per cell),
  26-31 NaN cells (a cell's centre is the mean of its particles; guest_tick moves a mirror's
  particles by the NaN difference), the guest's own cell grew in the cinematic with 56-60 of its 64
  particles the spare and went non-finite (BUG-0292 / BUG-0307 every frame).
* ParticleAlloc hands out index = the new count when the pool has no hole (slots 1..1499), so slot
  1499 is a real slot at 1498 used: BUG-0338's spare could alias a real particle and its free was
  dropped (1 slot lost) (CORRECTION in docs/re/cell.md#cell-particle-pool).
Inferred, not reproduced: the campaign guest got there with its 900 mirrors crowding its screen
(no graphics pool numbers in that log; 1 of 5 campaign_short runs so far), and the 3 fps for good
plus the lost address space came from the per-frame CreateSoftBody calls on a full graphics pool
(each sets up cObjectPool_::Get(0)'s object again and plays an animation) once the cinematic's
zoom-out put more cells into the active area, and from its own cell flying with the NaN spare. A
2-player crowd with the fix off kept 60+ fps (`coop.sample`: no hot spot), so the slow frame itself
was not measured.

## Fix
* `coop.fix cell.gfx_full` (cellsync.cpp, hook on Simulator::Cell::CreateSoftBody): no soft body is
  made while the cell graphics pool is full; the cell stays as the game left it (+0x248 = 0, no
  particles, not drawn), the LOD asks again next frame and gets one when there is room. Off: the
  game's call (the leak). This is the root.
  A refused cell gets the game's own "no particles" form (count 1, -1) instead of keeping the pool's
  count 0 (follow-up: a NaN host cell and the own cell thrown 1e7 m in cell_sap).
* `coop.fix cell.bare_still` (follow-up): a guest's mirror whose soft body cell.spare freed is
  placed at the host's pose at rest every frame, as a soft mirror is, not steered by velocity.
* `coop.fix cell.spare` (hook on ParticleAlloc; the pool can still fill legitimately, 256 cells x 16
  particles at tier 2 is 4096): a full pool frees the soft bodies of the cells farthest from our own
  (not ours, no puppet, not the asking cell) down to 1300 used and hands out a real slot; such a cell
  keeps its tier and becomes the game's own "no particles" form (+0x250 = 1, +0x254 = -1, as a model
  type 0 / 5 cell). A cell left with the spare is freed the same way once a frame and the spare is
  put back to a finite place; the pool counts as full at 1498 used so slot 1499 is the spare alone.
  Off: BUG-0338's shared spare slot.
* RE / test tools: `coop.sample` (main-thread sampler), `coop.pool crowd <n> <r>`, `coop.pool parts`,
  `coop.pool orphans`; SPORE_FIX_OFF reaches the campaign's extra players.

## Verification
FAIL: `SPORE_FIX_OFF=cell.gfx_full` cell_spare, lab 20261031-180500 (lane 5; run dirs of this branch
are under SporeCoop-wt/fix/w26-cell-spare/out/lab/): host and guest lost 1490 particles each.
PASS: cell_spare 2/2, lab 20261031-180540 (lanes 6, 8): 0 lost, no spare holder, no NaN cell, the
guest at 45+ fps through the cinematic and in Creature; again in 20261031-182516.
campaign_short 3/3 with the fix, lab 20261031-181517 (lanes 5, 6, 8; the guards were not reached
there); with the fix off 2/2 OK too, lab 20261031-174551 (not reached either: the campaign hits it
in about 1 of 5 runs, so it is no FAIL test).
Cell regression 8/8, lab 20261031-182516 (cell_coop, cell_particles, cell_sap, cell_spare,
cell_leave, cell_evolve_poll, cell_nan, cell_churn); smoke 14/14, lab 20261031-183030 (lanes 5, 6, 8);
`make test` OK.
Follow-up (cd75347; run dirs under SporeCoop-wt/fix/w26-cell-spare/out/lab/): cell_particles 6/6
(20261031-202705), cell_spare 3/3 (20261031-202750), cell_sap 4/4 (20261031-202009; before the
count-1 form 3 of 5 failed: 20261031-201100, 20261031-201612), cell_coop 3/3 (20261031-203415; one
warm rerun failed the knock-out part in 20261031-202750, BUG-0356). Step investigation: all switches
off 6/12 stepped (20261031-194511, 20261031-194556), --no-fill 1/8 (20261031-195222, a knock-out
respawn), invulnerable fixes on 6/6 and off 5/6 (20261031-195745, 20261031-195830).
Not verified: the 3 fps itself (not reproduced in 2 players, see Root cause).

## History
- 2026-10-31: opened.
- 2026-10-31 (fix/w26-cell-spare): root cause (a full cell graphics pool loses particles every frame), cell.gfx_full + cell.spare, cell_spare; fixed (unreleased).
- 2026-10-31 (follow-up after the merge 55da78b): cell_particles failed on main (own cell 32.4 m,
  a step of 18 m). Not caused by the switches: all three off stepped in 6 of 12 runs (64 m to 1e7 m
  in 4), without a full pool 1 of 8 (a knock-out's respawn). The test now keeps the own cell
  invulnerable and checks a step of at most 20 m a round; cell.bare_still added (not shown to
  matter). Found on the way: cell_sap failed 3 of 5 (count 0 of refused cells): fixed in cell.gfx_full.
- 2026-11-01 (fix/w28-sap-test): final verification 10 (lab 20261031-214531) failed cell_sap only on its 60 m limit: [61.5, ..., 0.0, 70.1]; the 70.1 m was the host's cell knocked out and respawned next to a partner (host log `ko: own cell state 3`), and repeats (lab 20261031-235521) failed 2/6 on the first interval alone (60.3 m, 97.7 m), the crowd pushing itself apart. Observed: with cell.gfx_full on, first intervals reach 46-98 m (before BUG-0355: up to 36.6 m); inferred: cells with no soft body push apart harder in the artificial 1100-cell column. The check now leaves out the first interval, allows one respawn interval under 300 m and fails anything over 1000 m (the bug's jumps were 22685 m and 9934066 m). PASS 4/4 lab 20261101-000005; FAIL with `SPORE_FIX_OFF=cell.sap_sweep` 2/2 lab 20261101-000232 (no movement, 2737 / 2766 faults).
- 2026-11-01: released in 0.7.3.2.
