---
id: BUG-0297
title: Cell, 4 players: guest crash reading null on a worker thread (dc04aee5:SporeApp.exe+0x6479B7)
status: fixed
kind: crash
stage: Cell
reported: 2026-10-30
source: lab 20261030-035318 campaign#3 i27
role: 
versions: main 1d51511
fixed_in: 0.7.3.2
branch: fix/w10-cell-4p
commits: 08428c0, 3a3292b, 1ebfd3e, 6a731e6, bea15ec
tests: cell_4p, campaign_short
signature: dc04aee5:SporeApp.exe+0x6479B7
docs: docs/re/cell.md#cell-broadphase-cap
related: BUG-0295
---

## Report
lab 20261030-035318 campaign#3, instance 27 (guest Carol): AV reading null at 0x00A479B7 on a worker thread (stack 0xA4F70E, 0xA49554, 0xA61735, 0xA5BE71).


## Root cause
Inferred: a consequence of BUG-0295. The same game faulted at 0x00BBB07B (the broadphase's stack buffer overrun, stack and heap corrupted) at 04:57:45, the crash came 7 s later at 04:57:52. Not seen without the overrun.


## Fix
The overrun no longer happens (BUG-0295, `coop.fix cell.budget`). No separate change.


## Verification
no-fail-run: a consequence of BUG-0295's overrun, not reproducible on its own; the BUG-0295 FAIL run (lab 20261030-052851) faulted at 0xBBB07B the same way. PASS: no crash or fault in cell_4p (lab 20261030-065411) and campaign_short (lab 20261030-070059).


## History
- 2026-10-30: opened.
- 2026-10-30: fix/w10-cell-4p: a consequence of BUG-0295 (same game, after the 0xBBB07B fault); fixed with it.
- 2026-11-01: released in 0.7.3.2.
