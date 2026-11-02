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
