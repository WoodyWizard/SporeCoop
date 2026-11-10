---
id: BUG-0875
title: Civ: a capture that crosses the shared-capital threshold deals every city out round-robin and takes the captured city from its captor
status: open
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0775, BUG-0593
---

## Report
Reported by the Tribe / Civ / Space audit while explaining the Discord report of 2026-11-09 ("their settlement is captured, the host's is not, and in reverse"). With 2 players and 1 city, the host captures a city and a tick later the city belongs to the guest: the captor sees "this city belongs to X" and cannot build or buy there. With 3-4 players every capture or loss across the threshold reshuffles all cities.

## Root cause
Read in the code (main e49e1e23), confidence high for the logic, not run:
* `mods/coop/civ.cpp:2310-2312`: `shared = cities.size() < players.size()`; when that differs from the capital's current owner state (kShared) a resplit is set.
* The loop at `civ.cpp:2314-2321` then writes `g_city_owner[...] = players[(1 + k++) % n]` for every city except the gifted ones (`g_city_gifted`), overwriting the owner `h_Capture` (`civ.cpp:1691-1704`) just set from the nearest vehicle.

## Fix idea
Keep a set `g_city_captured` like `g_city_gifted` (a city a player captured keeps its captor); at a resplit change only the capital, or only cities nobody captured or was given. Switch e.g. `civ.capture_keeps`.

## How to test
Two players, the civilization has 1 city (the shared capital). `coop.civ capture <idx>` on the host near the host's vehicles, then `coop.civ` on both games: the new city's owner must be the host (0) and stay so for 10 s; on main it becomes 1. Also check what the guest's game shows (the owner tag, the build menu refusing).

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
