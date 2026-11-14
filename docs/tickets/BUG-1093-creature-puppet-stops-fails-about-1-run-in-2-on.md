---
id: BUG-1093
title: Creature: puppet_stops fails about 1 run in 2 on rel/0.7.14: the puppet goes 0.5-0.7 m past 1-2 of 8-17 stops (coop.bodyrec)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-stops
commits: 46105539, 124a9ea9, 3a749886
tests: puppet_stops, net_tests
signature:
docs: docs/re/creature.md#creature-puppet-stop-send-phase, docs/06-testing.md
related: BUG-0680, BUG-0408, BUG-0742, BUG-1092, BUG-1095
---

## Report
Seen on fix/w49-puppet (BUG-1092) regression runs, with and without that branch's switch: `puppet_stops` FAILs "the
puppet does not go past where <walker>'s own creature stopped (over 0.3 m after 1-2 of 8-17 stops, coop.bodyrec)",
max 0.48-0.66 m. Fix on: FAIL 20261113-193854 (lane 3), 20261113-195009 (lane 2); PASS 20261113-191413,
20261113-195639. `SPORE_LAB_FIXOFF="puppet_stops=drive.agl_lerp"`: FAIL 20261113-200403 #1 (lane 2), PASS
20261113-195007, 20261113-200400 #1 (lane 3). drive.agl_lerp only changes the radial height of a PF_JUMPING state,
so not that change. Not checked on v0.7.13. Reproduce: `lab.py run puppet_stops --repeat 4 --retry 0`.

