---
id: BUG-0494
title: Civ: roads between cities (inter_city_road) built on the host are missing on the guest
status: fixed
kind: desync
stage: Civ
reported: 2026-11-03
source: split from BUG-0478 (detector 2026-11-03)
role: 
versions: 
fixed_in: 0.7.5
branch: fix/w41-civ-citizens
commits: 58e10578
tests: civ_roads (tools/tests/coop_civ_roads.py)
signature:
docs: docs/re/civ.md#civ-roads-citizens
related: BUG-0478
switch: civ.roads
---

## Report
Split from BUG-0478 (its vehicle half is fixed). Detector: `Civilization:noun_only_host:inter_city_road`
in 10% of the Civ comparisons (out/detector/20261103-0236.md): the host's game has a road (noun
0x02B8A4E7, vtable 0x146CE08) between two cities, the guest's none. Not fixed: where the host's game makes
a road is not traced (input commands "CityBuildRoad" 4000 / "CityCreateTradeRoute" 0x37E95BC registered
in 0xCF47C0 are candidates, docs/re/civ.md#civ-planner-lifetime). Reproduce: play_civ with SPORE_DETECTOR=20.
Target: 0.7.4 or later (cosmetic as far as known).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: the road (noun 0x2B8A4E7) is the ground object of a trade route: cCity::AddTradeRoute (GOG 2024 0xBE1490, ret 8) pushes {partner, route object, type} on +0x55C of both cities and makes the road for type 0 (both entries share one object, runtime). The host's AI makes routes; the guest's mirrors never do and nothing replayed them.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
The host sends its route list (MSG_CIV_BUILD, city index 0xFE) on change and every 10 s; a guest adds the routes it lacks with AddTradeRoute. Removal is not replayed (RemoveTradeRoute is one-sided on a shared object; captures remove routes on both games). `coop.fix civ.roads off`.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
FAIL with SPORE_FIX_OFF=civ.roads: out/lab/20261103-235005 (host 1 road, guest 0). PASS: out/lab/20261103-234403.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-04 (fix/w41-civ-citizens): fixed (the host's trade routes are added on the guests).
- 2026-11-04: released in 0.7.5.
