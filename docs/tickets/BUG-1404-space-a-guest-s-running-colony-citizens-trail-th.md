---
id: BUG-1404
title: Space: a guest's running colony citizens trail the host's by 3-4 m (snapshots every 0.5 s; space_planet_citizens 4 of 57 elsewhere)
status: open
kind: desync
stage: Space
reported: 2026-11-21
source: final check 20261121-003202 (space_planet_citizens)
role: guest
versions: 0.7.15, unreleased
fixed_in:
branch: fix/w62-flake-tcs
commits:
tests: space_planet_citizens
signature:
docs: docs/re/space.md#space-citizen-lead
related: BUG-1210, BUG-1227, BUG-0490
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
FAIL (no change): final check out/lab/20261121-003202 (4 of 57), rel/0.7.15 out/lab/20261117-154911 (6 of 57).
Open, waiting for the lab run:
* PASS wanted: `SPORE_DETECTOR=20 python3 tools/lab.py run space_planet_citizens --repeat 4 --until all --lane N --retry 0 --no-build`
  (4 of 4; the old code failed in about half of the runs).
* FAIL twin: `SPORE_FIX_OFF=space.citizen_lead ... run space_planet_citizens --repeat 4 --until all` should fail in about half of
  the runs (a flaky twin: the old failure rate); also space_planet_vehicles and space_colony_spice PASS (same file).

## History
- 2026-11-21: opened; fixed on fix/w62-flake-tcs (space.citizen_lead), waiting for the lab run.
