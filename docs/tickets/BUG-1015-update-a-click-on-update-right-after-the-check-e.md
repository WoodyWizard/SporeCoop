---
id: BUG-1015
title: Update: a click on Update right after the check ends is ignored (g_busy still set), and Try again shows the last download's progress
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-11
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: menu_diag
signature:
docs: docs/known-issues.md#issue-update-retry-progress, docs/06-testing.md#testing-p2-ui
related:
switch: core.update_busy
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
autoupdate.cpp: set_final() sets a thread's last state and clears g_busy under g_mu together; end_thread no
longer clears g_busy (a new thread may own it already); apply_work resets g_got / g_total before Downloading. Test seams
`coop.update url <u>`, `testdelay <ms>` (a thread waits after its final state: the old window widened), `applydelay
<ms>`; `coop.update` prints busy= and the window's line while downloading. Switch `coop.fix core.update_busy off`.

## Verification
coop_menu_diag.py: a local server offers the next version with a pack that is not one. (a) apply within the
check thread's 3 s after "available"; (b) after the failure, Try again with the download held 3 s: the line must start
at 0.
* FAIL (SPORE_FIX_OFF=core.update_busy): out/lab/20261113-032710 (a: stayed "available"; b: "Downloading ... 100 %");
  out/lab/20261113-032307-2 (a; b failed for a's reason, the test then waited for the thread: 66f43dd7)
* PASS: out/lab/20261113-032115, out/lab/20261113-032833 ("0 KB")

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
- 2026-11-14: released in 0.7.14.
