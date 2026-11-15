---
id: BUG-0893
title: Creature: the own-species damage rule in CreatureAbilityHit and other own / party rows of the BUG-0738 caller table were never checked or ticketed
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch: fix/w48-creature
commits: 99f351f2
tests: own_damage_scale
signature:
docs: docs/re/creature.md#creature-c2-audit-0713, docs/re/creature.md#creature-player-species-callers
related: BUG-0738, BUG-0418, BUG-0722, BUG-0732, BUG-0737
---

## Report
A guest playing its own creature (not the world's species) may deal different ability damage than a player of the world's species: its puppet on the host is a non-player-species attacker, so its damage gets the herd-archetype multiplier NPCs get. BUG-0738 and the umbrella BUG-0418 were closed with this row open.

Severity: medium. Confidence: low.

## Root cause
Decompiled (Ghidra, dc04) CreatureAbilityHit 0xC20390: for an attacker whose IsPlayerSpecies (vt 0xD0) is false, that is a cCreatureAnimal (0xAC8010 noun 0x18EB45E) and has a herd (0xC046F0 = `mov eax,[ecx+0x1674]`, mHerd, not an archetype), the damage is multiplied by herd +0x158. Runtime (lab 20261112-074532): the guest's avatar on its own creature (species 1BBA16A0, the world's 0F8F1218) stays in the avatar herd, whose +0x158 is 1.0 (as every NPC herd read: 1.0). The guest's own hits are not scaled.

The other open rows of the caller table, with a verdict: 0xC0EA30 (relation kind: PS + the target's pack bit = ally) keep: the party's allies already come from creature.party_allies (BUG-0556); 0xD7C290 / 0xD6E880 / 0xD6F500 / 0xD8D270 (animation pick, a 7.5 m distance, BT relations, stimulus 0x200000 to the PS in the awareness list) keep: host-side AI details with no effect a player reported, the npc.partners swap drives the NPCs at puppets; 0xD8CB20 / 0xD38150 / 0xD405F0 / 0xD2C000 (target UI, migration of non-PS herd members, pack UI cards) keep: a guest on its own creature has no PS pack members of its own (the host owns the pack).

## Fix idea
Decompile the vt 0xD0 branch in 0xC20390 and see what +0x158 is on a puppet's herd. If the rule should treat the attacker as the player, run it with the player-species override `creature.own_scale` uses. Give each of the other open rows a verdict and a ticket, or "keep" with a reason.

## How to test
Two games, the guest playing its own species and the host the world's, the same ability on the same kind of NPC; read the NPC's health drop per hit in the host's game. Must FAIL (the guest's damage differs, or a fault) with the fix off and PASS with it.

## Fix
None needed (factor 1.0).

## Verification
no-fail-run: not a bug (the factor is 1.0); own_damage_scale prints and checks it, lab 20261112-074532.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0738. Severity medium, confidence low.
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): not reproduced: the guest's own creature's herd factor is 1.0; the other rows got a keep verdict.
