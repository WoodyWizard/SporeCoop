---
id: BUG-9348
title: Galaxy: a planet's new Space era is not stored while the ship stays at its planet (Spore saves Space only in a solar system)
status: open
kind: bug
stage: space
reported: 2026-11-23
source: 
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9346
---

## Report
BUG-9346's run (TITAN, 2026-11-23): after the start crossing arrived as "Abber|Space" (hosted), `coop.galaxy save`
stored nothing (uploads 0) while the ship stayed at Abber, also after the tutorial boxes were closed and the view
zoomed out to the orbit. Reproduce: start Space from a Civ era (BUG-9346), stay at the planet, `coop.galaxy save`.

## Root cause
Observed: galaxy_client start_upload asks world::silent_save, which refuses in Space on a planet ("leave the planet
to save (space stage saves only in a solar system or the galaxy)", Spore's own rule, world.cpp). The upload retries
every 15 s, so it would store once the ship is in the solar system; not run (the wheel zoom stopped at the orbit and
the first Space tutorial keeps the ship at home). Open: a player who leaves the galaxy while still at the planet loses
the new Space era (the server keeps no world of it).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.

