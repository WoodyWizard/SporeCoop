---
id: BUG-1451
title: Tribe: a member mirror that started its walk late (player network) catches up only at the host's current speed (tribe.smooth_bound caps by lead_sp, not the error): over 2 m for 1.16 s
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16
fixed_in: unreleased
branch: fix/w70-catch
commits: fdea37ed, 8cf107d5
tests: tools/tests/coop_body_trail.py (body_trail_tribe_player)
signature:
docs: docs/re/tribe.md#tribe-smooth-catch
related: BUG-1235, BUG-0650, BUG-0651, BUG-0674
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, rel/0.7.16 5376d388): body_trail_tribe_player (SPORE_NET=player) "FAIL t: no copy over 2 m off the host's body for more than 1.0 s
(longest 1.16 s, t1)". Members p90 1.37 m (limit 2.0), animals 0.38 s, no frame over 5 m.

## Root cause
Mod (inferred from the track, not proven by a twin). Track out/lane3/track/body_trail_tribe_20261123-000348.json, t1
from the first host row: 10.37 s the host member starts at 9 m/s, the copy moves only at 10.89 s (a latency spike or
loss burst of the player network); 10.63 s over 2 m, 3.66 m at 10.92 s; 11.01-11.56 s the host slows to 1.6-5 m/s and
the copy creeps 0.12-0.22 m per 33 ms frame (4-6 m/s) at ~3.1 m behind; under 2 m at 11.79 s. That fits fbdd2510
(BUG-1235, tribe.smooth_bound, new in 0.7.16 after 21025f40): a smoothed walk start's frame is capped by the host
body's current speed (tribe_logic.h mirror_lead_speed `std::max(lead_sp / rate, 0.5f)`, used at tribe_mirrors.cpp
497), not by the error, so a copy that started late cannot close the gap while the host slows. The class is older
(rel/0.7.15: 2 of 3 failed, 1.22 s and 1.07 s; rel/0.7.16 before fbdd2510 4 of 4 OK, 0.65-0.99 s): probably made more
likely by the 0.7.16 cap.

## Fix
1. 2026-11-23 (fix/w65-night, `fdea37ed`), `coop.fix tribe.smooth_catch`: `tribe_logic::mirror_lead_speed` raised the
   smoothed path's `lead_speed` floor to `min(lag / 0.5 s, 9 m/s)`. On its own this showed no proven lab effect
   (20261123-111347 2/4 FAIL on vs 20261123-121417 3/4 FAIL off) because `walk_place` still subtracted `walk_sp = sp`
   (12-17 m/s during 2x catch-up) while the guest's game walked 0-0.8 m/s (walk start / `keep_lost` teleports), leaving
   placing starved at 0.7-6.7 m/s; reserving `walk_sp = 9 m/s` still left only 12 m/s placing (+3 m/s net vs a 9 m/s
   host); and during a second loss burst extrapolation decay dropped `sp` to 1-4 m/s so `(sp + catch_up)` clamped placing
   to 4.5-6 m/s.
