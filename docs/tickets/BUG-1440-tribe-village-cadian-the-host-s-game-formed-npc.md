---
id: BUG-1440
title: tribe_village_cadian: the host's game formed NPC tribe 1 during the Tribe map read: the host's half read the ground before the village's stamps, the guest's after (14 points, 7.07 m)
status: open
kind: test
stage: Tribe
reported: 2026-11-22
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits: 1a9f358f
tests: tools/tests/coop_tribe_village_ground.py (tribe_village_cadian, --map)
signature:
docs:
related: BUG-0332, BUG-0947
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 3, rel/0.7.16 5376d388): tribe_village_cadian "FAIL the
guest's ground is the host's over the whole region (1499 points within 45 deg of the village: 14 differ by more than
0.3 m, the largest by 7.07 m)". The same failure earlier: 20261115-182010 (feat/w51-tribe, 32 points, 8.06 m),
20261117-154911 (rel/0.7.15, 32 points, 8.49 m). Not new in 0.7.16.

## Root cause
Test: the map read spans a change of the terrain. `ground_map(host)` read all 1499 points, then `ground_map(guest)`
(~75 s in all). About 60-65 s into Tribe the host's game forms NPC tribe 1 at a spawn marker on its own (every run of
this test): host `21:52:36.147 terrain: ApplyStamp from 0x00C9DFBF pos (-11.84 -505.28 11.84) |r| 505.56 ... id
489159990`, `21:52:37.378` the 0x00C91981 stamp ("the village at (-12 -505 12) made during play gets the ground stamp a
load gives it", BUG-0947); the guest made the same tribe and stamps 0.12 s and 0.31 s later (`21:52:36.265 the host's
NPC tribe 1 at (-12 -505 12) is not in this game: made it`). This time it fell between the two halves of the read (the
guest arrived 9 s after the load instead of 4-5 s): the host's half had the ground before the village, the guest's
the flattened ground after. All 14 points lie 45-81 m from (-12 -505 12), guest lower, the guest's radius at the
stamp's 505.56 (505.77, 505.88, 505.95), the host's the old ground (506.36, 509.02, 506.45). Once settled the grounds
are the same: "stamps: host 162, guest 162; only on the host 0, only on the guest 0", rings around marker 1 0.00, "the
guest's ground round every village (7) ... 0.0 m". The places list was built before the map (marker 1 still there).
Not the mod: both games got the same stamps with the same ids.

## Fix
coop_tribe_village_ground.py: `settled_maps()` reads both games' ground point by point (host then guest at each point)
after both games' stamp counts (`coop.terrain`: `live stamp calls=`) stood still for 8 s, and reads again when a count
changed during the read; a terrain that never stands still within 240 s is a FAIL (the mod could be stamping in a
loop). The places list is built after the map, so a village formed during play is checked on rings too. The 0.3 m
limit and every check stay.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_village_cadian --lane N --retry 0
--no-build` PASS (3 runs: `--repeat 3`); a run with "(the terrain took stamps during the map read ...: reading the map
again)" that ends PASS shows the guard at work.
no-fail-run: a test fix; the FAIL before is 20261122-194210 tribe_village_cadian (a race of the village's forming
against the map read).

## History
- 2026-11-22: opened.
- 2026-11-22: fix/w65-night: cause from the stamp times and the 14 points' places; the map is read on a settled terrain.
