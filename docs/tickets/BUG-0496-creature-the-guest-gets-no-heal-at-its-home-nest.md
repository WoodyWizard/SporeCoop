---
id: BUG-0496
title: Creature: the guest gets no heal at its home nest (split from BUG-0460)
status: open
kind: bug
stage: Creature
reported: 2026-11-03
source: player report 2026-11-02 (BUG-0460), split on fix/w40-world-objects
role: guest
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
The guest's mark in BUG-0460 (s 115): 'for me nest is not extinct and i don't get any heals from it'. The extinct half is fixed in BUG-0460 (wobj.herd); the heal at the home nest was not reproduced or analysed: which nest heals the guest's avatar (its family herd's nest, or the host's) and whether the guest's game runs that heal at all is not known. Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
