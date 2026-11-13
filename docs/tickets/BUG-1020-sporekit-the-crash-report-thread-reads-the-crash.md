---
id: BUG-1020
title: SporeKit: the crash report thread reads the crashed thread's EXCEPTION_POINTERS after the filter gave up waiting
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-2
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
Audit 0.7.14, KIT-2. Not seen at runtime, found by code reading. When the full crash report takes longer than
kReportWaitMs (15 s: a slow disk, a lock the crashed thread holds, a large minidump), the unhandled exception filter
returns while the report thread goes on reading the EXCEPTION_POINTERS it was given. Those point into the crashed
thread's exception frame. If the next filter or a handler lets that thread go on (or the stack is reused while the
process is torn down), the report and the minidump get garbage registers / a wrong exception, or the report thread
faults itself and the full report is lost (the short one stays). A player can hit it only on a crash whose report is
slow: rare.

## Root cause
Observed in code (main efd2e0ba), the effect inferred:
* sporekit/diag.cpp:893-915 (crash_filter): `job.ep = ep` (the raw pointer the system passed), the report thread made
  with &job, `WaitForSingleObject(t, kReportWaitMs)`, on timeout a log line and `return g_next_filter(ep)` /
  EXCEPTION_CONTINUE_SEARCH.
* diag.cpp:801-835 (crash_report_thread) reads j->ep in diag_write_report_as (registers, the stack walk from
  ContextRecord->Ebp / Esp) and in write_minidump after that.
* The stack-overflow and off-stack paths do the same with g_so_job.ep (diag.cpp:840-890), waiting the same 15 s.
* exit_report (diag.cpp:745-768) already works on copies (exceptions_last into a local EXCEPTION_RECORD / CONTEXT).
Audit detail: the frame is not "unwound" by the filter's return in the common case (CONTINUE_SEARCH from the
top-level filter ends the process); the dangling read matters when a handler after us continues the thread, and on
the teardown itself.

## Fix idea
Copy the EXCEPTION_RECORD and CONTEXT into the static CrashJob (and g_so_job) before the thread starts and point
job.ep at the copies, as exit_report does (MiniDumpWriteDump with ClientPointers FALSE takes our own copy). The stack
memory the walk reads is still the crashed thread's; that part stays best effort. Switch: `kit.fix crashcopy`.

## How to test
`crash.test` with a report step made slow on purpose (a test-only `kit.test.slowreport <ms>` that sleeps in the report
thread beyond kReportWaitMs) and a next filter that continues the thread is hard to arrange in the game; a host unit
test is not possible (Windows SEH). Practical: a lab test `coop_crash_report_slow.py` on one lane: `kit.test.slowreport
20000` then `crash.test thread`; assert the written report's exception line names the code and EIP crash.test used.
Main: the report is written from the dangling pointer (wrong or missing exception section, seen as a FAIL only when
the stack was reused); expect a weak FAIL, so record `no-fail-run` if it cannot be made to fail.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
