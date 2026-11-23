---
id: BUG-1470
title: Own stage history per player: consequence cards, trait progress and Space archetype are each player's own (ideas from PR #207 and #217 by IltonBJSilva)
status: open
kind: feature
stage: Cell, Creature, Tribe, Civ, Space
reported: 2026-11-23
source: plan, PR #207, PR #217
role: 
versions: 0.7.17
fixed_in:
branch: feat/w70-history
commits:
tests:
signature:
docs:
related: 
---

## Report
Each player's consequence cards (Cell diet, Creature, Tribe, Civ, Space archetype) and the trait progress behind them are its own; one player's choices never change another's. Design (hook cPlayer::AddTraitProgress 0xC75840 inside an Actor scope, mods/coop/actor_scope.h of WP-0), wire (session 1.3, MSG 75 PLAYER_HISTORY), store record `history`, switches and tests: docs/plan-0.7.17.md item 3. Credit IltonBJSilva (PR #207, #217).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 3 (WP-H1), branch feat/w70-history.
