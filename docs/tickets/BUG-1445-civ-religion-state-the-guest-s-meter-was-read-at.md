---
id: BUG-1445
title: civ_religion_state: the guest's meter was read at the follower write, before the city's next update recomputes it (0.0 / 26.7, followers equal)
status: fixed
kind: test
stage: Civ
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w65-night
commits: 8fff6369
tests: tools/tests/coop_civ_influence.py (civ_religion_state, civ_religion_state_off)
signature:
docs:
related: BUG-1285
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): civ_religion_state "FAIL the guest's religious meter is the host's (0.0 / 26.7)"; the followers
({'000021CB': 13, '800021CA': 2}) and the converter 800021CA matched.

## Root cause
Test: the guest writes the follower map and +0x754 (civ_influence.cpp, "+0x750 follows from the map in the city's next
update"); the meter +0x750 is recomputed by the city update 0xBE2260 (docs/re/civ.md#civ-influence-relay). The test's
wait_for waited for the followers only and read the meter in the same poll, right after the write. The key frame a
moment later shows the guest's meter bar at the host's level. Runs 2 and 3 read 26.7 / 26.7. No load (fps ~64). The
test is new in 0.7.16 (BUG-1285); the mod's behaviour is as designed.

## Fix
coop_civ_influence.py case_religion: wait (5 s) for the followers and the meter within the check's 2 points; the three
checks are unchanged.

## Verification
Was waiting for the lab run: PASS `SPORE_DETECTOR=20 python3 tools/lab.py run civ_religion_state --lane N --retry 0
--no-build`; FAIL `SPORE_DETECTOR=20 python3 tools/lab.py run civ_religion_state_off --lane M --retry 0 --no-build`
(civ.religious_state off: the guest's followers stay 0). no-fail-run: a test fix; FAIL before 20261122-194210.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): civ_religion_state PASS in 20261123-015558 and 20261123-023643; the twin civ_religion_state_off FAIL in 20261123-020905 (the guest's followers 0 / 2). Closed.

## History
- 2026-11-23: opened; fix/w65-night: the test waits for the meter too.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
