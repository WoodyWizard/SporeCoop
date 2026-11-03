---
id: BUG-0494
title: Civ: roads between cities (inter_city_road) built on the host are missing on the guest
status: open
kind: desync
stage: Civ
reported: 2026-11-03
source: split from BUG-0478 (detector 2026-11-03)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0478
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