2. 2026-11-23 (fix/w70-catch, `8cf107d5`), under the same `coop.fix tribe.smooth_catch` switch:
   `tribe_logic::mirror_catch_bound` (called from `tribe_mirrors.cpp:mirror_drive`) caps `WalkPlace::walk_speed` at body
   speed whenever `sp > body` (even for `lag < 1 m`), ramps the walk cap from 9 m/s at `lag = 1.0 m` to 5 m/s at
   `lag >= 1.5 m` (shifting up to 16 m/s into deterministic placing while keeping `walk + placing <= 21 m/s`, 0.67 m at
   32 ms, well below `tribe_walk_player`'s 0.864 m jump threshold), and raises `WalkPlace::catch_up` when `sp` decayed so
   placing reaches up to `min(budget - walk, 18 m/s)`. Off (`tribe.smooth_catch off`): 0.7.16 behaviour. Unit test
   `tests/tribe_tests.cpp` `smooth_bound` and `late_catch` (both FAIL with `--nofix`).

## Verification
Verified on lanes 2-3 (fix/w70-catch `8cf107d5`, while `rel/0.7.17` ran on lanes 4-7):
- Fix ON (`SPORE_BODYREC_DIAG=1 SPORE_DETECTOR=20 python3 tools/lab.py run body_trail_tribe_player --repeat 6 --until all --lane 2 --lane 3 --retry 0 --no-build`, lab `20261123-132907`): **PASS 6 of 6** (0 % FAIL). Members longest over 2 m: `0.68 / 0.67 / 0.80 / 0.70 / 0.87 / 0.92 s` (median `0.75 s`, max `0.92 s`).
- Fix OFF (`SPORE_LAB_FIXOFF="body_trail_tribe_player=tribe.smooth_catch"` `--repeat 4 --until all`, lab `20261123-133430`): **FAIL 3 of 4** (75 % FAIL). Members longest over 2 m: `1.21 (FAIL) / 1.00 (OK) / 1.54 (FAIL) / 1.42 (FAIL) s` (median `1.32 s`, min `1.00 s`). Zero overlap between the ON and OFF distributions (Mann-Whitney U = 0, p < 0.01).
- Jump regression (`tribe_walk_player --repeat 4 --until all`, lab `20261123-133802`): **0 jumps in all 4 runs** (0 of 8 checks; walk gait 91-99 %; 3/4 PASS; #2 failed only the pre-existing `BUG-1235` slow-walk uneven check at `16 %` of 62 frames where every uneven frame had `err 0.09-0.90 m < 1.0 m`, untouched by `mirror_catch_bound`).
- Adjacent Tribe regressions (`body_trail_tribe,body_trail_tribe_hold,tribe_walk_mobile,tribe_coop,campaign_tribe_noclick`, lab `20261123-134309`): **PASS 5 of 5** (incl. 38 min `campaign_tribe_noclick`).

## History
- 2026-11-23: opened (fix/w65-night triage).
- 2026-11-23: fix/w65-night: tribe.smooth_catch, the bound grows with the lag (capped at BUG-1235's 9 m/s).
- 2026-11-23: fix/w69-triage2 (0.7.16.1 final check): no effect shown. Members' longest stretch over 2 m, fix on (20261123-111347): 2.02 / 0.99 / 1.09 / 0.85 s (2 of 4 FAIL); SPORE_FIX_OFF=tribe.smooth_catch (20261123-121417): 0.97 / 2.47 / 1.85 / 1.70 s (3 of 4 FAIL); rank test p ~0.24, and the off run's network was worse (NPC longest 0.38-1.31 s against 0.42-0.65 s, smooth_catch does not touch NPCs). rel/0.7.16 before the switch: 0.65-1.16 s, 1 of 6 over 1.0 s. Tracks: the fix-on 2.02 s (out/lane4/track/body_trail_tribe_20261123-111642.json, t1 37.4 s) is this ticket's case: the copy starts ~0.5 s late (4.9 m behind at 9 m/s), keeps 3.4-4.9 m while the host runs 9 m/s (the bound caps at kLagCatchMax 9 m/s = the run speed: no catching up while the member runs), then closes only ~2 m/s while the host slows (4.7 -> 3.2 m in 0.8 s); the off arm's 2.47 s (out/lane3/track/body_trail_tribe_20261123-121552.json, t1 24.0 s) closes at the same ~2 m/s (3.7 -> 2.1 m in 0.7 s). Mod: the fix does not change the slow-phase closing; proposal: let the catch-up bound exceed the member's speed (lead_sp + min(lag / 0.5 s, 9 m/s): 18 m/s is 0.58 m a 32 ms frame, under the test's 0.86 m jump limit) and check which lag mirror_drive feeds it (the distance to the aim, not to the host's body now). Stays open.
- 2026-11-23: fix/w70-catch (`8cf107d5`): `mirror_catch_bound` caps `walk_speed` and lifts `catch_up` under `tribe.smooth_catch`; verified 6/6 PASS (`0.67-0.92 s`, lab `20261123-132907`) vs off 3/4 FAIL (`1.00-1.54 s`, lab `20261123-133430`), 0 jumps in `tribe_walk_player` x4 (`20261123-133802`), 5/5 Tribe regressions (`20261123-134309`). Closed.
