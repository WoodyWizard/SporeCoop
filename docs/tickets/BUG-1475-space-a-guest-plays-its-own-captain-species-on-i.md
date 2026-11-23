---
id: BUG-1475
title: Space: a guest plays its own captain species on its own game (#217 a by IltonBJSilva)
status: open
kind: feature
stage: Space
reported: 2026-11-23
source: plan, PR #217
role: guest
versions: 0.7.17
fixed_in:
branch: feat/w74-captain
commits:
tests: space_own_captain, space_own_captain_off
signature:
docs: docs/re/space.md#space-own-captain
related: BUG-1111, BUG-1262, BUG-1279, BUG-1237, BUG-1321
switch: space.own_captain
---

## Report
The 0.7.16 decision "captain and crew later" ends here: cEmpire_SetCaptainKey (+0xC4) on the guest's own game, put back for every save write (the species-line rule), through BUG-1111's guard path; creations 1.2 MSG_CREATION kind 11 (reserved in WP-0). docs/plan-0.7.17.md item 7. Credit IltonBJSilva (PR #217).

## Root cause
Feature. A guest in the host's Space world plays the host's empire; its captain (cEmpire +0xC4 mCaptainKey, a key attribute of
cEmpire's serializer table 0x1572B60) is the host's from the save. Static RE: cEmpire_SetCaptainKey's 0xC75B40 writes
the player's cards into the captain creation's Sporepedia metadata in its own database (0x555F70): with the game's
setter a guest's own My Creations creature would be retagged for good.

## Fix
own_captain.cpp (`coop.fix space.own_captain`, off = 0.7.16.1; rules captain::own_step): a guest of creature.mode own that plays
an own creation (not one under a key of the host's world, BUG-0278) gets it as its empire's captain 5 s after the Space
world came, when the key is known here and has a species profile (BUG-1111's portrait and BUG-1278's stats read both,
their guards stay); +0xC4 is written as the setter writes it, without 0xC75B40. A world without a captain is left to "Name
Your Captain"; a captain the game sets later (named, outfitted) is the player's own: kept and noted per world
(worlds.ini [captain]). savestrip's save hooks write the world's captain into +0xC4 for every save and ours after it; the
session's end puts the world's back. No wire (each game sets its own). Compatible with feat/w70-history: it never calls
SpaceGame_SetArchetype; history's card writes in Space (0xC75B40) tag the current captain's metadata as the game does
for any captain. Idea of PR #217 by IltonBJSilva.

## Verification
open, waiting for the lab run (no game run on this branch by day). Built, `make test` passes (tests/core_tests.cpp captain_rules). pending night run: space_own_captain (PASS: the guest's
cEmpire +0xC4 is its own creature, the host's keeps its captain, the captain screen opens without a fault with a key
frame of the portrait, the guest's save names the host's captain only) and space_own_captain_off
(SPORE_FIX_OFF=space.own_captain, must FAIL). Regressions near it: space_captain, space_captain_none, own_cards_space,
space_coop. ~ Not known: what the captain outfitter (0xFE7A10) does with a creation that never was a captain (a creature
without a captain property list).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 7 (WP-K1), branch feat/w74-captain.
- 2026-11-23: feat/w74-captain (WP-K1): feature written (own captain, save write back), unit tests, lab test and twin; waits for the night run.
