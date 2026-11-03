---
id: BUG-0495
title: Tribe and Space objects the mod creates on a guest (tribe mirrors, UFO mirrors, puppets) go into the guest's saves
status: open
kind: bug
stage: Tribe
reported: 2026-11-03
source: split from BUG-0470 (its Civ half fixed on fix/w40-civ-crashes)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0470, BUG-0020
---

## Report
Split from BUG-0470, whose Civ half (mirror vehicles) is fixed on fix/w40-civ-crashes (`civ.save_strip`,
test `civ_save_strip`). Still open: the Tribe (`SpawnNpcTribe` tribes and their members' mirrors) and Space
(UFO mirrors and puppets, the fallback puppet in the host's pack herd: modes.cpp) objects the mod creates
on a guest go into its saves. The Civ fix shows the pattern: a savestrip filter, and the lists that hold
the objects emptied of them while the save is written (savestrip::add_save_hooks), since a stripped
reference is read back as a null (BUG-0470: civ vectors faulted at 0xBF2BF2 on load). Each stage needs
its own save + reload test.
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
