---
id: BUG-1457
title: Civ: right after a capture the host's citizens of the guest's city crowd its plaza while the guest's game has none there (civ_own_species key frames 02 / 03)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-23
source: lab
role: guest
versions: 0.7.16
fixed_in: 0.7.16.1
branch: fix/w67-citizens
commits: a7dea9df b636001b 0fca2c5e
tests: civ_own_species, civ_own_species_hostmode (the citizen pairing after the capture; key frames guest_citizen1 / 2), twin civ_own_species_pointer_off
signature:
docs: docs/re/civ.md#civ-citizens-pointer, docs/06-testing.md#testing-pointer-unplaced
related: BUG-0476, BUG-0526, BUG-0553, BUG-1273, BUG-0713
switch: civ.pointer_view
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
Observed (frame manifests, `coop.wobj`): not the capture and not 0.7.16. A Civ player's position is its pointer, the
planet point under the last mouse move over its game window (modes.cpp `mouse_on_planet`, BUG-0713), (0 0 0) until
the first one. The lab's guest got no mouse move in some tests: its position was (0 0 0) in every key frame of
civ_own_species and civ_own_species_hostmode in 20261122-194210 (also civ_own_species in 20261119-214823, the run with
the same 12 of 19), placed near the city in the matching runs 20261120-133703 and 20261121-003202
(`frames/*/manifest.jsonl` `games.guest.pos`; other Civ tests of the same runs vary too, not tied to a lane).
worldobj `citizen` (BUG-0476) centres the host's citizen snapshot for a guest on that position, on the host's capital
when it is (0 0 0) (200 m, the 56 nearest). Both cameras at the captured city, 430 m from the capital: the host's pool
had no citizen near the capital, the snapshots were empty (guest `kind citizen binds=0 snap_n=0 rx=226`, lab
20261123-095006 twin) and the guest's 19 citizens of that city walked on their own (8-14 of the host's 19 without a
guest citizen within 15 m for 20 s). The species change at the capture (BUG-1273) only made the key frames look there.
A player's game has a mouse move almost at once, so its pointer is placed; a player who has not moved the mouse over
the game since it started had the same.
docs/re/civ.md#civ-citizens-pointer.

## Fix
modes.cpp `mouse_on_planet`: in Civ, before the first mouse move over the game window, the pointer is the point under
the window's centre (where its camera looks), so the host sends the citizens of the city the guest looks at and the
guest binds them (`binds=19 snap_n=19`). Switch `coop.fix civ.pointer_view off`: (0 0 0) until the mouse moves
(0.7.16). Tests: `coop.npc marker unplace` (the pointer at (0 0 0), the last mouse move forgotten).
civ_own_species unplaces the guest's pointer at the start and checks after the capture that at most 2 of the host's
citizens of the city have no guest citizen within 15 m and at most 2 have no own copy (the guest's citizen bound to it,
`coop.wobj list citizen`) within 15 m, in one sample within 20 s; twin `civ_own_species_pointer_off`.

## Verification
- FAIL with SPORE_FIX_OFF=civ.pointer_view (civ_own_species_pointer_off): out/lab/20261123-095006 (8-14 of 19
  unpaired for 20 s, guest pointer (0 0 0), `binds=0 snap_n=0`); out/lab/20261123-095727 (best sample 3 of 19
  unpaired by position alone: a crowd paired by chance, hence the own-copy rule); with the own-copy rule
  out/lab/20261123-100251 x2 (7 and 14 of 19 unpaired, 19 of 19 without their copy).
- PASS: out/lab/20261123-095006 civ_own_species (0 of 19 unpaired, guest pointer at the city); out/lab/20261123-095217
  civ_own_species x3 and civ_own_species_hostmode x3 (0 of 19 each, no `unmatched:citizen` from the detector
  SPORE_DETECTOR=20; key frames 02 / 03: the same crowd in the plaza on both games); out/lab/20261123-100251
  civ_own_species x2 with the own-copy rule (0 of 19, 0 without their copy).
- Regressions (out/lab/20261123-095727): civ_owners_session, civ_citizens, civ_citizen_lod, find_partners_civ PASS;
  civ_planner_build FAIL, also with civ.pointer_view off (20261123-100735) and PASS with civ.planner_closed off
  (20261123-100903): BUG-1532 (BUG-1417's change on rel/0.7.16.1), not this one.

## History
- 2026-11-23: opened (fix/w66-frames, key frame review of the 0.7.16 final check, lab 20261122-194210 on 5376d388).
- 2026-11-23 (fix/w67-citizens): cause found (the guest's unplaced pointer, the citizens sent around the host's capital); fixed (civ.pointer_view).
