---
id: BUG-0803
title: Creature: NPC nests never evolve in a guest's own world while it is connected
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
related: BUG-0668 BUG-0130 BUG-0427
---

## Report
No player report. A connected guest that plays its own Creature save sees no nest evolve; that save keeps nest evolution and extinction states that single-player would not give.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): herd_evo_guard (wsync.cpp:1113-1131) switches CreatureGame_HerdEvolveOn off for any connected guest in Creature with no world check; MSG_WSTATE is gated for a guest elsewhere (host_world.h:112), so no host value replaces it. Confidence: high.

## Fix
Not fixed yet.

## Fix idea
Switch the step off only while the guest plays the host's world (not guest_elsewhere()).

## How to test
Guest connected, loads its own Creature world, raises DNA past a nest's +0x94 threshold (debug DNA command). The herd's +0xF0 generation changes with the fix, stays on main (and changes with SPORE_FIX_OFF=wsync.herd_evo).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
