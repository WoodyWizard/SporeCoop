---
id: BUG-1474
title: Adventure: shared enemies (the host's enemies mirrored on the guest, off-by-default adventure.shared_npcs; RE first)
status: open
kind: feature
stage: Adventure
reported: 2026-11-23
source: plan
role: 
versions: 0.7.17
fixed_in:
branch: feat/w83-advnpc
commits:
tests:
signature:
docs:
related: BUG-0793, BUG-1288
---

## Report
Each game plays its own copy of an adventure (docs/re/adventure.md#adventure-two-captains): the enemies are each game's own. RE first (the adventure's creature list and act spawns, whether the Creature NPC proxy path works in kScenario), then host-authoritative enemies behind `adventure.shared_npcs`, test coop_adventure_tutorial.py act 1: docs/plan-0.7.17.md item 6. Scope per owner question 6.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 6 (WP-A1), branch feat/w83-advnpc.
