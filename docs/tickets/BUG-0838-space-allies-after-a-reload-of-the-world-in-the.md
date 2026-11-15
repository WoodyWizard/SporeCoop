---
id: BUG-0838
title: Space allies: after a reload of the world in the session the mirror re-adds the old galaxy's cCivData (possible crash on landing, suspected)
status: fixed
kind: crash
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-freed
commits: fdc10ecc
tests: space_allies_snap
signature:
docs: docs/re/space.md#space-allies-record-reload
related: BUG-0197
---

## Report
Suspected from the code: a host that loads a world again while a partner's colony is mirrored may crash or see wrong cities when landing on that planet.

## Root cause
Observed at runtime (fix/w48-freed, 2026-11-12), partly as the ticket inferred: a mirror unmirrored before a reload of the world (`allies remove`, as the save before the galaxy menu does) kept its snapshot's cCivData, and the first apply after the load (Game0/Madas again) put that old object back into the planet's record (lab 20261112-011823: 15EE6CE8 again). Not as inferred: the record is the same object after the reload (our reference keeps it in the game's resource cache), so the old one was not released in that run; and no crash was seen (nobody landed there).

## Fix idea
Call forget() on every real reset_empire, or tag snapshots with spaceext::world_epoch() and drop stale ones.

## How to test
Host with a guest colony mirrored saves, goes to the galaxy menu and loads while connected; then `coop.space allies planet <id>` and land there: FAIL = crash or stale cities in the host log; PASS = clean rebuild.

## Fix
mods/coop/space_allies.cpp: a reset of the mirror's empire for another galaxy (reset_empire) also drops the snapshots while the mirror is not applied (forget); add_planet / add_planet_own drop the kept cities when the record is another object than the one they came from. Switch: `coop.fix allies.snap_world off`. Test hook: `coop.space allies civ <PlanetID>`.

## Verification
Test `space_allies_snap` (tools/tests/coop_space_allies_snap.py).
* FAIL (SPORE_FIX_OFF=allies.snap_world): out/lab/20261112-011823 (the old world's cCivData in the record again).
* PASS: out/lab/20261112-013003 (none re-added, new ones made); out/lab/20261112-011341 showed the same and failed only on a check for a new record object (it is the same object: the check is gone).

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
- 2026-11-12: fixed on fix/w48-freed (allies.snap_world); the record object comes back from the cache (docs/re/space.md#space-allies-record-reload).
- 2026-11-12: released in 0.7.13.
