---
id: BUG-1252
title: Creature: a hunger thought bubble over the guest's puppet in the host's game
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: host
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
A players' video: a thought bubble with a gnawed bone hangs over the guest's puppet in the host's game for a long time, while the guest's own creature is not hungry. Suggested cause: the puppet object's own hunger on the host lives its own life.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
