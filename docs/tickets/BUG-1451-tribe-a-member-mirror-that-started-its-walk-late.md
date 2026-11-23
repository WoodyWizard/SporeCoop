---
id: BUG-1451
title: Tribe: a member mirror that started its walk late (player network) catches up only at the host's current speed (tribe.smooth_bound caps by lead_sp, not the error): over 2 m for 1.16 s
status: open
kind: desync
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16
fixed_in:
branch: fix/w65-night
commits: fdea37ed
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
2026-11-23 (fix/w65-night), `coop.fix tribe.smooth_catch` (on, BUG-1451): tribe_logic::mirror_lead_speed: with
smooth_bound the bound of the smoothed path is max(lead_sp / late_rate, min(lag / kLagCatchTime 0.5 s, kLagCatchMax
9 m/s)), the lag = the mirror's distance to its aim (tribe_mirrors.cpp mirror_drive). What BUG-1235 fixed stays: its
case (13.4-13.7 m/s lead, 1-2 m behind) keeps the body's 9 m/s, and no lag makes a frame draw more than (2 x 9 + 3)
x dt = 0.67 m at 32 ms (the test's jump limit 0.86 m). The lab's case: 3.3 m behind, host at 2 m/s: the bound 6.6 m/s
instead of 2, the placing closes (sp + 3) x dt again as before 0.7.16. Off: the body's speed only (fbdd2510). Unit test
tests/tribe_tests.cpp smooth_bound (the BUG-1235 case unchanged, any lag under the jump limit, the lab's lag case
closes; the new case FAILs with `--nofix`). No CHANGELOG line: fbdd2510 is unreleased.

## Verification
Open, waiting for the lab run. PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run body_trail_tribe_player
--repeat 4 --lane N --retry 0 --no-build` (4 of 4, longest member stretch under 1.0 s) and the BUG-1235 test
`SPORE_DETECTOR=20 python3 tools/lab.py run tribe_walk_player --repeat 4 --lane M --retry 0 --no-build` (no jump over
0.86 m). No suite twin: the case is the network's timing (a late walk start, about 1 run in 3); with
`SPORE_LAB_FIXOFF="body_trail_tribe_player=tribe.smooth_catch"` stretches over 1.0 s are expected only in some runs.
Before (triage): `SPORE_DETECTOR=20 python3 tools/lab.py run body_trail_tribe_player --lane N --retry 0 --no-build` (usually
passes, network timing); comparison `SPORE_LAB_FIXOFF="body_trail_tribe_player=tribe.smooth_bound" ...` should show
shorter member stretches (not deterministic); the test's deterministic FAIL twin is drive.lead off (3.55 s, lab
20261106-014253).

## History
- 2026-11-23: opened (fix/w65-night triage).
- 2026-11-23: fix/w65-night: tribe.smooth_catch, the bound grows with the lag (capped at BUG-1235's 9 m/s).
- 2026-11-23: fix/w69-triage2 (0.7.16.1 final check): no effect shown. Members' longest stretch over 2 m, fix on (20261123-111347): 2.02 / 0.99 / 1.09 / 0.85 s (2 of 4 FAIL); SPORE_FIX_OFF=tribe.smooth_catch (20261123-121417): 0.97 / 2.47 / 1.85 / 1.70 s (3 of 4 FAIL); rank test p ~0.24, and the off run's network was worse (NPC longest 0.38-1.31 s against 0.42-0.65 s, smooth_catch does not touch NPCs). rel/0.7.16 before the switch: 0.65-1.16 s, 1 of 6 over 1.0 s. Tracks: the fix-on 2.02 s (out/lane4/track/body_trail_tribe_20261123-111642.json, t1 37.4 s) is this ticket's case: the copy starts ~0.5 s late (4.9 m behind at 9 m/s), keeps 3.4-4.9 m while the host runs 9 m/s (the bound caps at kLagCatchMax 9 m/s = the run speed: no catching up while the member runs), then closes only ~2 m/s while the host slows (4.7 -> 3.2 m in 0.8 s); the off arm's 2.47 s (out/lane3/track/body_trail_tribe_20261123-121552.json, t1 24.0 s) closes at the same ~2 m/s (3.7 -> 2.1 m in 0.7 s). Mod: the fix does not change the slow-phase closing; proposal: let the catch-up bound exceed the member's speed (lead_sp + min(lag / 0.5 s, 9 m/s): 18 m/s is 0.58 m a 32 ms frame, under the test's 0.86 m jump limit) and check which lag mirror_drive feeds it (the distance to the aim, not to the host's body now). Stays open.
