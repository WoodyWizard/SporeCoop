---
id: BUG-0525
title: Creature (guest): the NPC's prompts of the social game are not shown and any social ability counts as the answer
status: fixed
kind: feature
stage: Creature
reported: 2026-11-03
source: split from BUG-0179 on fix/w41-creature-left
role: guest
versions: 
fixed_in: 0.7.6
branch: fix/w42-creature-left
commits: caded79f
tests: creature_social_prompts (coop_creature_social_prompts.py)
signature:
docs: docs/re/creature.md#creature-guest-meter, docs/re/creature.md#creature-social-prompts
switch: social.prompts
related: BUG-0179
---

## Report
Split from BUG-0179 (fix/w41-creature-left, 2026-11-03). In a guest's social game the NPC does not show what to do next (the native game's prompts: the NPC performs the abilities of its prompt list, the player answers with the same), and any social ability counts as the answer, one press per round (social.cpp host_act counts every K_ACT).

## Root cause
Observed (Ghidra): the prompt list is built by SocialPropose 0xDA4360 (npc +0x1020, random from the NPC species profile +0x7A4) only for a native session, which exists only on the host's own social games. Inferred: a fix lets the host pick the prompts the same way for a guest's game, play them on its NPC (the npc.anims relay shows them on the guest's mirror) and count only the matching ability type (with the meter's bar following it).

Observed (Ghidra, fix/w42-creature-left): the native tick 0xDA4950 plays prompt i on the NPC and takes one answer per
prompt (0xD9B070): the answer ability's +0x10 equal to the prompt's gives its points, another 0.1 of them; the meter is
full at the prompts' points; a list through with an unfilled meter fails (0x53685D8F). Observed (runtime, lane 6): with
the fix off an answer sent before any prompt impressed a Kook at once on both games, and no prompt reached the guest.
docs/re/creature.md#creature-social-prompts.

## Fix
`social.prompts` (social.cpp): the guest reports its engagement (MSG_SOCIAL K_ENGAGE 4); the host picks the NPC's
prompts as SocialPropose does, plays them one by one on its NPC (npc.anims relays them) and sends each (K_PROMPT 5: the
guest's mirror performs it); it counts a guest's answer only while a prompt is open, by the ability's +0x10 and points
(0.1 for another kind), fails the game after the list (the mirror plays the failure). No new MSG id. Off:
`coop.fix social.prompts off` (0.7.5: any social ability at any time is a round).

## Verification
creature_social_prompts (tools/tests/coop_creature_social_prompts.py): real engagement on the guest; an answer before
the prompt does not impress (host and guest), the guest's mirror plays the prompt's animation, an answer of another kind
(a combat ability) does not impress and fails the game, the right answer to the prompt impresses on both games.
- FAIL with SPORE_FIX_OFF=social.prompts: out/lab/20261104-112706 (impressed by the early answer, no prompt).
- PASS: out/lab/20261104-112554.
- Regression: creature_social_ally, creature_social_bar, creature_social_meter, creature_social_ally_player out/lab/20261104-112826 (4/4).

## History
- 2026-11-03: opened.
- 2026-11-03: opened (split from BUG-0179).
- 2026-11-04: fixed on fix/w42-creature-left (social.prompts).
- 2026-11-04: released in 0.7.6.
