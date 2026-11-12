---
id: BUG-0953
title: Creature: at a guest's death in a hunt the host's monitor saw the guest's puppet at 2/15 health while the guest's game had 0/15 (long_creature, seen once)
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: fix/w48-freed lab 20261112-013858 long_creature (lane 7)
role: host
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
long_creature on fix/w48-freed (lab 20261112-013858, lane 7, 40 min, events death/editor/menu/restart/rejoin): all
checks passed, no faults; the lab failed the test on one invariant monitor line 2380 s in, right after the rejoin
event and around the guest's death in a hunt (guest.died=1): `puppet_hp host: host shows guest's creature at 2/15,
guest's own game 0/15`. Not analysed (not in the L1 package); the monitor rule and the health relay (BUG-0414,
creature_hp) are the places to look. Not known whether 0.7.12 shows it.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
