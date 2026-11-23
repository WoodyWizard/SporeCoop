---
id: BUG-1538
title: Cell: a player's DNA can go negative after the first Cell editor update
status: open
kind: bug
stage: Cell
reported: 2026-11-23
source: discord
role: all
versions: 0.7.16
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/cell.md
related: BUG-0630, BUG-1249
---

## Report
Player report in Discord (`#russian`, 2026-11-22, no report zip attached): while playing Cell stage in co-op, after the first Cell editor update, the player's DNA counter became negative.

## Root cause
Not investigated yet (no logs attached). Likely areas to inspect: Cell DNA budget calculation (`dna_for_food - spent` vs synced food / `dna_pace` / editor spend subtraction in `cellsync.cpp`, `dna.cpp`, and `BUG-1249`'s post-editor Cell DNA path).

## Fix
Not fixed. Target: **0.7.17.1** (not 0.7.17).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened for 0.7.17.1 from Discord `#russian` report (2026-11-22).
