---
id: BUG-0839
title: game::world_epoch() counts only Creature worlds: Tribe users of the epoch cannot tell two Tribe worlds apart
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0119 BUG-0427 BUG-0384
---

## Report
No player report. Code audit finding outside Creature: anything that uses game::world_epoch() in Tribe (the deferred destroy's world token, the nest egg mirrors in Tribe) sees one world across a Tribe load or a rejoin's world.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): game.cpp:1393-1409 world_epoch increments only when the mode is Creature (0x1654C01) or the editors after it; in Tribe it stays the last value. destroy_deferred_tick's WorldToken (game.cpp:1519-1528, avatar + epoch; the avatar is null in Tribe) therefore cannot drop a queued pointer of an older Tribe world; nest.cpp egg mirrors use the epoch in Tribe too. stage_epoch() (stage_epoch.h) exists per stage. Confidence: high on the code, impact not measured (whoever owns tribe.cpp should check its users).

## Fix
Not fixed yet.

## Fix idea
Use game::stage_epoch(mode) for the world token and in nest.cpp, or make world_epoch count every stage.

## How to test
Tribe: queue a deferred destroy during a cinematic, load the world again before it ends; the queued pointer must be dropped as 'another world' (fix) vs destroyed in the new world (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
