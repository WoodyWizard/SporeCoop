---
id: BUG-1141
title: C2: one world identity (world_id.h) and ordered lifecycle observers (LIFECYCLE_HOOK)
status: fixed
kind: tooling
stage: All
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-ccore, feat/w51-core2
commits: 423016ca, 8a24f582, 8e9aa765, c8725107, 10cb551e, beebd81e, a0e87040, d84fdf0e, bec12b3f
tests: tests/core_tests.cpp, refactor_dump, lifecycle_reset, lifecycle_reset_cell, stage_switch, tribe_host_reload_reset, tribe_editor_world, civ_reload, nest_mirror_epoch, wsync_transitions, editor_sync_host, away_editor, space_trade_reload, space_allies_reload, space_down_reload, tribe_bounce_mirrors, campaign_ct
signature:
docs: docs/01-architecture.md#arch-world-id, docs/01-architecture.md#arch-c2c3-wiring
related: BUG-0427, BUG-0217, BUG-0223, BUG-0119, BUG-1055, BUG-1142
---

## Report
Plan item C2 of docs/plan-0.7.15.md (audit STG-3, CORE-3; owner decision 2): three definitions of "the world changed" (coop.cpp avatar_change_check, tribe / civ world_check, lifecycle_world_tick) plus game::world_epoch and spaceext::world_epoch, and the lifecycle fan-out done by hand lists in end_session, avatar_change_check and the player callbacks.

## Root cause
Structure (umbrella BUG-0427: one verified world-identity service).

## Fix
Portable core (feat/w51-ccore): mods/coop/world_id.h (`world_id::Tracker`, `Id{stage, serial, mode, save}`: a world change is only another stage, the stage entered again (stage_epoch), another known save or another root object; an editor visit keeps the world, decision 2) and the observers in mods/coop/lifecycle.h (`LIFECYCLE_HOOK(name, events, order, fn, needs_game)`, `fire(ctx, game_ok)` with the LIFECYCLE_STATE resets at order 0, needs_game hooks queued for `pump_game()` outside g_sess.update(), re-entrant fires queued, a throwing hook counted, `set_runner` for a fault guard). Migration, proposed orders and switches (`core.world_id`, `<module>.world_hooks`): docs/01-architecture.md#arch-world-id.

The wiring (feat/w51-core2, docs/01-architecture.md#arch-c2c3-wiring):
* coop.cpp `world_id_tick` (frame_observe, after the stage counters and the world name) steps one `world_id::tracker()`
  (mode, stage counter, the planet file: a real `<planet>.spo` only; anchors `tribe::world_anchor`, `civ::world_anchor`,
  `spaceext::world_anchor`) and fires `lifecycle::fire(kWorldChange)`; hooks: puppets -100, npc -90, nest -88,
  fields -86, wobj -84 (a world that was open), tribe -50 / civ -44 (their stage's world), WORLD_STATE at 0.
* Deleted: `tribe::world_check` / `civ::world_check` and their `stage_epoch::Watch`, `lifecycle_world_tick`, space_ext's
  own epoch counter (`spaceext::world_epoch` is the tracker's last Space serial; its owed-refund ledger reads the
  tracker's open Space world), end_session's hand list (one `fire(kSessionEnd)` with 23 ordered hooks, the 0.7.14
  order), avatar_change_check's world resets. The migration switches `core.world_id` / `core.session_hooks` were
  deleted in the same branch (no new permanent switch); one switch for all modules instead of `<module>.world_hooks`
  (they were deleted together).
* Decision 2: the local editor visit no longer resets npc / nest / fields / wobj (the puppets are still forgotten as
  frame_drive does without a local entity); avatar_change_check only covers a local creature replaced in the same
  world (BUG-0833) and a teardown before the mode changes (BUG-0091 / BUG-0308).
* Space joins lifecycle.h: the spacecoop.session hook runs the Space features (`spaceext::session_end_all`; the
  features' functions are exempted in the ratchet with that reason), the Space world is the tracker's.
* WORLD_STATE now also resets when a world closes (menu, load) and on an in-stage save / anchor change; world.host_reload
  (BUG-0332) checked: set in phase 6 after the frame's change, the planet-only save crc keeps a new game's first save
  ("snapshot.spo" -> "<planet>.spo") from being a world change (tribe_host_reload_reset PASS).
* `coop.lifecycle` lists the hooks, `coop.lifecycle hooks` (calls, faults), `coop.lifecycle world` (the tracker).
* Ratchet "unregistered lifecycle functions" 51 -> 21.
Not done (not in this package): the player join / leave callbacks as kPlayerJoin / kPlayerLeave hooks (step 3 of the
migration); `game::world_epoch` (BUG-0119) stays its own counter (it also counts an in-stage avatar replacement,
nest_mirror_epoch relies on it); the decision-2 proxies test (a host editor visit keeping the guests' NPC proxies):
the guests' npc away edge still despawns them while the host is in an editor (npc.cpp, the C1 agent's file).

## Verification
Unit tests tests/core_tests.cpp (world_ids, lifecycle_hooks: order, needs_game queue, re-entrancy, a throwing hook, a faulting runner, the event mask); each fails on a mutation (editor keep, nested fire, catch).
Refactoring proof (plan rule): refactor_dump before 20261115-154735 (rel/0.7.15 9362bcdd), with the switches on
20261115-161010, after the deletion 20261115-164230: players, guards, SporeKit hooks identical; fix list +6 C3 switches
(and the two C2 switches only in the middle run); lifecycle: +1 entry (core.host_busy) and the 30 observers in their
order; commands: the coop.lifecycle help line; msgtable / manifest: MSG_HOST_BUSY (BUG-1142). The middle and the final
dump: lifecycle, commands, msgtable identical, fix list -core.world_id -core.session_hooks only.
World-change lab tests on the final code (the switch-on path, old paths deleted): lab 20261115-164230 (lane 9: refactor_dump,
lifecycle_reset, wsync_transitions, tribe_editor_world, tribe_host_reload_reset, civ_reload, nest_mirror_epoch,
tribe_host_pause, tribe_gameover_wait, cell_away, cell_pause PASS; world_reload FAIL, a test defect: BUG-1171) and
20261115-164240 (lane 10: lifecycle_reset_cell, stage_switch, editor_sync_host, away_editor, space_trade_reload,
space_allies_reload, space_down_reload, tribe_bounce_mirrors, civ_away, civ_away_buy, tribe_away PASS). Reruns
20261115-174822: world_reload PASS (its test fixed, BUG-1171), away_stages PASS (its first run failed on an invariant
monitor line during the test's own forced menu state). editor_strays (BUG-1170) and evolve_world (BUG-1173) FAIL the
same way on rel/0.7.15 9362bcdd without this branch (baseline worktree, lab 20261115-180220): not from C2. The C3 runs
20261115-161010 ran the switch-on build. campaign_ct (Cell -> Creature -> Tribe in one run) PASS 20261115-174830;
play_creature, play_tribe --quick PASS 20261115-181512, play_civ, play_space --quick PASS 20261115-182444.
no-fail-run: refactoring (no behaviour to break on purpose); the proof is the dumps and the PASS runs above.

## History
- 2026-11-15: opened.
- 2026-11-15 (feat/w51-ccore): the portable core and its unit tests; no stage file changed. Open until the stage adoptions land.
- 2026-11-15 (feat/w51-core2): wired into the mod, the old detectors and hand lists deleted; fixed (unreleased).
- 2026-11-18: released in 0.7.15.
