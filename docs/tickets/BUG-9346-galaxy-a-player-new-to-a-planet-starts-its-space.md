---
id: BUG-9346
title: Galaxy: a player new to a planet starts its Space stage when the planet's latest era is Civ
status: fixed
kind: feature
stage: civ
reported: 2026-11-23
source: 
role: 
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 9780bc18
tests: tools/qa_win (manual, 2026-11-23): TITAN, QA 9 (every stage reached, new to Abber) on a copy of the test galaxy; coop.fix galaxy.space_start off/on
signature:
docs: docs/12-server-and-players.md#server-space-start
related: BUG-9336, BUG-9343, BUG-9348, BUG-0311
---

## Report
Roadmap (alpha-now), after BUG-9343: "Civ -> Space" was the next new stage a player new to a planet could start
(decision 5R A: a later stage can be started when the player reached it in their own Spore).

## Root cause
Not built yet (a feature). Civ has no end request a mod can ask: the game checks the end only when a city
changes owner (docs/re/civ.md#civ-stage-end): the player civ's level reaches 4 when it owns every city the planet
will have (cCivExpansion +0xAC).

## Fix
galaxy_menu.cpp new_stage(): Civ -> Space (switch `galaxy.space_start`), in Spore's Choose A Stage screen, the box and
the planet details panel. In the start crossing (coop.cpp start_stage) the game asks civ::stage_end_request(): one
step a second outside the frame, first the expansion's cities still to come (SpawnNextCity, instant, while sites are
left), then every city of another nation captured by the player civ on the instant path with the arguments of a
nation's surrender (Capture(pol, the player city's specialty, 1, 0, 1)). The last capture makes the game's own
check end the stage. From there the player plays Spore's own sequence. `coop.civ stage_end` shows the counters,
`coop.civ stage_end now` asks it (tests).

## Verification
TITAN, a copy of the rebuilt test galaxy (Abber Civ), QA 9 on a copy of a test profile with every stage reached and
a new galaxy identity, Abber forgotten:
- PASS (on): Spore's Choose A Stage screen with the Space globe lit, the panel "Space: New here: start it"
  (images/bug9346_space_new.png). A real click on the Space globe: the server "starts its Space stage: crossing
  'Abber~2|Civ' from the stored world"; the game founded 6 cities and captured 9 (`coop.civ stage_end`), then
  "Congratulations! Global domination is yours!" (images/bug9346_domination.png), the Advance to Space button, History
  (images/bug9346_history.png), the spacecraft editor (images/bug9346_ship_editor.png), the Space stage load, the lift
  off (images/bug9346_space.png), the captain's name, the ship over Abber (images/bug9346_space_ship.png,
  images/bug9346_space_orbit.png). Server: "planet 'Abber|Space': the party of 2 arrived from its crossing and hosts it".
- FAIL (`coop.fix galaxy.space_start off`, Abber forgotten): Play on Abber opened no choice and entered Abber (Civ)
  (images/bug9346_off.png).
- Not stored: the Space era was not saved to the server (BUG-9348). Not run: a party, the MSI.
no-fail-run: a feature, Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-23: opened.
- 2026-11-23: built on feat/lineages-alpha and seen in game through to Space; switch off: no Space offer.
