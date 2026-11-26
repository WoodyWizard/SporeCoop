---
id: BUG-1581
title: Civ Rivals: guest superweapon crash, rejoin nation swap, turret despawn and planner building sync, vehicle sell and design sync, large starting city, diplomatic gifts, and capital softlock guard
status: fixed
kind: bug
stage: Civ
reported: 2026-11-26
source: YouTube stream (Vlaid18 + DERMOGEN, v0.8.1.1) + Discord host logs
role: both
versions: 0.8.1.1
fixed_in: 0.8.2
branch: main
commits: 3bb74b46
tests: civ_rivals_tests, civ_tests, rivals_civ, rivals_civ_spec_sw
signature: Civ_GetSwTool empty +0x470/+0x490 crash + pol=0x2282->0x2285 rejoin swap + 17x CMD_BUILD rejected
docs: docs/re/civ.md
related: BUG-1549, BUG-1563
---

## Report
During the `v0.8.1.1` stream (`Дандра.spo` and `Эномель.spo`):
1. Guest crashed every time they attempted to target or fire Civ superweapons (Gadget Bomb / ICBM) against Host.
2. After disconnecting and reconnecting 2.3 s later, Guest was reassigned to a completely different NPC nation (`pol=0x2282 -> 0x2285`) on another continent.
3. Guest's city turrets despawned after 1-3 s, city buildings were rejected (`17x CMD_BUILD rejected`, `12x BUG-0602`), money did not spend or snapped back, layouts reshuffled, and vehicle purchases hit `13x buys_refused`.
4. Guest could not sell vehicles, tank creation buttons greyed out, and custom vehicle models ("свиньи") reverted to generic tanks on Host.
5. Guest consistently started in a small coastal city (4-tank cap) and could not pick/convert captured city specialty.
6. Vanilla diplomatic gifts of Sporebucks from Host to Guest spent Host's money without crediting Guest.
7. NPCs endlessly shelled Guest's inviolable capital City Hall leaving Guest in a permanently ruined city, city-capture autosaves fired excessively, `cmd_gone` surged (`350 -> 2654`) at stage end, and takeover notices printed raw hex `0x00002285`.

## Root cause
- Disassembly of `SporeApp.exe`: `Civ_InitSwTools` (`0x00BF7020`) skips populating `+0x470` (`mSuperweaponTools`) on NPC-origin civs (`civ+0x89 == 0`) and `cCivilization::ctor` leaves `+0x490` (`mSuperweaponTimers`) empty; `Civ_GetSwTool` (`0x00BF18C0`) crashed on Guest targeting.
- `mods/coop/civ_rivals.cpp:501`: `kPlayerLeave` wiped `g_nations[c.player]` without remembering `player_key -> pol_id`.
- `mods/coop/civ.cpp`: city planner turret adds/sells and building moves/sells were not relayed to Host, `apply_guest_perspective()` overwrote Guest's `mWealth` while the planner was open, and `VehicleSlotsLeft` (`0x00BF2240`) applied the NPC 3/6 vehicle cap because Guest's civ had `mIsPlayer == 0` on Host.
- Vanilla diplomacy "Give Gift" (`0x00DCA938`) calls `SpendMoney` + `ApplyRelationship(..., 0x0526E4FB)` without crediting the recipient civ's `mWealth`.

## Fix
- `mods/coop/civ_rivals.cpp`, `mods/coop/civ_rivals_logic.h`, `mods/coop/civ.cpp`, `mods/coop/civ_editors.cpp`:
  - (`rivals.civ_guest_sw`): added `ensure_guest_sw_tools()` / `restore_guest_sw_tools()` and upgraded `SwToolsBorrow` to borrow both `+0x470` and `+0x490`; validated and relayed Guest Civ Rivals superweapons cleanly via `CMD_SUPER`.
  - (`rivals.civ_rejoin_nation`): added `NationMemory` + `"civ.rivals_nations"` store persistence so disconnecting preserves `player_key -> pol_id` and keeps NPC AI paused on reserved nations until reconnect.
  - (`civ.planner_sync`, `civ.rivals_slots`): added atomic planner diffing (`kCmdPlannerSync = 9`) for building removes/moves/adds, turret removes/adds, and clamped net wealth delta; paused wealth overwrite during open planner; added `turret_send_holds` in `guest_turrets()`; hooked `VehicleSlotsLeft` (`h_SlotsLeft`) so guest rival nations receive player vehicle caps.
  - (`civ.sell_vehicle`): hooked `a_VehicleRemove` to relay Guest vehicle sells (`kCmdSellVehicle = 11`), added `ensure_civ_vehicle_designs()` to prevent greyed-out buttons, and synced Guest custom designs via `civ_editors.cpp` + `host_sync_guest_designs`.
  - (`rivals.civ_large_start`): preferred non-small cities and highest building-slot count when assigning Guest's starting nation, and enabled captured city specialty prompts (`kCmdSpecialty`) for Civ Rivals guests.
  - (`rivals.civ_diplo_gift`): hooked `a_SpendMoney` + `a_RelApply` (`0x0526E4FB`) to credit diplomatic Sporebucks gifts directly to the recipient rival's `mWealth` (host->guest and guest->host).
  - (`rivals.civ_capital_softlock`): blocked/repelled NPC attacks on an inviolable capital's City Hall or last <= 2 buildings, clamped rival player City Hall hits at 25% HP floor, throttled Civ Rivals capture autosaves to 120 s on player captures only, switched non-command walks to `hv_alive_quiet()`, and replaced raw hex takeover notices with readable color/specialty text.

## Verification
- no-fail-run: reproduced in live v0.8.1.1 stream & host logs (`sporekit-20261126-190925.log`: superweapon crash, `pol=0x2282->0x2285` swap, 47 rejected commands, 13 `buys_refused`) and verified via disassembly + ASan+TSan unit tests (`tests/civ_rivals_tests.cpp`, `tests/civ_tests.cpp`).
- PASS runs: `20261126-155137:rivals_civ`, `20261126-155137:rivals_civ_spec_sw`, plus `make test` (all 10 suites).
