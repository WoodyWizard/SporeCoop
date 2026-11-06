---
id: BUG-0595
title: Space: the guest's money reads nan in the detector's state row (campaign_space)
status: fixed
kind: desync
stage: Space
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: guest
versions: 0.7.7-dev
fixed_in: 0.7.8
branch: fix/w44-world-state2
commits: 49ba4925 8b405a1c
tests: make test (tools_selftest.py worldstate)
signature:
docs: docs/re/space.md#space-money-unsynced
related:
---

## Report
campaign_space (3 samples): "money 100000 on the host, nan on the guest". Either the guest's money field is
read before its Space empire exists (a detector read) or the guest's value is really not a number. Reproduce:
`SPORE_DETECTOR=20 tools/lab.py run campaign_space`.

## Root cause
Observed (rel/0.7.7 lab 20261105-015350, campaign_space): a detector read. A guest's `coop.wstate` prints
`st res money=100000(unsynced)` until the host's first total after the guest's arrival in Space (shared::brief marks
a total not synced yet); worlddiff parsed `100000(unsynced)` as nan. The guest's game had 100000, the host's too
(the guests' trace lines of the same second: money=100000(unsynced)); 1 s later both synced.

## Fix
tools/tests/worlddiff.py: State reads the number before the `(unsynced)` suffix (the names go to `res_unsynced`); a
real difference of an unsynced total is still a row. No game change, no switch.

## Verification
make test: tools_selftest.py worldstate (an unsynced total reads as its number, the same money gives no row, other
money still does): FAIL against the rel/0.7.8 worlddiff.py (`SELFTEST_WORLDDIFF=<a copy of it>`: money nan, a
Space:state:money row), PASS on this branch. Detector: campaign_space out/lab/20261105-132636 and play_space
20261105-124754 / 20261105-131337 with SPORE_DETECTOR=20: no Space:state:money row (before: 3 samples in the rel/0.7.7
final campaign_space 20261105-015350).

## History
- 2026-11-05: opened.
- 2026-11-05: fixed in the detector (fix/w44-world-state2).
- 2026-11-05: released in 0.7.8.
