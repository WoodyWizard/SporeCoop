---
id: BUG-1002
title: Cell: per-session state survives a session end and the host's absence (queued food closures, the join-after-load avatar, the own cell's last good pose, the body factor)
status: open
kind: bug
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-10 WLD-11
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
Audit 2026-11-12 for 0.7.14, findings WLD-10 and WLD-11 (one root cause: cellsync's session end and the guest's
solo start do not reset all per-session state). Not seen at runtime, found by code reading (main efd2e0ba).
Unreset state and what it can do:
* `g_food_q` (food closures queued for the next cell step, BUG-0633): not cleared by `on_session_end()` or
  `solo_start()`; `food_drain()` does not check `connected()` / `g_solo`. A host total or a guest meal received in the
  last frame of a session is applied at the next cell step after the session ended (the guest's closure diffs against
  the just reset `g_meals` and `g_pend_food`; the host's calls `C.food_cause(from, ...)` for a player that left).
* `g_join_avatar`, `g_join_since`, `g_join_check`, `g_join_to`, `g_join_tries` (BUG-0352 join after load): kept across
  sessions; a new avatar with the same pool index as the old one (a fresh pool after a load) is not seen as new and
  the guest is not moved next to the host.
* `g_own_good_set`, `g_own_good`, `g_own_good_size/tsize/mass/imass` (BUG-0299 heal): kept across sessions and Cell
  games; a NaN in the first frame of a new avatar heals it to the old world's pose and size.
* `g_body`, `g_body_size` (BUG-0041 small respawn): kept; a session that ended while the own cell was small leaves the
  factor for the next Cell game until `symbiosis_tick` (4021) notices a resize.
For the shared "lifecycle reset" mechanism: all of these are per Cell world / per session and belong in one reset
called from `on_session_end()`, the stage leave in `tick()` (4296) and `solo_start()` (the food queue only).

## Root cause
Observed in code (mods/coop/cellsync.cpp on main): `g_food_q` 1518, `food_in_step` 1521, `food_drain` 1530 (no
session check), drained in `h_WorldScale` 2490 and `tick` 4300, cleared only when the stage is left (4298);
`solo_start` 4151 and `on_session_end` 4627-4648 do not touch it. `g_join_*` 3896-3901, reset only inside
`join_after_load` on an index change (3905). `g_own_good*` 3809-3816, set in `heal_own` 3824. `g_body` 344-345, reset
at 4021 only. Inferred (not run): the effects; each one needs a rare timing (a message in the last frame, a NaN in
the first frame, the same avatar index). Severity: low.

## Fix idea
One `reset_session_state()` in cellsync.cpp for these globals, called from `on_session_end()` and the stage-leave
block of `tick()`; `solo_start()` clears `g_food_q` (the host's totals queued before it went away) and
`food_drain()` returns when not connected. Switch: `cell.session_reset` (the general lifecycle-reset switch of the
0.7.14 package, if one is made).

## How to test
The lifecycle package's general test (every module's per-session state empty after a leave, through a
`coop.state dump`-style listing) covers the list. A twin test for the food queue: tools/tests/coop_cell_* with
`coop.pool food 50` sent by the host and the guest leaving in the same frame (a console batch), then the guest's
food after one step; main: changed after the leave, fixed: unchanged.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
