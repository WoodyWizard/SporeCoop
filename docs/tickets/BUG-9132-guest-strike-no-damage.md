---
id: BUG-9132
title: Creature: the guest's Strike does no damage to the host's creatures (Bite does)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages50-dev
role: guest
versions: 0.7.8-lineages50-dev
fixed_in: 0.7.8-lineages51-dev
branch: feat/lineages-47
commits:
tests: QA Samuth.spo host 9 + guest 8: guest `coop.abilities target <npc>` + `coop.abilities play <i>` beside the proxy; host log "npc: guest 1 hit npc ..."; guest log "npc: proxy N hit here"
signature: -
docs: -
related: BUG-0554
switch: creature.guest_strike
---

## Findings
* The guest's abilities (`coop.social abil -1`): 0x1E bite (points 1, range 1), 0x1F charge, 0x20 spit, **0x21 strike
  (points 6, range 1, effect kind 3 = area, chance 1.0)**; 0x1D is social (kind 1, range 100).
* Bite: the guest's TakeDamage on the proxy goes through npc::damage_filter (attacker = own avatar) to the host:
  "guest 1 hit npc 39: reported 1.00 type 2 applied 1.00, hp 34.50 -> 33.50".
* Strike (ability 10): no call of the damage filter on any proxy was logged on the guest and no hit reached the host.
  Its damage seems to come from the area effect of CreatureAbilityHit 0xC20390 (effect kind 3), not a TakeDamage with
  the avatar as attacker. Next: log CreatureAbilityHit / the area effect on the guest for the strike (attacker,
  targets, amount) and forward it like a bite. The QA runs were noisy (NPCs knocking the guest out).
* Diagnostics added: host "npc: guest %u hit npc %u: reported .. type .. applied .., hp .. -> .." and guest
  "npc: proxy %u hit here: attacker .." (rate limited), `coop.abilities target <npc>`.

## Cause (runtime, QA host 9 + guest 8, the A14FC517 group)
The Strike (0x21, 6 points, effect kind 3 = area) hits its target through TakeDamage with the guest's avatar as the
attacker (forwarded: "guest 1 hit npc 29: reported 6.00 ... 43.12 -> 37.12"), and every creature around it with
1.5 and **no attacker** (guest log "proxy 28 hit here: attacker 00000000 (none) damage 1.50"). npc::damage_filter
forwards a guest's hit on a proxy only when the attacker is its avatar: the splash was dropped. A Strike whose
target was out of reach did nothing at all on the host (only splash), the owner's "strike does no damage".
(The earlier "no hit" finding above came from strikes that missed.)

## Fix
player_rules: own_attack_running() is true inside the guest avatar's CreatureAttackTarget (hooked); npc's damage
filter forwards a hit with no attacker on a proxy during it. Also: an own attack on a proxy that sent no hit at all
sends the ability's points (+0x3C) as a fallback (not needed in the runs). Switch creature.guest_strike.

## Verification
* on: strike 1: 29 -6.00 and 28 -1.50 on the host; strike 2: 29 -6.00, 11 -1.50; strike 3 (target out of reach):
  28 -1.50 (it would have done nothing before).
* off: one strike hit 29 (6.00) and splashed 11, 28, 21 (1.50 each) on the guest; the host got only 29.
* 0 faults on both games.
