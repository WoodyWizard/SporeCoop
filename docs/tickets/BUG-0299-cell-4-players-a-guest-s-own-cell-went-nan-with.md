---
id: BUG-0299
title: Cell, 4 players: a guest's own cell went NaN with no NaN mirror (BUG-0292 again), then PopulateTiles faulted and the level-up crashed in PopulateTile+0x8A
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-053709 campaign_short#2 i14 (fix/w10-cell-4p)
role: guest
versions: fix/w10-cell-4p aaa9f5f
fixed_in: 0.7.3.2
branch: fix/w10-cell-4p
commits: a964c90
tests: cell_nan
signature: dc04aee5:Simulator::Cell::PopulateTile+0x8A
docs: docs/re/cell.md#cell-broadphase-cap, docs/known-issues.md#issue-cell-own-nan
related: BUG-0292
---

## Report
lab 20261030-053709 campaign_short#2 (fix/w10-cell-4p at aaa9f5f), instance 14 (guest Bob, level 4, 900 mirrors): `our own cell's position is not finite (BUG-0292): 900 mirrors, 0 of them with a pose that is not finite`, then a fault at CreateObject+0x56 under PopulateTiles+0x1EF, the tile pool's live count growing by one per frame, a 2 s hitch, and 90 s later at the level-up a crash in PopulateTile+0x8A (RemoveCellsOfOldScale).


## Root cause
Observed: the guest's own cell NaN -> its view boxes NaN -> PopulateTiles creates tiles from garbage -> the level-up repopulates them and crashes. Not known: what made the own cell NaN (no NaN mirror; BUG-0292's guard dropped none).


## Fix
Switch `coop.fix cell.nan_guard`: no PopulateTiles / PopulateTile while the own view boxes are not finite, and the own cell (both roles) is written back to its last finite position (position, previous position, soft-body particles on a small ring at rest). Test hook `coop.pool nanself`.


## Verification
FAIL: `SPORE_FIX_OFF=cell.nan_guard` cell_nan, lab 20261030-055502 (position stays NaN, the guest hung after the level-ups: hang report). PASS: cell_nan lab 20261030-055505 (lane 3), lab 20261030-064009 (lane 4) and lab 20261030-070300 (lane 4, final code).


## History
- 2026-10-30: opened.
- 2026-10-30: fix/w10-cell-4p: guarded and healed (cell.nan_guard), cell_nan; the NaN source stays open in known-issues#issue-cell-own-nan.
- 2026-11-01: released in 0.7.3.2.
