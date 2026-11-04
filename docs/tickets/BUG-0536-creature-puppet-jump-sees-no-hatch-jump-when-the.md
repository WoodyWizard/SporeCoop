---
id: BUG-0536
title: Creature: puppet_jump sees no hatch jump when the guest plays the world's species (BUG-0345 default)
status: open
kind: test
stage: creature
reported: 2026-11-04
source: lab final 0.7.5
role: guest
versions: 0.7.5-dev
fixed_in:
branch:
commits:
tests: puppet_jump
signature:
docs:
related: BUG-0345, BUG-0376
---

## Report
With the BUG-0345 default (a guest plays the world's species in a world it never played its own creature in),
coop_puppet_jump saw no jump of either creature out of its egg after the respawn (out/lab/20261104-061516,
20261104-072946); with `creations.guest_world_species` off it passed (20261104-074723). The test now chooses the
guest's own creature (20261104-074957 PASS). Open: whether two players of one species hatch without a jump
or the puppet track misses it.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
