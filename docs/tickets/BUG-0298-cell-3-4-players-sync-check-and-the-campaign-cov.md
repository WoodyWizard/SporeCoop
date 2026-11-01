---
id: BUG-0298
title: Cell, 3-4 players: sync-check and the campaign coverage count every host cell, also the ones around other partners that a guest is never sent
status: fixed
kind: test
stage: Cell
reported: 2026-10-30
source: lab 20261030-035318 campaign#1/#2, campaign_short#2
role: 
versions: main 1d51511
fixed_in: 0.7.3.2
branch: fix/w10-cell-4p
commits: 08428c0, a964c90
tests: cell_4p, campaign_short, unit (net_tests diag compare)
signature:
docs: docs/re/cell.md#cell-broadphase-cap, docs/06-testing.md#testing-cell-4p
related: BUG-0174, BUG-0295
---

## Report
lab 20261030-035318: the host's sync-check reported up to 1033 cells 'missing on the guest' (campaign#2), and campaign#1 / campaign_short#2 found 'too few of the host objects in cell (coverage)' for Bob, Carol and Dave.


## Root cause
Observed: since BUG-0174 each guest is sent the cells within 120 m of its own cell, and (BUG-0295) at most its nearest 900; the sync-check (net/diagmsg.h compare) and the campaign's coverage (coop_multi.measure) still counted every host cell, also the ones around the other partners.


## Fix
Both count only host cells within 60 m of the guest and among its nearest 900 (Limits::cell_near / cell_sent; measure() the same). Unit test in tests/net_tests.cpp.


## Verification
FAIL: lab 20261030-053709 campaign_short#1 / #3 (coverage 71-83% of 1080-1266 host cells within 60 m: the 900-nearest rule was missing then). PASS: PASS campaign_short with 4 players, final code bea15ec: lab 20261030-070059 (3/3 OK, lanes 2/3/7, no fault, no crash, no coverage finding). Earlier on a964c90: lab 20261030-060122 (Cell passed 3/3; #3 failed later in Creature, BUG-0300) and 20261030-061151 (2/2 OK). cell_4p coverage 94-100% (lab 20261030-065411).


## History
- 2026-10-30: opened.
- 2026-10-30: fix/w10-cell-4p: range rules in the sync-check and the coverage; fixed (unreleased).
- 2026-11-01: released in 0.7.3.2.
