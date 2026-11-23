---
id: BUG-1533
title: lifecycle: the events kPlayerJoin / kPlayerLeave are declared and listed by coop.lifecycle but never fired (a LIFECYCLE_HOOK on them is never called)
status: open
kind: tooling
stage: All
reported: 2026-11-23
source: feat/w69-progress code read 2026-11-23
role: host,guest
versions: 0.7.16.1
fixed_in:
branch: fix/w85-cell-pool
commits:
tests: lifecycle_player, lifecycle_player_off, core_tests lifecycle_hooks
signature:
docs: docs/01-architecture.md, docs/06-testing.md#testing-lifecycle-player
switch: core.player_events
related:
---

## Report
Found while writing own progress (feat/w69-progress, BUG-1469): a `LIFECYCLE_HOOK(..., lifecycle::kPlayerJoin |
lifecycle::kPlayerLeave, ...)` was never called. lifecycle.h declares the events (Ctx.player, event_slot, the
`coop.lifecycle` listing shows J / L), but only kSessionEnd (session_glue.cpp end_session) and kWorldChange (coop.cpp
world tick) are ever passed to `lifecycle::fire`. The join / leave work of every module is called by hand from
session_glue.cpp's `cb.player_join` / leave callbacks. Reproduce: grep `lifecycle::fire` (two callers) or register a
hook on kPlayerJoin and join a guest: its `calls=0` in `coop.lifecycle`.

## Root cause
Observed (code): the C2 migration of 0.7.15 (docs/01-architecture.md, step 3 "the player callbacks fire kPlayerJoin /
kPlayerLeave") was never done: lifecycle::fire had two callers (end_session, world_id_tick), and lifecycle::pump_game
(the queue of needs_game hooks fired inside g_sess.update()) had none. The registry, its order and its queue were
correct and unit-tested; only the firing was missing.

## Fix
* (feat/w69-progress) Not fixed there (not part of WP-P1): own_progress.cpp used hand calls from session_glue.cpp.
* fix/w85-cell-pool, `coop.fix core.player_events` (session_glue.cpp): the player callbacks fire kPlayerJoin /
  kPlayerLeave through `lifecycle::fire_player(fix, event, id)` (lifecycle.h; inside g_sess.update(): game_ok false)
  after player_life::forget_all, the leave for every leaver (also one without a Remote, before the early return);
  coop.cpp frame_receive calls lifecycle::pump_game() right after g_sess.update() (game calls allowed there), so a
  needs_game hook of these events runs in the same frame. Moved onto hooks: own_progress.player (join, leave; order 0)
  and revive.player (leave; order 10), both mod state only (no game call, not in player_life, independent of the
  other modules' order); their `lifecycle-exempt` marks go. The other modules' hand calls stay (each has an order
  written next to it: settings first, species after creations, ...; moving them is the architecture plan's step 3).
  Off: no event fired, the hand calls of 0.7.16.1 in their old places.

## Verification
Open, waiting for the lab run (pending night run: lifecycle_player, lifecycle_player_off):
`SPORE_DETECTOR=20 python3 tools/lab.py run lifecycle_player --lane N --retry 0 --no-build` must PASS; twin
`SPORE_DETECTOR=20 python3 tools/lab.py run lifecycle_player_off --lane N --retry 0 --no-build` must FAIL (join=0,
leave=0). Unit: core_tests lifecycle_hooks (fire_player fires both events with the player id, queues a needs_game hook
until pump_game, fires nothing with the switch off) passes in make test.

## History
- 2026-11-23: opened.
- 2026-11-23: fix/w85-cell-pool: the events fired from the player callbacks (core.player_events), own_progress and revive on hooks; waiting for the night run.
