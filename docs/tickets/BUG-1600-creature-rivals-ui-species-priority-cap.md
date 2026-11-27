---
id: BUG-1600
title: Creature Rivals: F9 Rivals species list balloons to 30+ pages of uninteracted wild NPCs
status: fixed
kind: bug
stage: Creature
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 225ccb98
tests: rivals_creature, make test
signature: Creature Rivals F9 species list shows dozens of uninteracted wild species pages
switch: none (UI species list sorting and row cap in creature_rivals.cpp)
docs:
related:
---

## Report
In Creature Rivals, F9 -> Rivals listed every wild species on the planet indiscriminately, creating 30+ pages and burying allied, hostile, and interacted species.

## Root cause
`creature_rivals.cpp` emitted all wild species entries without sorting by interaction status or capping the list length.

## Fix
Updated `mods/coop/creature_rivals.cpp` to sort allied, hostile, and interacted species first and cap the UI rows to prevent 30-page NPC bloat.

## Verification
- PASS run: `out/lab/20261127-145438` (`rivals_creature`, PASS) and `make test`.
- no-fail-run: UI sorting and pagination cap verified in `rivals_creature` and live Creature Rivals playtest.
