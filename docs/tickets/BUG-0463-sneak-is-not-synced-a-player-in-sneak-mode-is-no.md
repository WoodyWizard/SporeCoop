---
id: BUG-0463
title: sneak is not synced: a player in sneak mode is not shown as sneaking / invisible on the other screen
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in: 0.7.4
branch: fix/w40-creature-acts
commits: fc6119e0
tests: creature_sneak (tools/tests/coop_creature_sneak.py)
signature:
docs: docs/re/creature.md#creature-sneak
related: BUG-0418, BUG-0419, BUG-0451
switch: creature.sneak
---

## Report
Both players marked it (s 2142, 2161). The audit: sneak (+0xB80 / +0xBB1, read by the hearing check 0xC0D6C0) is never sent or applied. Class G1/G2 (BUG-0418, BUG-0419); with BUG-0451 the sneak effect may relay but the state must too.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 9, creature fixture, 2026-11-03, `coop.sneak`): the sneaking player's own game holds +0xBB1 = 1, the noise
radius +0xB80 = 0 and eases the model alpha (+0xB7C, model +0x80) towards 0.2 (CreatureSetSneaking 0xC1B030, the
creature update 0xC1CF30). The partner's puppet of that player kept +0xBB1 = 0, noise 6.0 and alpha 1.0 (lab run
20261103-151356): nothing of the sneak state was sent (PlayerState had no bit for it).
Inferred (not run as a behaviour test): on the host the NPCs whose AI looks at a guest's puppet (npc.cpp ai_target swaps
it in as the avatar of NPCTickAI) heard a sneaking guest at the full noise radius, since the hearing check 0xC0D6C0 and
the sneak reads (0xC0C290) read the puppet's own +0xB80 / +0xBB1.

## Fix
PlayerState flag PF_SNEAKING (64) from the avatar's +0xBB1 (modes.cpp creature_fill; older SporeCoop ignores the bit).
The receiver puts the puppet into the same state with the game's own setter (CreatureSetSneaking(puppet, on, force 1))
whenever it differs, and keeps the puppet's energy (+0xE58) from 0 so the game's update does not end the sneak. The
game then does the rest on the puppet (noise 0 for its NPCs, the alpha easing, the sneak sounds). Switch `coop.fix
creature.sneak off`.

## Verification
creature_sneak, lane 9: SPORE_FIX_OFF=creature.sneak FAIL out/lab/20261103-151356 (the puppets keep sneak 0, noise 6.0,
alpha 1.0 while the actor's game sneaks at alpha 0.2); with the fix PASS out/lab/20261103-151502 (both ways: sneak 1,
noise 0, alpha within 0.15 of the actor's own).

## History
- 2026-11-02: opened.
- 2026-11-03: fixed on fix/w40-creature-acts (creature.sneak).
- 2026-11-03: released in 0.7.4.
