---
id: BUG-0215
title: Tools: tickets marked fixed in 0.7.3.0 lack the evidence CLAUDE.md asks for, and tickets.py check does not notice (empty commits, no FAIL run, wrong fixed_in)
status: open
kind: tooling
stage: All
reported: 2026-10-28
source: GitHub #149 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #149: https://github.com/WoodyWizard/SporeCoop/issues/149 (the full analysis, links and the scenario are there).

**Summary**

Of the 157 tickets with `fixed_in: 0.7.3.0`, a good number do not meet CLAUDE.md 1 and 3:
- 8 tickets have no FAIL side at all;
- 17 game-test tickets describe a FAIL and a PASS but name no lab run directory;
- 3 fixed tickets have an empty `commits:` field, and one misses a commit;
- one ticket is "fixed in 0.7.3.0" although its only commit shipped in 0.6.0;
- one not-reproduced ticket's guard is listed in the release notes as a fix;
- one new test asserts only a mod counter.

`tools/tickets.py check` passes on all of them (`212 ticket(s), 0 problem(s)`): for a fixed ticket it only requires `fixed_in` and `tests` to be non-empty. Severity: low (process and tooling). This issue lists the cases and proposes checks that catch them automatically without blocking honest tickets.



## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
