---
id: BUG-0408
title: Creature: the partner's puppet slides 0.4-0.6 m past where the partner stopped and plays the fall animation for 0.2 s when it stops on a slope
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: lab
role: host
versions: 0.7.3.3
fixed_in:
branch: fix/w34-walk-stable
commits:
tests: puppet_slope, puppet_walk
signature: the puppet plays the fall animation 0x0692E6A9 for ~0.2 s right after the partner stops (its state speed 0), 0.1 m over the ground
docs:
related: BUG-0366, BUG-0360, BUG-0407
---

## Report
puppet_slope on fix/w33-null-call 793e207 (= main's driving), lab 20261101-135017 (lane 2): "the puppet does not
start falling more than the walker's own creature (3 <= 0 + 1)". Found while classifying the walk tests' failures
(BUG-0407).

## Root cause
Observed (coop.track, host watches guest, track `out/lane2/track/puppet_walk_host_sees_guest_20261101-135755.json` of
the fix/w33-null-call worktree): all 3 falls start when the partner stops, none while it walks downhill (BUG-0366's
case is fixed). At t 3.97 s the partner's state speed drops to 0; the puppet still has the game's speed 2.6 m/s,
`walk_end` gives the MoveTo to where the partner stands and `drive::step` takes over (no placing, no ground
correction). The puppet decelerates over ~0.4 s, slides 0.4-0.6 m past the target, rises to 0.11 m over this game's
ground on the slope and plays the fall animation for 198 ms (t 4.17-4.37), then stays ~0.5 m off for ~0.9 s until the
next walk. The same at t 30.2 s (200 ms). Over every recorded fix-on puppet track (198 stops): the drawn error in the
second after a stop p50 0.06 m, p90 0.33 m, max 1.0 m; 22 stops over 0.3 m; a fall at the stop in 1 of 5 slope runs.
Inferred: holding the puppet on the partner's stopped position (on this game's ground) for the deceleration would
end both; not done on fix/w34-walk-stable (the branch makes the tests reliable).

## Fix
not fixed yet.

## Verification
no-fail-run: open

## History
- 2026-11-01: opened.
