---
id: BUG-1036
title: Space: the session ended on an ally's mirror planet, then a menu and another session: the old partners' mirror colonies come back
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-5
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-space2
commits: daac928c, 8d73e8a9
tests: space_allies_end_pending
signature:
docs: docs/re/space.md#space-restore-window, docs/known-issues.md#issue-allies-end-pending
related: BUG-0038, BUG-0197
---

## Report
Code audit for 0.7.14, finding SPC-5. Not seen at runtime, found by code reading. Already named as not changed in BUG-0038's fix (its trigger 5).
The session ends while the player is on a mirror planet of an ally (space_allies), the player goes to the menu (or loads a save that is not Space) and then joins or hosts another session in Space: the mirror colonies of the old session's partners (frozen allies) are put into the new galaxy.

## Root cause
Observed in code (main efd2e0ba), confidence high for the state, medium for what the stale mirrors do in the new galaxy:
* space_allies.cpp:1119-1130 end_all: on a mirror planet it only sets g_end_pending.
* space_allies.cpp:1028-1042 tick: outside Space it forgets the applied state and returns before the g_end_pending block (:1043-1053), so g_m and g_watch are not cleared.
* Back in Space connected (a new session): :1050-1051 clears g_end_pending because it is connected; sync_from_rosters (:989-1018) marks the old names offline ("a frozen ally until the session ends") and apply_all applies them.
Audit detail: the early return is at :1041, not :1018.

## Fix idea
end_all's pending part runs outside Space too (forget the mirrors without unapplying the gone galaxy), or the session end that leaves g_end_pending clears g_m / g_watch as soon as the player leaves the mirror planet or Space; a new session start (on_session_start or the first connected tick) runs end_all first if g_end_pending. Switch: `coop.fix space.allies_end_pending off`.

## How to test
Extend coop_space_allies_reload.py: the host mirrors a guest's colony (`allies fake`), lands on the mirror planet, the session ends (guest leaves, host leaves), the host goes to the menu, loads the fixture and hosts again with another guest; assert the game's empire map has no mirror empire of the old partner (coop.space allies status and the cEmpire list). Main: FAIL.

## Fix
space_allies.cpp tick: outside Space, with a pending end and no mirror applied any more (the galaxy went), the old
mirrors are forgotten (`forget_all`, `end_pending_done` in `allies status`); back in Space connected with a pending
end, `end_all` runs (it waits again while the ship is on a mirror planet) instead of dropping it. Switch `coop.fix
space.allies_end_pending off` (0.7.13).

## Verification
`coop_space_allies_end_pending.py` (suite `space_allies_end_pending`, fixture space, ~1.2 min): the guest's fake mirror
"Zed" on the free planet that is its ship's active one after a jump, a landing on it, coop.leave (end_pending=1),
coop.menu, a new join. Asserts the guest game's star record owner (`allies star`).
* FAIL (SPORE_FIX_OFF=space.allies_end_pending): out/lab/20261113-023145 (the star owner is "Zed's empire" again in
  the new session's galaxy)
* PASS: out/lab/20261113-023145-2 (owner '-', end_pending_done=1)

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space2 (the pending end finishes outside Space; a new session ends the old mirrors first).
