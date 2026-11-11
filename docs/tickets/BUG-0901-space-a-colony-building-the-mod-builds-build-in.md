---
id: BUG-0901
title: Space: a colony building the mod builds (build_in_city: a partner's planner building) may not be kept when the planet is left and made again
status: open
kind: desync
stage: Space
reported: 2026-11-10
source: fix/w47-b2 (BUG-0766 test, 2026-11-10)
role: 
versions: 0.7.11 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0751, BUG-0766, BUG-0884
---

## Report
Found while testing BUG-0766 (fix/w47-b2, lane 4, `space_empire`, planet 00460035). When a planet becomes the active
planet again the game makes its colony cities again from what it keeps for the planet: a house destroyed at run time
(`coop.civ unbuild`, cCity::DestroyBuilding) was back after the host's ship left the planet and landed again (lab
20261110-185810). Not checked yet: whether a building the mod makes with build_in_city (the host's copy of a guest's
planner building, BUG-0751; the guests' copies of the host's, BUG-0884; a kept one, BUG-0766) is in that data, i.e.
still there after its game's ship leaves and lands again, and in the next save. If not, the host's world loses a
guest's colony building as soon as the host flies away (the guest's own planner purchase is the game's own path and
presumably kept).

Reproduce: coop_space_planner_build.py (guest buys, host on the planet: the host has 6 houses), then the host takes off
and lands again: count the houses (`coop.civ buildings s<i>`) and compare their positions with the guest's; the test's
own freed plot comes back too, so free the plot with the planner's own demolish (real input) instead of `coop.civ
unbuild`, or find the planet's city data (planet record) and read it.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened.
