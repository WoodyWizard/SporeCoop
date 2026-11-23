---
id: BUG-1527
title: Creature: a guest's kill raises the extinction count by one, then it goes back to zero
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.15.5
fixed_in:
branch: fix/w82-creature
commits:
tests:
signature:
docs:
related: BUG-1434
---

## Report
Player report on Discord 2026-11-22 (coop 0.7.15.5, Steam; out/research/dc-report-20261122-world-switch.md section 5, the guest's own marks after the world switch): mark 5: "when I kill a creature (guest) the extinction count goes up one, then back to zero". No matching ticket found with `tickets.py find`. Next: fix-player-report (reproduce with a test that fails first).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-C1), branch fix/w82-creature.
