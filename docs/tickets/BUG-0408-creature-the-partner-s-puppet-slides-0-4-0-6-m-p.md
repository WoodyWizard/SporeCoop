---
id: BUG-0408
title: Creature: the partner's puppet slides 0.4-0.6 m past where the partner stopped and plays the fall animation for 0.2 s when it stops on a slope
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: lab
role: host
versions: 0.7.3.3
fixed_in: unreleased
branch: fix/w45-remote-bodies-2
commits: aed2f801, 462fc0f0, 2f3cba6f, 42967676
tests: puppet_stops (coop_puppet_walk.py --dir both --guest-fps 30 --goto=-195,-465,-45 --stop-walk --secs 45, SPORE_NET=player)
signature: the puppet plays the fall animation 0x0692E6A9 for ~0.2 s right after the partner stops (its state speed 0), 0.1 m over the ground
docs: docs/06-testing.md#testing-detector-trail
related: BUG-0366, BUG-0360, BUG-0407
---

## Report
puppet_slope on fix/w33-null-call 793e207 (= main's driving), lab 20261101-135017 (lane 2): "the puppet does not
start falling more than the walker's own creature (3 <= 0 + 1)". Found while classifying the walk tests' failures
(BUG-0407).

## Root cause
Observed again (2026-11-06, fix/w45-remote-bodies-2) on both games' own positions (coop.bodyrec, the puppet against the walker's own creature at the same moment): the puppet went 0.50 m and 0.88 m past where the walker stopped (out/lab/20261106-053105), 0.81 m (20261106-065148), 0.3+ m (20261106-083248), in ~1 of 10-20 stops. Mechanism (code + rows): when the partner stops, the interpolated target still moves for the delay and drive::path_velocity keeps the walk going (`walk` while the path's speed > 0.3 m/s): walk_place's goal ahead and the game's deceleration carry the puppet past the stop; then walk_end's MoveTo and drive::step take over without placing it.

## Fix
`creature.puppet_stop` (modes.cpp creature_drive): when the partner's own state speed falls to 0.3 m/s or less after a walk, the puppet is held for 0.5 s: no path velocity, no walk, moved onto the partner's (stopping) target at most 4 m/s while within 1.5 m (placed at once, the puppet 0.5-1 m behind made a step), velocity 0. Off: 0.7.8. The 2026-11-04 version (hold only after walk_end) was not enough: it slid before the hold began (out/lab/20261106-053713, -061326 measured).

## Verification
puppet_stops judges the slide on coop.bodyrec (past where the walker's own creature stopped, at most 0.3 m in every stop; the old target-based 'slid on' measure counted walking up from 0.6 m behind and is printed only).
- FAIL: out/lab/20261106-083248 (SPORE_FIX_OFF=creature.puppet_stop: 0.99 m past the stop, 1 of 10 stops); also -065148 #2 (0.81 m) and -053105 (0.50 / 0.88 m, before the switch).
- PASS: out/lab/20261106-083011 (22 stops, at most 0.25 m past), -081748 #2 (<= 0.10 m).

## History
- 2026-11-01: opened.
- 2026-11-04: fix/w41-puppet-look: not reproduced with 21 stops (10 abrupt); the hold not kept.
- 2026-11-05 (fix/w44-height): not part of the height fix (a stop slide on the walk driving); not looked at again.
- 2026-11-06 (fix/w45-remote-bodies, M1): a different mechanism from the class's trail (BUG-0425): a slide past the
  stop point on the walk_end -> drive::step hand-over of the partner's puppet, not a target that is late. The lead
  (drive.lead) is not given to the partner's puppet (it would add an overshoot of speed x latency at every stop, the
  thing this ticket is about); not touched, stays open (not reproduced in 21 stops on 2026-11-04).
- 2026-11-06 (fix/w45-remote-bodies-2, M2): reproduced on both games' own positions (coop.bodyrec in coop_puppet_walk, suite puppet_stops); creature.puppet_stop again, the hold from the target's own stop; FAIL 20261106-083248, PASS 20261106-083011.
