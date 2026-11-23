---
id: BUG-1473
title: Difficulty for 3-4 players: a session setting scales the NPCs' challenge (all.party_difficulty; RE of the difficulty tunings first)
status: open
kind: feature
stage: Creature, Tribe, Civ, Space
reported: 2026-11-23
source: plan
role: host
versions: 0.7.17
fixed_in:
branch: feat/w72-difficulty
commits:
tests:
signature:
docs:
related: 
---

## Report
With 3-4 players the NPCs fall too fast. RE first: which DifficultyTuning keys each stage reads for NPC health, NPC damage, raids and epics; the host answers them multiplied by the setting's factor. Setting values, switch `core.party_tuning` and the 3-game test: docs/plan-0.7.17.md item 5.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 5 (WP-D1), branch feat/w72-difficulty.
