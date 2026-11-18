---
id: BUG-1255
title: Creature: the guest's first own creature from the evolution editor asks to replace the host's species
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1222, BUG-1240
---

## Report
A players' video: the guest accepts the evolution, switches to its own creature, and on the check mark gets the game's dialog to replace the host's species with its own (it had to pick "save as a new object"). BUG-1222 (0.7.15) covers guests on their own creature; the first pick of an own creature through the evolution of the host's world may still show the dialog.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
