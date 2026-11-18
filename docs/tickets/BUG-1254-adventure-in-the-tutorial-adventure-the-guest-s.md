---
id: BUG-1254
title: Adventure: in the tutorial adventure the guest's puppet appears as its old Creature-stage creature at the wrong size, and the guest's game stops seconds later
status: open
kind: crash
stage: Adventure
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
related: BUG-0845
---

## Report
A players' video: both players start the tutorial adventure from the main menu. In the host's game the guest's puppet appears at the captain's feet with an [Adventure] plate: the old Creature-stage creature, the size of a rabbit. 3 s later the "is not responding (game minimized or frozen?)" plate, and the guest says its game crashed. Suggested causes: the tutorial adventure has a fixed resource key, so adventure_place() (modes.cpp) matched on both players without console commands and the mod spawned a puppet (docs/re/adventure.md assumed it never matches, BUG-0845); the kScenario adapter calls the plain creature_spawn with creations::kCreature (no captain), and the body scale sync is off outside the Creature stage; the guest's crash matches docs/re/space.md's note: cScenarioPlayMode::SetCurrentAct crashes when a mod puppet is in the scenario's object list.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
