---
id: BUG-0541
title: Space: 9 ornaments by one colony city exist in one game only (Space:guest_only:ornament / host_only:ornament)
status: fixed
kind: desync
stage: Space
reported: 2026-11-04
source: divergence detector (space_play, out/lab/20261104-104326 of fix/w42-space-planets; rel/0.7.5 out/lab/20261104-013517)
role: 
versions: 
fixed_in: 0.7.9
branch: fix/w45-world-objects
commits: 4ff16746, c73a0a14
tests: host_space_ornament (tools/tests/coop_world_host.py --case ornament)
signature:
docs: docs/re/space.md#space-city-ornaments
related:
switch: wobj.city_ornament
---

## Report
Detector: `Space:guest_only:ornament` (9 ornaments, noun 0x18C88E4, near city 0 of the home planet at (146 325 -362), on
the guest only) once in space_play (out/lab/20261104-104326, after the guest's death and respawn at that city);
`Space:host_only:ornament` (9, the same spot, on the host only) once in rel/0.7.5 out/lab/20261104-013517. Found by the
lab (no player report). How to see it: `space` fixture, both land (`coop.space land`), `coop.census all 3000 0 0 0` on
both: lane 3 2026-11-04 the host had 14 ornaments (9 within 80 m of city 0 (153 314 -368)), the guest 5 (0 there), and
putting the guest's ship over city 0 (`coop.space over 153 314 -368`, 20 s) made none there: not the camera's level
of detail. The whole-list counts differed in other sessions too (32 / 41).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the ornaments are a city's decorations (noun 0x18C88E4, model 0x2AE5BA7 at +0x228), 9 per city from
City_MakeDecorations 0xBE6A50 (the city setup 0xBE7D40 and the callback 0xB90DA0), placed in free slots of the city's
layout. Which cities have their sets differs per game and per load (5 to 86 ornaments of the noun on the `space` home
planet, stable for a minute, not by the camera: the host's ship over a bare city for 25 s made none), and a city's
group variants differ too. Lab 20261106-044822, -045215, -045654, -050420. Not the planet record's flag-0x80 items
(0xFF96B0, model 0x345BD5D): none on the planet in either game (the earlier inference).

## Fix
Kind `city_ornament` (Space planets, whole planet, the 56 nearest the guest's ship): a guest's decoration binds to the
host's within 1.5 m, else its nearest unbound one anywhere is moved there (position and the planet's up); a host city
set the guest lacks is made on the guest's nearest bare city (City_MakeDecorations) and moved; a guest's decoration
without a host partner is removed as City_RemoveTurret removes a turret (Layout_ClearObject 0xAFADD0,
DestroyInstance, VectorPtr_EraseValue 0xD5AA90). The host also sends an empty Space snapshot now (a host planet with no
decorations has none on the guest). Switch `coop.fix wobj.city_ornament off`. ~ The model variant stays the guest's.

## Verification
host_space_ornament (space fixture, both land 3 times with a takeoff between, every ornament of the noun compared in
both games' lists):
- FAIL with `SPORE_FIX_OFF=wobj.city_ornament`: lab 20261106-050420 (all 3 landings differ: 77 / 14, 59 / 41, 50 / 59).
- PASS: lab 20261106-050848 (23 / 23, 50 / 50, 5 / 5; sets made 2, removed 45, no fault).
- Detector: play_space had no ornament row before (L1: Space guest_only ornament 7 %) nor after (lab 20261106-051337).

## History
- 2026-11-04: opened.
- 2026-11-06 (fix/w45-world-objects): fixed (kind city_ornament), test host_space_ornament.
- 2026-11-07: released in 0.7.9.
