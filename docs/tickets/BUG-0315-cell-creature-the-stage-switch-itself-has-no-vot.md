---
id: BUG-0315
title: Cell -> Creature: the stage switch itself has no vote (split from BUG-0178 item 1)
status: open
kind: feature
stage: Cell
reported: 2026-10-30
source: BUG-0178
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0178
---

## Report
Split from BUG-0178 item 1 (docs/known-issues.md): the host's click on the History's check starts the
Creature stage at once and the guests follow; only the first creature editor waits for the vote
(docs/09 9.3 wants a vote for the switch itself). A feature, not a bug.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
