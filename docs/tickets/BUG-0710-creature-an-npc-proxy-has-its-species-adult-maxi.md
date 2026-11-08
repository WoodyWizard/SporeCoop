---
id: BUG-0710
title: Creature: an NPC proxy has its species' adult maximum health on the guest, not the host's (a baby: 5 on the host, 10 on the guest; NpcSpawn.max_health never applied)
status: open
kind: bug
stage: Creature
reported: 2026-11-07
source: K2a detector run 20261107-231824 (Creature:max_hp, 20 of 23)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
