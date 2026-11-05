---
id: BUG-0427
title: class: world identity and transitions (umbrella)
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
Done: session end general (BUG-0033/0214), avatar() choke point (0264), frame_world_now (0271).
Siblings not covered: a null-local frame (host Tribe editor visit) despawns and rebuilds every NPC for guests and clears herd tables (coop.cpp avatar_change_check ~1915) (obs); Space world_epoch unverified for ship editor, outfitter, adventure: a kLoad frame can leave the ship invincible and drop owed spice (space_ext.cpp:61-91) (obs logic); several epoch rules (spaceext::world_epoch, stage_epoch, host_world); no space_ext / squadron / social / parts on_world_change; host in another stage counts neither as away nor elsewhere (coop.cpp:730-751) (obs); place probe one frame stale (coop.cpp:2345, world.cpp:784); save-safe predicate split between post and write time; queued world load can start from an adventure; leaving_stage() only in Cell modules (BUG-0308); editor settle stamps Creature only (BUG-0318).
General fix: one verified world-identity service driving every on_world_change; every queued task tagged with session/world epoch; one save-safe predicate. Outcome check: each real transition keeps the epoch and the proxies.
Source: docs/generalisation-plan.md G10.
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
