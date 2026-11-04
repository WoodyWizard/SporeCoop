---
id: BUG-0525
title: Creature (guest): the NPC's prompts of the social game are not shown and any social ability counts as the answer
status: open
kind: feature
stage: Creature
reported: 2026-11-03
source: split from BUG-0179 on fix/w41-creature-left
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-guest-meter
related: BUG-0179
---

## Report
Split from BUG-0179 (fix/w41-creature-left, 2026-11-03). In a guest's social game the NPC does not show what to do next (the native game's prompts: the NPC performs the abilities of its prompt list, the player answers with the same), and any social ability counts as the answer, one press per round (social.cpp host_act counts every K_ACT).

## Root cause
Observed (Ghidra): the prompt list is built by SocialPropose 0xDA4360 (npc +0x1020, random from the NPC species profile +0x7A4) only for a native session, which exists only on the host's own social games. Inferred: a fix lets the host pick the prompts the same way for a guest's game, play them on its NPC (the npc.anims relay shows them on the guest's mirror) and count only the matching ability type (with the meter's bar following it).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (split from BUG-0179).
