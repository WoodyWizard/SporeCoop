---
id: BUG-0449
title: Tribe: the guest's game faults every frame in the physics update at 0xB4C5B4 (a body with no owner; dc04aee5:SporeApp.exe+0x74C5B4)
status: fixed
kind: crash
stage: tribe
reported: 2026-11-01
source: lab: tribe_walk, out/lab/20261101-204356 (lane 6)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w37-tribe-fault
commits: 03aa8d9c fc77189e 81c2f03f
tests: physics_orphan (tools/tests/coop_physics_orphan.py), tribe_walk
signature: dc04aee5:SporeApp.exe+0x74C5B4
docs: docs/re/tribe.md#tribe-physics-orphan
related: BUG-0444, BUG-0440
---

## Report
The final 0.7.3.4 verification run (out/lab/20261101-204356, main f0cefbaa, lane 6) failed tribe_walk on
"guest faults: total faults: 1819": a caught access violation at 0x00B4C5B4 (SporeApp.exe+0x74C5B4, read
0x75, eax=0 ecx=0, esi=0EB8E050) at s 90.4, then in every frame to the end of the test. Calls: 00B4C6F1
00B5D229 00CDF46C 00922514 006B1C99 007C64B3 007EA2B1 00F47947 00F47A5D 00959FE1. The test's own checks
passed. At s 90.4 the guest had just started its own walk phase (box-select of its members 2, 7, 9 and the
first right-click); nothing was spawned or removed. After the first fault (a 2 s hitch while it was logged)
mirror 7 jumped 9.6 m (the run's single "driver teleport"). First and only occurrence of this address (or
the GOG 2016 0xB4C5A4) in every log under out/.

## Root cause
Observed (Ghidra, GOG 2024): 0xB4C5B4 is in `SporeCoop::PhysicsManager_Update` (0xB4C370, the per-frame
Update of the "Simulator/cPhysics" subsystem, slot 0x38 via the thunk 0xB4C6E0 = the first frame of the
chain). It walks the body pool at [0x167EB70]+0x3C (cObjectPool_::Iterate) and, for a body of kind 0 or 1
(+4), reads its owner (+0x40) unchecked: `mov eax,[esi+0x40]; cmp byte [eax+0x75],0`. The registers say a
live body of kind 0 (ecx) with owner 0 (eax). The game's handler unwound the rest of the update in every
frame. Details: docs/re/tribe.md#tribe-physics-orphan.

Inferred / unknown: how the body lost its owner. Bodies are created (0xB47830) with a non-null owner and
deleted through the owner hash map (0xB43810, 0xB47FE0). A body deleted twice would make the pool's free
list point at itself, so the slot looks live to Iterate and later creates share it (memset); that fits
eax=0/ecx=0 but is not proven. None of today's merged changes touches physics bodies: tribe.mirror_ground,
npc.proxy_ground and npc.walk_ground only teleport / walk creatures (vt 0x44, locomotion), nest.egg_ground
acts on host eggs only, parts.census_states and the vote busy gate touch no Simulator objects.

Not reproduced: tribe_walk x6 on lanes 9/10 with every candidate switch on (main f0cefbaa):
out/lab/20261101-223231-fix-w37-tribe-fault (0 faults in all 6; #6 failed on the open BUG-0440 proxies gate).

## Fix
Containment, behind `game.physics_owner` (mods/coop/gamefixes.cpp): a hook on PhysicsManager_Update looks
through the body pool before each update and gives a live body of kind 0/1 with a null owner kind 3, which
no physics loop handles (Update, 0xB46E10 and the contact check 0xB3FD80 skip any kind but 0..2). The rest
of the world is updated again; the first 8 such bodies are logged with the pool's free head (the
"freed twice" sign) for the next occurrence. GOG 2016 is frozen: the guard is off there
(frozen_off_922af7c7.txt). Tests: `coop.physorphan` makes, in the next update, a live body of kind 0 with no
owner that nothing refers to (the observed state; a slot past the pool's high water made live as CreateObject
does).

## Verification
physics_orphan (tools/tests/coop_physics_orphan.py, guest in Tribe, the stand-in of the observed body):
* FAIL with `SPORE_FIX_OFF=game.physics_owner`: out/lab/20261101-225013 (lane 9, 2 of 2): the guest faults
  at 0x00B4C5B4 data 0x75 in every frame (143 and 141 faults), call chain 00B4C6F1 00B5D229 00CDF46C ... as
  in the report.
* PASS with the fix: out/lab/20261101-225038 (lane 10, 2 of 2): 0 faults, one body left out each run.
* An earlier stand-in (owner taken off a body its owner still refers to) faulted elsewhere first (0xB3DD60,
  out/lab/20261101-224546) and was replaced.
* Not reproduced naturally: tribe_walk x6 on main f0cefbaa, out/lab/20261101-223231 (0 faults).
* Regression (lanes 9/10, fix on, 0 faults anywhere, the guard never fired on its own):
  tribe_walk x5 out/lab/20261101-225322 (4/5; #2 failed on the host's minimap calibration, no host walk:
  the known test flake of BUG-0365/BUG-0397), tribe_coop x2 out/lab/20261101-230117 (2/2),
  tribe_walk_player, tribe_mirror_ground, npc_walk_tribe, chaos_tribe out/lab/20261101-230308 (4/4),
  smoke out/lab/20261101-230838 (13/13, unit's lab selftest rows are its own expected fails). make test OK.

## History
- 2026-11-01: opened from the 0.7.3.4 verification run; contained behind game.physics_owner on
  fix/w37-tribe-fault (the cause of the ownerless body stays unknown).
- 2026-11-02: released in 0.7.3.4.
