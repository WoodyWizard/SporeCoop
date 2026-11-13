---
id: BUG-1014
title: tests: sreport status never says round=writing (start_write moves g_round out before g_writing is set)
status: fixed
kind: test
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-10
role: host
versions: 
fixed_in: unreleased
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: text_lines
signature:
docs: docs/06-testing.md#testing-p2-ui
related:
switch: core.sreport_writing
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-10. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
`coop.sreport status` prints round=none while the host writes the session zip: the "writing" value can never be
printed. Tests that wait for the write or tell writing from no round see "none" in both cases. No player effect
(the window line uses round_running(), which checks g_writing).

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/sreport.cpp:273 `j->r = std::move(g_round)` (g_round null)
before :278 `g_writing = true`; :705 `g_round ? (g_writing ? "writing" : "asking") : "none"`.

## Fix idea
Status: `g_writing ? "writing" : g_round ? "asking" : "none"`. Check coop_session_report.py:107 and coop_sreport_leave.py,
which read round=none as "the round is over": they may need "none or writing" handling. No switch (test output).

## How to test
coop_session_report.py: poll the status right after the last answer arrives with a write delay (`g_write_delay_ms`, the test
hook): expect round=writing once: FAIL on main, PASS with the fix.

## Fix
sreport.cpp status(): `g_writing ? "writing" : g_round ? "asking" : "none"`. coop_session_report.py and
coop_sreport_leave.py wait for round=none and are not affected. `coop.sreport writedelay <ms>` now also waits once
before the first byte (a zip under 1 MB waited nothing). Switch `coop.fix core.sreport_writing off`.

## Verification
coop_text_lines.py: the guest sends its report, the host writes with writedelay 3000; the polled status must say
round=writing once.
* FAIL (SPORE_FIX_OFF=...,core.sreport_writing): out/lab/20261113-032307 (seen: asking, none)
* PASS: out/lab/20261113-032115-2, out/lab/20261113-032426 (seen: asking, writing); sreport_leave (writedelay's
  users): see History

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
