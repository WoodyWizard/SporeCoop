---
id: BUG-1457
title: Civ: right after a capture the host's citizens of the guest's city crowd its plaza while the guest's game has none there (civ_own_species key frames 02 / 03)
status: open
kind: desync
stage: Civ
reported: 2026-11-23
source: lab
role: guest
versions: 0.7.16
fixed_in:
branch:
commits:
tests: civ_own_species, civ_own_species_hostmode (key frames guest_citizen1 / 2)
signature:
docs:
related: BUG-0476, BUG-0526, BUG-0553, BUG-1273
---

## Report
Key frame review of the 0.7.16 final check, lab 20261122-194210: the frames shot right after the host captured a
city for the guest, the same camera on both games at a host citizen of that city:
* `out/lab/20261122-194210/frames/sheets/017_civ_own_species_02_guest_citizen1.jpg`, `018_civ_own_species_03_guest_citizen2.jpg`,
  `021_civ_own_species_hostmode_02_guest_citizen1.jpg`, `022_civ_own_species_hostmode_03_guest_citizen2.jpg`: the host
  shows 8-12 citizens crowded in the plaza of the guest's city, the guest's game shows the same plaza empty (in the
  hostmode frame 03 three citizens far behind it).
* The test's checks pass (19 citizens of the right species, baked, on both games); the detector at the same moment:
  `Civilization:unmatched:citizen` "19 on the host, 19 on the guest, 12 host / 12 guest without a partner within 15 m"
  (14 / 14 in the next sample).
* Lab 20261120-133703 and 20261121-003202 (same test, frames 02): the citizens stood at the same places on both games.
  Lab 20261119-214823 had the same 12 of 19 unmatched (no frame of it checked).
* The guest's `coop.avatar` position in Civ is (0, 0, 0) in every frame of these tests.
A player does not see both screens: on the guest's own screen its city has its 19 citizens, at other places than the
host's game has them.

## Root cause
Not analysed. Candidates: the citizens are made again when the city's species changes at the capture (BUG-1273,
civ.own_species resync) and the guest's new citizens are not bound yet to the host's (BUG-0476's binding is by position
within 30 m and puts a citizen there only when 12 m off); or the host sends only the 56 citizens nearest each guest's
pointer and the guest's pointer / place is unknown here (avatar at the origin). Check `coop.civ citizens` and the
worldobj citizen bind counters on both games for 10 s after the capture.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened (fix/w66-frames, key frame review of the 0.7.16 final check, lab 20261122-194210 on 5376d388).
