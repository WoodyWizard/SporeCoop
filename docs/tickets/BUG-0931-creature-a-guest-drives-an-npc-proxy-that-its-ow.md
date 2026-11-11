---
id: BUG-0931
title: Creature: a guest drives an NPC proxy that its own game removed, for up to a second (29 caught faults in coop.dll game::teleport when the baby_game event started)
status: open
kind: crash
stage: Creature
reported: 2026-11-11
source: fix/w47-final12 lab 20261111-071518 long_creature (lane 3), the guest
role: guest
versions: 0.7.x
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
- 2026-11-11: opened.
