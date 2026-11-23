---
id: BUG-1448
title: space_planner_miss: the guest's real click on its city did not open the planner (ship steady over the city, no load, no dialog); cause not found
status: open
kind: test
stage: Space
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits: 8fff6369
tests: tools/tests/coop_space_planner_build.py (space_planner_miss)
signature:
docs:
related: BUG-0902, BUG-0751
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): space_planner_miss "FAIL the guest opened the city planner by a click on the city", and following from it "FAIL a
taken plot on the guest's planner screen" (the city seen from the ship, not the planner's row) and "FAIL the guest's
planner added the building at the drop (+0)".

## Root cause
Not found. The guest's ship stood over city s4 the whole time (305 -285 586 from 23:11:26 to 23:11:56, ~243 m from the
city), so all 4 clicks hit the same spot; no load (guest fps 61-65, 0 hitches; host 64), no busy / input_lock until the
test's DONE clicks; space_planner_open.png (23:11:55.87) shows the city, no planner, no dialog. The sibling tests with
the same click passed in the same night on the same commit (space_planner_build, _host, _away); 0 of 9 failed on
rel/main; no commit since 21025f40 touches the planner or the Space click path. Not shown to be a 0.7.16 regression.
Open question: whether the city's screen point was on screen at the click (`on`, not printed before).

## Fix
Diagnostics only (no check changed): each click that opens no planner prints the point, whether it is on screen, the
planner line and the busy line, and leaves a shot space_planner_try<N>.png. A rerun should pass; a repeat now says what
the click hit.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run space_planner_miss --lane N --retry 0
--no-build` PASS expected. no-fail-run: cause not found; FAIL 20261122-194210.
- Still open (2026-11-23): space_planner_miss PASS in 20261123-015558 and 20261123-023643, but the change is diagnostics only and no run missed the click again, so the cause is still unknown.

## History
- 2026-11-23: opened; fix/w65-night: evidence collected, diagnostics added.
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
