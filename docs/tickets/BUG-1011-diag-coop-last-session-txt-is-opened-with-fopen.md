---
id: BUG-1011
title: diag: coop-last-session.txt is opened with fopen on a UTF-8 path, lost when the game folder has non-ASCII letters (Windows)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-7
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: menu_diag
signature:
docs: docs/known-issues.md#issue-summary-non-ascii-path, docs/06-testing.md#testing-p2-ui
related:
switch: diag.summary_wide
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
diag_stats.cpp: the summary file is opened through summary_open(): _wfopen with the UTF-8 path widened
(MultiByteToWideChar CP_UTF8), as sreport.cpp. Test seams `coop.diag summarysub <folder>|-` (the file into a sub folder
of mods\logs) and `coop.diag summarywrite` (write and read back now). Switch `coop.fix diag.summary_wide off` (fopen).

## Verification
coop_menu_diag.py (suite `menu_diag`): the summary into mods\logs\Тест, the file must be on disk and read back.
Under Wine (ACP 1252) fopen with the UTF-8 path wrote nothing: the bug reproduces without a non-ASCII lane.
* FAIL (SPORE_FIX_OFF=diag.summary_wide): out/lab/20261113-032307-2, out/lab/20261113-032710 ("read back 0 bytes", no file)
* PASS: out/lab/20261113-032115, out/lab/20261113-032833

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-ui (0.7.14 P2).
