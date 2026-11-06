---
id: BUG-0619
title: Tribe: with drive.lead the guest's member mirrors catch up in 0.9 m steps per 32 ms frame over a jittery network (tribe_walk_mobile fails)
status: open
kind: desync
stage: Tribe
reported: 2026-11-06
source: fix/w45-remote-bodies-2 regression runs
role: guest
versions: unreleased (rel/0.7.9 + drive.lead)
fixed_in:
branch: fix/w45-remote-bodies-2
commits:
tests: tribe_walk_mobile (tools/tests/coop_tribe_walk.py --fps 30, SPORE_NET=mobile)
signature:
docs:
related: BUG-0425, BUG-0386, BUG-0365
---

## Report
Found in M2's runs (fix/w45-remote-bodies-2 on rel/0.7.9 + M1's drive.lead). tribe_walk_mobile (both games at 30 fps, netem mobile: 120 ms, 60 ms jitter, 3 % loss) fails its check 'guest sees the host's members (mirrors): no jump of a member while it walks': out/lab/20261106-064239 #1 (5 jumps: (9, 18.76 s, 0.96 m, 32 ms), (9, 18.79, 0.90), (1, 30.65, 0.95), (1, 30.68, 0.92), (1, 33.27, 0.91); 58 target corrections) and #2 (2 jumps); out/lab/20261106-081748 tribe_walk_mobile #1 and #2 (1-2 jumps each, 'uneven 18 %'); out/lab/20261106-063850 (4 jumps 0.9-1.25 m and 'uneven 36 %', with a tried lead smoothing since removed). With SPORE_FIX_OFF=drive.lead it passed 2 of 2 (out/lab/20261106-062826); with the lead it passed once (out/lab/20261106-053105).

How to reproduce: tools/lab.py run tribe_walk_mobile --repeat 3 --retry 0 (and with SPORE_FIX_OFF=drive.lead).

## Root cause
Inferred, not analysed: the steps come in pairs of 0.9-0.96 m per 32 ms frame (28-30 m/s) right after the lead target was corrected by a late state (jitter: the lead's age and velocity change between states); walk_place's catch_prop (BUG-0425: the error closed in ~0.25 s at up to twice the remote's speed + 3 m/s) then moves a 9-12 m/s member (2 x 9 + 3) x 0.032 = 0.67 m up to ~0.9 m per frame, which the test counts as a jump. Without the lead the same corrections are smaller (the interpolated target does not extrapolate).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

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
