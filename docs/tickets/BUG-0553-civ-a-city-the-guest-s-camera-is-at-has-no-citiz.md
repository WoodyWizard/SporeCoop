---
id: BUG-0553
title: Civ: a city the guest's camera is at has no citizens on the guest (its game moved them there, the mod puts them back at the host's city)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-04
source: final check out/lab/20261104-135838 civ_citizen_lod
role: guest
versions: 
fixed_in: 0.7.6
branch: rel/0.7.6
commits: 7566bc8e
tests: civ_citizen_lod (tools/tests/coop_civ_citizen_lod.py)
signature:
docs:
related: BUG-0476, BUG-0526
---

## Report
civ_citizen_lod failed in the 0.7.6 final check (out/lab/20261104-135838) and in the rerun (out/lab/20261104-152113):
`at city 2 the guest's game has citizens (0), the host's none (0)`: the guest's camera at an NPC city 855 m from the
capital, the guest's game had all 19 of its city citizens at the capital (where the host's are), the city it looks at
empty. A player who looks at another city than the host sees no citizens walking there.

## Root cause
Observed: with `coop.fix civ.citizens off` the same test passes (out/lab/20261104-153304: the guest's 19 at city 2).
Each game moves its pool of citizens to the city near its own camera (BUG-0526). In Civ the host sends its citizens
around the guest's position, which is the host's capital (civ_center) when the guest has none; the guest's copies stay
bound to them (BUG-0476: a bind is kept however far apart), and citizen_follow put every one the guest's game had moved
to city 2 back at the host's capital (d > kCitizenPut).

## Fix
worldobj.cpp citizen_follow: a bound copy farther than 60 m (kCitizenFar) from the host's is left where the guest's game
put it (no walk lags that much: it is the game's level of detail). Switch `coop.fix civ.citizen_far off`.

## Verification
civ_citizen_lod (the guest's game has at least half its city citizens at the city its camera is at):
- FAIL with SPORE_FIX_OFF=civ.citizen_far: out/lab/20261104-154612 (2 of 2: 0 of 19 there);
- PASS: out/lab/20261104-155021 (2 of 2); civ_citizens, play_civ, detect_civ PASS out/lab/20261104-153531.

## History
- 2026-11-04: opened.
- 2026-11-04 (rel/0.7.6): fixed (civ.citizen_far).
- 2026-11-04: released in 0.7.6.
