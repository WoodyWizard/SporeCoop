---
id: BUG-0248
title: SporeKit: every crash report names the report-writing thread instead of the thread that crashed, so a main-thread crash is never marked as one
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #182 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #182: https://github.com/WoodyWizard/SporeCoop/issues/182 (the full analysis, links and the scenario are there).

**Summary**
The full crash report is written by a thread of its own, and its exception line prints that thread's id (`GetCurrentThreadId()`), not the id of the thread that crashed. The crashed thread's id is stored (`CrashJob::tid`) and given to the minidump, but never printed. So every unhandled-crash report names a thread that did not crash, and the marker ", the game's main thread" never appears. The report written when the game exits seconds after a fault names the exiting thread, which may not be the one that faulted. Severity: minor (diagnostics: crash triage and tickets get a wrong thread). The report thread came with commit 2bb91eb (release 0.7.2.1); BUG-0056 (commit 1bebf57, release 0.7.3.0) added the stack-overflow report path with the same mistake.

**Where**
- the exception lin

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
