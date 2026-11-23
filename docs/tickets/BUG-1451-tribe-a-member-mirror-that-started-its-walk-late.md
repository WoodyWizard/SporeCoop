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
branch: 
commits:
tests: tools/tests/coop_body_trail.py (body_trail_tribe_player)
signature:
docs:
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
None yet (triage only). Idea: the cap also allows closing the error (max of the lead's speed and error / catch time).
Decision for the release: the orchestrator / owner (a possible 0.7.16 regression of BUG-1235's fix).

## Verification
Open. `SPORE_DETECTOR=20 python3 tools/lab.py run body_trail_tribe_player --lane N --retry 0 --no-build` (usually
passes, network timing); comparison `SPORE_LAB_FIXOFF="body_trail_tribe_player=tribe.smooth_bound" ...` should show
shorter member stretches (not deterministic); the test's deterministic FAIL twin is drive.lead off (3.55 s, lab
20261106-014253).

## History
- 2026-11-23: opened (fix/w65-night triage).
