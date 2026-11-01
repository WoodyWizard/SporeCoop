---
id: BUG-0426
title: class: object lifetime and identity (liveness, address reuse, our objects in saves) (umbrella)
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
Fixed one site at a time: BUG-0040, 0044, 0054, 0066, 0081, 0099, 0171, 0223, 0300 (Creature proxies), 0342 (Cell puppets).
Siblings not covered: Tribe and Civ held pointers checked with object_alive which reads freed memory (tribe.cpp:658/890/933/1034/1884, civ.cpp:1118/1884/1939/1970) (obs); identity by raw pointer in game::is_puppet, creature_hp g_puppets, npc g_proxy_of/g_host_npcs/g_ai_target, tribe g_hm/g_gid_of/chiefs, civ g_city_owner/g_hv/g_gv_of, squadron g_hid (obs); raw puppet_of passed to TakeDamage as attacker (npc.cpp:1502, social.cpp:503) (inf); savestrip rules Creature only (only add_filter user nest.cpp:626): tribe mirrors, SpawnNpcTribe tribes, civ vehicle mirrors, UFO mirrors/puppets, fallback puppet in the host's pack herd (modes.cpp:59-60) (inf); SaveGame hook lives in space_allies and is skipped when ~36 Space symbols are missing (tribe.cpp:1665) (obs); host forgets partner puppet UFOs without destroying them (coop.cpp:366-379) (obs); deferred destroy creatures only (game.cpp:767); WorldToken stage epoch Creature-only.
General fix: identity = pointer + serial everywhere; liveness from the game's noun list, lint against object_alive on stored pointers; one registry of mod-made objects for savestrip (all stages) and destroy; independent SaveGame hook. Outcome check: chaos lane frees/recreates objects per stage; save/load per stage with no mod objects.
Source: docs/generalisation-plan.md G9.
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
