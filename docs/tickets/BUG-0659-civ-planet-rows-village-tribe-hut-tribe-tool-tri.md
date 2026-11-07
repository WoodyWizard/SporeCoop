---
id: BUG-0659
title: Civ: planet rows village, tribe_hut, tribe_tool, tribe_food_mat in organic play_civ, 3-5 rows a run on rel/0.7.9 vs 1 on 0.7.8 (the BUG-0539 raid case passes)
status: open
kind: desync
stage: Civilization
reported: 2026-11-06
source: lab log sweep follow-up 2026-11-06 (detector rows rel/0.7.8 vs rel/0.7.9)
role: 
versions: 0.7.8, rel/0.7.9
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Detector rows Civilization:planet:village / tribe_hut / tribe_tool / tribe_food_mat in play_civ: rel/0.7.8 (out/lab/20261105-*) 1 row of n1 in each of 3 runs; rel/0.7.9 (20261106-*) 3-5 rows of n1-2 in each of 3 runs (n2 in 20261106-200316). BUG-0539 (fixed 0.7.6) verified only the raid case civ_tribe_left (14/14, 20261104-134924). Possible 0.7.9 regression: decided in the 0.7.9 final check by play_civ x3 on v0.7.8 and on rel/0.7.9 on the same lane; if 0.7.9 is worse it blocks the release.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened from the lab log sweep follow-up; checked in the 0.7.9 final check.
