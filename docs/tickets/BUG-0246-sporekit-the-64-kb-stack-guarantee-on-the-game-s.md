---
id: BUG-0246
title: SporeKit: the 64 KB stack guarantee on the game's main thread makes its usable stack smaller, and no test covers it
status: wontfix
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #180 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 
branch: fix/w5-net
commits: e6100f9
tests: 
signature:
docs: docs/re notes: this ticket
related:
---

## Report
From GitHub issue #180: https://github.com/WoodyWizard/SporeCoop/issues/180 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0056 fix calls `SetThreadStackGuarantee` with 64 KB on the game's main thread at boot. On Windows that keeps the last 64 KB of the main thread's stack for exception handling after an overflow, so the game itself can use 64 KB less stack than before 0.7.3.0: a deep recursion in the game that fitted before could now end in a stack overflow. The kit_crash stack-overflow test overflows a separate thread, so neither the guarantee nor a main-thread overflow report is tested. Severity: PLAUSIBLE, low (no crash seen; depends on the exe's stack reserve and on whether Wine honours the guarantee). Introduced by BUG-0056 (commit 1bebf57) in release 0.7.3.0.

**Where**
- the call: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/sporekit/diag.cp

## Root cause
Measured: SizeOfStackReserve of SporeApp.exe is 1 MB (1048576, commit 4096) in both the GOG 2024 build (lane copy) and the GOG 2016 build (_ref/gog2016). The 64 KB guarantee takes 6 % of the main thread's stack. No player report or ticket shows a main-thread stack overflow (tickets.py find 0xC00000FD: only BUG-0056, a review finding).

## Fix
Kept, as step 1 of the issue allows ("measured, kept"). The boot log now prints "main thread stack: reserve 1024 KB, guarantee 64 KB" so a player log shows it. Not done: the main-thread overflow test (`crash.test stackoverflow main`): a fault in a console command on the main thread is swallowed by the game's own handler (crash.test main wrote no report on lane 6), so the test needs another place; and the Windows check of smaller guarantees.

## Verification
PE header read (python, struct at OptionalHeader+72) on both exes. Boot line built, seen in the lab logs of the regression run.

## History
- 2026-10-28: opened.
- 2026-10-28: measured and kept (wontfix) on fix/w5-net (wave 5).
