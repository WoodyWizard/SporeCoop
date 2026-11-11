---
id: BUG-0817
title: Creature: the guest's mirror of the host's drama UFO is not kept out of the guest's saves
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0558 BUG-0470 BUG-0495 BUG-0020
---

## Report
No player report. A save the guest's game writes while the host's drama UFO is over the players contains the mod's mirror ship (a real type-7 UFO).

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): player_rules.cpp:1000-1011 makes the mirror with UFOAbductionEvent_SpawnUFO; no savestrip filter covers it: squadron.cpp ufo_save_start returns unless in_space(), savestrip's creature filter looks at creatures and herds only. Breaks CLAUDE.md 'our objects never go into a save'. Confidence: medium on the code; how often a guest saves then is not known.

## Fix
Not fixed yet.

## Fix idea
Add a savestrip filter (and save hooks) for g_ufo_mirrors in player_rules.cpp.

## How to test
Host `coop.drama start 2` (UFO visit), guest saves while its mirror exists; assert the save log line counts the mirror among 'objects of ours kept out of it' and a solo reload has no type-7 ship. Main: the ship is in the save.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
