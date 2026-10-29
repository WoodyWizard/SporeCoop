---
id: BUG-0225
title: "is this still the same galaxy?" is decided by a heap address and a global save counter (allies mirror reuse, refunds paid into the wrong game)
status: open
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #159 (review of release 0.7.3.0, 5801eea)
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
From GitHub issue #159: https://github.com/WoodyWizard/SporeCoop/issues/159 (the full analysis, links and the scenario are there).

Two Space fixes of release 0.7.3.0 decide whether the loaded game is still the one they knew by comparing the player's cEmpire pointer:
- the allies mirror reset after a reload (BUG-0038, d0f6003 and f581bf2);
- the owed trade refunds (BUG-0058, fd814d3), which also count every game save of any stage.
A new cEmpire at the old address hides a reload, and any save in between makes a refund payable even into a game that still has the items or into another game. Severity LOW: the fixes are incomplete, not worse than before them.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
