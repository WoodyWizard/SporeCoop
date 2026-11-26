---
id: BUG-1570
title: Space: guest missing Flight School starter tools (scan, laser, abduct, dropcargo) after tutorial sync or kept-hold restore
status: fixed
kind: bug
stage: Space
reported: 2026-11-26
source: Discord (Doctor)
role: guest
versions: 0.8.0.2, 0.8.1, 0.8.1.1
fixed_in: unreleased
branch: main
commits: 21177330
tests: space_starter_tools, space_starter_tools_off, space_tests
signature:
docs: docs/re/space.md
related: BUG-0678, BUG-0679, BUG-1559
---

## Report
Discord (`Doctor`): on one Space Stage save, a guest ended up without the basic Space ship tools (scanner, laser, abduction beam).

## Root cause
Observed in `SporebinEP1/SporeApp.exe` (GOG 2024 `dc04aee5`) and `Data/PatchData.package`:
- When entering Space Stage before Flight School finishes, the ship has none of the basic Flight School tools. Spore grants all 6 tools in one batch via `cPlayerInventory::AddDefaultTools` (`0x00FF4590`), reading property `0xF0609EC3` of `0x30608F0B!0x59252EE9` (`PatchData.package`): `scan` (`0x08EF6A62`), `laserlevel1` (`0x6DB01899`), `abduct` (`0xF58C1654`), `dropcargo` (`0xD001F5E2`), `healthupgrade1` (`0x88914EF2`), and `energyupgrade1` (`0x33E11760`).
- `0x00FF4590` is called only inside the three `cMissionFlightSchool` exit handlers (`0x00C55A40`, `0x00C55A70`, `0x00C55AC0`, vtable `0x0146EC38`) directly before `MissionManager_SetFlightSchoolFinished(1)` (`0x00FEB8F0`). Those 6 tools are not listed in `cMission` reward fields (`+0xA4` / `+0x11C`), so `K_MISSION` never broadcasts them.
- Two bugs in `SporeCoop` left guests without them:
  1. `BUG-1559` (`space.tutorial_drive_sync` in `mods/coop/spacecoop.cpp`) set `mbFlightSchoolFinished = 1` and granted only `interplanetarydrive` / `interstellardrive`, leaving the guest without the 6 Flight School starter tools and unable to replay Flight School.
  2. `BUG-0678` / `BUG-0679` (`space.keep_items` / `apply_inv` in `mods/coop/space_trade.cpp`) stripped any tool missing from an older kept cargo hold (`holds/<crc>.txt` or `K_INV_KEPT`) via `cPlayerInventory::TakeItem`. A guest whose hold had been recorded before Flight School had `scan`, `laserlevel1`, `abduct`, `dropcargo`, `healthupgrade1`, and `energyupgrade1` deleted every time they rejoined that save.

## Fix
- Added `COOP_FIX("space.starter_tools_sync", "BUG-1570")` (default ON) in `mods/coop/spacecoop.cpp` (exposed via `spaceext::starter_tools_sync()` in `mods/coop/space_ext.h`) backed by pure decision logic in `mods/coop/space_logic.h` (`kTutStarterKit`, `is_laser_tool`, `is_health_tool`, `is_energy_tool`, `is_interstellar_tool`, `keep_starter_in_restore`, extended `TutorialState` / `TutorialApply` / `tutorial_sync_decide`).
- **Tutorial sync (`mods/coop/spacecoop.cpp`):**
  - Host packs bit 5 (`kTutStarterKit`) into the existing `0x54555400 | mask` `K_MISSION` sentinel (no wire-format change; backward-compatible with older hosts via `host.flight_school`).
  - Guest monotonically grants any missing Flight School starter tools (`scan`, `laserlevel1`, `abduct`, `dropcargo`, `healthupgrade1`, `energyupgrade1`) while respecting upgrade lines (`laserlevel1..3`, `healthupgrade1..5`, `energyupgrade1..5`, `interstellardrive` + `interstellardrive_upgrade1..5`) so upgraded guests never receive duplicate tier-1 tools.
  - Added diagnostic counter `tut_starter_granted` to `coop.space` status and `coop.space tool remove <name|0xhash>` test helper.
- **Kept-hold restore protection (`mods/coop/space_trade.cpp`):**
  - In `apply_inv()`, never strip permanent Flight School starter tools or starter drives (`keep_starter_in_restore`) that exist in the freshly loaded host save when restoring a guest's older kept hold, allowing poisoned `holds/<crc>.txt` records to self-heal on rejoin. Added `starters_kept` counter to `coop.space trade kept`.

## Verification
- Unit tests and lints (`make unit-cpp unit-net unit-lint`, `python3 tools/msg_manifest.py check`, `python3 tools/tests/test_lint.py`): `space_tests` (expanded `test_tutorial_drive_sync` covering starter sync, backward compatibility, upgrade lines, and `keep_starter_in_restore`) and all checks PASS.
- Two-game Wine lab ON/OFF twin (`out/lab/20261126-100346`):
  - `space_starter_tools` (ON): **PASS** (46 s, 0 faults on both games). Phase 1: rejoined guest restored pre-Flight-School kept hold (`spiceA=(7, -1)`, `restored=1`) while keeping all 6 Flight School starter tools (`starters_kept=6`, `missing=[]`). Phase 2: live Flight School unlock on host automatically granted all 6 starter tools to guest without reload (`tut_starter_granted=12`, `missing=[]`, idempotent).
  - `space_starter_tools_off` (`SPORE_FIX_OFF=space.starter_tools_sync`): **FAIL** (60 s, 0 faults) on all 4 target assertions across both phases (`starters_kept=0`, `tut_starter_granted=0`, `missing=['scan', 'laserlevel1', 'abduct', 'dropcargo', 'healthupgrade1', 'energyupgrade1']`).
- Space regression suite (`out/lab/20261126-100513`): **4/4 PASS** (1.4 min, 0 faults): `space_tut_drive` (39 s), `space_items_rejoin` (42 s), `space_items_limits` (52 s), `space_grant_restore` (53 s).

## History
- 2026-11-26: opened, fixed (`21177330`), verified with unit tests, ON/OFF lab twin (`20261126-100346`), and 4 Space regressions (`20261126-100513`).
