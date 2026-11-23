---
id: BUG-1530
title: npc_nest_backoff: an unmet precondition (no nest animal backs off in time) fails the test instead of giving INCONCLUSIVE
status: open
kind: test
stage: Creature
reported: 2026-11-23
source: plan
role: test
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w76-motion
commits:
tests:
signature:
docs:
related: BUG-0562, BUG-1360
---

## Report
The npc_nest_backoff test fails when its precondition is not met (no nest animal backing off in time) instead of saying INCONCLUSIVE through the test helper's inconclusive path (as civ_planner_miss). Needed for the BUG-1360 mound `place` precondition. docs/plan-0.7.17.md items 10 and 12.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-F1), branch fix/w76-motion.
