---
id: BUG-0674
title: Tribe and Creature: a remote body goes on the wrong way through a latency spike (the lead extrapolates up to 0.6 s at the old speed while the host body slows or turns): members 8-9 m off, NPC proxies teleported 2.5-5.6 m
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-07
source: fix/w46-k3-bodies lab 20261107-102038
role: guest
versions: unreleased (rel/0.7.10 + fix/w46-k3-bodies)
fixed_in: 0.7.11
branch: fix/w46-k4-bodies
commits: a25b8569, 6f79bc2e, 090caf4d, afc003d4, 34c2d19c
tests: net_tests test_lead_fall, npc_walk_tribe (SPORE_FPS=30 SPORE_NET=player), body_trail_hits, body_trail_tribe_player
signature:
docs: docs/re/tribe.md#tribe-lead-fall, docs/06-testing.md#testing-lead-fall
related: BUG-0410, BUG-0425, BUG-0445
---

## Report
body_trail_tribe_player#1 of lab 20261107-102038 (fix/w46-k3-bodies 6baee26a, lane 7, load ~15-25 on 6 cores) failed `no copy over 2 m off for more than 1.0 s` by 0.01 s (longest 1.01 s, t2 at 4.1 s; members p90 1.81 m passed). In the track (out/lane7/track/body_trail_tribe_*.json of that run, coop.bodyrec): at 3.85 s the guest's mirror of member 2 stepped 2.97 m in one 75 ms frame (a 151 ms guest frame follows), then walked 5 m in the opposite direction from the host's member (y -5.5 -> +1.3 while the host's went -4.0 -> -6.3) for 0.45 s, 8.9 m off at 4.6 s, and closed it in ~1 s. The host's rows show no jump of the member.

How to reproduce: tools/lab.py run body_trail_tribe_player --repeat 3 --retry 0 with SPORE_BODYREC_DIAG=1 (the aim / raw lead rows show whether the target went the wrong way); seen once in 9 runs of this branch.

## Root cause
Observed (lab 20261107-102038 #2, member 1 at 19.4-20.6 s, coop.bodyrec rows of both games): the host's member walked 1.4 m one way (0.29 m a frame) and turned round at 19.6 s; the guest's mirror went on in the first direction at ~12 m/s for 0.55 s (4.6 m), 8.5 m off at 20.25 s, then came back in ~0.6 s; over 2 m 1.18 s. #1 (member 2 at 3.8-5.0 s) is the same shape (5 m the other way, 8.9 m off). That is the lead extrapolating the newest state for up to kMaxLead 0.6 s while no newer state comes (the player network's +250 ms spikes and 30 % loss bursts) and the body reversing meanwhile: error up to 2 x speed x gap. Inferred: 0.7.9 had it too wherever the lead was on (M1's raw lead goes the same way); the interpolated 0.7.8 driving never overshoots but is 0.2 s + latency late everywhere. Options (not done): extrapolate past the usual horizon at a falling speed (costs lag on straight walks in every spike), or close errors over ~5 m faster.

How to tell it in a run: a member's worst samples of 8-9 m at 4-10 m/s in body_trail_tribe_player while its p90 is fine.

K4 (observed): the same in a Space ship's VelocityEstimate::at (11.4 m past a stopped ship, space_fast_sync) and in
the walked NPC proxies over the player network (npc_walk_tribe track rows, lab 20261108-011449 / -012638-2): a walk that
started in a spike was known 0.3-0.5 s late and the proxy's target jumped 2.6 m at the start (no drive.lead_late for
proxies), and a turn known late jumped the target 2.6-4 m, onto which walk_place's snap teleported the proxy.

