---
id: BUG-1471
title: Creature: a guest's kills and social wins that land on the host's game raise the host's trait progress (cPlayer::AddTraitProgress), not the guest's
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: plan, PR #207 review
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w70-history
commits: aecef640
tests: own_cards_kills, own_cards_social (tools/tests/coop_own_cards.py); validate_tests test_actor_scope, test_history
signature: cPlayer_AddTraitProgress 0xC75840 called on the host for a guest kill
docs: docs/re/creations.md#creations-own-history
related: BUG-1470, BUG-1469
switch: core.trait_credit
---

## Report
Found in the review of PR #207 (its RE note on the trait progress): a guest's hit on a host NPC lands as the host's hit (npc.cpp) and a guest's social win is played on the host's game (social.cpp); the game then credits the host's cPlayer (+0x1278 / +0x127C) through AddTraitProgress 0xC75840 (ret 8). The guest's combat / social progress does not move, the host's does: the host's Creature card follows the guest's play. Fix with the Actor scope (docs/plan-0.7.17.md item 3).

## Root cause
Observed in PR #207's runtime probe (BUG-9264 of the Lineages fork), confirmed by reading our code and the GOG 2024
disassembly: npc.cpp lands a guest's lethal hit as the host avatar's (MSG_DAMAGE_REQ, for the DNA credit), so the kill
in TakeDamage (0xC07C0C) calls cPlayer::AddTraitProgress(amount, combat) on the host's cPlayer (taken from the noun
manager), and the species goals (0xC2EFE9 / 0xC2F0F1) do the same; the guest's game never runs it (its animals are
proxies). social.cpp's replica of the social win (apply_success) never calls it at all (the game's social win does at
0xD9BD36 with the creature's DNA worth): a guest's social win counted for nobody.

## Fix
The Actor scope of WP-0 (actor_scope.h): npc.cpp opens actor::Scope(guest) around the game call that lands a guest's
hit, social.cpp around the impress event of a guest's social win; history.cpp hooks cPlayer::AddTraitProgress: inside
a scope naming a guest whose own history is in play (creature.mode own, session 1.3) the amount goes to that guest's
record (BUG-1470), not to the host's cPlayer. The replica of the social win now credits the creature's DNA worth as
social progress through the game's AddTraitProgress inside the guest's scope (own history: the guest's; shared
history: the party's). game.cpp reset_call_guards ends a scope a caught fault left open, once a frame (with
core.call_guard_reset). WP-P1 / P2 reads actor::current() for its own-progress credit. Switch `coop.fix
core.trait_credit off`: no redirect, no social credit (0.7.16.1). Credit: IltonBJSilva (PR #207).

## Verification
Unit: validate_tests test_actor_scope, test_history (credit rules); `make test` passes. Lab: pending night run:
own_cards_kills, own_cards_social (PASS wanted) and the twins own_cards_kills_off, own_cards_social_off (FAIL wanted:
the host's combat rises / the guest's social does not).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 3 (WP-H1), branch feat/w70-history.
- 2026-11-23: fixed on feat/w70-history (aecef640): the scopes in npc.cpp / social.cpp, the hook in history.cpp, the frame reset; lab tests written, waiting for the night run.
