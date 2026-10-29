---
id: BUG-0248
title: SporeKit: every crash report names the report-writing thread instead of the thread that crashed, so a main-thread crash is never marked as one
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #182 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-net
commits: b3bfa90
tests: tools/tests/kit_crash.py cases thread_worker, thread_exit (--old fails); SporeKit crash.test faultthread
signature:
docs: docs/02-modkit.md (kit.fix)
related:
---

## Report
From GitHub issue #182: https://github.com/WoodyWizard/SporeCoop/issues/182 (the full analysis, links and the scenario are there).

**Summary**
The full crash report is written by a thread of its own, and its exception line prints that thread's id (`GetCurrentThreadId()`), not the id of the thread that crashed. The crashed thread's id is stored (`CrashJob::tid`) and given to the minidump, but never printed. So every unhandled-crash report names a thread that did not crash, and the marker ", the game's main thread" never appears. The report written when the game exits seconds after a fault names the exiting thread, which may not be the one that faulted. Severity: minor (diagnostics: crash triage and tickets get a wrong thread). The report thread came with commit 2bb91eb (release 0.7.2.1); BUG-0056 (commit 1bebf57, release 0.7.3.0) added the stack-overflow report path with the same mistake.

**Where**
- the exception lin

## Root cause
Observed on lane 6 with kit.fix crashtid 0: the exception line named the report thread (468) while the fault logger recorded tid=464; the exit report after a worker-thread fault named the exiting main thread with ", the game's main thread".

## Fix
diag_write_report(_as) take the fault thread (CrashJob::tid, the stack-overflow tid, and g_last_tid stored with the last fault under the same lock, returned by exceptions_last). The exception line keeps its format with that id; a line "report written by thread N" follows when they differ; the exit report's minidump names the fault thread when it still exists. kit.fix crashtid. NOTE for the merge: tools/crash_triage.py REPORT_EXC matches "(thread N)" but not "(thread N, the game's main thread)", which now appears on every main-thread crash: #181 (fix/w5-tools) must land with this.

## Verification
kit_crash --case thread_worker,thread_exit on lane 6: PASS (464 = tid=464 both); --old: FAIL both (468 vs 464; 308 main vs 464). A main-thread unhandled crash could not be made from a console command (the game swallows crash.test main).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-net (wave 5).
