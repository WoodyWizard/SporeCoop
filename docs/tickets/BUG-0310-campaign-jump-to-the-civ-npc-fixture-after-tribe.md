---
id: BUG-0310
title: Campaign jump to the civ_npc fixture after tribe: faults while leaving tribe and in cSpaceGfx init, the host never finishes loading
status: open
kind: bug
stage: Tribe/Civ
reported: 2026-10-30
source: lab 20261030-104149 campaign#3
role: host
versions: 0.7.3.1
fixed_in:
branch:
commits:
tests: campaign
signature:
docs:
related: BUG-0284, BUG-0285, BUG-0305
---

## Report
Lab 20261030-104149 campaign#3 (main 813f8cd, lane 7): tribe did not end in 20 min (the raids left
4 other tribes; members spawned for the raids), so the host jumped to the civ_npc fixture
(`coop.load`, quit to the galaxy menu). 1.4 s after the quit: `0xC0000005 at 0x00C0DB23
CreatureBase_SetMoveScale+0x13` from a citizen's DoAction; then while loading the fixture
`0x00C70A20 cPlanet::GetRingEffect+0x90` (cSpaceGfx::InitializeInternal) and `0x010350D7
cSpaceGfx_EnableHeatEffects+0x847`; `the host did not load the civ_npc fixture within 180 s`.
The campaign's tribe sync also showed kind t (tribe members) 4.1-4.8 m off on all three guests at
every check from 11:07 (BUG-0305 saw kind n in the run before).

## Root cause
Not analysed. The leave fault is BUG-0285's family (a citizen acting while the tribe stage is left);
the load faults follow a stage left with a fault, as BUG-0284 (fixed: the stale menu player), at
other places. Inferred: load-related in part (host fps 17-27 in tribe), not only load: the faults
are in the stage change itself.

## Fix
Not fixed.

## Verification
Not reproduced on purpose yet.

## History
- 2026-10-30: opened.
