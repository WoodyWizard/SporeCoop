---
id: BUG-0794
title: Feature idea: per-stage Together/Rivals session state, relation table and faction diagnostics (Rivals foundation)
status: open
kind: feature
stage: All
reported: 2026-11-10
source: fork nvstlex/SporeCoopNVST nvst/rivals-foundation
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: docs/11-rivals.md, docs/plan-0.7.12.md F3
---

## Report
Feature idea, not a bug. The fork nvstlex/SporeCoopNVST (branch nvst/rivals-foundation, 138 commits of 2026-11-03,
+1264 / -43 lines, MIT licence with our copyright line) started a "Rivals foundation". Most of it is a design; the parts
that exist are useful for our docs/11-rivals.md (per-stage Together / Rivals table, 11.2) and for docs/plan-0.7.12.md F3
(session settings).

## Root cause
Not a bug. What exists (not verified in game by us; the fork has no in-game tests):
* `mods/coop/rivals_state.h` (178 lines, works as data, unit test tests/rivals_tests.cpp): a per-stage Together / Rivals
  choice for five stages, a symmetric 4x4 relation table (Neutral / Ally / War), a versioned fixed-size snapshot decoded
  into a temporary and size-checked; it rejects Civilization = Rivals (the fork keeps Civilization co-op only).
  Network message MSG_RIVALS_STATE = 61 (collides with our MSG_EFFECT = 61; 62-65 are taken too).
* coop.cpp (+224) / coop_ui.cpp (+64), partial: console `coop.rivals`, a lobby page "Stage modes" (host edits, guests
  view), the host sends the state on join and leave, guests accept it only from player 0; Space switching reuses our
  space_rivalry with a 5-line `set_enabled(bool)`, only while in Space.
* Tribe (tribe.cpp +88), partial: attack orders (CitizenAction 7) on another player's citizen are allowed only at War,
  for guest commands and the host's own selection, with a notice when blocked. Their own doc says real PvP damage will not
  happen this way because all player citizens belong to one native cTribe (mpOwnerTribe at citizen +0x1014).
* shared.cpp (+49), partial: in Tribe Rivals food is per player; on return to Together each guest's balance is merged
  once, guarded against a race with the host's total.
* Read-only diagnostics (work, cheap): `coop.tribe factions` (every cTribe: pointer, player mark, position, members, hut,
  food, archetype, food container) and `coop.civ factions` (every civ: political id, player flag +0x89, money, city and
  vehicle counts, culture set +0x6C).
* Design: docs/11-rivals-research.md (a real rival needs its own cTribe / cCivilization and a separate local-player
  pointer per client; prototype order for Tribe: SpawnNpcTribe an empty tribe, move one citizen with AddMember /
  RemoveMember vt 0x98 / 0x9C, then find how to set the player-tribe pointer per client, or stop) and
  docs/12-four-player-coop.md (four-player layouts per stage). Their direction differs from ours: it calls our PvP design
  "legacy" and aims at four-player co-op with separate native factions.
* Do NOT copy: their civ.cpp `host_assign_cities` change (city i -> players[i % n]) drops the shared capital (0xFD) and
  gives a guest no city with one city and two players.

## Fix idea
From the fork (nvstlex/SporeCoopNVST nvst/rivals-foundation); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
Cheap to adopt: (1) the stage-mode / relation table as the data model of docs/11-rivals.md 11.2, but stored in our F3
session settings (saved with the world, which theirs is not), with a new message number; (2) the two faction diagnostics
as debug commands; (3) the research note's blocker list into docs/11-rivals.md for the Tribe and Civilization sections
(a separate native faction per player is required, an owner filter on orders is not enough). Everything else waits for
the Rivals 0.8 architecture.

## How to test
Unit: snapshot round trip, size and version rejection, symmetric relations (as their rivals_tests). In game: four players,
host changes a stage's mode in the lobby; every guest's lobby page shows the same table (read from the UI caption), a
rejoining guest gets it on join.

## Fix
## Verification

## History
- 2026-11-10: opened.
