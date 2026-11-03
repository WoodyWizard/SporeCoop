---
id: BUG-0491
title: guest crash in Civ at cLayoutSlot::RemoveObject+0x13 through the mod's city planner hook (CityPlanner_AddBuilding_Ret) while the host's cities are attacked, 0.7.3.6
status: open
kind: crash
stage: Civ
reported: 2026-11-03
source: player report SporeCoop-report-20261103-194448.zip (owner downloaded 2026-11-03; 0.7.3.6, Steam guest, Civ), out/reports/2026-11-03
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Guest (Steam 3010a413), Civ, 19:40:32, 1191 s into the session: 0xC0000005 at 0x107A033 Simulator::cLayoutSlot::RemoveObject+0x13 (read 0x43CC4CA6, a garbage pointer), chain ... < SporeCoop::CityPlanner_AddBuilding_Ret+0x28F < App::cMessageManager::MessageSend. Just before: a partner's city under attack, civ(guest) mirrors=45 created=250 removed=201, builds_in=31. Inferred: a building the mod adds or removes in a city layout (build_in_city / the 0.7.3.6 building_model_ok path BUG-0469, or the BUG-0325 building sync) leaves a stale slot object. Check first whether 0.7.3.5 had the same signature (tickets, lab), then the 0.7.3.6 building code.
Target: 0.7.4 (first: these are crashes on the current release).
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
