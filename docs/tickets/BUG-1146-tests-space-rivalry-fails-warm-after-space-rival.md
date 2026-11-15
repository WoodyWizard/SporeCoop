---
id: BUG-1146
title: Tests: space_rivalry fails warm after space_rivalry (the rivalry hit counters of the run before are not reset by the warm reset)
status: open
kind: test
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`lab.py run space_rivalry,space_trade --in-order --repeat 2` on fix/w51-space2 (out/lab/20261115-042847, lane 6):
space_rivalry#1 (cold) PASS, space_rivalry#2 (warm after it) FAIL "neutral players cannot hurt each other" and "a
lethal PvP hit destroys the guest's ship". The guest's `coop.space rivalry` line printed before the test's first shot
already says hits_got=43 dmg_got=1500.0 pvp_deaths=1 (the PvP phase of the run before); the check wants hits_got == 0.
Inferred: the rivalry counters live in the game process and the warm reset does not clear them (a cold start does);
the test compares absolute counters. Fix idea: compare with the values at the test's start, or clear the counters in
the warm reset. Not a mod regression; not fixed here (outside BUG-1139).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
