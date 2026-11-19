---
id: BUG-1322
title: Civ: the host faulted (EIP inside a preaching vehicle) in the mod's shot observer: a preacher's target spatial is not +0x34 of an object
status: fixed
kind: crash
stage: Civ
reported: 2026-11-19
source: lab
role: host
versions: 0.7.16 (unreleased)
fixed_in: unreleased
branch: fix/w57-sync, fix/w57-crashes
commits: f2b44601 1defae3e 0ffc91f2
tests: civ_preach_far, civ_shot_objects, civ_shot_objects_off
signature: exception 0xC0000005 at a heap address inside a vehicle object, stack scan 00BFD8DB 00AE75E0
docs: docs/re/civ.md#civ-npc-fixture-slots, docs/re/civ.md#civ-shot-observer-targets, docs/06-testing.md#testing-civ-shot-objects
related: BUG-1286
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->
Lab run out/lab/20261119-114030 (fix/w57-sync worktree, lane 9), civ_preach_far: the host spawned three religious
vehicles past the civilization's allowance (`coop.civ spawnveh ... force`, the civ_npc fixture allows none) at
207E4128 / 208134E0 / 20841300, put them 80-100 m from the far NPC city along x (about 30 m over the ground: the old
put_near) and ordered a religious attack. 3 s later: `exception 0xC0000005 at 0x207E4131 (data 0x01464301)`, i.e. EIP
inside the first vehicle object (+9), then one at 0xCCCCCCCC; the stack scan starts 00BFD8DB (FUN_00bfd750, a
combatant's target-point list: owner vt+8, vt+0xAC / +0x34 / +0x68) 00AE75E0 00BEAA93; 191 faults counted in all, the
test went on. Two of the three preachers were gone on the host afterwards (in other runs too, without a fault).
Not seen in 3 other runs with the vehicles in the air, nor in 2 runs with them on the ground (20261119-115145).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->
Not known. Inferred: a vtable call through an object whose vtable slot holds a vehicle pointer, i.e. a stale or wrong
combatant / owner pointer after a preacher was taken away (the city's retaliation 0xBEB6F8 or a disband past the
allowance). The fault came 90 ms before the BUG-1286 hook set the slot's places for the first time, so that hook is not
in it by timing. Whether a player can get there (vehicles only from the game's buy path, on the ground) is open.

Analysis (2026-11-19, fix/w57-crashes; docs/re/civ.md#civ-shot-observer-targets). The fault is in the mod, and a player
reaches it: neither the vehicles in the air nor the forced spawn are part of it.
* Observed (the run's module addresses, symbolized on a build of its commit dee4405f): the first fault's return address
  on the stack is coop+0x9B6AA = civ::fire_observed (civ_shots.cpp, the target branch), the second fault's coop+0x17AC18 =
  game::object_position (the Cast vcall, slot 0xC) under it, both from squadron::h_FireTool (coop+0x2CAD3D); esi held the
  Cast id 0x1186577. 0xBFD8DB / 0xAE75E0 were stale stack entries of cCombatSimulator::Update's own work before FireTool.
* Observed (code): fire_observed took the object at target spatial - 0x34 and, when its first dword lay in SporeApp.exe's
  image (BUG-0535's check), called its vtable.
* Observed (runtime, lab 20261119-141549 civ_shot_objects; Ghidra): a preacher's FireTool target is the spatial at
  +0x100 of Simulator/cCulturalTarget (vtable 0x1468478; noun factory 0xB1EB40, ctor 0xBDACB0), not +0x34: target - 0x34
  is its field +0xCC (0x00000018 in that run). When that field holds a value in the image's range, the observer calls
  through it: the jump into a vehicle's memory, then 0xCCCCCCCC. Every religious preaching in a co-op host's game passes
  there; the field's value decides (1 run in 4).

## Fix
(fix/w57-sync: the test puts its preachers on the ground; not the cause.)
`civ.shot_target_listed` (civ_shots.cpp, fire_observed): a shooter or target that is not one of the host's listed
vehicles is used only when the object at spatial - 0x34 is in the game's object list (game::listed: pointers compared,
nothing called; once per object and frame); else nothing is called and the shot has no target the guests could find
(kind 2, as before for unknown targets). Off: the image check only (0.7.15). `coop.civ shotobjs` prints the unlisted ones,
`coop.civ shotprobe` (tests) hands the observer a shot at a target that is no object, the memory before it starting with
the exe's base (its +0xC is 0x0000FFFF).

## Verification
Test civ_shot_objects (coop_civ_influence.py --case shotobjs, lane 9, fix/w57-crashes worktree):
* PASS: out/lab/20261119-141549:civ_shot_objects (preachers on the ground: 6 frames with the cCulturalTarget spatial as
  an unlisted target, nothing called; the probe: kind 2; 0 faults).
* FAIL (twin): out/lab/20261119-141549:civ_shot_objects_off (civ.shot_target_listed off): the observer called into the
  probe's fake vtable: host fault 0xC0000005 at 0x0000FFFF, as the run of the report jumped into a vehicle.

## History
- 2026-11-19: opened.
- 2026-11-19: cause found and fixed on fix/w57-crashes (the shot observer, cCulturalTarget targets).
