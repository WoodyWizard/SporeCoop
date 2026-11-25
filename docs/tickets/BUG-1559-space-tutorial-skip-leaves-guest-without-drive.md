---
id: BUG-1559
title: Space: skipping the Space tutorial on host leaves guest without interstellar drive and tutorial unlock
status: fixed
kind: bug
stage: Space
reported: 2026-11-25
source: GitHub #221 (Dedlection)
role: both
versions: 0.7.14.1, 0.7.15.5, 0.8.0.1
fixed_in: 0.8.0.2
branch: w0802/space-tut-drive
commits: b586b9de, 344219ea
tests: space_tut_drive, space_tut_drive_off, space_tests
signature:
docs: docs/re/space.md
related: BUG-0181, BUG-1539
---

## Report
GitHub #221 (`Dedlection`): if the host skips the Space stage tutorial (or finishes it in-session), the guest is not granted the interstellar drive (`0x2569FAE8`) and remains locked inside the home star system unable to travel to other stars.

## Root cause
`mods/coop/spacecoop.cpp` only broadcast tool grants when a mission completed via `cMission::SetState(..., 5)` (`K_MISSION`) or through player tool trades. When the host skipped the Space tutorial, completed Flight School / Story201 (`0x7BDBB97D`), or scanned the crashed ship wreck, Spore directly set `cMissionManager` tutorial flags (`+0x16 mbFirstCommunication`, `+0x17 mbFlightSchoolFinished`, `+0x18 mbMission201Finished`) and added `interstellardrive` (`0x2569FAE8`) / `interplanetarydrive` (`0x4133FE05`) to the host's inventory without a synced mission reward broadcast. On the guest, lacking `interstellardrive` prevented zooming out to the galaxy view, and `cMissionManager +0x18 == 0` caused Spore's galaxy travel check (`0xFFC780`, `docs/re/space.md#space-tutorial-lock`) to refuse travel to all stars outside home and the Story201 target.

## Fix
- Added `COOP_FIX("space.tutorial_drive_sync", "BUG-1559")` in `mods/coop/spacecoop.cpp` (default ON) backed by pure decision logic in `mods/coop/space_logic.h` (`tutorial_pack`, `tutorial_unpack`, `tutorial_sync_mission_id`, `is_tutorial_sync_mission_id`, `tutorial_sync_decide`, `tutorial_sync_should_send`).
- On the host in Space (`tut_sync_tick`), polls `cMissionManager +0x16/+0x17/+0x18` and ship inventory presence of `interstellardrive` (`0x2569FAE8`) and `interplanetarydrive` (`0x4133FE05`). Whenever the host's non-zero unlock mask changes, the Space world epoch changes, or a new guest enters Space, the host broadcasts the mask over existing `K_MISSION` using the synthetic `mission_id` sentinel `0x54555400 | mask` (keeping `wire/space_core.h` and the `space` family wire lock unchanged, while still placing the drive keys in `msg.tools` for older peers).
- On the guest in Space (when `space.tutorial_drive_sync` is ON and neither `own_world::elsewhere()` nor `own_world::space_other()`), monotonically applies missing tutorial flags via `SporeCoop::MissionManager_SetFlightSchoolFinished` (`0x00FEB8F0`), `SporeCoop::MissionManager_SetMission201Finished` (`0x00FEB910`), and `+0x16`, and grants missing `interstellardrive` / `interplanetarydrive` via `cPlayerInventory::AddItemByKey` (waiting while `spaceext::hold_grants()` is active so kept-hold restore never overwrites the grant). Never downgrades flags or removes drives.
- Added diagnostic counters (`tut_sync_tx`, `tut_sync_rx`, `tut_flags_applied`, `tut_drive_granted`) to `coop.space` status and `coop.space tutorial set <fc> <fs> <m201>` test helper.

## Verification
- Unit tests (`make unit-cpp unit-net unit-lint` under ASan+UBSan, `python3 tools/msg_manifest.py check`, `python3 tools/tests/test_lint.py`): `space_tests` (`test_tutorial_drive_sync`), `validate_tests`, `wire_tests`, and all lints PASS.
- Two-game Wine lab ON/OFF twin on lane 2 (`out/lab/20261125-154512`):
  - `space_tut_drive` (ON): **PASS** (38 s, 0 faults on both games). Host unlocked tutorial `(1, 1, 1)` + `interstellardrive` (`0x2569FAE8`) on `space` fixture without reloading; guest automatically received `first_comm=1 flight_school=1 m201=1`, `drive=1` (`tut_sync_tx=3`, `tut_sync_rx=3`, `tut_flags_applied=3`, `tut_drive_granted=1`), zoomed out to the galaxy view by mouse wheel, clicked nearby unowned star `Alatalva` (`4587540`), and flew there (`arrived`).
  - `space_tut_drive_off` (`SPORE_FIX_OFF=space.tutorial_drive_sync`): **FAIL** (57 s, 0 faults) on the exact tutorial unlock, drive grant, and galaxy zoom-out assertions (guest remained locked at `first_comm=0 flight_school=0 m201=0 drive=0 tool=0`).
- Regression check on lane 2 (`out/lab/20261125-154703`): `space_free` (37 s) and `rivals_space` (88 s) **PASS** (2/2, 0 faults).
