---
id: BUG-0365
title: Tribe: the guest's mirrors of walking tribe members stop and go at every start and end of a walk (drive::step; no sender time)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-01
source: owner's request after BUG-0360 (re-check the other modes)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-tribe-walk
commits: 356bbf1, 90024d2, c6c3fd1, 747f76a, 846a63a, 490423e, 1e5caaa, c6eb380, e147fea, 8dfa06d, f2edb1b, d1f2a0a
tests: tribe_walk, tribe_walk_mobile (tools/tests/coop_tribe_walk.py)
signature:
docs: docs/re/tribe.md#tribe-mirror-walk, docs/06-testing.md#testing-tribe-walk
related: BUG-0360, BUG-0281, BUG-0329, BUG-0122, BUG-0386, BUG-0366, BUG-0390, BUG-0393
switch: tribe.mirror_walk, tribe.mirror_time
---

## Report
After BUG-0360 (the partner's creature in Creature moved in steps and teleported: "updates about every
half second, mostly teleports, sometimes walks smoothly") the owner asked to re-check the other modes.
In Tribe a guest shows every tribe member as a mirror (the host's members and its own, whose orders go
to the host), driven by the same drive::step.

## Root cause
Observed (coop.track in Tribe: every member of the player tribe, every frame, at the position the game
drew; coop_tribe_walk.py: members walked by box-select and right-click, both directions):

1. No "every half second" steps and no teleports: at full speed (9 m/s in the fixture) the mirrors
   followed (0.1 s windows matched 77-100 %, walk gait 96-99 % on screen, 0 driver teleports).
2. Stop-and-go at every start and end of a walk: drive::step gives a MoveTo 0.25 s ahead every 0.1 s
   and the mirror walks at 9 m/s (~ a MoveTo given after SetDesiredSpeed resets the speed), so while the
   host's member moves at 1-5 m/s the mirror reaches the point ahead in one or two frames, stops and
   walks again: the drawn step is off the target's in 14-40 % of those frames (the host's own game
   0-5 %); it sets off 0.5-1.6 m late and catches up at 15-24 m/s; drawn error p50 0.44-0.48 m.
3. The tribe state had no sender time: the target was the latest snapshot moved on by its arrival age,
   so network jitter went into the path (BUG-0122 class). Over SPORE_NET=mobile the old driving matched
   the path in 0.1 s windows only 60-71 % of the time; with mirror_walk alone (mirror_time off) 71-80 %,
   uneven 28-50 % (20261101-082322).