## Fix
`coop.fix drive.lead_fall` (net/interp.h `net::fall_time`, one rule for every body moved on past its newest state): at
the full speed up to the usual horizon (the median of the last 32 lead horizons) + kFallMargin 0.04 s, then falling
exponentially (kFallTau 0.12 s), the velocity handed to the driving with it. The cost: NPCs walking straight on through
a spike are shown behind (body_trail_hits NPC p90 0.33-0.37 -> 0.56-0.58 m of the 0.6 m limit, A/B lab 20261108-014500 /
-2); moving the knee on by half a late state's extra age (kFallSpike, tried) made the proxies worse (npc_walk_tribe 0 of 2,
body_trail_hits 1 of 2, lab 20261108-015610) and was reverted. VelocityEstimate::at (Civ vehicles,
Space ships) with the plain knee. The NPC proxies also get drive.lead_late's late_time (`late_lead`) and the smoothing of
every lead jump up to 8 m (`smooth_max`). Off: 0.7.10. Unit tests: a member turning round in a 0.35 s spike 6.0 -> 4.5 m
worst, stopping 3.0 -> 1.5 m, straight on 0 -> 1.7 m (the cost), a ship stopping 8.4 -> 5.1 m, a proxy's late start 3.3 ->
0.64 m per frame, a proxy's turn 3.0 -> 0.44 m per frame.

## Verification
- FAIL: out/lab/20261108-011449-2 (SPORE_FIX_OFF=drive.lead_fall, npc_walk_tribe at SPORE_FPS=30 SPORE_NET=player: 3 of 3 FAIL,
  4-12 proxy jumps); K3's 20261107-112016 / -112016-2 on the code before (6 of 6). `SPORE_FIX_OFF=drive.lead_fall
  build/host/net_tests`: FAILED (6).
- PASS: out/lab/20261108-014500-2 (npc_walk_tribe 2 of 2 and body_trail_hits 2 of 2 with the fix; at the same time on lane 6
  with the K4 switches off, out/lab/20261108-014500: npc_walk_tribe 1 of 2); out/lab/20261108-013741 #1, #3; net_tests ALL OK.
- Fragile: npc_walk_tribe on the kFallSpike build showed a proxy's 6 m one-frame jump (out/lab/20261108-015610 #1: its target
  right for one frame and back on the old line the next; not reproduced by a unit simulation of 400 random turns in spikes).

## History
- 2026-11-07: opened.
- 2026-11-07: opened (fix/w46-k3-bodies, K3), not fixed there.
- 2026-11-07 (fix/w46-k3-bodies): cause observed in the two runs (the lead's extrapolation through a spike while the host's member turned round); a known limit of the lead, left open for 0.7.11.
- 2026-11-07 (fix/w46-k3-bodies): the after-part fixed for members (drive.lead_keep: no step back when continuity is lost, net_tests: over 2 m 0.67 s -> 0 s after the news); the extrapolation itself stays. The same mechanism makes walked NPC proxies jump 2.5-5.6 m over the player network (BUG-0445's variant, npc_walk_tribe labs 20261107-103414, -112016, -112016-2: on 0.7.9 and 0.7.10 alike). Open for 0.7.11: a lead that slows past the usual horizon, or a smaller kMaxLead for proxies.
- 2026-11-08 (fix/w46-k4-bodies, K4): fixed (drive.lead_fall: the fall past the usual horizon, the proxies' late lead and smoothing); see Verification.
- 2026-11-08 (fix/w46-k4-bodies, 456ced2c, rel/0.7.11 merged in): final runs: out/lab/20261108-020506 (puppet_stops, body_trail_tribe, body_trail_tribe_player, body_trail_creature, body_trail_civ, body_trail_player OK; play_creature failed 'joint hunts' once, OK on rerun out/lab/20261108-023606), out/lab/20261108-020506-2 (play_civ, body_trail_tribe_hold, body_trail_civ_loss, body_trail_hits OK; play_tribe failed its order checks once (BUG-0397 / BUG-0531 class), OK on rerun out/lab/20261108-023535; puppet_stops failed only 'enough stops to judge' (6 of 8), 0 stops past), out/lab/20261108-023036 (puppet_stops 2 of 2): puppet_stops 4 runs, 0 of 53 stops over 0.3 m past.
- 2026-11-08: released in 0.7.11.
