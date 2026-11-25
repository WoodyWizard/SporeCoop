---
id: BUG-9269
title: Galaxy planet: an NPC treats a partner of another party by that party's relations (attack, flee, ally)
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner decision 6W (continue: A, B, C)
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: c76950ca
tests: manual on TITAN QA 8 + 9 (coop.social pevent <npc> 7 <player>, coop.social goto, the host log's hits)
signature: "party relations: ... AI ticks N" in coop.social species (host)
docs: docs/12-server-and-players.md#server-party-ai
related: BUG-0016, BUG-0168, BUG-9267
---

## Report
Decision 6W (the owner 2026-11-19: "Just continue as far as you can"), B: the 6K plan's step 3 (NPCs by relation).
With relations per party (BUG-9267) an NPC near a partner still behaved by the host's party's relations: a species a
stranger's party made aggressive (an eaten egg) left that stranger alone, one its party allied could still attack it.

## Root cause
The host's game runs every NPC's AI. An NPC nearer a partner ticks with the partner's puppet as the player (BUG-0016,
game.cpp h_NPCTickAI); its behaviour tree reads its species' relation from the game's one table (GetRelationship,
docs/re/creature.md#creature-npc-aggression) and the herd's ally flag (+0x191).

## Fix
game::set_relation_context: around a swapped AI tick social.cpp puts that partner's party's record (and the herd's
ally flag) in place and takes it back after (what the tick changed goes to that party's record; an unwind past the
leave is undone on the next social tick). Switch `coop.fix social.party_ai` (galaxy planets, with
social.party_relations).

Found on the way: relation 5 was taken for "extinct" (docs/re/creature.md 8.1's inferred list); it is "aggressive"
(BUG-0168's static analysis: an eaten egg, a killed young). BUG-9267 copied a party's 5 to every party: one party
eating an egg made the species hostile to everyone. Removed (the game's extinction acts on the shared herds anyway).

## Verification
no-fail-run: no lab test (galaxy planets have none yet). Manual, TITAN QA 8 hosting Samuth, QA 9 joined (not in a
party), NPC 61 of species 06539202 next to the guest (`coop.social goto 61` every 3 s), hits on the guest counted in
the host's log ("damage: 1 hit by npc"):
* control (no egg eaten), 24 s: 0 hits.
* `coop.social pevent 61 7 1` (the guest's party eats an egg): the host's record relation 0, the guest's party's 5.
* fix off (FAIL), 30 s: 0 hits (the NPC used the host's record).
* fix on (PASS), 30 s: 19 hits, the guest's health 100.8 -> 1.0.
* An earlier run (species 06806931, fix on) knocked the guest out the same way; four other species never attacked
  (their trees have no attack on relation 5, as BUG-0168 found for some).

## History
- 2026-11-19: opened and fixed.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
