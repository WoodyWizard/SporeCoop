---
id: BUG-0902
title: Space: a guest's colony planner building is sent to the host at AddBuilding, before the planner kept it (a failed drag may build a house on the host)
status: open
kind: desync
stage: Space
reported: 2026-11-10
source: fix/w47-b2 (BUG-0884 test, 2026-11-10)
role: 
versions: 0.7.11.1 and earlier (since BUG-0751)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0751, BUG-0884
---

## Report
Seen for the host's side in lab 20261110-193142 (space_planner_host, fix/w47-b2 before its last change): the host's
drag of the house tile did not take (price $0, the host's city still 5 houses), yet the guest's city got a 6th house:
the planner's AddBuilding (return address a_PlannerAddBuildingRet) runs for a building the planner then takes away
again. The host's relay (BUG-0884) now waits until the planner closed with the building in the city. The guest's
send of BUG-0751 (civ.cpp h_AddBuilding -> send_space_build) still goes at AddBuilding: inferred, a guest's failed
drag makes the host build an unpaid house. Not run.

Reproduce: coop_space_planner_build.py with a drag that ends off the plot (e.g. on a taken plot); the host's city
must stay at 5 houses. Fix idea: the same deferred send as space_note_host_build (planner closed, building still in
the city), for the guest's CMD_SPACE_BUILD and the Civ CMD_BUILD (BUG-0189) too.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened.
