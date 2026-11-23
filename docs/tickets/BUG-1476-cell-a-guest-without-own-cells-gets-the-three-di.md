---
id: BUG-1476
title: Cell: a guest without own cells gets the three diet premades in the "My cell" picker (#210 small part by IltonBJSilva)
status: open
kind: feature
stage: Cell
reported: 2026-11-23
source: plan, PR #210
role: guest
versions: 0.7.17
fixed_in:
branch: feat/w81-cell
commits:
tests:
signature:
docs:
related: 
---

## Report
A guest with no cells in My Creations gets the three diet premades (E219E5D2 / 252A132F / 0C176D89) in the picker; take #210's RE (diet flags of 44 cell models, Cell_GetConsequenceTrait 0xE5C9D0). Test: an empty test profile picks a carnivore premade, both games show it. docs/plan-0.7.17.md item 9. Credit IltonBJSilva (PR #210).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 9 (WP-L1), branch feat/w81-cell.
