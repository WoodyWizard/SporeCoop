---
id: BUG-0675
title: Creature: over the player network a pushed NPC's copy stands while the host's NPC is thrown, then steps after it (body_trail_hits at 30 fps, SPORE_NET=player: over 2 m 0.88-0.96 s, max 6-7.4 m)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-07
source: fix/w46-k3-bodies labs 20261107-103414, 20261107-112617-2
role: guest
versions: 0.7.9 driving (NPC proxies unchanged in 0.7.10)
fixed_in: unreleased
branch: fix/w46-k4-bodies
commits: a25b8569, 1005b829, afc003d4
tests: net_tests test_lead_fall (throw_stand, jitter_err), body_trail_hits (SPORE_FPS=30 SPORE_NET=player)
signature:
docs: docs/re/tribe.md#tribe-lead-fall, docs/06-testing.md#testing-lead-fall
related: BUG-0409, BUG-0425, BUG-0674
---

## Report
body_trail_hits at 30 fps over SPORE_NET=player (45 ms, +250 ms spikes, loss bursts) failed 2 of 4: out/lab/20261107-103414 #2 (n19 hit at 44.0 s with a push: over 2 m 0.96 s, max 6.01 m) and out/lab/20261107-112617-2 #2 (n7 at 44.1 s, push: 0.88 s, 7.43 m); the limits are 0.75 s and 6 m. On a clean network the same test passes (out/lab/20261107-103912 2 of 2, 20261107-101948 era runs). In both the guest's game never threw the proxy (guest lm 0 only, the host's NPC lm 3 for 104-165 frames).

How to reproduce: SPORE_FPS=30 SPORE_NET=player tools/lab.py run body_trail_hits --repeat 4 --retry 0.

## Root cause
Observed (coop.bodyrec rows of 103414 #2, n19 after 44.0 s): the copy stood for 3-6 guest frames at a time (step 0) while the host's NPC moved 0.4-1.2 m a frame, then stepped 1.9-2.1 m: 7.2 m off at 45.18 s. Inferred: the lead does not move a body on whose path between its two newest states is faster than 15 m/s (net/interp.h lead_velocity: taken as put somewhere), and a push throws the host's NPC at 13-31 m/s, so the copy waits for each state; over the player network the states of a throw come in a spike or a loss burst and the waits add up. Not BUG-0409's hold (the guest's game undoing the placing): no throw on the guest.

K4 (observed, coop.npctrack rows of lab 20261107-103414 #2, n19 at 44.0 s): the host's thrown NPC reports its throw
speed (path 19.6-20.4 m/s, state velocity 19.4-20.4 m/s, same way), so the 15 m/s rule took a real move for a put. And
(lab 20261108-005832, t3 at 3.2-3.6 s) under a lab load of ~30 two member states 10-20 ms apart in the host's time carry
a 33 ms walk: a 9-10 m/s member read 12-18 m/s and its lead stood 0.4 s (6 m off).

## Fix
`coop.fix drive.lead_throw` (net/interp.h lead_velocity): a path over 15 m/s is not a put when the state's own velocity
agrees (within 35 %, ~20 degrees: a throw) or the step is under kPutStep 1 m (the sender's stamps off its frames). The lead
then goes on with the state's velocity (bounded by drive.lead_fall). Off: 0.7.10.

## Verification
- FAIL: out/lab/20261108-013741-2 #2 (SPORE_FIX_OFF=drive.lead_throw, body_trail_hits at SPORE_FPS=30 SPORE_NET=player: max 6.50 m,
  over 2 m 0.76 s, n19); `SPORE_FIX_OFF=drive.lead_throw build/host/net_tests`: FAILED (2) (a thrown NPC's target stands 0.07 s
  per gap, a stamp-jittered member 0.24 m mean error).
- PASS: out/lab/20261108-014500-2 (body_trail_hits 2 of 2: hits max 4.05 / 3.10 m, over 2 m 0.45 / 0.26 s), out/lab/20261108-015610
  #1; net_tests ALL OK.

## History
- 2026-11-07: opened.
- 2026-11-07: opened (fix/w46-k3-bodies, K3, from the bad-network attempt on BUG-0409); not fixed there.
- 2026-11-08 (fix/w46-k4-bodies, K4): fixed (drive.lead_throw).
- 2026-11-08 (fix/w46-k4-bodies, 456ced2c, rel/0.7.11 merged in): final runs: out/lab/20261108-020506 (puppet_stops, body_trail_tribe, body_trail_tribe_player, body_trail_creature, body_trail_civ, body_trail_player OK; play_creature failed 'joint hunts' once, OK on rerun out/lab/20261108-023606), out/lab/20261108-020506-2 (play_civ, body_trail_tribe_hold, body_trail_civ_loss, body_trail_hits OK; play_tribe failed its order checks once (BUG-0397 / BUG-0531 class), OK on rerun out/lab/20261108-023535; puppet_stops failed only 'enough stops to judge' (6 of 8), 0 stops past), out/lab/20261108-023036 (puppet_stops 2 of 2): puppet_stops 4 runs, 0 of 53 stops over 0.3 m past.
