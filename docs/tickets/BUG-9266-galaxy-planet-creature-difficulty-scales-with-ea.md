---
id: BUG-9266
title: Galaxy planet: Creature difficulty scales with each party's own size, not the planet's seats
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner decision 6K A / 6R A
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: unreleased
branch: feat/lineages-alpha
commits: ac304d73
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
* Not in this step: Cell, Tribe and Civ on galaxy planets still use the planet's rules; the progress debt and goal
  rewards are still one per world (the plan's later steps).

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

## History
- 2026-11-19: opened.
