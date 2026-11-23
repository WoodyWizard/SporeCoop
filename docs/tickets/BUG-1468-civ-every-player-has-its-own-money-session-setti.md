---
id: BUG-1468
title: Civ: every player has its own money (session setting civ.own_money; idea and RE from PR #205 by IltonBJSilva)
status: open
kind: feature
stage: Civ
reported: 2026-11-23
source: plan, PR #205
role: 
versions: 0.7.17
fixed_in:
branch: feat/w68-wallet
commits:
tests:
signature:
docs:
related: BUG-1236, BUG-1461
---

## Report
Owner decision 2026-11-20 (a friend's request): each player has its own Civ money: its cities' income is its own, its purchases are paid from it, a newcomer gets a fair part, nothing is made or lost. Design, wire (civ 1.2, MSG 74 CIV_WALLET), store record `civ.wallet`, switches and tests: docs/plan-0.7.17.md item 1. Credit IltonBJSilva (PR #205) in the CHANGELOG and release notes.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 1 (WP-M1), branch feat/w68-wallet.
