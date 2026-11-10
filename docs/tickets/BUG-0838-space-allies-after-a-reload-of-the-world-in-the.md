---
id: BUG-0838
title: Space allies: after a reload of the world in the session the mirror re-adds the old galaxy's cCivData (possible crash on landing, suspected)
status: open
kind: crash
stage: Space
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0197
---

## Report
Suspected from the code: a host that loads a world again while a partner's colony is mirrored may crash or see wrong cities when landing on that planet.

## Root cause
Inferred, confidence medium. space_allies.cpp:473-494 unapply keeps PlanetSnap.rec and .civ; the reload checks (:626, :1013) run only while the mirror is applied, which it is not here; ensure_empire (:358) calls reset_empire without forget(); add_planet (:531-534, :566-569) then AddCivData's the old pointers into the new world's planet record (freed objects of the previous load). Line numbers are of main e49e1e23 (0.7.11.1); the function names hold on rel/0.7.12.

## Fix idea
Call forget() on every real reset_empire, or tag snapshots with spaceext::world_epoch() and drop stale ones.

## How to test
Host with a guest colony mirrored saves, goes to the galaxy menu and loads while connected; then `coop.space allies planet <id>` and land there: FAIL = crash or stale cities in the host log; PASS = clean rebuild.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened (code audit for the 0.7.13 plan, Tribe / Civ / Space area).
