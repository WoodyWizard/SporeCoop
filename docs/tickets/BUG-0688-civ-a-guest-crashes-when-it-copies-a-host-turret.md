---
id: BUG-0688
title: Civ: a guest crashes when it copies a host turret's shot: the turret's weapon is read through the building combatant offset (3010a413:coop.dll civ weapon_of, AV at 0xCCCCCCCC)
status: open
kind: crash
stage: civ
reported: 2026-11-07
source: GitHub #198
role: guest
versions: 0.7.9
fixed_in:
branch:
commits:
tests:
signature: 3010a413:0xCCCCCCCC exec (coop.dll civ::weapon_of <- guest_fire <- guest_tick <- civ::tick)
docs: docs/re/civ.md#civ-combat-shots
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

## Fix
Not fixed yet. Proposed: take a turret's combatant at its own offset (to confirm at runtime: +0x588, vtable
0x1467758 in the 0x400000 numbering) and check the combatant's vtable lies in SporeApp.exe's image
(vtable_in_game) and that slot 0x14 points into the game's code before the vcall in weapon_of, for vehicles too
(one check for every shooter kind). Switch: a new coop.fix (civ.turret_weapon) next to civ.shots.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
