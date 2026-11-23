---
id: BUG-1447
title: civ (coop_mode --fixture civ): the host's marker moved at the test's click and was put back at once by a pointer move (ptr_still reset at the click, host moved 0.0 m)
status: open
kind: test
stage: Civ
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits: 8fff6369
tests: tools/tests/coop_mode.py (civ, and the other click modes)
signature:
docs:
related: BUG-0042, BUG-0251, BUG-0713
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): civ (coop_mode.py --fixture civ) "cursor marker (network): host moved 0.0 m, guest has it 0.0 m off (0.0 m from the
start)", "FAIL the checks above". The same on rel/0.7.15 (20261117-154911, 20261117-213354); 2 of 15 failed on rel/main.

## Root cause
Test input (a flake outside the mod): the host's Civ marker follows the last mouse move over its window (modes.cpp
mouse_on_planet, mouse.cpp). Host trace: pos=(277.0 394.4 -147.3) at s 0, 10, 20 and 30, but `ptr_still=8.6` at s 30,
i.e. the marker moved (over 0.5 m) at s 21.4 = 00:39:52.8, the test's click; a mouse move right after put it back on
its start point, the same point the guest's never-clicked marker shows in passing runs (the pointer's own place). No
fault of the mod shown; a player has one pointer.

## Fix
coop_mode.py: in Tribe / Civ the click is repeated (up to 3 times, printed) until the host's own marker left its start
point; the checks (the host moved, the guest has it) are unchanged.

## Verification
Open, waiting for the lab run: PASS `SPORE_DETECTOR=20 python3 tools/lab.py run civ --lane N --retry 0 --no-build`.
no-fail-run: a test fix; FAIL before 20261122-194210 civ.

## History
- 2026-11-23: opened; fix/w65-night: the click again while the marker is back at its start.
