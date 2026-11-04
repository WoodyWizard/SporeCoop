---
id: BUG-0518
title: Creature (guest): a click on the social ability's button of the ability bar does nothing; only the keys 1-4 play the social game
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: BUG-0179 player reports 2026-11-03 (two groups), w40 lab measure
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-dna-social
commits: 7d2bfa0d 9a5bfb11
tests: creature_social_bar (tools/tests/coop_creature_social_bar.py)
signature:
docs: docs/re/creature.md#creature-guest-social-bar
switch: social.bar_click
related: BUG-0179, BUG-0462, BUG-0021
---

## Report
BUG-0179 was reported again by two player groups (0.7.3.4, 0.7.3.6): the guest cannot befriend creatures
in the Creature stage ("they do not want to be friends"). Measured on the w40 branch (with the relations fix
BUG-0462 merged) to find what is still broken.

## Root cause
Observed (lab, lane 4, creature fixture): with an engaged mirror the mod's social path works for every
socializable species near the nest (065CB8BD, 065CBF12, 065CB8AB: one round each, impressed on the host;
the own species 065CBE70, social mode 0, refused as the game does); the relations of the mirrors on the
guest equal the host's (relation 3, social mode 2). The engagement (a click on the NPC in the social
stance) and the key path pass (creature_social_ally, lab 20261103-180738). But only the keys 1-4 were
polled: a click on the social ability's button of the HUD bar reaches the game's AbilityBar_Use
0xD49560 (ability index; the same function the keys reach through AbilityBar_OnKey 0xD4A450, the
button through AbilityBar_OnButton 0xD497D0, Ghidra GOG 2024) and the game does nothing without its
own NPC session: no animation, nothing sent (lab 20261103-181651: bar_passed 6 for 4 clicks, acts_sent
0, host acts_in 0). A player who plays with the mouse could never befriend anything.
Inferred, not checked with players: this is the main reason of the reports (both groups did not say
whether they used the keys); the remaining simplifications (no meter, no prompts) stay BUG-0179.

## Fix
social.cpp hooks AbilityBar_Use: on a guest with an engaged mirror, in the social stance, a social
ability (+0x10 == 1) is taken like the key (the avatar plays it, MSG_SOCIAL K_ACT to the host); the key
poll and the hook do not send the same press twice (0.5 s). Anything else goes on to the game.
Switch `coop.fix social.bar_click off` (keys only, as before).

## Verification
FAIL: SPORE_FIX_OFF=social.bar_click creature_social_bar, lab 20261103-181651 (three Kooks engaged, four
clicks each on the sing button: nothing sent, none impressed).
PASS: creature_social_bar, lab 20261103-181540 (Kook #19 impressed on the host's game and on the guest's
proxy, the species table 0 -> 1 impressed on both).

## History
- 2026-11-03: opened.
- 2026-11-03: fix/w40-dna-social: the bar's click plays the social game (social.bar_click); fixed (unreleased).
- 2026-11-03: released in 0.7.4.
