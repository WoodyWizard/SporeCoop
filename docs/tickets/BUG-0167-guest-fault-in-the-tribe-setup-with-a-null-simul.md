---
id: BUG-0167
title: Guest: fault in the tribe setup with a null simulator object (seen once)
status: fixed
kind: crash
stage: Tribe
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: unreleased
branch: fix/w46-n1-crashes (guard: d605e96)
commits: d605e96, 3198f742
tests: tribe_camera_null
signature: dc04aee5:SporeApp.exe SporeCoop::SimCamera_LookAt+0x52 (0xB12D62, data 0x2B4)
docs: docs/re/tribe.md#tribe-camera-null, docs/re/space.md
related: BUG-0656, BUG-0607, BUG-0626
switch: tribe.camera_guard
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `exception 0xC0000005 at 0x00B12D32` (data 0x2B4) on a guest of a 4-player tribe churn
  (`coop_multi.py`, 2026-10-23): the tribe setup 0xCD78E0, one step after a load, calls 0xB12CE0 on
  `[0x167FAAC]` (getter 0xB3D350), which was null (it is null in the galaxy menu). Not reproduced on
  2 players (`coop_rejoin_loop.py` plain, `--race`, `--reshare`, `--menu`: 0 of about 250 rounds).
  Since 2026-10-23 a load leaves a stage only when it has been playable for 3 s (`world.cpp`
  `kStageSettle`), which should avoid a quit during the setup; if it comes back, the fault log now
  shows the call chain (`exc`: "calls:"). docs/re/space.md 8.3.
* 2026-10-23: it came back once more (4-player tribe, `coop_multi.py`). Since then `gamefixes.cpp`
  skips `SimCamera_LookAt` and `SimCamera_Update` while the camera object is null (`coop.status`:
  "tribe camera skipped N"); the stage setup goes on without pointing that camera.

## Root cause
Observed (0.7.11 N1, GOG 2024, docs/re/tribe.md#tribe-camera-null): the galaxy menu's own intro timer (~15.85 s after
the menu came up) fires in the first frames of a guest's Tribe stage when the guest joined ~12.5 s after its menu
came up (its load comes 3-3.5 s after the join); "GGE_intro" starts in the Tribe mode 01654C02 and 0.07 s later
TribeGame_Setup's LookAt meets the null simulator camera [0x167FAAC]. Without the guard: `0xC0000005 at 0x00B12D62
SporeCoop::SimCamera_LookAt+0x52 data=0x000002B4`, calls 0xCD7D22 (TribeGame_Setup): the report's fault (GOG 2016
0xB12D32, data 0x2B4). A 4-player churn has guests joining at all moments of their menu, hence "seen twice in
4-player churns, never on 2 players". The 7 lab sightings of BUG-0656 are the same order.
Inferred: the camera object is made a frame after the stage begins; the intro started in that frame takes that
step (the intro started by hand in the menu, at the request or in the Loading mode did not null it, 15 loads).

## Fix
Two layers, both on in 0.7.9+:
* `tribe.camera_guard` (gamefixes.cpp, d605e96 in v0.6.0; a lab switch since 0.7.11, 3198f742, COOP_FIX_UNSAFE):
  `SimCamera_LookAt` and `SimCamera_Update` are skipped while the camera object is null (`coop.status`: "tribe camera
  skipped N"; the log says "skipped the tribe camera set-up without a simulator camera (mode, cinematic)").
* `world.intro_at_once` (BUG-0626, 0.7.9): the menu's intro does not start outside the galaxy menu, so the camera is
  no longer taken in the first place (the 4-player churns of lab 20261107-223550: "would start outside the menu (mode
  01654C02): not started" in the guests' logs).

## Verification
Test tribe_camera_null (coop_tribe_camera_null.py): world.intro_at_once off on the guest before its join (to make the
race of 0.7.8 and older), the join 12.5 s after its menu (more join times if the race is missed), then the guest's
log must show the setup meeting the null camera and its game must be in a playable Tribe with no fault.
* FAIL (SPORE_FIX_OFF=tribe.camera_guard): out/lab/20261107-231118 and out/lab/20261107-231257 (lane 10): the race on
  the first join, `0xC0000005 at 0x00B12D62 SporeCoop::SimCamera_LookAt+0x52 data=0x000002B4`.
* PASS: out/lab/20261107-231113 (lane 9), out/lab/20261107-231252 (#1, #2, lane 9): the race on the first join,
  "skipped the tribe camera set-up", 0 faults.
* Hunts of the report's own case on this build (both fixes on): multi4_tribe and multi4_tribe_slow (4 players, 6 min,
  churn; the slow one at 20 fps over the players' networks): OK, 0 faults on all 4 games (out/lab/20261107-223550).

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: closed on fix/w1-tribe-civ as fixed by the camera guard of d605e96 (checked: guard on, GOG 2024 symbols verified).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: status fixed -> not-reproduced: its only commit d605e96 shipped in v0.6.0; nothing was changed in 0.7.3.0, the ticket was closed because the guard was never seen failing again. fixed_in cleared (BUG-0215).
- 2026-10-30 (fix/w13-tribe-civ): still not reproduced. All lab game logs of the main checkout (out/lab, 2026-10-22..30) searched for tribe-stage faults: none at this ticket's address in any tribe test; the only Tribe faults in campaigns are BUG-0285 / BUG-0310 (0xC0DB23, fixed by tribe.leave_walk). On this branch tribe_coop, tribe_play, tribe_far, chaos_tribe, menu_stale_player, campaign_tribe, tribe_leave_walk (3x) passed on lane 6 without a fault, freeze or crash (lab runs 20261030-141833, 20261030-140307). Status kept.
- 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): reproduced with the join timing of BUG-0607 (the menu timer starts the intro in the first frames of Tribe); FAIL/PASS twin on tribe.camera_guard; status fixed (the guard was the fix, d605e96; the cause was found now).
