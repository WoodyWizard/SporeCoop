---
id: BUG-0790
title: TakeDamage hook turns every attacker combatant into a creature by subtracting 0x5A8, wrong for vehicle, UFO, building attackers (suspected)
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-main 03de36a3 (game.cpp h_TakeDamage)
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-creature
commits: 7d823115
tests: attacker_owner
signature:
docs: docs/re/creature.md#creature-c2-audit-0713
related:
---

## Report
No player report. Wrong attacker pointers reach our damage filter when a creature is hit by something that is not a
creature (a vehicle, UFO, building, turret). Possible effects: an attack misattributed or, at worst, a read of a garbage
address in the filter (suspected, no crash seen).

## Root cause
Observed (Ghidra, dc04) and at runtime. The combatant vt 0x0C of every class is its owner conversion: creature 0x1469E30 and citizen 0x146AAB0 -> 0xC0BFC0 `lea eax,[ecx-0x5A8]`, vehicle 0x1474CE8 and UFO 0x146BB48 -> 0xC3BDB0 `[ecx-0x508]`, turret 0x1466C28 -> 0xBCD080 `[ecx-0x588]`, hut (table at 0x1474318) -> 0xBCD230 `[ecx-0x120]`. The hook's `attacker - 0x5A8` gave a UFO hit the address UFO - 0xA0 (lab 20261112-073101), and the filter read creature fields there (the party check host_party, the pack defence's stimulus sender).

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
h_TakeDamage takes the attacker's object from its combatant vt 0x0C; drop_party_hit and pack_defend get the attacker only when it is a listed creature. Switch `coop.fix creature.attacker_owner off`: the subtraction. The fork's idea, verified on our build.

## Verification
Test `attacker_owner` (coop_attacker_owner.py): the host's Drama Manager makes its UFO (`coop.drama start 2`), the UFO's combatant hits the guest's puppet through the hook (`coop.hp ufohit`); the attacker in `coop.hp dmglog`.
* FAIL: out/lab/20261112-073101 (`SPORE_FIX_OFF=creature.attacker_owner`, lane 6): 0x213a0348 for the UFO 0x213a03e8.
* PASS: out/lab/20261112-073116 (lane 7): the UFO object.
* PASS: out/lab/20261112-080216 (lane 6, the switch on; only nest.mirror_epoch off in that run): the second PASS.

## History
- 2026-11-10: opened.
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): fixed (creature.attacker_owner); FAIL 20261112-073101, PASS 20261112-073116, 20261112-080216.
- 2026-11-12: released in 0.7.13.
