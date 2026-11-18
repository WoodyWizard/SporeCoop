---
id: BUG-9177
title: Creature: the mate's card shows the friendly face, not the pink mate face (the game's player species is not the creature played)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win/hover.ps1 (manual, the real mouse on TITAN 2026-11-13: the host with the mod, without it, with the key written by hand)
signature:
docs: docs/re/creature.md#creature-mate-keeper
related: BUG-9174 BUG-9169 BUG-9161 BUG-0462 BUG-0517 BUG-0508
switch: creature.own_player_species
---

## Report
Owner (2026-11-13): "It's always been able to show the heart cursor, it's more of the issue with the friendly icon. They should show the pink mate icon." Hovering the mate showed the card with the green friendly face (the sun), on the host and on guests.

## Root cause
Observed with the real mouse on TITAN (hover.ps1): the host's own mate (class 9, hearts): the cursor with hearts, the card the green face; the same with the co-op mod unloaded, so not our UI. The game's player species (cSpeciesManager +0xA4, read by PlayerSpecies 0x4DF490) was 2919A821, the world's; the avatar and its family wore 2920607A, the player's own creation (apply_own_avatar, BUG-0517). With the key written by hand to 2920607A (mod unloaded) the mate's card showed the pink mate face at once. The card reads the creature's vt 0xD0 (IsPlayerSpecies: +0xB20 == PlayerSpecies) at 0xB6C9D1. The game's invariant "the creature played is the player species" was broken by putting a creation on the avatar only; BUG-9161 (the family's species), BUG-9169 (the copies' species) and BUG-0462 (the relations) each patched one consequence.

## Fix
creature.own_player_species (on), creations.cpp own_player_species_tick: in Creature the key at cSpeciesManager +0xA4 follows the species the own avatar wears, once that species is the manager's profile of its key; on every game (roadmap 1b, step 1). Not yet removed: the patches it makes redundant (to check one by one).

## Verification
no-fail-run: manual with the real mouse (no Windows lab lane). FAIL: out/shots/hover-host-native-mate.png and native-both.png (the green face, with and without the mod). PASS: the host with the mod and the fix: "world player species 2920607A", out/shots/host-mate-pink.png: the pink mate face on the mate's card, the heart cursor. Guests: "world player species 291D668C" (TITAN) and 2920408F (the MSI), their own creatures; both joined their own nests and were offered mates (class 9); the MSI loaded the host's world saved with the new key. Not seen: a guest's card in a picture (the TITAN guest and its family were killed by wildlife at its nest three times during the tries; the MSI has no real-mouse tool); the host loading its own save made with the new key; Creature -> Tribe with it.

## History
- 2026-11-13: opened.
