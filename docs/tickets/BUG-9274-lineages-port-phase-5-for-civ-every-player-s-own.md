---
id: BUG-9274
title: Lineages port, phase 5 for Civ: every player's own nation (own capital of their own species, cities, vehicles, money), allied, the team wins together
status: open
kind: feature
stage: Civ
reported: 2026-11-20
source: Lineages port (PR #196)
role: 
versions: 0.7.15.5
fixed_in:
branch: feat/lineages-civ (fork IltonBJSilva/SporeCoop)
commits: e80c559c be54fe64 2e429fef
tests: tools/tests/coop_civ_seats.py (suite civ_seats), tests/civ_seats_tests.cpp
signature:
docs: docs/re/civ.md#civ-own-nations docs/01-architecture.md#architecture-own-nations
related: BUG-9259 BUG-9260 BUG-9261 BUG-9268 BUG-9276 BUG-9277
---

## Report
The owner's rule (2026-11-20): in Civilization every player has their OWN nation: own capital founded from their own
tribe (own species), own cities, own vehicles, own money, own colour; each player commands only their own nation; a city
a player conquers becomes that player's; the players' nations are permanently allied; when only the players' nations
are left on the planet they win together and go to Space together (the existing ready vote). 0.7.15.2 has one shared
nation whose cities are dealt between the players (civ_host.cpp city_owner). Lineages (PR #196) has it (BUG-9135,
BUG-9054, BUG-9056 there); phase 5 of the port, for Civ.

## Root cause
Not a bug: a feature to port. Lineages keeps founder nations in a founder-ordered roster with much more around them
(offline control, late capital catch-up, party wipe, takeover and combat scaling, history credit); the port takes the
core on top of the party seats and the Tribe villages (BUG-9268).

## Fix
civ_nations.{h,cpp} (new), civ_seats.h (new, pure), founders::Roster Stage::Civilization by party seat
(`<planet>.spo.duonations`), hooks and calls in civ.cpp, civ_host.cpp, civ_shots.cpp, civ_wallet.cpp, tribe_villages.cpp:
the host founds a partner's capital of the partner's species at the next free site (SpawnNextCity through the co-op
hook, the new city told apart by the cities before, the nation's species key +0x268, the roster, the world shared);
the nation's cities and vehicles are that player's (nobody's while away), its money its own (+0x98: GetMoney /
SpendMoney of a player's nation, host_buy / build / city purchase from it; the guest's HUD through MSG_CIV_WALLET's new
K_NATION), its AI runs the player branch (Civilization_AI with GetPlayerCivilization at its return), the players'
nations are allied (relations raised, no damage, capture or purchase between them); the team's cities raise every
player nation's level and the HUD progress (CivMode_Progress), and with only the players' nations left the host's game
shows the planet won (GameCiv_ShowPlanetWon + PlanetWonPause: the existing Space vote follows). On a guest its own
nation is the player's (City_IsPlayerOwned, Vehicle_IsPlayerOwned, the terrain cursor list). The host playing alone
keeps the nations waiting. Switches `civ.own_nations` (off: the shared nation), `civ.nations_native`,
`civ.team_campaign`; `coop.civ nations [found | ally | relation | citizens | spawn | capture <ci> <seat>]`. Symbols from
Lineages: SporeCoop::City_IsPlayerOwned, Vehicle_IsPlayerOwned, Civilization_AI, Civilization_AI_PlayerCiv_Ret,
CivMode_Progress, CivHUD_Progress_Ret (overrides_dc04aee5.sym).

## Verification
no-fail-run: a new feature (on main `coop.civ nations` is unknown and coop_civ_seats.py fails at check 1; with
SPORE_FIX_OFF=civ.own_nations no nation is founded and it fails there too). Not run in the Linux lab (no lane there for
this batch); run by hand on Windows lane C (two instances, the Steam exe = the GOG 2024 code), 2026-11-20:
- agent A's Boheus Creature fixture with the guest on its own creature: Creature -> Tribe (the guest's own village,
  BUG-9268), saved as a Tribe fixture, then Tribe -> Civ the players' way (`coop.tribe advance`, the History check, the
  vote, the City Hall and vehicle editors): after the Civ caption the host founded the guest's capital 000022A8 of the
  guest's species 7CA81684 at the next free site (419 m from the host's) and shared the world; relation 8.0 both ways;
  the guest's game had it as its own nation; 19 citizens of that capital were of 7CA81684 on both games.
- on the guest, with real clicks: its city's menu and planner (a house built in the host's game, 1600 from its
  nation's money), the land vehicle button (a vehicle of 000022A8 owned by the guest in the host's game, 1000 from the
  nation; the guest's HUD money 4000 -> 2400 -> 1400 = the nation's), a click selected the vehicle and a right-click
  drove it 37 m in the host's game.
- the host's nation could not take the guest's city (`coop.civ capture`: refused); an NPC city the guest's nation took
  was the guest's on both games; both HUD bars counted the team's cities (2/10, 3/10 ... 10/10).
- a save, a restart of both games, the host loading alone: the nation (species kept, money kept) was there, nobody's;
  the guest joined and had it again; a leave and rejoin: the same; none founded twice.
- the same build mid-Civ (the Boheus Civ save without the rosters: NPC nations there, 7 free sites): the guest's capital
  000022A7 founded 859 m away, its purchase, money, move order, the refused capture, the rejoin.
- the team victory: all ten sites founded and taken in turns by both nations (`coop.civ nations spawn` / `capture`):
  "only the players' nations are left: the planet is won", the end window, History, the end cinematic, the vote for the
  spaceship editor, the guest's "I'm ready", the editor, Space for the host and, when its ship left the home planet,
  for the guest.
`make test` passes (with the new tests/civ_seats_tests.cpp).

## History
- 2026-11-20: opened; ported on feat/lineages-civ and run on lane C.
