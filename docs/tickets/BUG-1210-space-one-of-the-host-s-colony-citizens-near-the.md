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
* Observed (`coop.wobj` of the guest at the end of the failing runs before 0.7.15's fixes): `kind space_citizen
  binds=18 own=1 snap_n=19`: one host citizen near the guest had no guest citizen within the bind radius (30 m) for a
  while (`missing` counts it per snapshot), and the guest's own citizen of that place stood elsewhere.
* Observed (test diagnostics added here, lab 20261116-092223, -092716, -093620): most of the misses were the test's
  pairing, not the game: citizens stand in groups 0.5-2 m apart, and the test paired each host citizen with the nearest
  guest citizen not yet taken, in host order (then nearest first over all pairs): a host citizen took its neighbour's
  partner, the neighbour counted as "elsewhere" (its nearest guest citizen 0.5-2.3 m away) or got a guest citizen of
  the other size (pairs such as 2.15/1.88 and 1.79/2.15 in the same group, every round). Real misses were runners
  3-5 m behind and, now and then, the unbound citizen (19-21 m).
* The unbound one: a host citizen standing 0.1 m from a bound one (two host citizens on one spot) with no guest
  citizen for it; the guest's spare citizen of that group stood elsewhere (lab 20261116-094313 #3: `binds=18 own=1
  missing=148`, the miss's nearest guest citizen 0.1 m away: its neighbour's partner, in every round).
* Seen on the way: with the relocation below the first runs had "citizens of another size" at the host's places;
  those were the test's pairing too (the same swaps without a relocation, lab 20261116-093620), so the relocation was
  backed out (73b8bc2a) and put back (c271236b) once the test paired correctly.

## Fix
* Mod (worldobj.cpp, kind `space_citizen`): `relocate` 400 m (the snapshot's range) with `space_citizen_place`
  (game::stop + game::place, as the drive does): a host citizen with no guest citizen within 30 m takes the guest's
  nearest citizen without a partner, which is put at the host's place and bound (then driven every frame like the
  others). Switch `coop.fix space.citizen_relocate off`: it stays unbound (0.7.14). No wire change.
* Test: space_planet_citizens counts with a maximum matching (augmenting paths): every host citizen near the guest
  gets a guest citizen of its own within 3 m, at its size (12 %) first, then any size; the limits (5 % elsewhere, 5 %
  of another size) are unchanged. It prints each miss (the nearest guest citizen's distance, or host/guest size).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-16: opened.
