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
branch: fix/w62-flake-tcs, fix/w76-motion
commits: fbdd2510 46863dd0
tests: tribe_walk_player, tribe_walk_player_lead_cap_off (twin), body_trail_tribe_player, tests/tribe_tests.cpp lead_cap
signature:
docs: docs/known-issues.md#issue-tribe-walk-player-jumps
related: BUG-0650, BUG-0651, BUG-1451, BUG-0619, BUG-0674
switch: drive.lead_cap
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
this cause; it was not seen in this run (5 % / 8 %). 2026-11-23 (fix/w76-motion, item 12 of docs/plan-0.7.17.md "raw-path catch_prop jumps"): the jumps left on fd28f88f
(after smooth_bound) are the RAW lead's frames, read in the per-frame tracks of 20261123-023643 / -071312 / -075458:
the game walks the mirror at its full speed (9-12 m/s) and the placing adds sp + catch_prop's catch-up (err / 0.25 s,
up to sp + 3): member 8 2.74 m behind at 9 m/s, 0.29 + 0.64 m; member 1 1.40 m behind in a 12 m/s run, 0.37 + 0.54 m.
The raw lead had a frame bound only while its late lead caught up (tribe_logic::mirror_lead_speed: 0 otherwise), and
that bound used catch_prop's catch-up (2 x lead + catch_up - sp = sp + catch_up: no bound). The stop-and-go half:
surges, not stands: at 1-5 m/s the frames drew the target's speed + 3.0-3.3 m/s (the base catch-up 3 m/s on top of
the game's walk; the guest's member 9 at 1.6 m/s drawn 4.8-5.0 m/s, uneven 20 % in 023643).
docs/re/tribe.md#tribe-raw-catch-frames. Earlier observations: old, v0.7.14.1 fails 4 of 4 on lanes 6 / 7 (tag/v0.7.14.1-w51tribe out/lab/20261117-231627:
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
2026-11-23 (fix/w76-motion), `coop.fix drive.lead_cap` (on; off = 0.7.16.1 exactly), the idea of fix/agy-open-bugs
c8c548db (the outside agent's branch), narrowed after its review (out/research/agy-review-misc.md finding 4):
drive_plan.h walk_place_max_step (walk_place's placing step as a pure rule) and tribe_logic::mirror_lead_speed. (1)
The frame bound covers every walked member mirror: the raw lead that does not catch up is bounded by the body's speed
(it was 0, no bound). (2) The bound uses the base catch-up (3 m/s), not catch_prop's: a frame draws at most
2 x lead_speed + 3 m/s (BUG-0619's promise; 0.67 m at 9 m/s and 32 ms). (3) With catch_prop the game's walk now counts in
the catch-up's budget: the frame closes the error at the catch-up over the body's speed (sp + catch_up in all, as
catch_prop meant; a 6-9 m error, BUG-0674, at 12 m/s over a 9 m/s body: under 2 m in about 0.6 s, was 0.33 s). (4)
The stop-and-go half: while the target is under 5 m/s and the error under 1.5 m a frame draws at most 1.5 x sp + 0.9
m/s whether or not the game's walk comes next frame (inside the test's uneven line). Untouched: the smoothed path over
1.5 m (BUG-1451's lag catch-up keeps (sp + 3) m/s of placing on top of the walk) and every walk_place without
lead_speed (NPC proxies, Civ). Unit test tests/tribe_tests.cpp lead_cap (the lab's two frames, the hard bound over
errors and walks, BUG-0674's big error, BUG-1451's case unchanged, the slow frames inside the uneven line; FAILs with
`--nofix`). Twin tribe_walk_player_lead_cap_off.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Open, waiting for the lab run: `lab.py run tribe_walk_player --repeat 4` must PASS 4 of 4 on the jump check (fails
before: 20261121-003202, 20261119-214823, 20261117-154911). The case is the game's timing (a walk start in a latency
spike of the player network, about 1 run in 3), so no suite twin: `SPORE_FIX_OFF=tribe.smooth_bound lab.py run
tribe_walk_player --repeat 4` is expected to show the 0.95 m jump in some runs; the deterministic proof is
tribe_tests smooth_bound (PASS with the rule, FAIL with `--nofix`).
- 2026-11-23 (fix/w76-motion), drive.lead_cap: open, waiting for the lab run (pending night run: tribe_walk_player
  tribe_walk_player_lead_cap_off body_trail_tribe_player body_trail_tribe body_trail_tribe_hold tribe_walk_mobile
  tribe_coop). `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_walk_player --repeat 4 --lane N --retry 0 --no-build`
  must PASS 4 of 4 (no jump, no stop-and-go over 10 %); `SPORE_DETECTOR=20 python3 tools/lab.py run
  tribe_walk_player_lead_cap_off --repeat 3 --lane M --retry 0 --no-build` must FAIL on the jump check in most runs (the
  timing of the network: 4 of 4 on fd28f88f); regression `body_trail_tribe_player --repeat 4` (longest member stretch
  over 2 m under 1.0 s), body_trail_tribe, body_trail_tribe_hold, tribe_walk_mobile, tribe_coop.
- Still open (2026-11-23). On 5376d388 no jump: tribe_walk_player PASS in 20261122-194210 and 3 of 4 in 20261122-193406 (no jump in any of the 5; #1 FAIL on the speed part, "at its speed in 83% >= 85%"). On fd28f88f (the same mod code) every run FAILs on jumps of 0.85-0.93 m in one ~32 ms frame, on the host's members and the guest's own: 20261123-023643 (3 jumps, and the stop-and-go part 20%), 20261123-071312 (3), 20261123-075458 #1 and #2 (2 each). Those are the catch-up frames of a late walk start (BUG-1451, tribe.smooth_catch, for 0.7.16.1), so the jump this ticket is about is not proven gone; the speed / stop-and-go part is open too. The CHANGELOG line was removed; the known issue stays.

## History
- 2026-11-23: lab evidence of the 0.7.16 final check added; stays open.
- 2026-11-21 (fix/w62-flake-tcs): the jump's cause found (the smoothed walk start without the frame bound, since
  1fcf09b2), tribe.smooth_bound; the stop-and-go half stays open.
- 2026-11-18: opened in the second 0.7.15 final-check triage; old (0.7.14.1 fails it too), left for the next release.
- 2026-11-23: fix/w65-night: smooth_bound held a late walk start to the host's current speed (BUG-1451, body_trail_tribe_player 1.16 s over 2 m); tribe.smooth_catch lets the bound grow with the lag, capped at the 9 m/s this ticket's case keeps (its frames still at most 0.67 m).
- 2026-11-23 (fix/w76-motion): the remaining jumps are the raw lead's catch-up frames (tracks of the 0.7.16 final check); drive.lead_cap bounds them and the slow surges; waiting for the lab.
