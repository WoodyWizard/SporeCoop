---
id: BUG-0359
title: Creature -> Tribe, 4 players: host heap corruption found freeing a plant's spatial-grid node (GeneralAllocator_FreeInternal+0x64 from 0x702C44)
status: open
kind: crash
stage: Creature
reported: 2026-10-31
source: lab 20261031-185843 campaign#3
role: host
versions: 0.7.3.1
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
- 2026-10-31: opened.
