---
id: BUG-1471
title: Creature: a guest's kills and social wins that land on the host's game raise the host's trait progress (cPlayer::AddTraitProgress), not the guest's
status: open
kind: bug
stage: Creature
reported: 2026-11-23
source: plan, PR #207 review
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: feat/w70-history
commits:
tests:
signature:
docs:
related: 
---

## Report
Found in the review of PR #207 (its RE note on the trait progress): a guest's hit on a host NPC lands as the host's hit (npc.cpp) and a guest's social win is played on the host's game (social.cpp); the game then credits the host's cPlayer (+0x1278 / +0x127C) through AddTraitProgress 0xC75840 (ret 8). The guest's combat / social progress does not move, the host's does: the host's Creature card follows the guest's play. Fix with the Actor scope (docs/plan-0.7.17.md item 3).

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
