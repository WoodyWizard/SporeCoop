---
id: BUG-1011
title: diag: coop-last-session.txt is opened with fopen on a UTF-8 path, lost when the game folder has non-ASCII letters (Windows)
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-7
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-summary-non-ascii-path
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-7. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
When the game folder (SporebinEP1\mods\logs) has letters outside the ANSI code page (on Windows; e.g. a Steam library
in a folder with a Cyrillic or CJK name), coop-last-session*.txt is never written or read: the last session's summary
line is missing from the top of the next problem report. Other report files use wide paths and work.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/diag_stats.cpp:301-306 summary_file() builds the path from
H->logs_dir() (UTF-8: sporekit/diag.cpp:319 narrow() = CP_UTF8, sporekit/log.cpp:228-236); :322 fopen(f, "w") and
:515 fopen(f, "r") take the narrow path in the ANSI code page. sreport.cpp uses widen() + _wfopen / CreateFileW (correct).
Inferred: under Wine with a UTF-8 code page it may work.

## Fix idea
_wfopen(widen(f)) (a widen helper as in sreport.cpp:42). No switch (a plain I/O fix); or `diag.summary_wide`.

## How to test
A lane whose mods dir path contains a non-ASCII folder is not available; a unit test is not possible without the game.
Test: tools/tests coop with SPORE_LANE pointing at a copy under a path with "Тест" (lane create to such a path), end a
session, assert coop-last-session.txt exists: FAIL on main, PASS with the fix. Or no-fail-run with a code review if
the lane path cannot be made.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
