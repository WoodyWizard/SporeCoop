---
id: BUG-0892
title: Cell: no test checks that a guest's poison clouds hurt anything; cell_poison passes on the mod's cloud counter
status: open
kind: test
stage: Cell
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0716, BUG-0428, BUG-0715, BUG-0799, BUG-0835
---

## Report
A guest's poison part may still do no damage to the host's creatures. Its clouds are now made in the host's game, but no run has shown them taking health off a creature; the test passes as long as the host's game makes clouds at all.

Severity: medium. Confidence: high.

## Root cause
* Observed (code): tools/tests/coop_cell_parts_attack.py:108, for --part poison the only check is `puppet_clouds > 0`, a mod counter incremented at mods/coop/cellsync.cpp:753.
* Observed (code): tools/tests/coop_cell_parts_attack.py:111-112, for poison the creature's health is only printed (the electric case asserts it at :110).
* Observed (code): tools/tests/suite.py:319 says "they hurt the host's creatures", which the test does not check.
* Observed (ticket): BUG-0716 "Not verified: the clouds' damage": one run lost 2 hp, the other none; with the grafted model data even the host's own avatar's clouds hurt nothing. Only docs/known-issues.md#issue-cell-guest-poison-damage records it, no ticket.
* Inferred (medium): the graft writes only model data +0x1270..+0x1278; a real poison part carries more.

## Fix idea
Find what a real poison part carries (Ghidra on the poison step 0xE76880 and the cloud's damage path), then give the test a real poison part (a pool cell that has one, or a creation with the part) instead of the graft.

## How to test
cell_poison with a real poison part: the host creature next to the guest's clouds must lose health and die in the HOST's game (game hp and dead flag), and the guest's mirror must show the same. Must FAIL with SPORE_FIX_OFF=cell.puppet_poison and PASS with the fix; the counter must not be the pass condition.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0716. Severity medium, confidence high.
