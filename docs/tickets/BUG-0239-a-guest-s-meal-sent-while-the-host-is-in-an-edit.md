---
id: BUG-0239
title: a guest's meal sent while the host is in an editor (or has no cell) is never acknowledged, so the guest's food total drops by it later
status: open
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #173 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #173: https://github.com/WoodyWizard/SporeCoop/issues/173 (the full analysis, links and the scenario are there).

BUG-0051 (#29, commit e7ab004, release 0.7.3.0) lets a guest count its own meals as "on the way" until the host echoes their number in a food total. The host only echoes meals it processed. It ignores the guest's MSG_CELL_EAT / MSG_CELL_FOOD entirely while it is outside the Cell stage (an editor) or has no avatar. Such a meal stays "on the way" on the guest, so the guest's total stays above the host's. When the guest's next meal is acknowledged, the guest drops by the whole missing amount at once, which is the BUG-0051 symptom (a level down and up when a level line lies in between). The net loss is the same as before 0.7.3.0; it only comes later and at an unexpected moment. Severity LOW.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
