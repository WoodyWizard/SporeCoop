---
id: BUG-0931
title: Creature: a guest drives an NPC proxy that its own game removed, for up to a second (29 caught faults in coop.dll game::teleport when the baby_game event started)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-11
source: fix/w47-final12 lab 20261111-071518 long_creature (lane 3), the guest
role: guest
versions: 0.7.x
fixed_in: unreleased
branch: fix/w47-final12
commits: fb8b2e2f
tests: proxy_ghost, long_creature
signature: coop.dll game::teleport (vcall on a null vtable, read 0x44) from drive::step, a guest, Creature
docs: docs/06-testing.md#testing-dialog-note
related: BUG-0300, BUG-0850, BUG-0926
---

## Report
The first long_creature with BUG-0926's fix (lab 20261111-071518, lane 3) passed its session and failed on the guest:
`guest faults: total faults: 30`, 29 of them 0xC0000005 in coop.dll (`game::teleport` called by `drive::step`, a read of
0x44: the vtable of the creature's spatial part was 0), all within 0.45 s after `drama: baby_game event starts
(guest)`, each one a cut-off frame; then `npc 171 / 172: the proxy's creature ... is no creature of this world any
more ... made again`. For a player: a short stutter and "SporeCoop caught an error in its own code" when the baby
is born after the evolution, with animals nearby.

## Root cause
Observed: the guest's own game removed and freed two NPC proxies when its baby_game event started. The guard of
BUG-0300 (`npc.proxy_listed`: the proxies against the creature noun list) ran once a second; the proxies are driven
in every frame, so the freed creatures were driven until the next check (the log: first fault 2 ms after the event's
line, the "no creature of this world" lines 0.45 s later, no fault after them).
Not from this branch's other changes (the host's destroy hook only): the race is as old as BUG-0300's guard (0.7.3.2).

## Fix
mods/coop/npc.cpp: the list check runs in every frame, right before the proxies are driven (pointer compares against
the noun list, about 200 objects, a set that is reused). Switch: `coop.fix npc.proxy_listed_now off` (once a second).
Test command `coop.npc ghost <id>` (a guest): the proxy's creature is destroyed and the proxy names a zeroed block.

## Verification
Test `proxy_ghost` (tools/tests/coop_proxy_ghost.py, lane 5): three proxies in turn name memory that is no creature.
* FAIL: out/lab/20261111-080410 (`SPORE_FIX_OFF=npc.proxy_listed_now`): 139 caught faults at the same place
  (coop.dll game::teleport, read 0x44). Also out/lab/20261111-071518 (long_creature, before the fix).
* PASS: out/lab/20261111-080502, out/lab/20261111-080548 (0 faults, 3 of 3 named in the log and made again).

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-final12 (npc.proxy_listed_now).
