---
id: BUG-1235
title: Tribe: member mirrors on the guest jump once or stop-and-go 12-14% of frames at 30 fps over the player network (tribe_walk_player, old: 0.7.14.1 4 of 4)
status: open
kind: desync
stage: Tribe
reported: 2026-11-18
source: final check 20261117-154911
role: 
versions: 0.7.14.1, unreleased
fixed_in:
branch: fix/w62-flake-tcs
commits: fbdd2510
tests: tribe_walk_player
signature:
docs: docs/known-issues.md#issue-tribe-walk-player-jumps
related: BUG-0650, BUG-0651
---

## Report
0.7.15 final check (lab 20261117-154911): tribe_walk_player FAIL "guest sees its own members (mirrors): no jump of a
member while it walks (3)"; rerun 20261117-213354 "no stop-and-go while the members start and stop (uneven in 12% <=
10% of 183 frames at 1-5 m/s)". The test runs at 30 fps (`SPORE_FPS=30`) with the player network profile.

## Root cause
(2026-11-21, fix/w62-flake-tcs) The jump: found. Every "no jump" FAIL of this test in the lab's kept runs is one
signature: 20261121-003202 (rel/0.7.16 21025f40, host's member 3 at 23.55 s: 0.95 m in 32 ms), 20261119-214823
(rel/0.7.16 before w61, guest's member 7 at 28.49 / 28.53 s: 0.96, 0.95 m), 20261117-154911 (rel/0.7.15, member 2:
1.01, 0.90, 0.88 m). The test's limit is 3 x 9 m/s x 32 ms = 0.86 m. In the per-frame samples (out/lane4/track/
tribe_walk_host_walks_060526.json, out/lane3/track/tribe_walk_guest_walks_014348.json of the rel/0.7.16 worktree):
the member stood 5-8 s, starts to walk; on the guest the mirror stands still for ~0.1 s, falls 1-2 m behind its target,
the aim speed (coop.track rv) goes to 13.3-13.7 m/s (the late lead catching up at 1.5x the body's 9 m/s, BUG-0650) and
the frames that jump are the placing's step (13.6 + 3 m/s) x 32 ms = 0.52 m plus the game's walk of the next frame at
the measured 13.4-13.7 m/s (gv) = 0.43 m. That placing has neither WalkPlace::lead_speed's bound (2 x the body's speed
+ catch_up a frame, BUG-0651) nor catch_prop (catch_up stays 3 m/s; with catch_prop and a 1.9 m error it would be
7.6 m/s): the mirror was driven by the SMOOTHED target (a walk start: the switch from the interpolated stand to the lead
goes through the smoothing, interp.h smooth(), mirror_target's `lead_sp >= 0` path). BUG-0619 bounded that path by
lead_sp (0de80a04); the BUG-0651 change 1fcf09b2 (0.7.9) replaced the bound with the raw lead's (`lead_sp < 0`) only,
so a smoothed walk start whose late state is caught up walks at 13.4-13.7 m/s with the full (sp + 3) x dt placing on
top: 2 x 13.6 + 3 = 30 m/s frames. Old (0.7.9 on), not a w61 regression: drive.lead_gap (BUG-1376) acts only after a
gap of over 2 s between two host states, and the host sends every member each cycle (tribe_host.cpp), no host stall in
the run (host log 06:05:00-06:05:30: no hitch). The stop-and-go part (uneven 12-14 %, BUG-1235's other half) is not
this cause; it was not seen in this run (5 % / 8 %). Earlier observations: old, v0.7.14.1 fails 4 of 4 on lanes 6 / 7 (tag/v0.7.14.1-w51tribe out/lab/20261117-231627:
one jump of a member, or stop-and-go 13-14%), rel/0.7.15 3 of 4 (out/lab/20261117-223551, lanes 4 / 5: one jump, walk
gait 79% < 80%, at its speed 84% < 85%), the headless Xwayland saturated (90-92%) in two of them. History: 28 of 70
failed on all branches since 2026-11-01.

## Fix
Not fixed in 0.7.15 (an old desync at 30 fps, owner informed); next release.
2026-11-21 (fix/w62-flake-tcs): `coop.fix tribe.smooth_bound` (on): tribe_logic::mirror_lead_speed: in the smoothed
path the placing is held by the body's own speed too (lead_sp / late_rate: 9 m/s in the lab's case), so the frame
draws at most (2 x 9 + 3) x dt = 0.67 m; the raw path keeps drive.lead_keep's rule. Off: no bound in the smoothed path
(0.7.16 rc). Unit test: tests/tribe_tests.cpp smooth_bound (the lab's numbers; fails with `--nofix`). The stop-and-go
half of this ticket has no fix yet (see Root cause).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Open, waiting for the lab run: `lab.py run tribe_walk_player --repeat 4` must PASS 4 of 4 on the jump check (fails
before: 20261121-003202, 20261119-214823, 20261117-154911). The case is the game's timing (a walk start in a latency
spike of the player network, about 1 run in 3), so no suite twin: `SPORE_FIX_OFF=tribe.smooth_bound lab.py run
tribe_walk_player --repeat 4` is expected to show the 0.95 m jump in some runs; the deterministic proof is
tribe_tests smooth_bound (PASS with the rule, FAIL with `--nofix`).

## History
- 2026-11-21 (fix/w62-flake-tcs): the jump's cause found (the smoothed walk start without the frame bound, since
  1fcf09b2), tribe.smooth_bound; the stop-and-go half stays open.
- 2026-11-18: opened in the second 0.7.15 final-check triage; old (0.7.14.1 fails it too), left for the next release.
- 2026-11-23: fix/w65-night: smooth_bound held a late walk start to the host's current speed (BUG-1451, body_trail_tribe_player 1.16 s over 2 m); tribe.smooth_catch lets the bound grow with the lag, capped at the 9 m/s this ticket's case keeps (its frames still at most 0.67 m).
