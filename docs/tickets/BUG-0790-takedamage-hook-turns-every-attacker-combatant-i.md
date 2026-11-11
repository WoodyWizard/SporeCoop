---
id: BUG-0790
title: TakeDamage hook turns every attacker combatant into a creature by subtracting 0x5A8, wrong for vehicle, UFO, building attackers (suspected)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main 03de36a3 (game.cpp h_TakeDamage)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
No player report. Wrong attacker pointers reach our damage filter when a creature is hit by something that is not a
creature (a vehicle, UFO, building, turret). Possible effects: an attack misattributed or, at worst, a read of a garbage
address in the filter (suspected, no crash seen).

## Root cause
Observed in code, consequence inferred (confidence medium): `mods/coop/game.cpp:182` (h_TakeDamage on
cCreatureAnimal::TakeDamage) computes `att = attacker - kCombatOff (0x5A8)` for every attacker. 0x5A8 is the creature's
cCombatant subobject; other classes put it elsewhere (vehicle combatant at vehicle+0x508, turret at +0x588, see our
overrides_dc04aee5.sym). `npc::damage_filter` passes `att` to `creature_hp::drop_party_hit` -> `host_party`
(`creature_hp.cpp:248`, reads `att + kFlagsOff` and `herd_of(att)`), `tribe::note_animal_hit`, `log_hit`. With a vehicle
or UFO attacker (Civ, Space on a planet) these read a wrong object. The fork found that a combatant's virtual +0x0C is
its ToGameData (owner object) conversion, valid for every combatant class.

## Fix idea
From the fork (jwhans0n/SporeCoopGA 03de36a3, docs/re/adventure-world.md "Shared actor identity, combat and lifecycle"); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
In h_TakeDamage they replaced the subtraction with `att = attacker ? vcall<void*>(attacker, 0x0C / 4) : nullptr`
(combatant vt +0x0C = ToGameData). Verify the slot on dc04 for creature, vehicle, UFO and building combatant vtables
(creature 0x1469E30, vehicle, turret 0x1466C28) before using; keep the old line behind a switch. Note that the
apply_damage path (`game.cpp:204`) builds the attacker combatant as `attacker + kCombatOff` and needs the same care for
non-creature attackers.

## How to test
Creature or Civ stage with a vehicle attacking an animal (or Space: the UFO's laser on a creature), damage log on
(`coop.dmglog`): FAIL: the logged attacker pointer is not a live game object (object list / vtable check fails); PASS:
it is the vehicle / UFO object.

## Fix
## Verification

## History
- 2026-11-10: opened.
