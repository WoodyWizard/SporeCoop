---
id: BUG-1446
title: civ_mirror_lifetime: the new guest vehicle stood 24.4 m from the host's vehicle, inside the 25 m bind-by-kind radius: bound by that rule, not through the freed block (not BUG-0951's case)
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
tests: tools/tests/coop_civ_mirror_lifetime.py (civ_mirror_lifetime)
signature:
docs:
related: BUG-0951, BUG-1140
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): civ_mirror_lifetime "FAIL that vehicle was not driven as the host's vehicle (taken for it: [7])". The same on
rel/0.7.15 (20261117-154911: taken for it [8], the new vehicle 20.2 m from the host's).

## Root cause
Test (its precondition): the new guest vehicle at the freed block of mirror #7 was made at (255.00 426.52 -84.06), the
host's vehicle at (234.02 437.29 -90.33): 24.4 m, inside the 25 m in which a mirror binds a local vehicle of its
civilization, purpose and locomotion (civ_guest.cpp, `float best = 25.f`; `own_civ=1`, the hunt falls back to any
mirror when there is no NPC mirror, civ_debug.cpp). `bound_as=7` came from that designed rule, not from the freed
address (BUG-0951's case). The game makes the vehicle near the asked point, not at it; passing runs made it 27-32 m
away. Not a 0.7.16 regression.

## Fix
coop_civ_mirror_lifetime.py: a round whose new vehicle stands under 30 m from the host's vehicle is not counted
(another round, printed); the checks are unchanged.

## Verification
Was waiting for the lab run: PASS `SPORE_DETECTOR=20 python3 tools/lab.py run civ_mirror_lifetime --lane N --retry 0
--no-build`. no-fail-run: a test fix (the test is a regression guard without a twin); FAIL before 20261122-194210.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): civ_mirror_lifetime PASS in 20261123-015558 and 20261123-023643. Closed.

## History
- 2026-11-23: opened; fix/w65-night: rounds inside the bind radius are not this test's case.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
