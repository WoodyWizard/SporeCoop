---
id: BUG-9266
title: Galaxy planet: difficulty scales with each party's own size, not the planet's seats (every stage)
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner decision 6K A / 6R A
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: ac304d73, a1d5059e
tests: tests/party_tests.cpp (test_party_count); manual on TITAN QA 8 + 9 (coop.npc hit / hosthit, coop.party status)
signature: "npc: guest N hit npc M: reported 1.00 ... applied 0.87" in a party of 2
docs: docs/12-server-and-players.md#server-party-difficulty
related: BUG-9199, BUG-9254
---

## Report
Owner 2026-11-18 (decisions 6K A, 6R A): "Difficulty scales on the individual party." On a galaxy planet every player
fought at the planet's seats (16, counted as 4 since 6E B): a player alone met a 4-seat party's animals (health x1.45 at
100% difficulty, their bites x1.15, hunger and the DNA pace of four).

## Root cause
The Creature extras read world::rules_for_current(): one set of rules per world (the campaign's seats). The host did not
know the guests' galaxy parties (the galaxy server sends each game its own party only).

## Fix
Step 1 of the 6K plan (docs/12-server-and-players.md#server-party-progression), Creature, galaxy planets only:
* A guest tells the host its galaxy party id (MSG_PARTY kGxParty, at the start and on every change);
  parties::party_size(p) counts the players of p's party on the planet (the host's own party from the server's view;
  a guest naming the host's party without being in it counts alone).
* world::rules_for_player(id) / rules_for_me(): the campaign rules with that party's size as the seats.
* An NPC keeps the game's own health (several parties fight it); a player's hit (the host's, its pack's, a guest's
  MSG_DAMAGE_REQ) is divided by its party's health factor instead (the same number of hits). An NPC's hit on a player
  is scaled by the hit player's party. Hunger and the Creature award scale by the own party; the upstream 1/N DNA pace
  is off on a galaxy planet (the party's size already scales the gains).
* `coop.fix creature.party_difficulty off`: the old rules (the planet's seats for everybody). Direct co-op sessions
  are unchanged.
* 2026-11-19, the other stages (owner: "Cell, Tribe, and Civ being planet wide difficulty makes no sense for a party
  based system"):
  * Cell: a wild cell keeps the game's own health; this game's own bite on it is divided by its party's health factor
    (fractions carried); a wild cell's bite on a player gets its party's bonus (the host asks per guest:
    MSG_CELL_COMBAT); a meal is paid by the eater's party (the host awards every meal on a galaxy planet). The
    predator groups' extra member and the plant bonus follow the largest party (one area for everybody).
  * Tribe: a fight between a founder village and an enemy village uses the party of that village's player (the enemy
    keeps the game's own health, the founder side's hits are divided, the enemy's hits on it multiplied). The number
    of rival tribes follows the largest party.
  * Civ: a rival's vehicle, turret or building scales by the party of the founder nation it fights; takeover
    resistance by the attacking founder's party; the NPC nations' army size and pace by the largest party.
  * `world::rules_for_largest_party()` for what no party owns. The progress debt and the shared DNA and food totals
    stay one per world (stage progress per party is step 4, decision 6N A).

## Verification
no-fail-run: no lab test (galaxy planets have none yet). Manual, TITAN QA 8 hosting Samuth from the test galaxy, QA 9
joined, NPC 3 (12 HP), `coop.npc hit 3 1` on the guest, the host's log "applied":
* fix on, alone: applied 1.00 (12.00 -> 11.00). A party of 2 (`coop.party status`: "0=2 1=2"): applied 0.87. The
  host's own hit (`coop.npc hosthit`): 10.13 -> 9.04 (1.25 teamplay bonus / 1.15).
* fix off (FAIL): the NPC's health went to the planet's 4-seat level at once (9.04 -> 13.11, x1.45), the hit applied
  1.00: a party of 2 fought a 4-seat party's animal.
* fix on again: the health back to the game's own (12.11 -> 8.35), the hit 0.87; the guest left the party: 1.00.
* Not seen at runtime: an NPC's bite on a player scaled by its party, hunger and the award (code reading; they use the
  same party size).
* tests/party_tests.cpp test_party_count: the count and the rules it gives (alone 100%, a party of 6 counted as 4).
* Cell (2026-11-19, a Cell planet added to the test galaxy: Cellfour from the DuoQACellPartsHost save, QA 8 host,
  QA 9 guest; `coop.pool goto 100 30` and `coop.pool realbite 80` on the host, `coop.pool hp 100` + `enemyhit` on the
  guest): 55 predators spawned, none with added health (`assigned=0`); the host's own bites: alone 0 of 5 scaled
  (`party_bites=0`), in a party of 2 10 of 10 scaled. The guest's enemy hits mostly found no hittable moment (1 of 50
  landed, alone: raw 2, loss 2, no bonus asked): the bonus in a party is code reading.
* A save that is no Lineages campaign (rules {1, 0}: Cellfour) had no extras at all; on a galaxy planet its parties
  play the standard 100% rules now (world.cpp party_base), so a party of 2 meets the same difficulty everywhere.
* Tribe, Civ (2026-11-19): built, not seen at runtime. The test galaxy has no Cell or Civ planet (Clyth and Jiff
  are Tribe), and a Tribe fight between villages could not be started from the console in this pass. Code reading
  only; the Tribe run found BUG-9268 instead.

## History
- 2026-11-19: opened.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
