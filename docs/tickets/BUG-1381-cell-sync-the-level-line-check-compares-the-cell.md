---
id: BUG-1381
title: cell_sync: the level-line check compares the cell's target size across a world rescale (food 60 -> 101: level 1 -> 2, the target shrinks 0.82 -> 0.49)
status: fixed
kind: test
stage: Cell
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 cell_sync
role: 
versions: unreleased
fixed_in: unreleased
branch: fix/w61-cell
commits: 14417063
tests: cell_sync
signature:
docs: docs/re/cell.md#cell-world-scale
related: BUG-0041, BUG-0519
---

## Report
0.7.16 second final check (lab 20261120-133703, rel/0.7.16 673f7a55, lane 2, warm run 4): cell_sync FAIL "over a level
line (1 -> 2): the host cell grew (0.82 -> 0.49)". The same FAIL once before: rel/0.7.15.2 lab 20261118-144132 (lane 4,
warm), also "food 60 level 1 ... food 101 level 2". The cold runs pass ("food 10 level 0 ... 51 level 1: 0.55 -> 0.82").

## Root cause
Observed (cell_sync.log and host log): the guest's idle cell ate 2 plants at the start (`eat: from 1 food +6 plant` x2,
food 12), so the test's "10 points over a level line" went to the NEXT line: food 60 (level 1), then +41 = 101 (level 2).
At food 100 the world scale changes (host log `wscale=0.5` at s 10, `1.46601` at s 20, `1.5` at s 30;
docs/re/cell.md#cell-world-scale: 0.5 at level 1, 1.5 at level 2): WorldScaleStep's 0xE6DB70 divides every cell's target
size +0xB4 by the ratio, so the raw target fell 0.82 -> 0.49 while the cell grew (target x scale 0.41 -> 0.72, x1.75, the
avatarSize step 1.73 -> 3). The line 0 -> 1 has no rescale, so the cold runs passed. The mod is right: guest and host
had the same target (0.49 / 0.49) and BUG-0041's ratio check passed. A test fault.

## Fix
coop_cell_sync.py: sizes are compared as target x world scale (`coop.pool`: the avatar's target= and `world_scale=`,
read in one call), after both totals arrived and both world scales reached their target G+0x5150 (a wait for that state
instead of the fixed 3 s). The checks (grew by >10 %, the guest within 5 % of the host's ratio) are unchanged.

## Verification
no-fail-run: test-only change (the comparison unit). Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_sync
--lane N --retry 0 --no-build` PASS (warm or cold; its log prints both sizes and "settled True/True"). The BUG-0041 twin
`SPORE_CELL_FIX=0` keeps failing as before (manual). Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_sync PASS in 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the test on fix/w61-cell.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
