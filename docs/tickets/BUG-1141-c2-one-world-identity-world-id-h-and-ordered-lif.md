---
id: BUG-1141
title: C2: one world identity (world_id.h) and ordered lifecycle observers (LIFECYCLE_HOOK)
status: open
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch: feat/w51-ccore
commits: 423016ca
tests: tests/core_tests.cpp
signature:
docs: docs/01-architecture.md#arch-world-id
related: BUG-0427, BUG-0217, BUG-0223, BUG-0119, BUG-1055, BUG-1142
---

## Report
Plan item C2 of docs/plan-0.7.15.md (audit STG-3, CORE-3; owner decision 2): three definitions of "the world changed" (coop.cpp avatar_change_check, tribe / civ world_check, lifecycle_world_tick) plus game::world_epoch and spaceext::world_epoch, and the lifecycle fan-out done by hand lists in end_session, avatar_change_check and the player callbacks.

## Root cause
Structure (umbrella BUG-0427: one verified world-identity service).

## Fix
Portable core (feat/w51-ccore): mods/coop/world_id.h (`world_id::Tracker`, `Id{stage, serial, mode, save}`: a world change is only another stage, the stage entered again (stage_epoch), another known save or another root object; an editor visit keeps the world, decision 2) and the observers in mods/coop/lifecycle.h (`LIFECYCLE_HOOK(name, events, order, fn, needs_game)`, `fire(ctx, game_ok)` with the LIFECYCLE_STATE resets at order 0, needs_game hooks queued for `pump_game()` outside g_sess.update(), re-entrant fires queued, a throwing hook counted, `set_runner` for a fault guard). Migration, proposed orders and switches (`core.world_id`, `<module>.world_hooks`): docs/01-architecture.md#arch-world-id. Stays open until the detectors and hand lists are deleted.

## Verification
Unit tests tests/core_tests.cpp (world_ids, lifecycle_hooks: order, needs_game queue, re-entrancy, a throwing hook, a faulting runner, the event mask); each fails on a mutation (editor keep, nested fire, catch). Lab proof with the adoption.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
