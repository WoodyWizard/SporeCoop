---
id: BUG-0898
title: Tests: frames_loop fails 'F9 opened the co-op window' on warm games: an earlier test's report question left the guest's window open
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: 0.7.12 integration run 20261108-170205 lane 5
role: 
versions: 0.7.12 (rel/0.7.12, unreleased; the test)
fixed_in: 0.7.12
branch: fix/w47-b2
commits: 8fa8f163
tests: frames_loop
signature:
docs: docs/06-testing.md#testing-sreport-leave
related: BUG-0770, BUG-0767
---

## Report
The 0.7.12 integration run (lab 20261108-170205, lane 5) failed frames_loop: "FAIL guest: F9 opened the co-op window"
after a warm reset (warm run 1 of these games). Every other check of the test passed.

## Root cause
Observed (the run's logs): the test before it on the same games was report_shot, whose host clicks "Report a problem":
since 0.7.12 F4 that asks every guest, and the guest's game opened its co-op window by itself with the question
(guest log 17:21:43 "Alice asks for your problem report ... (F9)", "ui: co-op dialog ready"). Nobody answered.
twoinst.warm_reset made both games leave and the host host again, but left the window open (and, BUG-0770, the
guest's question state too). frames_loop then pressed F9 (`key 0x78`) on the open window: F9 toggles, so it closed
it and `coop.uistate` said open=0. Not a player bug: F9 worked (it closed the open window); the F1 name step
(coop_ui.cpp kStepName) is not involved (the window state, not the page, is checked). The stale question after a
Leave was BUG-0770 (fixed on the same branch).

## Fix
Tests only: `twoinst.warm_reset` closes the co-op window on both games after the leave (`coop.uistate 0`), as a cold
start has it; coop_frames_loop.py closes it itself before F9, checks it was closed, that F9 opens it and that a
second F9 closes it.
No mod change (a test fix): no switch; the FAIL run is the integration run before the change.

## Verification
* FAIL: lab 20261108-170205 (rel/0.7.12 out/lab, lane 5: report_shot then frames_loop warm).
* PASS: lab 20261110-184452 (report_shot then frames_loop warm on lane 4, the same order).

## History
- 2026-11-10: opened.
- 2026-11-10: cause found (an earlier test's report question left the window open), test fixed on fix/w47-b2.
- 2026-11-11: released in 0.7.12.
