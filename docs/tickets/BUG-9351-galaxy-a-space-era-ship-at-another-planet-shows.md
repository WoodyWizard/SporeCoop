---
id: BUG-9351
title: Galaxy: a Space era ship at another planet shows over its home planet's other eras
status: fixed
kind: bug
stage: space
reported: 2026-11-23
source: QA (MSI)
role: host
versions: 0.7.14.1-lineages69-dev
fixed_in: unreleased
branch: feat/lineages-alpha
commits: efc895d5
tests: tools/qa_win (manual, 2026-11-23): two MSI games on TITAN's galaxy server; msi2 Abber (Space), msi Abber (Civ); coop.fix galaxy.space_ships_home off/on
signature:
docs: docs/12-server-and-players.md#server-space-ships
related: BUG-9349
---

## Report
Found reading era_visitors.cpp while planning the Creature / Tribe viewer test (2026-11-23), then seen: a Space era
game sends its ship's VISIT whenever the ship is at a planet (context 0), any planet, while its galaxy planet stays
its home planet's Space era. The home planet's other eras then showed the ship over their planet while it was over
another planet of the system (or of another star).

## Root cause
era_visitors.cpp stream_tick: the ship was sent on spacecoop::ship_on_planet() alone (context 0, not in a
cinematic); nothing compared the planet the ship is at with the era's planet.

## Fix
spacecoop.cpp ship_at_home(): the active planet's record (planet +0x13C) is the one GetPlayerHomePlanetRecord
returns. era_visitors.cpp sends the ship only then; elsewhere it counts "away from its planet: N not sent" (`coop.visit
status`). With no VISIT the other eras drop the ship after 6 s, as when it leaves the planet. Switch `coop.fix
galaxy.space_ships_home off`: sent at any planet (the old behaviour).

## Verification
The MSI (two games, profiles MSIGuest and MSISpaceStart), TITAN's galaxy server, a fresh copy of the test galaxy:
msi2 in Abber (Space) (its stored world after Spore's own tutorial was skipped in the comm screen), msi in Abber (Civ).
- FAIL (`coop.fix galaxy.space_ships_home off` on msi2): the ship flew to another planet of the system (Lacionator,
  PlanetID 02B66023; Abber is 00B66023) and landed by the wheel; Abber's Civ game: "space ships: shown 1, live 1", the
  ship drawn over Abber at the position it had over Lacionator.
- PASS (on): msi2 "away from its planet (BUG-9351, on): 31 not sent", Abber's Civ game live 0 within 15 s.
- Back home (on): the ship flew back to Abber and landed: sent again, Abber's Civ game live 1.
no-fail-run: Windows QA by hand; the runs above are the switch off and on.

## History
- 2026-11-23: opened.
- 2026-11-23: fixed on feat/lineages-alpha (efc895d5), seen off and on on the MSI.
