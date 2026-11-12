---
id: BUG-0875
title: Civ: a capture that crosses the shared-capital threshold deals every city out round-robin and takes the captured city from its captor
status: fixed
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 
fixed_in: unreleased
branch: fix/w48-captures
commits: 70334b34, 0f46393a, 3ec69627
tests: civ_capture_keeps
signature:
docs: docs/re/civ.md#civ-capture-keeps
related: BUG-0775, BUG-0593, BUG-0648
---

## Report
Reported by the Tribe / Civ / Space audit while explaining the Discord report of 2026-11-09 ("their settlement is captured, the host's is not, and in reverse"). With 2 players and 1 city, the host captures a city and a tick later the city belongs to the guest: the captor sees "this city belongs to X" and cannot build or buy there. With 3-4 players every capture or loss across the threshold reshuffles all cities.

## Root cause
* Observed (lab 20261112-004217, civ.capture_keeps off; and lab 20261112-003012, the first fix without the pending
  captor): the host's capture of an NPC city on civ_npc (2 players, 1 city: the shared capital) became the guest's
  city (owner 1 on both games) a tick later; the guest's game let the guest buy there.
* Two causes, both in host_assign_cities: (1) the resplit at `cities < players` deals every city but the gifts out in
  turns (`players[1]` gets the second city); (2) Capture changes the owner only when its cinematic ends (8 s), so the
  drop loop ("cities the player lost") erased the captor h_Capture had just written: every capture was dealt like a
  new city. docs/re/civ.md#civ-capture-keeps.

Earlier reading (2026-11-10):
Read in the code (main e49e1e23), confidence high for the logic, not run:
* `mods/coop/civ.cpp:2310-2312`: `shared = cities.size() < players.size()`; when that differs from the capital's current owner state (kShared) a resplit is set.
* The loop at `civ.cpp:2314-2321` then writes `g_city_owner[...] = players[(1 + k++) % n]` for every city except the gifted ones (`g_city_gifted`), overwriting the owner `h_Capture` (`civ.cpp:1691-1704`) just set from the nearest vehicle.

## Fix idea
Keep a set `g_city_captured` like `g_city_gifted` (a city a player captured keeps its captor); at a resplit change only the capital, or only cities nobody captured or was given. Switch e.g. `civ.capture_keeps`.

## How to test
Two players, the civilization has 1 city (the shared capital). `coop.civ capture <idx>` on the host near the host's vehicles, then `coop.civ` on both games: the new city's owner must be the host (0) and stay so for 10 s; on main it becomes 1. Also check what the guest's game shows (the owner tag, the build menu refusing).

## Fix
civ.cpp: h_Capture keeps the captor in `g_capture_by` until the city is the player civilization's (120 s at most) and
marks it captured (`g_city_captured`); host_assign_cities gives a waiting captor its city, keeps a captured or given
city with its owner at a resplit while that player is here, deals the free cities to players without a city first,
and keeps the capital shared while a player would have none. Switch `coop.fix civ.capture_keeps off` (0.7.12).

## Verification
`coop_civ_capture_keeps.py` (suite `civ_capture_keeps`, civ_npc): the host's capture through the game's capture
meter (`coop.civ siege <city> 0 150 pc`); for 12 s on both games the city is the player civilization's and the host's
(owner 0), the guest's game refuses the guest a purchase there, the capital stays shared.
* FAIL with `SPORE_FIX_OFF=civ.capture_keeps`: lab 20261112-004217 (owner 1 on both games, the guest's purchase
  requested).
* PASS: lab 20261112-003405, 20261112-004830 (lane 4).

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: a second candidate for BUG-0648's host report (0.7.8: the guest's captures do not count); the resplit code is from 74144ecf (2026-10-21), so it was in 0.7.8. Linked.
- 2026-11-12 (fix/w48-captures): fixed, FAIL and PASS runs in Verification.
