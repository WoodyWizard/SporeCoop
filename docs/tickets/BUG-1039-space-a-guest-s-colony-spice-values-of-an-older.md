---
id: BUG-1039
title: Space: a guest's colony spice values of an older world (or of Rivals) are taken as a collection after a world load in the session
status: open
kind: desync
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-14
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-colony-spice-old-world
related: BUG-0822, BUG-0840
---

## Report
Code audit for 0.7.14, finding SPC-14. Not seen at runtime, found by code reading.
The guest's last-known spice of each shared colony (space_planets g_gc) is kept for the whole session. After the guest loads another world in the session (a rejoin without a session end, the host loading another save of the same galaxy) or after a Rivals period, the first host K_SPICE makes the guest compare its new world's values with the old ones; lower values count as "this game collected" and K_SPICE_TAKEN writes them into the host's colonies: the host's colony spice drops to the older world's amounts.

## Root cause
Observed in code (main efd2e0ba), confidence medium (the paths are certain, how often the values differ by more than 0.5 is inferred):
* space_planets.cpp:253 g_gc, cleared only in on_session_end (:362-366); no world_epoch tag.
* space_planets.cpp:306 guest_state calls guest_check(now, true) before writing the host's values; guest_check (:257-289) sends K_SPICE_TAKEN for any city whose value is 0.5 below g.last.
* active() (:129-132) is false in Rivals, so g_gc is frozen at the values before Rivals.
Audit detail: the guest sends its current values, not necessarily 0.

## Fix idea
Tag g_gc with spaceext::world_epoch() (and the Rivals switch): another epoch or a Rivals period clears it, so the first K_SPICE after it only writes. Switch: `coop.fix space.colony_spice_epoch off`.

## How to test
Extend coop_space_colony_spice.py: sync once, the host lets the colony grow, the guest reloads the host's world in the session (share again) so its values are older and lower, then the first K_SPICE; assert the host's game colony value did not drop. Main: FAIL.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
