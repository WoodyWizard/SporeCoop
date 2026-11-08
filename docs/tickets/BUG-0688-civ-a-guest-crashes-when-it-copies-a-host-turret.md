---
id: BUG-0688
title: Civ: a guest crashes when it copies a host turret's shot: the turret's weapon is read through the building combatant offset (3010a413:coop.dll civ weapon_of, AV at 0xCCCCCCCC)
status: fixed
kind: crash
stage: civ
reported: 2026-11-07
source: GitHub #198
role: guest
versions: 0.7.9
fixed_in: unreleased
branch: fix/w46-issue198
commits: 0643d9ca, 9cc0f350
tests: civ_turret_shot
signature: 3010a413:0xCCCCCCCC exec (coop.dll civ::weapon_of <- guest_fire <- guest_tick <- civ::tick)
docs: docs/re/civ.md#civ-combat-shots, docs/re/civ.md#civ-turret-combatant, docs/06-testing.md#testing-civ-turret-shot
related: BUG-0477, BUG-0484, BUG-0534, #198
---

## Report
Steam build (exe 3010a413, the GOG 2024 code), coop 0.7.9, SporeKit 0.3.12, 2 players. The guest joined, loaded
the host's Civ world and crashed about 10 s into Civilization, every time; the player thinks it happens when one
of the players fights with tanks (vehicles attacking a city, whose turrets fire back). Crash report: access
violation, execute of 0xCCCCCCCC on the main thread. The log's last coop lines before the fault are two
"this guest's game fired a tool itself" lines with owner noun 0x0436F342 (a city turret).

## Root cause
* Observed (the 0.7.9 build, addr2line on build/coop.dll): the stack scan is `civ::weapon_of` (coop.dll+0x4158E)
  <- `civ::guest_fire` (civ.cpp:905) <- `civ::guest_tick` <- `civ::tick` <- on_frame. weapon_of calls vt 0x14 of the
  combatant it is given; that slot held 0xCCCCCCCC.
* Observed (minidump): the shooter `o` = 0x23A4CB78 (on the stack next to the return address) has the vtable
  0x01956E30 (exe base 0x008F0000, 0x1466E30 in the 0x400000 numbering), the owner vtable of the turret in the
  "fired a tool itself" line; o+0x34 holds the spatial vtable 0x01956D20 and the turret's position. The
  combatant pointer was o+0x120 (ecx = edi = 0x23A4CC98), which holds 0x6DB2C23E: no game vtable. Its 0x18 bytes
  were readable, so the IsBadReadPtr checks in weapon_of passed, and the call went to 0xCCCCCCCC.
* Cause: guest_fire takes the combatant of every non-vehicle shooter (kind 3, a turret found by noun 0x436F342
  near the host's shooter place) at `kBCombatant` = +0x120, the offset of a building's combatant. A turret is not a
  building: its combatant is elsewhere (docs/re/civ.md: the turret TakeDamage `this` = turret+0x588, combatant
  vtable 0x1467758). So every host turret shot that a guest copies calls a virtual of a non-object. BUG-0477
  relayed turret shots "the same way, not seen in a test", so the path never ran in the lab.
* Inferred: the crash needs a turret on the host that fires (a vehicle in its range) while the guest has a
  turret at the same place (civ.turrets, BUG-0484, makes them match); hence "after interacting with tanks".
* Unchanged in 0.7.10 (weapon_of and guest_fire are the same as in v0.7.9).
* Confirmed at runtime (lab 20261107-204140, GOG 2024): a turret's vtable is 0x1466E30 (the minidump's), its combatant
  is at turret+0x588 with vtable 0x1466C28 (GOG 2016 0x1467758); +0x120 held 0, 0x3DA4944E, 0xAA670100, 0x2134B200 in
  four turrets. The attacker of a turret's hits is that combatant (att_vt 0x1466C28). With the old offset the guest
  faulted executing the garbage slot at every copied turret shot (284 faults in 60 s, lab 20261107-204522).

## Fix
Fixed (fix/w46-issue198, switch `civ.turret_weapon`): guest_fire and cosmetic_hit take a turret's combatant at
turret+0x588 (kTCombatant); weapon_of calls vt 0x14 only when the combatant's vtable lies in SporeApp.exe's image and
its slot 0x14 in an executable section of it, for every shooter kind; a wrong one is skipped, counted (`turret_shots:
bad_combatant`, `coop.civ`) and logged once per vtable. `coop.fix civ.turret_weapon off` restores the 0.7.10 path.

## Verification
Test `civ_turret_shot` (tools/tests/coop_civ_turret_shot.py, lane 2): the host adds two turrets to an NPC city, its
army attacks it, the turrets fire for 60 s.
- FAIL: out/lab/20261107-204522 (SPORE_FIX_OFF=civ.turret_weapon): guest 284 faults (execute 0x8080FF00 on the main
  thread), 0 copies fired of 252 host turret shots.
- PASS: out/lab/20261107-204738 (2 runs): 208 / 210 and 451 / 451 copies fired, bad_combatant 0, no fault.

## History
- 2026-11-07: opened.
- 2026-11-07: fixed on fix/w46-issue198 (civ.turret_weapon), test civ_turret_shot.
