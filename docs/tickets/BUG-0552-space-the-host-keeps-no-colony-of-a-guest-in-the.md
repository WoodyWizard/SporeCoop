---
id: BUG-0552
title: Space: the host keeps no colony of a guest in the host's home system (allies.own_star lists it in the host's roster; BUG-0195 x BUG-0197)
status: fixed
kind: bug
stage: Space
reported: 2026-11-04
source: final check out/lab/20261104-135838 space_colony_rejoin
role: host
versions: 
fixed_in: 0.7.6
branch: rel/0.7.6
commits: 7566bc8e
tests: space_colony_rejoin, space_allies_own
signature:
docs:
related: BUG-0195, BUG-0197
---

## Report
space_colony_rejoin failed in the 0.7.6 final check (out/lab/20261104-135838) and passed before S3 was merged (out/lab/20261104-123722): `the host's roster of the guest has colony 01460035 as the guest's own` FAIL, the kept list empty (`colonies=0`), after the rejoin the colony is gone in the guest's game (tech 1). The test's guest founds the colony in the home system it shares with the host.

## Root cause
Observed (code, logs): allies.own_star (BUG-0197, a4a27f04) makes a partner's colony in a star system of mine a record-level colony of my empire (cEmpire_AddPlanet). The host's own roster (space_map.cpp read_local walks its empire's stars and planets) then lists the guest's colony as the host's, and own_colony(guest, c) is false for every planet the host's roster has: the host kept nothing for the guest (BUG-0195).

## Fix
space_allies.cpp `own_mirror`, space_map.cpp read_local: a planet allies.own_star shows as a colony of my empire is left
out of my own roster (it is the partner's). No own switch: `coop.fix allies.own_star off` takes the planet out too.

## Verification
space_colony_rejoin: FAIL without the fix out/lab/20261104-152113 (and the final check out/lab/20261104-135838); PASS with
it out/lab/20261104-153307 (with space_allies_own, both OK).

## History
- 2026-11-04: opened.
- 2026-11-04 (rel/0.7.6): fixed (own_mirror out of the roster).
- 2026-11-04: released in 0.7.6.
