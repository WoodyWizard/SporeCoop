---
id: BUG-1095
title: Creature: a puppet in the air stalls when the partner's states come late and jumps 1 m when they come (drive.puppet_fall in the air)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-stops
commits: 124a9ea9, 3a749886
tests: puppet_stops, net_tests
signature:
docs: docs/re/creature.md#creature-puppet-stop-send-phase
related: BUG-1093, BUG-0680, BUG-0376
---

## Report
Seen on fix/w49-stops (BUG-1093) in `puppet_stops` (lab 20261113-203423 #4, lane 2, host sees guest): "no jump of the
puppet while it walks (1)", a 1.02 m step of the drawn puppet in one 16 ms frame at 22.82 s. Observed in the track rows
(out/lane2/track/puppet_walk_host_sees_guest_20261113-204611.json, columns ex / age): the guest's creature was in the air
(flags 15, PF_JUMPING, 1.8-2.5 m over the host's ground, 9-10 m/s); its states came 0.2 s apart (age 0.203); past the
newest state the target's speed fell with drive.puppet_fall (rv 9.70 -> 1.83 m/s at ex 0.02 -> 0.10 s), the puppet,
placed on the target in the air, stopped in mid-air; the next state put the target 0.86 m on and the puppet was placed
there at once. Inferred: BUG-0680's fall of the extrapolation (a partner that stopped in a spike) also applies to an
airborne state, which cannot stop in the air. Reproduce: `lab.py run puppet_stops --repeat 4` (~1 run in 8 on a
loaded lab).

## Root cause
Observed (track rows ex / age / rv of 20261113-204611 and 20261113-211453 #1, guest sees host at 21 fps: flags 11, the
target at rv 0.1-0.3 m/s with ex 0.15-0.2 s, then a 0.9 m step at the next state): SnapshotBuffer's interpolated
extrapolation past the newest state falls (drive.puppet_fall, BUG-0680) for every state, an airborne one too; in the
air creature_drive places the puppet on the target (not walked), so the puppet stood still in mid-air and was put on at
once. Inferred: the fall is right for a walker that may have stopped, wrong for a body in flight.

## Fix
`coop.fix drive.air_keep` (net/interp.h, SnapshotBuffer::sample_raw): no fall for a newest state with PF_JUMPING; it is
moved on at its velocity up to max_extrap 0.25 s as before BUG-0680. The height in the air stays the state's agl over
this game's ground (drive.sender_agl), so a landing inside a spike does not take the puppet under the ground. Off:
0.7.13. Unit test: tests/net_tests.cpp puppet_air_step (a flight at 9.5 m/s with its states 0.25 s late: the target's
largest step in a 60 fps frame 0.96 -> 0.16 m; `SPORE_FIX_OFF=drive.air_keep` fails).

## Verification
- FAIL (without the fix): out/lab/20261113-203423 #4 (host sees guest, a 1.02 m step at 22.82 s), out/lab/20261113-211453
  #1 (guest sees host, 1.13 m at 15.83 s, 21 fps); `SPORE_FIX_OFF=drive.air_keep build/host/net_tests` FAILED (1)
  (the target's step 0.96 m).
- PASS (with it): out/lab/20261113-212915 (puppet_stops 3 of 3 at 21 fps), out/lab/20261113-212918 (puppet_stops,
  puppet_walk, puppet_air, puppet_jump, puppet_fight), out/lab/20261113-214145 (puppet_stops 2 of 2): no jump of the
  puppet while it walks in any; net_tests ALL OK (0.16 m).

## History
- 2026-11-13: opened.
- 2026-11-13 (fix/w49-stops): fixed (drive.air_keep) on the way of BUG-1093.
