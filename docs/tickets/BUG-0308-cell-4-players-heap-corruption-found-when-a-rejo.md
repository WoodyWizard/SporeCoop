---
id: BUG-0308
title: Cell, 4 players: heap corruption found when a rejoined guest's Cell pool is freed for the world reload (GeneralAllocator_FreeInternal from cObjectPool dtor under SimUpdate)
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign_short#3
role: guest
versions: 0.7.3.1
fixed_in: unreleased
branch: fix/w13-cell, fix/w22-cell, fix/w45-crashes
commits: 4ae7640, 21b178c, 90f091b, 27245c9, 150439df, 3a5db144
tests: campaign_short, cell_rejoin_leave, cell_pause_rejoin
signature:
docs: docs/re/cell.md#cell-rejoin-paused-heap, docs/re/cell.md#cell-sap-pending, docs/re/cell.md#cell-leave-guard
related: BUG-0295, BUG-0271, BUG-0028, BUG-0348
---

## Report
Lab 20261030-104149 campaign_short#3 (main 813f8cd, lane 7), guest Dave (instance 3x, the 4th
player, 900 mirrors): the test made Dave leave and join again in Cell; 3 s after the join the host's
world arrived and the guest left the stage for the galaxy menu to load it. In the leaving frame:
`0xC0000005 at 0x009272C4 GeneralAllocator_FreeInternal+0x64 (data 0x5DC664DC)` from
`GeneralAllocator::delete_ < cObjectPool::_dtor+0x16 < Simulator::Cell::SimUpdate+0x6ED (0xE7FA8D)`,
then `0x00DDA358` (data 0x18) and, while loading, `0x00DE81FE` under
`cGameModeManager::ClearActiveMode`; the campaign then saw Dave stop drawing ("no frames for 10 s").

## Root cause
Observed (2026-11-05, fix/w45-crashes; docs/re/cell.md#cell-rejoin-paused-heap): BUG-0348's overflow
of the cell broadphase's pending arrays. Dave's pause menu was open (the campaign log: "sync: not
compared (pause menu open): Dave"; his position stood still at (33.6 47.8) from 12:39:51 to 12:40:51
while his pool churned 900 -> 806 -> 900 -> 770 -> 900 mirrors): his game did not run SimUpdate's
sweep while our guest code made mirrors from the host's states, so the endpoints piled up in the
arrays of 2 x 4096 entries, which AddObject fills without a bound check; the leave and the rejoin
(900 more mirrors at once) added to them, and the overflow wrote floats (endpoint coordinates) over
the heap. The free of a Cell pool in the stage teardown then found the float header (0x41FAECC0 =
31.37f). The same path on the lab with `cell.sap_pending` off gives the same three faults in the same
order (FreeInternal+0x64 from the pool dtor with a float header, 0x00DDA358 data 0x18, then
0x00DE81FE while loading): see Verification. Inferred: that Dave's pending count itself overflowed
(0.7.3.1 had no counter of it).
Earlier candidates (not the cause): our cell work in the leaving frames (guarded by cell.leave_guard,
no fault without it in 24 rounds), BUG-0314's background cells.

## Fix
`coop.fix cell.sap_pending` (BUG-0348, fix/w22-cell 21b178c / 90f091b / 27245c9, released in 0.7.3.2):
`cell_create` / `cell_spawn` make no cell while the pending list is near its arrays' end; the
mirrors come with a later state once the game simulates again. Unchanged here; this branch adds the
campaign guest's exact path as a test. The guard `cell.leave_guard` (4ae7640) stays.

## Verification
Test `cell_pause_rejoin` (`coop_cell_pause.py --rounds 8 --rejoin`, 2 players: the mechanism needs
one paused guest, not four): pause menu, 8 rounds of mirror churn, still paused the guest leaves and
joins, the host's world arrives and the guest leaves Cell to load it.
* FAIL `SPORE_FIX_OFF=cell.sap_pending`: out/lab/20261105-231554 (lane 4): pending 12 242 of 8 192
  after the churn, 13 680 after the join; on leaving Cell the guest faulted at FreeInternal+0x64
  (header 0x41557170 = 13.34f) from cObjectPool::_dtor < SimUpdate+0x750, then 0x00DDA358 (data
  0x18), then 0x00DE81FE (map_uint32_uint32__get_+0xC1E): the three faults of the campaign log;
  10 336 faults in all, the guest never came back with the world.
* PASS: out/lab/20261105-231944 (lane 4): pending capped at 7 680 (mirrors held while paused: 77 285
  / 85 481 held), the guest left Cell, loaded the host's world and was back in Cell in 8.3 s, no fault
  on either game.

## History
- 2026-10-30: opened.
- 2026-10-30 (fix/w13-cell): not reproduced. Observed in the log: the guest had left the session
  (12:40:46, its 900 mirrors destroyed), rejoined 5 s later in Cell and had 900 mirrors again within
  0.1 s; 3 s later the world arrived and the stage was left; the free failed 0.83 s after the quit, in
  the third leaving frame. Inferred candidate not checked: our guest state handler (`guest_state`)
  still creates and writes mirrors in the leaving frames (it checks the Cell mode, not
  `game::leaving_stage()`); rejoin loops do the same without faults, so it is not shown to matter.
  BUG-0314 (background cells piling up to the pool's 1500 cap) is another way a guest's pool grows
  toward the broadphase limit of BUG-0295; whether it played a part here is not known (the guest's
  pool held 904 cells before the leave).
- 2026-10-30 (fix/w13-cell): guard `cell.leave_guard` added (window confirmed: 1185 applied states / ticks in 12 rejoins without it); the crash not reproduced, stays open.
- 2026-10-31 (fix/w22-cell): BUG-0348 reproduces this exact signature (FreeInternal+0x64 freeing pool G+0x8C, return 0xE7FA8D, then 0x00DDA358) by overflowing the cell broadphase's pending arrays (a guest that creates mirrors while its game does not simulate), guarded by `cell.sap_pending`; whether this report's guest was in that state is not shown, so it stays open.
- 2026-11-05 (fix/w45-crashes): root cause observed: the campaign guest sat in the pause menu while its mirrors churned (BUG-0348's state); the same path with cell.sap_pending off reproduces the three faults in order; test cell_pause_rejoin; fixed by cell.sap_pending (0.7.3.2).
