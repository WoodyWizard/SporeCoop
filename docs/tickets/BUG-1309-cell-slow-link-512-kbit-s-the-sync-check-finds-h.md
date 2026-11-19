---
id: BUG-1309
title: Cell, slow link (512 kbit/s): the sync-check finds hundreds of the host's cells missing on a guest that just arrived
status: open
kind: desync
stage: Cell
reported: 2026-11-19
source: lab: evo_screens_switch_slow
role: guest
versions: 0.7.15.1, 0.7.15.2, 0.7.16
fixed_in:
branch:
commits:
tests: evo_screens_switch_slow
signature: sync-check "c: N missing on the guest" under SPORE_NET=lowband
docs: docs/known-issues.md#issue-cell-slow-link-missing
related: BUG-1240, BUG-1245, BUG-1255
---

## Report
The lab marks every run of `evo_screens_switch_slow` (coop_evo_screens.py --part switch, SPORE_NET=lowband: the
guest's link capped at 512 kbit/s, 40 ms delay) FAILED by the log gate sync-check, while the test's own checks pass
(RESULT: OK). rel/0.7.16 lab 20261119-102828 (lane 2): host and guest log
`sync-check: MISMATCH ... (Cell 9652F8AD, 135 objects compared): c: 247 missing on the guest` 7.5 s after the guest
appeared in the Cell world (its world transfer took 56 s at 55 KB/s with 72 % resends), and again
`c: 13 missing on the guest` at the start of the Creature switch vote (100 s).

Not new in 0.7.16: the same signature in every run of this test since it exists (2026-11-18), on the release trees
too: rel/0.7.15.1 out/lab/20261118-110806 (301 missing), 20261118-121937 (304), rel/0.7.15.2 out/lab/20261118-144132
(295 missing, 1 extra), 20261118-164423 (165), fix/w53-cellvote 20261118-123948 (188). BUG-1240's ticket noted it
on 2026-11-18 as outside that fix. The clean-link twins (evo_screens_switch, _noready, _lowfps) have no Cell
mismatch.

## Root cause
Not analysed (found by the 0.7.16 fix agent fix/w57-cellkin, whose task was to tell flaky / new / old).
Inferred: the guest's first Cell state after its arrival (the host's pool of ~400 cells, mirrors=364 on the guest at
60 s) does not fit the 512 kbit/s link in the sync-check's first window, and a level change or the vote start
reshuffles the host's pool faster than the slow link carries it. Open question: does the guest's screen really lack
those cells for long (a player on a slow link), or only for the seconds the check samples (then a gate rule with the
reason is the answer, not a code change).

## Fix
Not fixed: outside the 0.7.16 fix lane of fix/w57-cellkin ("fix if ours": it is not, the same in 0.7.15.1 / 0.7.15.2).

## Verification
Open. Every run of evo_screens_switch_slow shows it (run ids in the Report).

## History
- 2026-11-19: opened.
