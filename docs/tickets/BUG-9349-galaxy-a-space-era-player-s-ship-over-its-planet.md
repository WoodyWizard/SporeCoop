---
id: BUG-9349
title: Galaxy: a Space era player's ship over its planet is shown as a UFO in the planet's other eras
status: fixed
kind: feature
stage: space
reported: 2026-11-23
source: 
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 9188054a
tests: tools/qa_win (manual, 2026-11-23): TITAN, QA 9 in Abber (Space) at the planet, QA 8 entering Abber (Civ); coop.fix galaxy.space_ships off/on
signature:
docs: docs/12-server-and-players.md#server-space-ships
related: BUG-9287, BUG-9290, BUG-9346, BUG-0558
---

## Report
Roadmap (alpha-now): "Space ships over planets" (docs/12-server-and-players.md: Space ships over any planet as UFOs;
Spore's Creature, Tribe and Civ stages fly UFOs of their own). With BUG-9346 a planet can have a Space era.

## Root cause
Not built yet (a feature).

## Fix
spacecoop.cpp ship_on_planet(): the player's ship while the game is at a planet (context 0): its position (the
planet's centre at the origin, as the other eras) and its model key (cGameDataUFO +0x780..+0x788, the key SpawnUFO
stores). era_visitors.cpp: a Space era game sends that as its VISIT (era Space, the ship's key) and puts the ship's
design in the creation store; a Creature, Tribe or Civ game shows a Space visitor as a UFO of ours: SpawnUFO(7, no
empire, the model installed from the store; the default UFO after 15 s without it), placed where the ship is every
frame (the spike's place(): position, the ship's own destination and zoom altitude). Removed with its visitor (6 s
without a VISIT, the ship left the planet) and kept out of saves (savestrip filter on the UFO noun). `coop.visit
status` has a "space ships" line. Switch `coop.fix galaxy.space_ships off`: none sent or shown.

## Verification
TITAN, a fresh copy of the test galaxy; QA 9 started Space on Abber (BUG-9346) and stayed with its ship at the
planet (sent 256 VISITs); QA 8 entered Abber (Civ) from the galaxy:
- PASS (on): QA 8 "space ships: shown 2, live 1"; zoomed out, QA 9's own ship design (its white hull) over Abber's
  horizon beyond the city (images/bug9349_ship_in_civ.png).
- FAIL (`coop.fix galaxy.space_ships off` on QA 8): live 0, no ship; on again: shown 3, live 1, the ship back at the
  same place (images/bug9349_off_on.png, left off, right on).
- QA 9 took off to the solar system: QA 8's ship went (live 0).
- Not run: the ship moving (the first Space tutorial step kept QA 9's ship in place), a Creature or Tribe game seeing
  it, a name tag over the ship, the MSI.
no-fail-run: a feature, Windows QA by hand; the runs above are the switch off and on.

## History
- 2026-11-23: opened.
- 2026-11-23: built on feat/lineages-alpha and seen in game (a Space ship over Abber in its Civ era).
