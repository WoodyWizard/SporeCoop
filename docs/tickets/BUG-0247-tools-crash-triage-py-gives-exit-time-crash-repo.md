---
id: BUG-0247
title: Tools: crash_triage.py gives exit-time crash reports a different signature per report, and finds no signature in sporekit.log or in a short crash file
status: open
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #181 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #181: https://github.com/WoodyWizard/SporeCoop/issues/181 (the full analysis, links and the scenario are there).

**Summary**
`tools/crash_triage.py` is new in 0.7.3.0 (BUG-0158, commit ee9b778). It turns a crash report into a signature `<exe hash>:<where>` and looks for earlier tickets. Three cases go wrong:
- When the report was written on the game's main thread, `where` keeps the text `(thread N, the game's main thread)`, so every such report gets its own signature. Today these are the reports of a game that ended right after a fault (`exit_report`). #182 will make every main-thread crash report look like this.
- In `sporekit.log`, the `the game crashed: ...` line is found only when it is the last line of the file.
- The short crash file that SporeKit writes first, before the full report, is not recognised: no signature, and no exe hash.

Severity: minor (triage tool). One crash splits into several

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
