---
id: BUG-0175
title: coop_sync.py: mirrors trail more at low frame rates, the GOG 2024 guest pauses after a load (the test failures were fixed 2026-10-24, fix/gog2024-sync)
status: fixed
kind: bug
stage: All
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.3.2
branch: fix/w13-net
commits: 755e432, fcba17c, a0f0c0b
tests: tests/npc_tests.cpp test_pull_per_second; tools/tests/coop_sync.py --fps 15 (suite sync_lowfps, regression)
signature:
docs: docs/known-issues.md, docs/06-testing.md#testing-sync-lowfps
related: BUG-0282, BUG-0313
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The full lab run 20261023-225702 (feat/space-07, lane 10, 9 lanes busy) failed `coop_sync` twice:
  cell/players facing p90 15.6°, tribe/citizens 2.39 / 2.13 m, civ/vehicles 4.4 / 6.0 m and 33.6°,
  space/ships 33 m (first run). It had never run on GOG 2024, and ran on the new fixtures.
* Cause 1 (GOG 2024, test timing): the 2024 guest keeps its game time paused (cGameTimeManager
  +0x48 = 3, black screen, ~23 fps) for ~7 s after its world load in the tribe stage, exactly the
  window the test measured in. Citizens do not get `Update` then (`coop.tribe` on a guest prints
  `ai_skipped=0/0`), so mirrors do not walk and follow the host by drive.h's per-frame 15 % nudge
  only: the error is speed x frame time / 0.15 (2.4 m at 5.5 m/s and 15 fps; the lab's load lowered
  the frame rate). Forcing 15 fps on both builds (`frame.limit 66`) reproduced it on 2024 (citizens
  2.7 m) and not on 2016 (0.6 m). No wrong symbol: once the game time runs the 2024 mirrors walk.
  Players do not see it (the screen is black then).
* Cause 2 (test, both builds): the facing checks counted the guest's designed lag. The cell host
  turns every 4th round and the civ fixture has a single vehicle that turns in place, so 1 of 8
  samples sat at 15-30° (raw p90 2016: 12-21.5°, 2024: 2.6-18°).
* Vehicles at a low frame rate trail by ~2 m on both builds (15 fps: 4.2 m on 2016 and 2024), which
  is where the lab's 4-6 m came from.
* Fix: coop_sync.py waits until neither game is paused and compares within a 0.15 s lag window
  (docs/06-testing.md 6.5 "Sync accuracy"). After: 3 of 3 passes on lane 10, 2 of 2 on lane 3.
* Not fixed: drive.h's nudge is per frame, so a mirror that cannot walk trails more at low frame
  rates; the 2024 guest's paused seconds after a load are not explained (planet still loading?).

## Root cause
Observed (2026-10-24, see the report): a mirror that cannot walk follows the host only by
`drive::step`'s nudge, `k = 0.15` of the way **per frame**. With the target moving at v that
settles (1 - k) / k * v * frame_time behind: 0.52 m at 60 fps, 2.08 m at 15 fps for 5.5 m/s
(the 2.4-2.7 m measured at 15 fps in the paused window). Inferred: the same holds for any
drive::step mirror whose own walking does not keep up (creature partners, NPC proxies, citizens).
Civ vehicles (civ.cpp guest_tick, a 50 % glide per frame after StopMovement) have the same form
(v * frame_time behind), not changed here, see Fix. Cell mirrors are steered by velocity in
cellsync.cpp (BUG-0282, another code path, not touched).

## Fix
fix/w13-net (switch `coop.fix drive.nudge_dt`, off = 15 % every frame): `drive::pull_k(k60, dt)`
(drive_plan.h) turns the per-frame factor into a per-second one, `1 - (1 - k60)^(dt * 60)` with dt
capped at 0.1 s; at 60 fps it is the old 15 %. `drive::State::last_step` gives dt per mirror.
Shared code: drive::step (driver.h) drives creature partners (modes.cpp), NPC proxies (npc.cpp)
and tribe citizens (tribe.cpp); **not** the Cell mirrors (cellsync.cpp, BUG-0282) and not the
civ vehicles. The civ glide got the same change first and lost it again: four 15 fps lab runs
(one vehicle, 16 samples) showed no difference between per-frame and per-second glide (p90 1.6-2.5 m
off, 2.2-3.1 m on): noise of one vehicle, so the patch release keeps the old civ code.
The GOG 2024 guest's paused seconds after a load are split off as BUG-0313 (game behaviour, black
screen).

## Verification
- Unit (the mechanism, portable): `npc_tests` PASS (a mirror that only gets the pull, 5.5 m/s:
  0.52 m behind at 60 fps, 0.48 m at 30, 0.40 m at 15); `npc_tests --nofix` FAIL (0.52 / 1.04 /
  2.08 m: `c15 <= c60 * 1.05`, `c30 <= c60 * 1.05`).
- no-fail-run: in game the lab cannot separate fix on and off: with the game time running the
  mirrors walk and the 0.15 s lag window of coop_sync hides a one-frame difference; the paused
  window where mirrors only get the nudge is a black screen on the guest. `coop_sync.py --fps 15
  civ tribe` (suite sync_lowfps) passed with the fix off (20261030-133141, 20261030-133514,
  20261030-134029) and with it on (20261030-133830, 20261030-134756 with the final code), once failed with it
  on with the civ glide change (20261030-133658: vehicles p90 3.05 m against 3.0, one vehicle):
  that change was taken out.

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached. The test-side cause is handled since 2026-10-23 (coop_sync waits for unpaused games); what is left is mod behaviour (drive.h's per-frame nudge, the 2024 guest's paused seconds), not a test change. Left open.
- 2026-10-30: fixed on fix/w13-net (wave 13): drive::step's pull per second (drive.nudge_dt); the 2024 paused seconds split off as BUG-0313.
- 2026-11-01: released in 0.7.3.2.
