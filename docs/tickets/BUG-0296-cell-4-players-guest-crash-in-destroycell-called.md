---
id: BUG-0296
title: Cell, 4 players: guest crash in DestroyCell called by coop (dc04aee5:SporeApp.exe+0x681FE6, cCellGFX::AddPreloadedTexture+0x134)
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-035318 campaign#3 i37
role: 
versions: main 1d51511
fixed_in: 0.7.3.2
branch: fix/w10-cell-4p
commits: 08428c0, 3a3292b, 1ebfd3e, 6a731e6, bea15ec
tests: cell_4p, campaign_short
signature: dc04aee5:SporeApp.exe+0x681FE6
docs: docs/re/cell.md#cell-broadphase-cap
related: BUG-0295
---

## Report
lab 20261030-035318 campaign#3, instance 37 (guest Dave): AV at 0x00A81FE6 on the main thread, chain cCellGFX::AddPreloadedTexture+0x134 <- Simulator::Cell::DestroyCell+0x98 <- coop.dll (a mirror destroyed by guest_state's purge).


## Root cause
Inferred: a consequence of BUG-0295. The same game faulted at 0x00BBB07B (the broadphase's stack buffer overrun, stack and heap corrupted) at 04:57:45, the crash came at 04:59:51: the cell's graphics object (+0x144..+0x148 list, +0x38 vtable) read garbage in DestroyCell; the mirror itself was ours and live. Not seen without the overrun.


## Fix
The overrun no longer happens (BUG-0295, `coop.fix cell.budget`). No separate change.


## Verification
no-fail-run: a consequence of BUG-0295's overrun, not reproducible on its own; the BUG-0295 FAIL run (lab 20261030-052851) faulted at 0xBBB07B the same way. PASS: no crash or fault in cell_4p (lab 20261030-065411) and campaign_short (lab 20261030-070059).


## History
- 2026-10-30: opened.
- 2026-10-30: fix/w10-cell-4p: a consequence of BUG-0295 (same game, after the 0xBBB07B fault); fixed with it.
- 2026-11-01: released in 0.7.3.2.
