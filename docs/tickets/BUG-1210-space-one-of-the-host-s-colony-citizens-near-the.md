---
id: BUG-1210
title: Space: one of the host's colony citizens near the guest stands elsewhere in the guest's game (unbound: no guest citizen within 30 m)
status: open
kind: desync
stage: Space
reported: 2026-11-16
source: final check 20261116-025154 (space_planet_citizens)
role: guest
versions: 0.7.14.1, unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
0.7.15 final check (lab 20261116-025154): space_planet_citizens "the guest's game shows the host's citizens where they
stand (3 of 57 elsewhere, at most 5 %)"; rel/0.7.15 again on lane 7 (lab 20261116-082811: 7 of 57) and v0.7.14.1 on
lane 6 (lab 20261116-084330: 3 of 57, 1 in each round). The test's history: 4 of 8 failed since 2026-11-04 (6, 4, 3
of 57): an old failure, not a 0.7.15 regression.

## Root cause
* Observed (`coop.wobj` of the guest at the end of each failing run): `kind space_citizen binds=18 own=1 snap_n=19`:
  of the host's 19 citizens near the guest, 18 were bound to a guest citizen; one host citizen had no guest citizen
  within the bind radius (tol 30 m) and stayed unbound for the whole test (`missing` counts it every snapshot), and the
  guest's own citizen of that place, without a partner, stood where its own game put it. The test sees that one
  citizen in every round (1 of 19 elsewhere: 3 of 57 > 2.85).
* Each game puts the colony's pool of citizens at the cities near its own camera (docs/re/space.md#space-planet-life);
  the space_citizen kind only bound, it had no second pass (worldobj.cpp `relocate`, as fruit, cultural targets and
  ornaments have) for a host object without a copy within tol.

## Fix
worldobj.cpp, kind `space_citizen`: `relocate` 400 m (the snapshot's range) with `space_citizen_place` (game::stop +
game::place, as the drive does): a host citizen with no guest citizen within 30 m takes the guest's nearest citizen
without a partner, which is put at the host's place and bound (then driven every frame like the others). Switch
`coop.fix space.citizen_relocate off`: unbound as before (0.7.14).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
