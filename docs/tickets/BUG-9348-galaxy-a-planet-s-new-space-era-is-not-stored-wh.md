---
id: BUG-9348
title: Galaxy: a planet's new Space era is not stored while the ship stays at its planet (Spore saves Space only in a solar system)
status: fixed
kind: bug
stage: space
reported: 2026-11-23
source: 
role: 
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 4442e63b
tests: tools/qa_win (manual, 2026-11-23): TITAN, BUG-9346's start run again on a fresh copy of the test galaxy, then `coop.space takeoff` and `coop.galaxy save`
signature:
docs: docs/12-server-and-players.md#server-space-start
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
No change to the upload: it already retries every 15 s and stores once Spore can save. galaxy_client.cpp
start_upload: a Space era host whose save is refused tells the player once per planet "<planet> (Space) is saved to the
galaxy once your ship is out in the solar system." Switch `coop.fix galaxy.space_save_hint off`: no word. A player who
leaves with the ship still at the planet is held by the leave's own wait ("Waiting to save ...", as before).

## Verification
TITAN, BUG-9346's flow on a fresh copy of the test galaxy (QA 9 started Space on Abber):
- At the planet: `coop.galaxy save` stored nothing (as before).
- `coop.space takeoff` (the zoom out's own step: the solar system, images/bug9348_solar_system.png), then
  `coop.galaxy save`: the server "planet 'Abber|Space': stored its world from 2 (3282 KB)"; the worlds folder holds
  Abber_Space-6A4BB1E8.world beside Abber's Civ world.
- The hint line itself was not seen in a picture (a notice lives 6 s; said once per planet).
no-fail-run: the storing needs no fix (Spore's own rule); the hint is a message.

## History
- 2026-11-23: opened.
- 2026-11-23: the Space era is stored once the ship is in the solar system (seen); a hint for the player added.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
