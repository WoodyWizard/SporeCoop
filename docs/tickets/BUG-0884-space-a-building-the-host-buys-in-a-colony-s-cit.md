---
id: BUG-0884
title: Space: a building the host buys in a colony's city planner (or builds for a guest) never appears in the other guests' games
status: fixed
kind: desync
stage: Space
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in: 0.7.12
branch: fix/w47-b2
commits: 8b6a60dd aca22a9c 3946ad47
tests: space_planner_host
signature:
docs: docs/re/space.md#space-colony-build-hold
related: BUG-0751, BUG-0766, BUG-0189, BUG-0901
---

## Report
In Space the host buys a house in a colony's planner: the money goes down for everyone, but the guests' colony city does not get the house. With 3-4 players, a building guest A buys is built on the host (BUG-0751) but never on guest B. The colony cities differ until a rejoin share.

Severity: medium. Confidence: medium.

## Root cause
* Observed (code): mods/coop/civ.cpp:1444-1447 h_AddBuilding sends only the guest's Space planner building (send_space_build), then `if (!active() || g_applying || !b) return r;`: active() is Civ only, so the host's broadcast_building (civ.cpp:1453) never runs in Space.
* Observed (code): civ.cpp:4043-4070 host_space_build builds through build_in_city with g_build_from set; it goes through h_AddBuilding, which returns at :1447, so nothing is relayed to the other guests.
* Inferred (medium): no other path carries Space colony buildings host -> guest (no worldobj or realign kind for Space city buildings found).

## Fix idea
Host in Space: on a planner AddBuilding (its own, or the one built for a guest), broadcast a Space build {place, noun, city position, position} to every guest except the origin; a guest on that planet builds it into the city near that position (build_in_city, not paid), the mirror of host_space_build. Twin switch e.g. `civ.space_planner_relay`.

## How to test
Extend coop_space_planner_build.py: host and guest on the home planet 00460035 with a plot freed in both games; the host drags a house in its planner; assert the guest's city (`coop.civ buildings s<i>`, the game's city list) goes 5 -> 6 houses. With 3 players: guest A buys, assert guest B's city has it. FAIL now, PASS with the fix.

## Fix
In Space the host now relays a colony building it gets: its own planner building (h_AddBuilding with the planner's
return address; the planner places it a frame later, so `space_tick` sends it once its position is set, or after 3 s,
with that position; since 3946ad47 only once the planner has closed with the building still in the city: a drop that
missed the plot adds the building and takes it away again, and the guest got a house the host did not have, lab
20261110-193142) and the one it builds for a guest (host_space_build, or a kept one of BUG-0766), as CMD_SPACE_BUILD
{planet, noun, city position, position} host -> every guest except the buyer. A guest on that planet (5 s after it
came) builds it into the city near that position (build_in_city, not paid, `g_applying` so it is not sent back);
otherwise it keeps it until its ship is there (the same hold as BUG-0766). An older guest drops the message (a guest
ignores MSG_CIV_CMD outside Civ). Counters `relayed= relay_in=`. Twin switch `coop.fix civ.space_planner_relay off`
(0.7.11: the host's game and the buyer's only).

## Verification
`coop_space_planner_build.py --buyer host` (suite `space_planner_host`): the host buys a house in its colony's
planner with real input (the city flown to, clicked, the house tile dragged onto a free plot); the guest's city must
get it.
* FAIL with `SPORE_FIX_OFF=civ.space_planner_relay`: lab 20261110-190114 (the guest's city 5 -> 5 houses).
* PASS: lab 20261110-184846 (5 -> 6 houses, the money -1,600 on both), 20261110-191918; with the planner-closed send
  20261110-194600 and 20261110-193940 #2 (its #1: the host's drop missed the plot, and the guest correctly got no
  house: the check "nobody gets a house the buyer's planner did not keep"; the test now drops once more).
* Not run: three players (guest A buys, guest B gets it): the path is the same send with the buyer excluded.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0751. Severity medium, confidence medium.
- 2026-11-10: fixed on fix/w47-b2 (the host relays its Space colony buildings; test space_planner_host).
- 2026-11-10: the relay waits for the planner to close with the building (3946ad47); BUG-0902 opened for the guest side.
- 2026-11-11: released in 0.7.12.
- 2026-11-12: listed in the 0.7.13 package S1; already fixed in 0.7.12. space_planner_host passes again on fix/w48-space (lab 20261112-041012). The untested case named in Verification was not run.
