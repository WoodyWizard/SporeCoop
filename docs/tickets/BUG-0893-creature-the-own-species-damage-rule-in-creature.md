---
id: BUG-0893
title: Creature: the own-species damage rule in CreatureAbilityHit and other own / party rows of the BUG-0738 caller table were never checked or ticketed
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0738, BUG-0418, BUG-0722, BUG-0732, BUG-0737
---

## Report
A guest playing its own creature (not the world's species) may deal different ability damage than a player of the world's species: its puppet on the host is a non-player-species attacker, so its damage gets the herd-archetype multiplier NPCs get. BUG-0738 and the umbrella BUG-0418 were closed with this row open.

Severity: medium. Confidence: low.

## Root cause
* Observed (docs): docs/re/creature.md:3615, BUG-0738 caller table: "vt 0xD0 in CreatureAbilityHit 0xC20390..0xC21190 | a non-PS attacker's damage x its herd archetype (0xC046F0) +0x158 | own (inferred; not checked)", with an empty "Now" column (no switch, no ticket).
* Observed (docs): the same table (docs/re/creature.md:3591 on) has other rows with a party or own verdict and nothing done: 0xC0EA30 (ally relation kind, "party (guest, small; inferred)"); 0xD7C290 / 0xD6E880 / 0xD6F500 / 0xD8D270 ("party (host), low"); 0xD8CB20 / 0xD38150 / 0xD405F0 / 0xD2C000 (pack UI cards only for player-species members, "guest side, inferred").
* Inferred (low): the puppet of a guest's own species has a herd without an archetype (BUG-0722); the multiplier may read a field NPC herds set and this herd lacks (wrong scale, or a fault).

## Fix idea
Decompile the vt 0xD0 branch in 0xC20390 and see what +0x158 is on a puppet's herd. If the rule should treat the attacker as the player, run it with the player-species override `creature.own_scale` uses. Give each of the other open rows a verdict and a ticket, or "keep" with a reason.

## How to test
Two games, the guest playing its own species and the host the world's, the same ability on the same kind of NPC; read the NPC's health drop per hit in the host's game. Must FAIL (the guest's damage differs, or a fault) with the fix off and PASS with it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0738. Severity medium, confidence low.
