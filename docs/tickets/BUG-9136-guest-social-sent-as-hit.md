---
id: BUG-9136
title: Creature: a partner's social ability reaches the host as a bite, so friendly creatures fight partners and cannot be befriended
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: owner report on 0.7.8-lineages53-dev ("friendly creatures are attacking connected players. They aren't able to befriend them.")
role: guest
versions: 0.7.8-lineages51-dev to 0.7.8-lineages53-dev (since BUG-9132)
fixed_in: 0.7.8-lineages54-dev
branch: feat/lineages-47
commits:
tests: QA DuoQACreatureHost/Guest (Samuth.spo); guest `coop.prules play <proxy id> 25` (the sing on a host creature); host log "npc: guest N hit npc"; coop.fix creature.social_no_hit off / on
signature: host log "npc: guest N hit npc M: reported <points> type 2" for a creature of a species being befriended, then "damage: N hit by npc"
docs: -
related: BUG-9132 BUG-0021
switch: creature.social_no_hit
---

## Report
Three players in Creature (host, Kittykath, BigTexas). Creatures the party was befriending attacked the partners, and
the partners could not befriend them. Host log 2026-11-11 21:01: while the host impressed species 291E6BFD (npcs 251,
252, 254; allied at 21:01:52), both guests "hit" npc 253 of the same herd (`guest 1 hit npc 253: reported 1.00 type 2`),
and npc 253 then hit guest 1 (`damage: 1 hit by npc`).

## Cause
BUG-9132 (Lineages 51) made a guest's Strike reach the host: when an own ability reached a proxy (CreatureAttackTarget
of the avatar) and the game's damage sent no hit, player_rules forwarded the ability's points as a hit. That fallback did
not look at the kind of ability. A social ability (sing, dance, charm, pose) played next to a creature goes through the
same hit call without damage, so every social move of a partner reached the host as a bite: the creature was hurt,
the species took the "attacked" event, the creature fought back, and the social game failed.

## Fix
player_rules.cpp: an own ability is no attack when it is a social one (ability +0x10 not 0, the kind a social answer
must match) or is used in the social stance (GetAbilityMode 0). No hit is forwarded for it, and own_attack_running()
is false while it runs (so npc.cpp does not take a no-attacker hit as the guest's either). Switch
`creature.social_no_hit` (off: forwarded as in 51..53).

## Test (2026-11-11, QA DuoQACreatureHost / DuoQACreatureGuest, Samuth.spo, Windows)
Real input on the guest (scratchpad socialhit.py: social stance button, `coop.social goto`, a click on the mirror, key 1):
* switch off: every sing (ability 6, type 0x25, points 2) in the social stance was first dropped by `social.no_bite`
  ("a hit of the avatar on npc 14 in the social stance is not sent") and then sent by the BUG-9132 fallback ("hit sent
  (BUG-9132)"): 5 sent; the host applied them ("guest 1 hit npc 14 ... hp 43.12 -> 41.12", npc 24 three times).
* switch on: npc 14 engaged, two sings: "social: no hit (BUG-9136)" both times, 0 hits from the guest on the host.
* Also seen: `coop.prules play <id> 25` (the sing aimed by SetTarget) in the **combat** stance is a real hit by the game's
  own damage (2.00, sent by damage_filter); in the social stance the sing played that way reaches no CreatureAttackTarget.
  Only the click-engaged sing hits in the social stance.
* Note: these console `click` / `key` tests move the real mouse of the PC that runs them.
* Two PCs (TITAN hosts a copy of the owner's Vandh.spo, the MSI befriends by real input on its own desktop): switch
  on: the MSI's sing reached the guard once (`coop.prules`: social_kept=1), 0 hits from the MSI on the host. The bad
  path is intermittent (the engage click's hit, BUG-0021): the switch-off two-PC run did not happen to take it.
