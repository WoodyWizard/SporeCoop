---
id: BUG-9196
title: Sporepedia: the game crashed in the card view (cSPAssetView) browsing My Creations after a co-op session (read of 0x10FC)
status: fixed
kind: crash
stage: menu
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 64c3afed
tests: tools/qa_win (to run: the Sporepedia from a game, then My Creations in the galaxy menu)
signature: cPlayer::GetConsequenceTrait 0xC75880 read 0+0x10FC from the Sporepedia card (0xDD1000) in the galaxy menu
docs:
related: BUG-9191
switch: gamefixes.trait_without_player
---

## Report
Owner live test 2026-11-13 20:31-20:55 (their own PC hosting Vandh.spo in Creature; two test guests from the MSI joined late: MSIGuest with a creature, MSIJoiner without one, editor first). "Game crashed when leaving and viewing My Creations in Sporepedia." crash-20261113-205550 (galaxy menu): 0xC0000005 at 0x011958A4 (pref. 0xC75880), read of 0x10FC, ecx=0, from UI::cSPAssetView.

## Root cause
Observed (the dump's code bytes, the exe's .rdata, spore-decomp; a subagent): the function is cPlayer::GetConsequenceTrait (thiscall(stage id) ret 4, reads cPlayer +0x10FC..+0x110C). Its caller 0xDD1000 (vtable 0x147CA48 slot 0x8C, a Simulator "discovery" card data class) asks cGameNounManager (+0x74) for the player without a null check; the object was the owner's own species 291EE5B5 'Clop' with in-game data, still drawn in the galaxy menu where there is no player. Inferred: the card data was made in the game (the portrait clicks opened the global Sporepedia at 20:43).

## Fix
gamefixes.trait_without_player (on): a hook on cPlayer::GetConsequenceTrait answers 0 ("no trait") when there is no player.

## Verification
no-fail-run: not reproduced yet (it needs a hover in the galaxy menu's Sporepedia after one opened in a game).

## History
- 2026-11-13: opened.