Found on the way: the placing of a frame is drawn in the next one; after a 100-230 ms hitch of the game
(the minimap's camera jumps) the next short frame shows the way made up (1.1-1.3 m). The host's own
members start and stop within one frame (0 -> 8 m/s).

## Fix
* `tribe.mirror_time`: every tribe state chunk ends with an 8-byte trailer (magic 0x54435354, the
  host's ms; guests before this version stop reading after the entries and ignore it); each mirror keeps
  a net::SnapshotBuffer (delay 0.2 s) and is driven towards the interpolated state, at the interpolated
  path's velocity over the last 0.1 s. Off (or from an older host): the latest state + vel * age.
* `tribe.mirror_walk`: while the member moves and its mirror is within 8 m of the path, the shared
  `drive::walk_place` (driver.h; BUG-0360's creature_drive walk branch moved there, the Creature puppet
  calls it unchanged) walks the mirror (speed after the MoveTo for citizens) and places it on the path
  every frame at its own ground height; `drive::walk_end` when it stands. Off: drive::step (0.7.3.3).
* Diagnostics: `coop.track` in Tribe (the member id as the last column, after main's hd ht), `coop.status` tribe(guest)
  `mirror_walks=` and `timed=` (chunks with the host's clock).

## Verification
`coop_tribe_walk.py` (suites tribe_walk in full, tribe_walk_mobile nightly: SPORE_NET=mobile, both games
at 30 fps). Checks for the guest's mirrors of the host's members and of its own: no frame step over 3x
the top speed x max(this, the previous frame time); 0.1 s windows standing <= 5 %, matched (0.6-1.4x the
target's path) >= 85 %; frames at 1-5 m/s with the drawn step off the target's previous step by more
than half + 1 m/s x dt <= 10 %; walk gait >= 80 % on screen.
* FAIL with `SPORE_FIX_OFF=tribe.mirror_walk,tribe.mirror_time`: out/lab/20261101-084031 (4 of 4: tribe_walk x2,
  tribe_walk_mobile x2; uneven 10-40 %, matched 60-90 %, drawn error p50 0.37-0.48 m). Also
  20261101-081533 (3 of 3, an earlier version of the check), 20261101-074523 (the 0.7.3.3 driving
  before the switch existed, 3 of 3). `SPORE_FIX_OFF=tribe.mirror_time` over mobile: 20261101-082322
  (2 of 2 fail: matched 71-80 %, uneven 28-50 % with that check).
* PASS: out/lab/20261101-084159 (tribe_walk 3 of 3, tribe_walk_mobile 2 of 3: uneven 1-5 %, matched
  91-100 %, standing 0-3 %, no jumps, drawn error p50 0.01-0.07 m / p90 0.08-0.25 m, gait 95-99 % on
  screen; tribe_walk_mobile#1 did not start: the host game did not answer `game.mode` at the load,
  a lab start-up timeout, no check ran).
* More PASS runs: 20261101-092116 (tribe_walk 2 of 2, tribe_walk_mobile 1 of 2: the failed one ran while
  `make test` compiled on the same machine, the guest's game had 558 frames over 0.1 s, 3 driver
  teleports, matched 70 %), 20261101-092756 (tribe_walk_mobile 2 of 2), 20261101-091623 (tribe_walk_mobile
  1 of 2: the failed one had the host's minimap calibration fail (no host walk; the test calibrates
  again now) and one held-back mirror that lurched 0.9-1.1 m per frame to catch up: BUG-0386).
* Regression (lanes 2, 3): out/lab/20261101-085611: tribe_coop, tribe_play, chaos_tribe, puppet_walk,
  creature 5 of 5; puppet_walk again 2 of 2 in 20261101-091623 (one earlier jump of 0.9 m in
  20261101-075538 followed a game hitch, the same as tribe_walk's hitch catch-up).
* Smoke (lanes 2, 3): out/lab/20261101-090144: 14 of 14. `make test` passes.
* After merging main 15539ad (490423e; BUG-0366's ground handling moved to drive::on_own_ground /
  WalkPlace::ground; lab log gates on; the machine at load 18-31 on 12 cores from other lanes):
  * the mirrors catch up over frames up to 0.4 s (c6eb380: a guest at 3-4 fps fell 8 m behind and was
    teleported) and never walk faster than the member (d1f2a0a: a network correction of the path made
    one surge 0.95 m in a frame); tribe_walk leaves out the steps within 0.2 s after a game hitch or a
    network correction of the target and prints NOT MEASURED when over 3 % of the frames are over 0.1 s.
  * PASS: out/lab/20261101-112603: tribe_walk 2 of 3, tribe_walk_mobile 3 of 3 (uneven 0-3 %, matched
    98-100 %, no jumps, drawn error p50 0.01-0.05 m / p90 0.08-0.28 m, gait 93-99 %); the failed one is
    only the sync-check gate on the RTS player point (BUG-0393).
  * FAIL with `SPORE_FIX_OFF=tribe.mirror_walk,tribe.mirror_time`: out/lab/20261101-113345 (2 of 2:
    uneven 13-32 %, matched 79 %), out/lab/20261101-112034 (tribe_walk and tribe_walk_mobile: uneven
    19-27 %, matched 67-70 %).
  * Regression: smoke out/lab/20261101-105555 14 of 14; tribe_coop, tribe_play, chaos_tribe
    out/lab/20261101-113349 3 of 3; puppet_walk / puppet_slope / puppet_under 8 of 10 in
    out/lab/20261101-104554 (the 2 failures are 0.1 s window checks of the Creature puppet at 24-32 fps
    under the load, the same as other branches' puppet_walk runs then; the jump check of
    coop_puppet_walk.py now also allows the frame before's time, 8dfa06d); `SPORE_FIX_OFF=creature.puppet_ground`
    puppet_slope still fails (10 falls, out/lab/20261101-111051; 8 in 20261101-095228).

## History
- 2026-11-01: opened.
- 2026-11-01: measured and fixed on fix/w30-tribe-walk (tribe.mirror_walk, tribe.mirror_time); BUG-0386
  opened on the way.
- 2026-11-02: released in 0.7.3.4.
