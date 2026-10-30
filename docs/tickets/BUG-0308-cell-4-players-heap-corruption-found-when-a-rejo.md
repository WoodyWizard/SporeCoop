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
branch:
commits:
tests: campaign_short
signature:
docs:
related: BUG-0295, BUG-0271, BUG-0028
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
Not fixed.

## Verification
Not reproduced on purpose yet.

## History
- 2026-10-30: opened.
