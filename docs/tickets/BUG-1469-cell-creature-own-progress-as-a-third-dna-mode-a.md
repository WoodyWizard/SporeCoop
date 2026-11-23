---
id: BUG-1469
title: Cell, Creature: own progress as a third DNA mode (all.shared_dna 2: own DNA and evolution bar, the stage moves on when every bar is full; idea from PR #209 by IltonBJSilva)
status: open
kind: feature
stage: Cell, Creature
reported: 2026-11-23
source: plan, PR #209
role: 
versions: 0.7.17
fixed_in:
branch: feat/w69-progress
commits:
tests:
signature:
docs:
related: BUG-1291, BUG-1269
---

## Report
Owner decision 2026-11-20: a lobby setting with three values: shared, own wallet (0.7.16's default), own progress (each earns alone, its own DNA and evolution bar; nobody evolves alone). Design, wire (creature 1.3, cell 1.2, MSG 76 PROGRESS), store record `progress`, switches and tests: docs/plan-0.7.17.md item 2. Credit IltonBJSilva (PR #209).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 2 (WP-P1, WP-P2), branch feat/w69-progress.
