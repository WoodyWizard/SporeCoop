---
id: BUG-0491
title: guest crash in Civ at cLayoutSlot::RemoveObject+0x13 through the mod's city planner hook (CityPlanner_AddBuilding_Ret) while the host's cities are attacked, 0.7.3.6
status: fixed
kind: crash
stage: Civ
reported: 2026-11-03
source: player report SporeCoop-report-20261103-194448.zip (owner downloaded 2026-11-03; 0.7.3.6, Steam guest, Civ), out/reports/2026-11-03
role: guest
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-civ-crashes
commits: 6db6dcbd, 9a9262ab
tests: civ_planner
signature: 3010a413:SporeApp.exe Simulator::cLayoutSlot::RemoveObject+0x13
docs: docs/re/civ.md#civ-planner-lifetime
related: BUG-0493, BUG-0492, BUG-0426, BUG-0469
---

## Report
Guest (Steam 3010a413), Civ, 19:40:32, 1191 s into the session: 0xC0000005 at 0x107A033 Simulator::cLayoutSlot::RemoveObject+0x13 (read 0x43CC4CA6, a garbage pointer), chain ... < SporeCoop::CityPlanner_AddBuilding_Ret+0x28F < App::cMessageManager::MessageSend. Just before: a partner's city under attack, civ(guest) mirrors=45 created=250 removed=201, builds_in=31. Inferred: a building the mod adds or removes in a city layout (build_in_city / the 0.7.3.6 building_model_ok path BUG-0469, or the BUG-0325 building sync) leaves a stale slot object. Check first whether 0.7.3.5 had the same signature (tickets, lab), then the 0.7.3.6 building code.
Target: 0.7.4 (first: these are crashes on the current release).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (disassembly, GOG 2024 = Steam code): the chain's frames 0xD0FE76 / 0xD12AFE / 0xD1442B are the
  city planner (cCommunityEditor): its input handler 0xD12910 calls the pick-up 0xD0FCC0, which takes the
  layout slot nearest to the building (0xAFAAF0), sets the slot index to -1 when that slot's occupant is
  not the building, and then calls cLayoutSlot::RemoveObject (and SetObject) on `slots + (-1)*0x18`, the
  0x18 bytes before the slot vector: a float of heap memory (0x43CC4CA6) read as the occupant. The name
  `SporeCoop::CityPlanner_AddBuilding_Ret+0x28F` in the chain is only the nearest symbol (0xD1442B is
  inside the planner's HandleMessage 0xD130D0); the mod's AddBuilding hook is not on the stack.
* Observed (the report): `busy: paused` (the planner pauses the guest's game), the partner's city under
  attack, `buildings_fixed=+36/-64` growing during the minute before: the guest's reconciliation built and
  destroyed buildings of its cities while the host's world went on.
* Observed (lab, `civ_planner` with `civ.planner_hold` off): a building the host makes in the city reaches
  the guest's game while its planner edits that city (the city's own building list changes under the
  planner).
* Observed (code): `build_in_city` with no free slot left the building it had made in the world, in no
  slot and no city list, at the host's spot over another building's slot: a building in no slot of its
  own, which is exactly the -1 case of the pick-up.
* Inferred: the building the guest picked up was one the mod had put into or taken out of that city's
  slots while the planner was open (or such an orphan). Not reproduced through a real mouse pick: the
  civ_npc planner tiles have no designs (BUG-0189), and a direct call of the pick-up on a building out
  of its slot returned 2 before the slot step in the lab.
* 0.7.3.5 had the same reconciliation and planner code (not 0.7.3.6's BUG-0469 building check: it only
  skips a build); no earlier report with this signature (`tickets.py find RemoveObject`).

## Fix
* `civ.planner_hold` (BUG-0493, shared): while the local planner edits a city the mod changes nothing of
  it (no reconciliation, host building, ruin or capture for that city; the host holds a guest's build for
  the city its own planner edits) and removes no vehicle the planner moves; the next snapshots after the
  planner bring the city up to date.
* `civ.planner_pick`: the planner's pick-up of a building that is not in its nearest slot is refused (the
  game's "nothing picked", 2) instead of writing before the slot list.
* `civ.build_orphan`: a building made for a city with no free slot is destroyed, not left in the world.
* Symbols (GOG 2024): `SporeCoop::CommunityEditor_Update` 0xD124E0, `SporeCoop::CommunityEditor_PickUp`
  0xD0FCC0 (GOG 2016: off, frozen_off_922af7c7.txt).

## Verification
* FAIL (`SPORE_FIX_OFF=civ.planner_hold`): `out/lab/20261103-131021`: the host's building is built into the city under
  the guest's open planner.
* PASS: `out/lab/20261103-125816` (civ_planner#1, #2; repeat 2): the city's buildings stay while the planner
  is open, the guest has the host's buildings 2 s after the planner; the pick-up guard lets every building
  in its slot be picked (`pick_ok=1` for the city hall, a house and a factory: the guard compares the
  game's own fields).
* Not verified: a real mouse pick-up of a building out of its slot (`civ.planner_pick` FAIL side) and a
  building dropped from the palette (the moved-objects map is empty in the test).

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-civ-crashes (planner hold, pick-up guard, no orphan buildings).
- 2026-11-03: released in 0.7.4.
