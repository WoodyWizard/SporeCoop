---
id: BUG-0619
title: Tribe: with drive.lead the guest's member mirrors catch up in 0.9 m steps per 32 ms frame over a jittery network (tribe_walk_mobile fails)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-remote-bodies-2 regression runs
role: guest
versions: unreleased (rel/0.7.9 + drive.lead)
fixed_in: unreleased
branch: fix/w45-remote-bodies-2
commits: fce3ec36..42951bdc (25 BUG-0619 commits of M3 on fix/w45-remote-bodies-2; final rule 911d7b86, 923a098e)
tests: tribe_walk_mobile (tools/tests/coop_tribe_walk.py --fps 30, SPORE_NET=mobile)
signature:
docs: docs/re/tribe.md#tribe-lead-smooth
related: BUG-0425, BUG-0386, BUG-0365, BUG-0650
---

## Report
Found in M2's runs (fix/w45-remote-bodies-2 on rel/0.7.9 + M1's drive.lead). tribe_walk_mobile (both games at 30 fps, netem mobile: 120 ms, 60 ms jitter, 3 % loss) fails its check 'guest sees the host's members (mirrors): no jump of a member while it walks': out/lab/20261106-064239 #1 (5 jumps: (9, 18.76 s, 0.96 m, 32 ms), (9, 18.79, 0.90), (1, 30.65, 0.95), (1, 30.68, 0.92), (1, 33.27, 0.91); 58 target corrections) and #2 (2 jumps); out/lab/20261106-081748 tribe_walk_mobile #1 and #2 (1-2 jumps each, 'uneven 18 %'); out/lab/20261106-063850 (4 jumps 0.9-1.25 m and 'uneven 36 %', with a tried lead smoothing since removed). With SPORE_FIX_OFF=drive.lead it passed 2 of 2 (out/lab/20261106-062826); with the lead it passed once (out/lab/20261106-053105).

How to reproduce: tools/lab.py run tribe_walk_mobile --repeat 3 --retry 0 (and with SPORE_FIX_OFF=drive.lead).

## Root cause
Observed (M3, track JSON of tribe_walk_mobile, out/m3/jumps.py logic: per jump frame the drawn step, the target's steps of
the frame and the 3 before, the error before and after): 12 of 12 decomposed jumps (0.88-0.97 m in 32 ms) come 1-3 frames
after the lead target ITSELF jumped 2.7-4.8 m in one frame at the start of a walk (target steps 0.0, then 2.7-4.8 m, then
0.29 m = 9 m/s): the first walking state arrives a latency + jitter late and the lead moves it on by v x (age + one-way),
up to 0.45 s x 9 m/s over the mobile network. In the jump frames the target is steady; the drawn step is the game's walk
plus catch_prop's catch-up measured from where the walk had put the body (lab out/lab/20261106-130123, -064239).
Observed too: a frame's drawn step is the placing's step from the walked position plus the NEXT frame's walk (a bound
from the last placing did not stop the jumps, 20261106-145657 / -145700); and with the jumps gone a smoothed lead still
went stop-and-go over the mobile network (uneven 11-22 %) where 0.7.8 passed at the same time (20261106-154457 0 of 3
vs SPORE_FIX_OFF=drive.lead -154500 3 of 3; lanes swapped -155349 3 of 3 / -155352), because the lead extrapolates
0.2-0.3 s there and every change of speed between two states moves the target. Inferred: the same on any link with
a one-way latency over ~0.1 s.

