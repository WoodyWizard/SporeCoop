---
id: BUG-1531
title: Civ -> Space: check that a guest of a shared nation gets its own spaceship editor after the vote (PR #219's question)
status: open
kind: bug
stage: Civ, Space
reported: 2026-11-23
source: plan, PR #219
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w68-wallet
commits:
tests:
signature:
docs:
related: 
---

## Report
PR #219's question: does a shared-nation guest get its own spaceship editor at Civ -> Space? Lab check on rel/0.7.17 in WP-M1's Civ -> Space segment; if not, #219's recipe (level 4 + GameCiv_ShowPlanetWon + PlanetWonPause on the guest's game after the vote). Closes as not-reproduced when the editor opens.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-M1), branch feat/w68-wallet.
