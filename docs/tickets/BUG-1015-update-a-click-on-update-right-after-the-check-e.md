---
id: BUG-1015
title: Update: a click on Update right after the check ends is ignored (g_busy still set), and Try again shows the last download's progress
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-11
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-update-retry-progress
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-11. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
(a) Update: the check thread sets the state to Available before it clears g_busy in end_thread; an Update click in
that window is ignored without a message (the button stays, a second click works). The window is a log call long and
the button is shown only on a later frame, so a player is very unlikely to hit it; `coop.update apply` from a test right
after `check` can. (b) Try again after a failed download shows "Downloading ... N %" of the last attempt until the
first progress callback, because g_got / g_total are not reset.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/autoupdate.cpp:97-106 the state is set under g_mu, :68-70
end_thread clears g_busy later; :235-246 start_update returns silently when g_busy.exchange(true) was true; :23 and
:129-131 g_got / g_total set only by the progress callback, never reset in apply_work / start_update.

## Fix idea
(a) Clear g_busy under g_mu together with the final state (or let start_update queue the apply when busy); (b) g_got =
g_total = 0 before set(Downloading). Switch `core.update_busy`.

## How to test
tools/tests/update_check.py (local release server): `coop.update check` and at once `coop.update apply` in the same pump,
repeated: main sometimes stays Available (FAIL, flaky by nature); a failing first download then Try again: the first
ui line after the retry must show 0 % (FAIL on main).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