## Fix
`coop.fix drive.lead_smooth` (off = M1's drive.lead as merged; `coop.fix drive.lead off` stays 0.7.8), Tribe member
mirrors only (SnapshotBuffer::smooth_max 6 m; NPC proxies unchanged):
1. No lead for member mirrors over a one-way latency over 100 ms (the session's half round trip, median of the last 64
   states; back under 85 ms): interpolated and driven exactly as 0.7.8 (no catch_prop), smooth and ~0.2 s + latency
   late. Measured: netem mobile 123-149 ms; the player network and a local link under 100 ms. (The states' arrival
   lateness, by mean, median or IQR, did not part the networks at the lab's load: the guest's frame times dominate it.)
2. Under that latency the lead is M1's (raw, catch_prop); a switch between the two goes through a decaying offset
   (net/interp.h SnapshotBuffer::smooth: closes at d / 0.12 s, at most the speed + 3 m/s; the mirror walks at the
   target's velocity eased up at 40 m/s2 and at most 3 m/s over the lead; driver.h WalkPlace::lead_speed holds the
   placing's step to (2 x lead speed + 3 - walk speed) x dt, so such a frame never draws more than 2 x speed + 3 m/s).
Tried and dropped (each in the branch history with its runs): smoothing every lead step (no jumps, but stop-and-go over
mobile in about half the runs and more trail over the player network), eased closing, 1.25x closing, walking at the full
target speed, a bound from the last placing, closing only over 5 m/s, interpolation under 5 m/s, smoothing lead steps
over 1.5 m with the lead on.

## Verification
FAIL (SPORE_FIX_OFF=drive.lead_smooth, i.e. M1's lead): tribe_walk_mobile out/lab/20261106-130123 (0 of 3: 1-4 jumps
per view, 0.89-0.97 m), out/lab/20261106-172431 (0 of 2: 1-5 jumps); load 13-20 and 8-10 on 6 cores.
PASS, final code (96db920b + the revert 359ddb5e): tribe_walk_mobile out/lab/20261106-185808 (2 of 2), -185811 (2 of
2), -191334 / -191337 (2 of 2 each, same no-lead rule); body_trail_tribe -185811 (2 of 2, members p90 0.56-0.59 m),
body_trail_tribe_player -185808 (2 of 2, members p90 1.05-1.37 m), tribe_walk -185811 (2 of 2); load 4-9.
With the no-lead rule and earlier smoothing variants: tribe_walk_mobile -163609, -164635, -165644, -171543 (3 of 3
each), -171546 (1 of 1). Two failures in that family (-170614 #1, -172428 #2) were one member mirror held by its game
and teleported 8.1 m: BUG-0386 (evidence there).

## History
- 2026-11-06: opened.
- 2026-11-06: opened by M2 (fix/w45-remote-bodies-2): a regression of drive.lead (M1, BUG-0425) over a jittery network; not fixed here (outside M2's tickets), for M1 / the orchestrator.
- 2026-11-06 (fix/w45-remote-bodies-2, M2, after the merge of rel/0.7.9 1e155b53): tried, measured and removed (the code is back to M1's driver.h / interp.h):
  1. `drive.catch_bound` v1: catch_prop's placing at most (speed + catch_up) x dt from last frame's placing. Observed cause of the ~1 m steps: catch_prop measured its step from where the game's own walk had taken the body, so a frame's drawn step was walk + (2 x speed + 3) x dt (0.9-1.25 m at 9 m/s, 32 ms). With it no member jumped in 4 of 6 runs, but it pulled bodies back behind the game's walk: 'at its speed in 82 % of windows' (out/lab/20261106-105608, 1/6 passed).
  2. v2 (never behind the game's walk) + a slow catch-up cap: stop-and-go stayed (uneven 10-19 % of the 1-5 m/s frames; 20261106-113612, 1/6); the cap for NPC proxies lagged animals (body_trail_tribe).
  3. v4 (cap 0.35 x speed + 0.5 m/s for member mirrors only) and v5 (a new state's step of the lead target spread over 0.25 s for member buffers): still failing (20261106-121922 and after); the new hold probe showed members held 2-5.6 m off their aim for 1-21.8 s with `place lost 0` and no object within 3 m: the cap itself held them.
  Observed in the analysis of the failing frames (tribe_walk track JSON): the uneven frames are the mirror catching up 1.5-4 m at 6-10 m/s on a 2-5 m/s target after a lead correction at a walk's start or end; the target itself was steady in those frames (44 of 45 uneven frames).
  A/B under the same load (lab load 11-16 on 6 cores from other lanes, 'both games ready' 40-60 s instead of 30 s): drive.lead off passed 2 of 2 (20261106-121046, -121649), on failed (-121343). Under that load body_trail_civ (code no change of mine touches) failed too (p90 3.8-6.2 m, lag 0.3-0.8 s): the lab could not judge body_trail_* or a 6/6 at that time.
  What is needed: a quiet lab for the measurement, and a decision: the test judges a mirror's step against its (lead) target, which a start known a latency late must correct; either the mirror closes the start's error slowly (then it trails at every start, M1's BUG-0425 measure at 0.25-0.75 s gets worse) or quickly (then this test sees stop-and-go). Options: judge tribe_walk_mobile against the host's own members at the same moment (coop.bodyrec, as body_trail), or a jitter-aware lead for member mirrors (no lead while the link's jitter is high).
- 2026-11-06 (fix/w45-remote-bodies-2, M3): cause observed (the lead target's own start jump + catch_prop's catch-up),
  fixed with drive.lead_smooth (smoothed lead target, the placing bound counting the next walk, no member lead over a
  one-way latency over 100 ms); docs/re/tribe.md#tribe-lead-smooth. Status fixed, fixed_in unreleased.
- 2026-11-06 (M3, later): final: no member lead over 100 ms one-way, M1's lead under it (always smoothing it trailed
  more over the player network). tribe_walk_player (not in this ticket's list) still has M1's start jumps over the player
  network's latency spikes: BUG-0650.
