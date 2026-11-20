---
id: BUG-9262
title: Lineages port, phase 4: every player's own nest of their own species next to the host's
status: open
kind: feature
stage: Creature
reported: 2026-11-20
source: Lineages port (PR #196)
role: 
versions: 
fixed_in:
branch: feat/lineages-nests
commits:
tests: tools/tests/coop_home_nests.py (suite home_nests)
signature:
docs: docs/re/creature.md#creature-own-nests, docs/01-architecture.md#architecture-own-nests
related: BUG-9260, BUG-9261, BUG-9267
---

## Report
Lineages port, phase 4 (PR #196 home_nests.{h,cpp}, nest.cpp MSG_NEST kind 2; there BUG-9135 and its follow-ups
9128, 9151, 9167, 9173, 9193, 9204, 9210, 9215, 9234, 9246). Every player with their own species has their own nest
next to the host's; the partner's game lives, hatches and respawns there; the party's species are best friends.

## Root cause
Not in main. Two bugs of the Lineages code found in the port (fixed here, both seen on lane A):
1. CreateHerd(size 4, createNest) makes the nest but no member, and cHerd::Update never fills an empty herd: the
   Lineages nest stayed empty (0 members after 3 min, mRespawnRate 2000); its world went out after 90 s without a
   family and the guest's join of the nest was skipped ("no members here yet").
2. wsync's nest row (BUG-0640) took a guest living in its own nest for one that missed the host's migration: it set
   the guest's migration property and emptied its nest herd's brain range ("Migrate to a new nest" on the guest).

## Fix
* home_nests.{h,cpp} (host): a nest herd of each seat's own species 45-100 m from the host's home nest, archived
  (BUG-9261), best friends (relation 6 via social::set_party_ally, cHerd_SetBestFriends), discovered on the minimap,
  noted in worlds.ini, follows an evolution of the seat's creature; the share hold (world::set_share_hold) and the
  world resend once the nest has members. Bug 1: `fill_nest` makes the members with cCreatureAnimal::Create (4 into a
  new nest, 1 when its family is gone while its player is online). game::create_nest_herd.
* nest.cpp: MSG_NEST kind 2 (host -> guest, its home nest 1 Hz; older guests skip it), the guest joins that nest's herd
  (CreatureGame_JoinNestHerd) and is put next to it once per world; `coop.nest home`.
* Bug 2: `wsync.own_nest_stays`.
* npc.cpp `creature.party_species_peace`: a player's hit on a party species is dropped.
* Left out of PR #196: the ring cleanup (BUG-9182 there), the guest's own-family tick (BUG-9175 there), the game's
  player species following the own creature (BUG-9177 there); the site switches of 9167/9173/9246 are always on.
* Switches: creature.own_nests (all of it), creature.nest_before_world, creature.nest_share_member,
  creature.nest_family_back, creature.nest_follows_evolution, creature.join_after_respawn,
  creature.nest_at_migration_target, creature.nest_note, creature.party_nests_known, nest.follow_home,
  creature.place_at_own_nest, wsync.own_nest_stays, creature.party_species_peace.

## Verification
Windows, lane A, Creature game Boheus (made for this: a new game, Maxis Burpee), guest with its own creature
(Guleskate 7CA81684), the same steps as coop_home_nests.py driven by hand:
* host: seat 1's nest of 7CA81684 at 60 m from the host's, members 4 living 4, relation 6, best_friends 1; the world
  went out again once the nest had members (one resend).
* guest: `coop.nest home`: its home nest at the seat nest (0.0 m), herd species 7CA81684, avatar 7CA81684; the guest
  sees its own family at its own nest, the host sees the same nest with the guest's species (screenshots).
* guest put at the host's nest (60 m away), `coop.team die`, OK on the caption: respawned 1 m from its own nest.
* host `coop.nest kill <seat herd> 1`: dropped (`npc: player 0's hit on a party species 7CA81684 dropped`), living 4;
  with `coop.fix creature.party_species_peace off`: living 3.
* guest left, host to the menu, host game killed and started cold, Boheus loaded with the guest offline: the archive
  registered first, seat 1's nest loaded with 4 living members of 7CA81684 drawn with the guest's model; guest
  rejoined: made=0 (no second nest), its home its own nest again (joins=2). No fault, no crash report in either game.
* Before the bug 2 fix the guest got "Migrate to a new nest" and its nest herd's brain range was emptied (and that
  run's guest died 7 s after joining its nest, during the game's cinematic: not seen again after the fix); after it, no
  migration line in the guest's log.
* Lab: coop_home_nests.py not run on the Linux lab yet.
  no-fail-run: feature port, verified by hand on two instances (a new feature: the test fails on main, unknown commands).

## History
- 2026-11-20: opened, ported, two Lineages bugs fixed, verified on lane A (feat/lineages-nests).