## Root cause
Two causes, both seen in the new rows (coop.track columns ex / age / knee / hz / ow, the bodyrec rows of both games
saved in the track json; scripts in the branch's analysis, numbers below).

1. The test (observed, 4 of the 7 failing directions): `body_trail.stop_slides` measured the copy for 1.5 s after the
   stop, but the stop walk's next walk starts 1.0-1.2 s after a stop (`time.sleep(1.4)` from the key up) and its "to the
   partner (gopos)" teleports the walker 10-15 m: the puppet that followed the walker's NEW walk counted as gone past the
   old stop. Lab 20261113-202523 #2 (host sees guest): 0.35 m, maximum at +1.50 s, the walker's own creature left the
   stop at +1.03 s, 0.00 m while it stood; 20261113-195009 (12.73 m) and -200403 #1 (10.89 m) are gopos teleports
   inside the window (the guest's walk log: "back 15m (no closer); to the partner (gopos)").
2. The mod (observed, 20261113-193854 guest sees host 0.66 m, 20261113-204729 #2 0.37 m): the walker's game sent its
   state at the first frame at least 50 ms after the last send. At 21-25 fps (48 / 40 ms frames, the loaded lab's host)
   that is every other frame: the host's states 91-95 ms apart (send interval p50 of the walker's own rows), 10.4/s
   instead of 20/s. The puppet's buffer (interpolated 0.1 s back) then ran past its newest state in 17-26 % of the frames
   (ex > 0), its usual horizon grew and with it the full-speed part of drive.puppet_fall's extrapolation (knee p50
   0.04-0.05 s, up to 0.14 s; 0.020 s at 20 states/s). A stop whose news came late was shown walking on: 20261113-205159
   (21 fps, guest sees host, stop at 9.6 s): the target at the full 4.75 m/s at ex 0.136 s (age 0.236 s), then stepped
   back 0.6 m when the stop came; the puppet 0.37 m past the walker's own stop (bodyrec, while it stood).
   Frame-rate dependent: at the lab's usual 40-60 fps on the host the interval is 50-66 ms and the overshoot stays under
   0.3 m (20261113-202523 / -203423: 0 of ~200 stops over 0.3 m while the walker stood). Also on v0.7.13 (the send rule
   is older than 0.7); not run there.

## Fix
* Test (tools/tests/body_trail.py `stop_slides(cut=True)`): the slide is measured only while the walker's own creature
  still stands within 0.15 m of its stop (the radius that defines the stop); the uncut measure is printed, not judged.
* Mod `coop.fix net.send_phase` (net/interp.h `net::state_due`, used by coop.cpp's state send): the schedule keeps its
  phase (the next state is due 50 ms after the last one was due, from now again after a hitch of over 100 ms), so the
  rate stays 20/s down to 20 fps; and a creature that walked (over 1 m/s in the last state sent) and stands now (under
  0.3 m/s) is sent at once. Off: 0.7.13. The other stages' senders (tribe 0.12 s, civ / cell / squadron 0.1 s) have the
  same first-frame rule; at 21 fps they lose a third of their rate, not half; not changed here.
* Diagnostics: coop.track dump columns ex age knee hz ow (the puppet's extrapolation, its newest state's age, the fall
  knee, the median horizon less the one-way latency, the one-way latency; the own avatar: age = time since its state
  was due); coop_puppet_walk saves both games' bodyrec rows in the track json.
* The puppets' fall knee (net.send_phase) reads the median of the last 64 arrival horizons, not 32: at 20 states/s 32
  were 1.6 s and a spike of SPORE_NET=player (+250 ms for 0.7 s) filled half of them (knee 0.15-0.17 s, a stop shown
  0.46 m / bodyrec 0.52 m past, lab 20261113-211453 #3); 64 is 3.2 s again. The members' / proxies' lead keeps 32.

## Verification
- FAIL (test cause, the old 1.5 s window): out/lab/20261113-202523 #2 (host sees guest 0.35 m, walker off its stop at
  +1.03 s); the same rows judged with the cut: PASS (0 of 13). Before: 20261113-195009, 20261113-200403 #1 (gopos).
- FAIL (mod cause, without net.send_phase, SPORE_FPS=21): out/lab/20261113-204729 #2 (guest sees host: bodyrec 0.37 m
  while the walker stood, 1 of 12 stops), also 20261113-193854 (0.66 m, observer at 20 fps). 21 fps without the fix
  (20261113-204729, -204732, -205731, -205734 with SPORE_LAB_FIXOFF=puppet_stops=net.send_phase): host's sends p50
  91-95 ms, ex > 0 in 18 % of the frames, knee p50 0.036 s, 8 of 234 track stops with the target over 0.3 m past.
  Unit: `SPORE_FIX_OFF=net.send_phase build/host/net_tests` FAILED (3) (10.8 states/s, target past p90 0.47 max 0.67 m).
- PASS (21 fps, the fix): out/lab/20261113-210507, -210510 (an earlier build, same rule): sends 50 ms, knee p50
  0.020 s, 0 of 134 track stops over 0.3 m, bodyrec max 0.06 m; out/lab/20261113-212915 (3 of 3, the final build):
  bodyrec 0 of 99 stops over 0.3 m, max 0.27 m. Unit: net_tests ALL OK (20 states/s, p90 0.32 max 0.55 m).
- PASS (the lab's usual frame rate, final build): out/lab/20261113-212918 (puppet_stops, puppet_walk, puppet_air,
  puppet_jump, puppet_fight OK), out/lab/20261113-214145 (puppet_stops 2 of 2).
- Not fixed: a stop inside a long spike (0.5-0.6 s without a state) can still overshoot 0.3-0.5 m
  (20261113-211453 #3 before the 64-horizon knee); the interpolation can not know of the stop before its state comes.

## History
- 2026-11-13: opened.
- 2026-11-13 (fix/w49-stops): two causes: the test's stop window ran into the next walk / gopos, and the walker's
  state rate halved at 21-25 fps (the first-frame send rule). Fixed: `stop_slides(cut=True)`, `net.send_phase` (phase
  kept, a stop sent at once, the puppets' knee over 64 horizons). BUG-1095 opened and fixed on the way (drive.air_keep).
