---
id: BUG-0421
title: class: guest-local game objects removed or never created (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
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
Root: update_filter (npc.cpp ~212-226) deletes every guest-local creature except avatar and puppets; the Cell stray purge (cellsync.cpp ~2603-2615) deletes other local cells; squadron keeps only types 3/6. Fixed one exception at a time: BUG-0004 mate id, BUG-0014 hatch, BUG-0018, BUG-0084 posse, BUG-0111, BUG-0218, BUG-0381 NPC tribes, BUG-0384 eggs, BUG-0401 parts.
Siblings not covered: family / mate egg (BUG-0177), the guest's pack, part tokens (BUG-0316), timed spawns (BUG-0411 may be this), Space type 8 ships (envoy, ambush) destroyed at the host's place (squadron.cpp 488-510) (inf), tribe tools / hut upgrades bought on the host (cTribe +0x36C, +0x19D8) (unknown), other collectables cPlayer +0x10FC..+0x1108, corpses eaten by a guest not forwarded, fruit pools local.
General fix: keep guest-local objects by origin and role, not an id list (Space: DestroyNPCUFOs' own keep rules). Outcome check: per-kind presence digests (eggs, tribes, cities, buildings, tools, parts hash, relation table hash).
Source: docs/generalisation-plan.md G4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
- 2026-11-04 (fix/w43-realign, W2 of 0.7.7): one mechanism for a guest's planet after transitions: realign.cpp (the host's whole-planet list of villages, huts, tools, food mats, nests, cities every 2 s and at once after the host's load / game over reload and a guest's (re)arrival; a guest removes what the host has not through the game's own removal, fixes the buildings' health, counts what it lacks). Tribe villages and buildings done (BUG-0564, BUG-0566, BUG-0567), tools on the whole planet (BUG-0488, wobj.tools_planet); Creature nests compared (realign_creature_rejoin, the same on both after a rejoin); Civ cities compared only (no removal). Class test coop_realign_planet.py (docs/06-testing.md#testing-realign).
