---
id: BUG-0308
title: Cell, 4 players: heap corruption found when a rejoined guest's Cell pool is freed for the world reload (GeneralAllocator_FreeInternal from cObjectPool dtor under SimUpdate)
status: open
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-104149 campaign_short#3
role: guest
versions: 0.7.3.1
fixed_in:
branch: fix/w13-cell
commits: 4ae7640
tests: campaign_short, cell_rejoin_leave
signature:
docs: docs/re/cell.md#cell-leave-guard
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
Observed: the free of a pool's block (the Cell pool torn down by SimUpdate, a rebuild or the stage
leave) found a corrupt heap header: the heap was damaged before, in or next to that block. Not known
by whom. Candidates (inferred, not checked): a write past a pool's end while 900 mirrors were
created again right after the rejoin, a stack/heap overrun of the BUG-0295 kind with another count,
or a freed mirror written later. First time in the lab history (no other log has
`cObjectPool::_dtor`).

## Fix
Guard, not a proven fix (status stays open, as BUG-0292's guard): `coop.fix cell.leave_guard`
(modes.cpp `cell_leaving()`): while `game::leaving_stage()` (quit posted for a world load or the menu,
until the mode changes) the guest's state handler applies no MSG_CELL_STATE (no CreateCellObject, no
field writes, no DestroyCell), `tick()` runs no guest / host / symbiosis tick and no background cull,
and h_SimUpdate writes no puppet poses. Static evidence: cellsync's `in_cell()` and `avatar()` read
the mode and the pool directly and stayed true through the leaving frames, unlike `game::avatar`
(BUG-0264); docs/re/cell.md#cell-leave-guard. `coop.pool` prints `leave_writes=` / `leave_dropped=`.

## Verification
The heap corruption itself was not reproduced. `coop_rejoin_loop.py --fixture cell --rounds 12 --gap
0.3:1.5 --cell-leave` (suite cell_rejoin_leave): the guest leaves and rejoins inside Cell, the host's
world arrives and the guest leaves the stage to load it, 12 times.
* FAIL `SPORE_FIX_OFF=cell.leave_guard`: out/lab/20261030-143913 (lane 2): 1185 cell states / ticks
  of ours ran on the guest's pool while it was being left (the window exists and is hit every round);
  no fault in 12 rounds.
* PASS: out/lab/20261030-143918 (lane 3): 0 ran, 932 skipped, no fault.
* no-fail-run (for the crash): the corruption did not happen in 12 + 12 rounds with the old code;
  what corrupted the heap in lab 20261030-104149 is not shown.

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
