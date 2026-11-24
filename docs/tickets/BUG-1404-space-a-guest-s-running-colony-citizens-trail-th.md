---
id: BUG-1404
title: Space: a guest's running colony citizens trail the host's by 3-4 m (snapshots every 0.5 s; space_planet_citizens 4 of 57 elsewhere)
status: fixed
kind: desync
stage: Space
reported: 2026-11-21
source: final check 20261121-003202 (space_planet_citizens)
role: guest
versions: 0.7.15, unreleased
fixed_in: 0.7.99
branch: rel/0.7.99
commits: 80ee2745, 84c8f9e6, 587a24de
tests: space_planet_runners, space_planet_runners_lead_off, space_planet_citizens
signature:
docs: docs/re/space.md#space-citizen-lead
related: BUG-1210, BUG-1227, BUG-0490
switch: space.citizen_lead
---

## Report
0.7.16 third final check (lab 20261121-003202, lane 2, warm): space_planet_citizens "the guest's game shows the host's
citizens where they stand (4 of 57 elsewhere, at most 5 %)". History: 4 of 9 failed on rel/main, 16 of 32 on all
branches since 2026-11-04 (`lab.py flaky --test space_planet_citizens`); after BUG-1210 (0.7.15) also
20261117-154911 rel/0.7.15 (6 of 57). An old failure, not a 0.7.16 or w61 regression (no w61 commit touches
worldobj.cpp's space kinds or the test).

## Root cause
* Observed (the test's miss lines and `coop.wobj` at the end): every round has 19 host citizens near the guest and 19
  guest citizens, all bound (`kind space_citizen binds=19 own=0 ... missing=0`); the misses' nearest guest citizen is
  2.7-4.3 m away (20261121-003202: [4.3, 3.3], [2.7, 2.8]; 20261117-154911: [3.4, 3.5, 3.5, 3.2], [4.2, 3.1]), 0 of
  another size. So no unbound citizen (BUG-1210) and no pairing error of the test: bound copies off by 3-4 m.
* Inferred: those are the runners (~5 m/s, docs/re/space.md#space-planet-life). The kind was sent every 0.5 s;
  space_citizen_drive puts a copy every frame at the last snapshot place moved on with the velocity of two snapshots
  0.5 s apart (at most 1 s ahead). A runner's start, stop or turn reached the copy 0.5-1 s late: 2.5-5 m off for a
  moment. Civ's city citizens had the same with 0.5 s snapshots (BUG-1227: p90 3.4 m with the glide, 1.4-1.7 m at
  0.25 s); the Space kind never got that faster tick.

## Fix
Mod (worldobj.cpp, kind `space_citizen`): the BUG-1227 fast tick: the host sends the kind every 0.25 s to guests of
0.7.15 on (HELLO families; an older guest still every 0.5 s) and the guest takes the velocity from snapshots over 0.15 s
apart (closer ones keep the last). Switch `coop.fix space.citizen_lead off`: every 0.5 s (0.7.15). No wire change
(the same snapshot message, twice as often for this kind: at most 56 citizens).
The test is unchanged (limits 3 m, 5 %); its docstring names the twin.

## Verification
PASS: out/lab/20261124-152417 (space_planet_runners, 0 faults).
FAIL: out/lab/20261124-161419 (space_planet_runners_lead_off: 11/263 over 2.5 m, p90 1.89 m).

## History
- 2026-11-21: opened; fixed on fix/w62-flake-tcs (space.citizen_lead), waiting for the lab run.
- 2026-11-23: BUG-1454: the twin passed 4 of 4 (20261122-192851): space_planet_citizens cannot show this switch (2 of 9 runs fail at 0.5 s snapshots, the "about half" was before BUG-1210); the claim moves to the new space_planet_runners (limits to be measured).
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
- 2026-11-23: fix/w69-triage2: space_planet_runners shows the switch (BUG-1454): fix on 0-1.4 % of runner samples over 2.5 m and p90 0.90-1.28 m (20261123-111347), off 6.1-9.9 % and p90 2.03-2.48 m (20261123-121417); new limits 3.5 % / p90 1.6 m and twin space_planet_runners_lead_off.
- 2026-11-24: released in 0.7.99.
