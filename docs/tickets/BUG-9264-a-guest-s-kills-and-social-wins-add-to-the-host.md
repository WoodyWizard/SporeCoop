---
id: BUG-9264
title: A guest's kills and social wins add to the host's trait progress (cPlayer +0x1278/+0x127C): the guest's own counts nothing
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: agent
role: 
versions: 
fixed_in:
branch: feat/own-cards
commits:
tests: tools/tests/coop_own_cards.py
signature: cPlayer_AddTraitProgress 0xC75840 called on the host for a guest kill
docs: docs/re/creations.md#creations-own-cards
related: BUG-9263
---

## Report
Found while porting the own stage history (BUG-9263): a guest's kills raised the host's combat progress (the host's
Creature card), the guest's game counted nothing; a guest's social win counted for nobody.
Reproduce: two games in Creature, `coop.wstate` on both ("social= combat="), the guest's `coop.npc hit <id> 1000` on
an animal of another species: the host's combat grows by the kill's worth, the guest's stays.

## Root cause
✔ (runtime probe on both games) npc.cpp lands a guest's lethal hit as the host avatar's (the DNA credit), so the
game's kill path in TakeDamage calls cPlayer::AddTraitProgress(amount, combat) on the host's cPlayer; the guest's game
never runs it (its animals are proxies). social.cpp's replica of the social win (apply_success) does not call it.

## Fix
cards.cpp hooks cPlayer::AddTraitProgress: inside a `cards::Actor` scope (npc.cpp around a guest's hit, social.cpp
around a guest's social win's relationship event) the amount goes to the guest's party seat; the social win also
credits the creature's DNA worth as the game's does. The guest's game gets its seat's progress (BUG-9263's record).
Switch `coop.fix party.trait_credit off`: as 0.7.15.

## Verification
By hand on lane C (this PC), tools/tests/coop_own_cards.py's step 2: with the fix the guest's game's combat 0 -> 7,
the host's unchanged (PASS); with `coop.fix party.trait_credit off` the host's 0 -> 8.75, the guest's 0 (FAIL).
Not yet run in the Linux lab.

## History
- 2026-11-20: opened.
