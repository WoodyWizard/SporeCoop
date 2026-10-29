---
id: BUG-0250
title: Tools: Ctrl+C on lab.py during the exclusive tests (ui, update) leaves the test driving its lane and the games running
status: open
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #184 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #184: https://github.com/WoodyWizard/SporeCoop/issues/184 (the full analysis, links and the scenario are there).

**Summary**
Since BUG-0052, `lab.py` starts every test in its own session so that a timeout can kill the whole process group. As a side effect, the terminal's Ctrl+C no longer reaches the test. Only the parallel phase catches `KeyboardInterrupt` and kills the running test groups. If Ctrl+C comes during the exclusive tests (`ui` in smoke, `update`), the lab exits and the test keeps driving its lane. The final `spore kill` of the run's lanes is skipped on any interrupt. Severity: low (developer tooling). An orphaned `update` test goes on wiping and installing the mod on its lane unseen, and a later run on that lane races with it.

**Where**
- The test in its own session (BUG-0052): https://github.com/WoodyWizard/SporeCoop/blob/5801eea9688f134472f260facd5001ec395ec859/tools/lab.py#L139-L156
-

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
