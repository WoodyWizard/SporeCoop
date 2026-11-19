---
id: BUG-9267
title: Galaxy planet: each party keeps its own species relations (impress, attack, kill, alliances)
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner decision 6V A
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: unreleased
branch: feat/lineages-alpha
commits: a1d5059e
tests: manual on TITAN QA 8 + 9 (coop.social pevent, coop.social species)
signature: "social: relationship event N species X by player P: party K's record"
docs: docs/12-server-and-players.md#server-party-relations
related: BUG-0462, BUG-9266
---

## Report
Owner 2026-11-19, decision 6V A: each party its own creature relations (the 6K plan's step 2). Before, one game's table
held the planet's relations and every guest got it (BUG-0462): one stranger who hunted a species made it hostile, or
broke an alliance, for every player on the planet.

## Root cause
cSpeciesRelationshipManager keeps one table per game; ApplyEvent names the victim creature, not the actor.

## Fix
* The host keeps another party's records itself (social.cpp g_party_rel, keyed by parties::table_key: the party's
  galaxy id, 0x10000 | player for a player in no party; 0 is the host's own party, the game's table).
* The actor is known where the mod applies it: a guest's hit (npc.cpp MSG_DAMAGE_REQ, the attack and the kill) and a
  social win (apply_success). social::set_event_actor marks it; h_RelEvent swaps that party's record into the game's
  table, runs the game's own ApplyEvent (its rules: impressed to ally, a death breaks the alliance, kills to
  extinction) and swaps the host's back; the herds' ally flag is set back to the host's.
* Extinction stays one per planet (6M B): the game's own extinction stops the shared herds. (CORRECTION
  2026-11-19, BUG-9269: relation 5 was copied to every party as "extinct"; 5 is "aggressive" (an eaten egg): one
  party's hostility reached everyone. Removed.)
* Each guest gets its party's table (MSG_SPECIES per party, on change and every 10 s); a goal card of another party
  pays by its table, once per party.
* Switch `coop.fix social.party_relations` (needs creature.party_difficulty: galaxy planets only). In memory on the
  host: a hand-over or a restart keeps only the new host's own party's table (its game's).

## Verification
no-fail-run: no lab test (galaxy planets have none yet). Manual, TITAN QA 8 hosting Samuth, QA 9 joined, NPC 33
(species 065CBF32), `coop.social pevent 33 0 <player>` (the game's "killed" event as that player's), then
`coop.social species` on both:
* the guest alone: the host's record killed 0, the guest's party's record 1, the guest's game shows 1.
* the host: the host's record 1, the guest's game still its party's 1.
* fix off (FAIL): the guest's kill went into the host's table (2) and the guest's game showed the host's 2.
* a party of both: the guest's kill went into the host's table (3, relation 1), the guest's game showed it.
* Not seen at runtime: the "impressed" event (3) changed no record in this run, also the host's own and with the fix
  off (the game ignored it for that NPC), so a social win's alliance per party is code reading; extinction from
  another party's kills (this species did not go extinct at 3 kills).
* Found on the way: `coop.social event <crt> 3` (BUG-0168's test command) crashes the game in ApplyEvent+0x12B: events
  0 to 5 read the creature's tuning and the command passes none. It refuses 0 to 5 now; the new `pevent` uses a living NPC.

## History
- 2026-11-19: opened and fixed.
