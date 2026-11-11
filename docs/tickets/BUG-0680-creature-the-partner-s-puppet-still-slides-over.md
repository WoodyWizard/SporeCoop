---
id: BUG-0680
title: Creature: the partner's puppet still slides over 0.3 m past where the partner stopped at about 1 stop in 12 (rest of BUG-0408; puppet_stops flaky)
status: fixed
kind: desync
stage: creature
reported: 2026-11-07
source: lab 20261107-132459 (A/B: rel/0.7.9 1/4 PASS, rel/0.7.10 3/4 PASS) and 20261107-124806
role: 
versions: 0.7.9, rel/0.7.10
fixed_in: 0.7.11
branch: fix/w46-k4-bodies
commits: a25b8569, afc003d4
tests: puppet_stops, net_tests test_lead_fall (puppet_stop_past)
signature: a stop in a latency spike: the target steps back 0.7 m when the stop state comes
docs: docs/re/tribe.md#tribe-lead-fall, docs/06-testing.md#testing-lead-fall
related: BUG-0408
---

## Report
puppet_stops (SPORE_NET=player) failed 3 of 4 on v0.7.9 and 1 of 4 on rel/0.7.10 (lab 20261107-132459 of the rel/0.7.9 and
rel/0.7.10 worktrees): the puppet over 0.3 m past where the walker's own creature stopped after 1 of 10-14 stops.

## Root cause
Observed (the track rows of 20261107-132459 #1, guest sees host, the stop at 24.85 s): the walker at 4.75 m/s stopped
while its states came late (a spike of the player network); the puppet's buffer (interpolated, delay 0.1 s) extrapolated the
newest walking state at 4.75 m/s (max_extrap 0.25 s) and the target walked on; when the stop came it stepped back 0.72 m and
the puppet stood 0.33 m (bodyrec) / 0.86 m (target) past the stop. The same class as BUG-0674 (a body moved on through a
spike).

## Fix
`coop.fix drive.puppet_fall` (net/interp.h, SnapshotBuffer::interp_fall, set for Creature puppets in coop.cpp
drive_puppet): past the newest state the target goes on at full speed for kPuppetFallMargin 0.02 s past the usual render
horizon, then falls (kPuppetFallTau 0.05 s), its velocity too, so creature.puppet_stop's hold (own speed <= 0.3 m/s) holds
the puppet where the target slows. Unit test: a stop in a 0.25 s spike, the target 0.71 -> 0.17 m past it. Off: 0.7.10.

## Verification
- FAIL: `SPORE_FIX_OFF=drive.puppet_fall build/host/net_tests`: FAILED (2) (the target 0.71 m past the stop, its speed never falls);
  the lab failures before: out/lab/20261107-132459 (v0.7.9 1 of 4 PASS). With the switch off on this branch puppet_stops did not
  fail in 58 stops (out/lab/20261108-012638, max 0.15 m past): the spike has to fall on a stop.
- no-fail-run: the lab repro needs a spike on a stop (~1 stop in 12); the deterministic FAIL is the unit test above.
- PASS: net_tests ALL OK; puppet_stops out/lab/20261108-020506, -020506-2, -023036 (4 runs, 0 of 53 stops over 0.3 m past).

## History
- 2026-11-07: opened.

Observed: puppet_stops x4 on each, same time, lanes 4/5: v0.7.9 1/4 PASS (fails: 1 of 14, 1 of 11, 1 of 10 stops over 0.3 m, both directions), rel/0.7.10 3/4 PASS (the fail: too few stops to judge). Not a 0.7.10 regression; BUG-0408 holds for most stops but not all.
- 2026-11-08 (fix/w46-k4-bodies, K4): fixed (drive.puppet_fall); the lab did not reproduce it with the switch off in 58 stops, the unit test does.
- 2026-11-08 (fix/w46-k4-bodies, 456ced2c, rel/0.7.11 merged in): final runs: out/lab/20261108-020506 (puppet_stops, body_trail_tribe, body_trail_tribe_player, body_trail_creature, body_trail_civ, body_trail_player OK; play_creature failed 'joint hunts' once, OK on rerun out/lab/20261108-023606), out/lab/20261108-020506-2 (play_civ, body_trail_tribe_hold, body_trail_civ_loss, body_trail_hits OK; play_tribe failed its order checks once (BUG-0397 / BUG-0531 class), OK on rerun out/lab/20261108-023535; puppet_stops failed only 'enough stops to judge' (6 of 8), 0 stops past), out/lab/20261108-023036 (puppet_stops 2 of 2): puppet_stops 4 runs, 0 of 53 stops over 0.3 m past.
- 2026-11-08: released in 0.7.11.
- 2026-11-10 (plan/w47-closed-fixed): fixed-ticket review: drive.puppet_fall is applied to Creature puppets only (coop.cpp:901), the Cell puppet keeps the full-speed extrapolation; new ticket BUG-0887.
