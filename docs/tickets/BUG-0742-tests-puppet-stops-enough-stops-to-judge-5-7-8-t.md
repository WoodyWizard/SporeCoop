---
id: BUG-0742
title: tests: puppet_stops 'enough stops to judge (5-7 >= 8)': the stop walker stuck 12-19 m off on the slope (back_to_partner found the tag but did not bring it closer) and the walk ended by time
status: fixed
kind: test
stage: Creature
reported: 2026-11-08
source: G4 analysis of 49 puppet_stops runs 2026-11-06..08
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-g4-tests
commits: 2c4db665, d5af56db
tests: puppet_stops
signature:
docs: docs/06-testing.md#testing-g4-tests
related: BUG-0394, BUG-0407, BUG-0408, BUG-0680
---

## Report
puppet_stops (SPORE_NET=player coop_puppet_walk.py --dir both --guest-fps 30 --stop-walk --secs 45) fails "enough stops
to judge (5-7 >= 8)" in 11 of 98 directions (49 runs of 2026-11-06..08 over many branches): 8 of 49 with the guest
walking, 3 of 49 with the host walking. The slide checks of BUG-0408 / BUG-0680 pass in those runs.
Reproduce: `tools/lab.py run puppet_stops --repeat 3 --retry 0` (about 1 run in 5).

## Root cause
Observed (G4 analysis of the runs' test logs and per-frame track files):
* The walker gets stuck 12-19 m from the observer on the slope of the stop walk: `back_to_partner()` finds the partner's
  tag on the screen (found=True) but its click and W walk do not bring it closer; the gopos way back fired only after two
  "not found" in a row, so the walk loop spent 8-10 "back" turns of 2-4 s each (failing runs: 8.1 "back" per walk, passing
  5.1) and made 4-5 W-and-stop steps in its 45 s instead of ~13. Example: fix/w46-k3-bodies out/lab/20261107-122016 #1/#2
  (track puppet_walk_host_sees_guest_20261107-122252.json: the guest 12-19 m off from 9 s to 48 s, 5 m up and down).
* The walk ended by time: under a loaded machine every step takes longer, and WALK_MORE (BUG-0407) walked on only when
  too few frames were measured, not when too few stops were made.
* Also seen: stops() needs 3 samples in 0.3 s before a stop; at 16-23 fps observers (3 host-walker failures) some stops
  had 2. Not changed (it would loosen what counts as a stop).

## Fix
Test only (tools/tests/coop_puppet_walk.py), the limit of 8 stops unchanged: a "back" that found the tag but did not
bring the walker below min(d - 3, TETHER) counts as no way back; in the stop walk the first such turn (else the second)
puts the walker next to the partner (gopos 4 m to the side along the ground, `beside_pos`, shared with beside()); the
stop walk walks on (at most WALK_MORE more walks) until it made the 8 stops the check needs.
The way back teleports the walker more often now, so the "no jump of the puppet" check does not count a jump of the
puppet within -0.2..+0.8 s of a teleport of the walker's own creature on its own track (the puppet follows its walker;
every other jump still counts).

## Verification
FAIL (before): the 11 failures of the report (e.g. fix/w46-k3-bodies out/lab/20261107-122016 #1/#2, rel/0.7.11
20261108-035548-2 #2). First version of the fix: stops 12-13 but "no jump of the puppet" failed at the gopos teleports
(out/lab/20261108-055918, 3 of 3; re-analysed with the exemption: no jump left).
PASS 4 of 4 on lane 2: out/lab/20261108-081528 (stops 10-15 in every direction, no puppet jump).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4-tests): found and fixed (test only); 4 of 4 PASS.
