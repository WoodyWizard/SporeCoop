---
id: BUG-0884
title: Space: a building the host buys in a colony's city planner (or builds for a guest) never appears in the other guests' games
status: open
kind: desync
stage: Space
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: both
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0751, BUG-0766, BUG-0189
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0751. Severity medium, confidence medium.
