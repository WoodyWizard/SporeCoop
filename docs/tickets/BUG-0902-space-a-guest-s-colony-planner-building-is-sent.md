---
id: BUG-0902
title: Space: a guest's colony planner building is sent to the host at AddBuilding, before the planner kept it (a failed drag may build a house on the host)
status: fixed
kind: desync
stage: Space
reported: 2026-11-10
source: fix/w47-b2 (BUG-0884 test, 2026-11-10)
role: guest
versions: 0.7.11.1 and earlier (since BUG-0751)
fixed_in: 0.7.12
branch: fix/w47-colony
commits: 1cfd4d5f
tests: space_planner_miss
signature:
docs: docs/re/space.md#space-city-data
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
* Observed (lab 20261110-201036, the code before the fix, lane 4, `space_empire`): the guest dropped the house tile of
  its colony's planner on a taken plot (real input); the planner added the building (civ.cpp h_AddBuilding with the
  planner's return address ran: `sent=1`) and took it away again (the guest's city 5 -> 5 houses, no money taken), yet
  the host's city went 5 -> 6 houses (`built=1`): an unpaid house only the host had.
* Observed (code): h_AddBuilding called send_space_build at AddBuilding, before the planner kept the building; the
  host's relay had the same pattern until 3946ad47 (BUG-0884).
* The Civ stage's guest send (CMD_BUILD, BUG-0189) is the same pattern in Civ (h_AddBuilding sends at AddBuilding):
  not run, not changed here (Civ has its own planner hold and reconciliation of the host's buildings, which
  presumably removes such a house again on the guest; the host's game would still build it). Left for a ticket of
  its own if seen.

## Fix
civ.cpp: in Space the guest's own planner building goes into the same queue as the host's (space_note_host_build
with `planner`): space_tick sends CMD_SPACE_BUILD to the host only once the planner has closed (or after 10 min) with
the building still in the city, with its placed position. A building the planner took away again is not sent (counter
`space_planner: dropped=` in `coop.civ`, log "was taken away again: not sent (BUG-0902)"). Twin switch
`coop.fix civ.space_planner_closed off` (sent at AddBuilding, 0.7.11).

## Verification
`coop_space_planner_build.py --miss` (suite `space_planner_miss`): the guest opens its colony's planner (real
clicks), drags the house tile onto a taken plot; the guest's planner must have added the building (`sent` or
`dropped` went up), the guest keeps 5 houses, and the host's city must stay at 5 houses, the shared money unchanged.
* FAIL on the code before the fix: lab 20261110-201036 (the host 5 -> 6 houses).
* FAIL with `SPORE_FIX_OFF=civ.space_planner_closed`: lab 20261110-201757 (the host 5 -> 6 houses).
* PASS: lab 20261110-201407 (`dropped=1` on the guest, the host 5 -> 5).
* PASS again in lab 20261110-203201 (lanes 4 / 5), with the area's tests: space_planner_build (a kept purchase still
  reaches the host after the planner's close), space_planner_away, space_city_keep, space_sculpt_tool, space_sculpt_away.

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-colony (the guest's send waits for the planner's close; switch civ.space_planner_closed; test space_planner_miss).
- 2026-11-11: released in 0.7.12.
