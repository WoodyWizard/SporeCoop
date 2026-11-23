---
id: BUG-1472
title: Creature: a partner revives a downed player; the cost is the downed player's own (session setting creature.revive; idea from PR #214 by IltonBJSilva)
status: open
kind: feature
stage: Creature
reported: 2026-11-23
source: plan, PR #214
role: 
versions: 0.7.17
fixed_in:
branch: feat/w71-revive
commits:
tests:
signature:
docs:
related: BUG-1173
---

## Report
A lethal hit while a partner is in the world downs the creature (1 hp, held, 60 s); a partner within 6 m for 1.5 s revives it at 40 %; otherwise it dies the game's way. Cost charged to the downed player (own wallet, own progress debt, or the shared pool). Wire (creature 1.3, MSG 77 RESCUE), the debt in the `progress` record, switches and tests: docs/plan-0.7.17.md item 4. Credit IltonBJSilva (PR #214).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 4 (WP-R1), branch feat/w71-revive.
