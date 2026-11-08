---
id: BUG-0710
title: Creature: an NPC proxy has its species' adult maximum health on the guest, not the host's (a baby: 5 on the host, 10 on the guest; NpcSpawn.max_health never applied)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-07
source: K2a detector run 20261107-231824 (Creature:max_hp, 20 of 23)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state
commits: 6b4dec56 97392cd1 
tests: npc_max_hp (coop_npc_max_hp.py); state_fields_check (make test)
signature:
docs: docs/re/creature.md#creature-proxy-max-health
related: BUG-0419, BUG-0698
---

## Report
Found by the K2a final detector run (out/lab/20261107-231824, detect_creature): the new max_hp row (BUG-0698) in 20 of 23 Creature comparisons, e.g. creature 4 key=065CBE70: max health 5 on the host, 10 on the guest. A baby NPC's copy on the guest had the adult maximum of its species, so its health bar showed another fraction than the host's.

## Root cause
Observed: NpcSpawn.max_health was sent and stored (px.max_health) and never applied (BUG-0698 had listed it as "not needed: the game gives the proxy the host's max"; this run disproves that: CORRECTION). The proxy's max came from the game's own species rule (cCombatant vt 0x58).

## Fix
`npc.max_health`: the proxy's health goes through creature_hp::drive_puppet with the host's max, whose GetMaxHp hook (cCombatant vt 0x58) answers the host's maximum for the mod's creatures (the partner's puppet's way, BUG-0373); the entry is forgotten when the proxy is destroyed or the switch is off. state_fields_check lists the apply site.

## Verification
coop_npc_max_hp.py (four worlddiff comparisons, the game's max of every matched creature):
* FAIL with `SPORE_FIX_OFF=npc.max_health`: out/lab/20261107-235030 (4 max_hp findings).
* PASS: out/lab/20261107-235152 (2 runs).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): found by the detector, fixed, npc.max_health.
