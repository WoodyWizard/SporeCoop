---
id: BUG-0531
title: Tribe: a member stalled in the gather node (0x0569236E, state 1, walk goal an old tree 100 m off) does not act on a new order
status: open
kind: bug
stage: Tribe
reported: 2026-11-04
source: split from BUG-0505 (fix/w41-tribe-orders, 2026-11-04)
role: guest
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
Seen in play_tribe (out/lab/20261104-030728 #3 in the fix/w41-tribe-orders worktree): member #29 has the guest's new order record (walk, then hunt the epic, target set) but stays in the gather node 0x0569236E, state 1; its walk goal is still the old tree 100 m off and it does not move. A new order does not take a member out of a stalled gather. Inferred, not shown: host orders suffer the same. Details: docs/re/tribe.md#tribe-orders. Target: 0.7.6 or a 0.7.5.x.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
