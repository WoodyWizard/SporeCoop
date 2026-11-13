---
id: BUG-1036
title: Space: the session ended on an ally's mirror planet, then a menu and another session: the old partners' mirror colonies come back
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-5
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-allies-end-pending
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
