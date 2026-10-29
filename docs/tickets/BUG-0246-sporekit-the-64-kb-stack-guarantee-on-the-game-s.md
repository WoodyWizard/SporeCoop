---
id: BUG-0246
title: SporeKit: the 64 KB stack guarantee on the game's main thread makes its usable stack smaller, and no test covers it
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #180 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #180: https://github.com/WoodyWizard/SporeCoop/issues/180 (the full analysis, links and the scenario are there).

**Summary**
The BUG-0056 fix calls `SetThreadStackGuarantee` with 64 KB on the game's main thread at boot. On Windows that keeps the last 64 KB of the main thread's stack for exception handling after an overflow, so the game itself can use 64 KB less stack than before 0.7.3.0: a deep recursion in the game that fitted before could now end in a stack overflow. The kit_crash stack-overflow test overflows a separate thread, so neither the guarantee nor a main-thread overflow report is tested. Severity: PLAUSIBLE, low (no crash seen; depends on the exe's stack reserve and on whether Wine honours the guarantee). Introduced by BUG-0056 (commit 1bebf57) in release 0.7.3.0.

**Where**
- the call: https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/sporekit/diag.cp

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
