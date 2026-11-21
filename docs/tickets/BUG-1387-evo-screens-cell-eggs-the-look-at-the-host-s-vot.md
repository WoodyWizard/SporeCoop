---
id: BUG-1387
title: evo_screens_cell_eggs: the look at the host's vote window after the vote ran out counts as a missing window (17 of 18 looks)
status: open
kind: test
stage: Cell
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 evo_screens_cell_eggs_lowfps
role: 
versions: unreleased
fixed_in:
branch: fix/w61-cell
commits:
tests: evo_screens_cell_eggs_lowfps, evo_screens_cell_eggs
signature:
docs:
related: BUG-1245, BUG-1263
---

## Report
0.7.16 second final check (lab 20261120-133703, lane 4, cold, host at 10 fps): evo_screens_cell_eggs_lowfps FAIL "the
host's co-op window with the vote stays on its screen (17 of 18 looks)". The first final check passed (17 of 17).

## Root cause
Observed: host log `vote: end: time is up, ready 1/2 after 59.1 s` at 18:47:05.571; the test's samples ran every ~3.5 s
from its own vote start, the last at "59s", i.e. right after the vote's end: the loop checked "vote active", then the
look found the window gone (closed with the vote, the editor coming). At 10 fps (hitches up to 3.9 s in that minute)
the gap between the check and the look is long enough. A test fault.

## Fix
coop_evo_screens.py part_cell_eggs: a look without the window counts only when the vote is still active after it. A
missing window during the vote still fails.

## Verification
no-fail-run: test-only change. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run evo_screens_cell_eggs,evo_screens_cell_eggs_lowfps
--lane N --retry 0 --no-build` PASS. Open, waiting for the lab run.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the test on fix/w61-cell.
