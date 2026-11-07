---
id: BUG-0410
title: tribe: the player tribe's citizens on the guest 1.9-2.0 m (p90) from the host's in play_tribe (limit 1.5 m)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-01
source: play_tribe, labs 20261101-134020 and 20261101-145528 (feat/w33-tests-session)
role: guest
versions: main a05d1f6
fixed_in: unreleased
branch: fix/w46-k3-bodies
commits: 407c1c97, 50232092, 1fcf09b2, b0f4e2eb, 0f0f32f1, 6baee26a, 51744dce, e266ef4e, 836b0b5e, d42bf1d5
tests: body_trail_tribe_player, body_trail_tribe, tribe_walk_player, tribe_walk_mobile, net_tests test_lead_keep
signature:
docs: docs/re/tribe.md#tribe-lead-keep, docs/06-testing.md#testing-bodyrec-diag
related: BUG-0425, BUG-0619, BUG-0650, BUG-0651, BUG-0674
---

## Report
`play_tribe` (10 min of two bots playing Tribe by real clicks, tribe fixture) failed `sync t*: 90% within 1.5 m`
in both of its runs on main a05d1f6 (lane 10): `sync citizens: samples 435 pos p50 0.16 p90 1.89 max 7.15 m`
(lab 20261101-134020, seed 490568738) and `samples 343 pos p50 0.30 p90 1.98 max 4.97 m` (lab 20261101-145528,
seed 789470738). Animals in the same runs: p90 1.04 / 0.98 m. Each sample is the guest's position of a citizen
against the host's positions read right before and right after it (the nearer one counts), so a plain
interpolation delay is mostly inside the bracket.

How to reproduce: `SPORE_LANE=N python3 tools/tests/play_tribe.py --minutes 10 --seed 789470738`.

## Root cause
Observed (2026-11-07, fix/w46-k3-bodies, coop.bodyrec diag rows: the driving's aim and M1's raw lead next to the drawn mirror, docs/06-testing.md#testing-bodyrec-diag). Three parts on rel/0.7.9's code:
1. BUG-0619's no-lead rule (no member lead over a one-way latency over 100 ms) turned the lead off for whole runs: the player network reads 80-101 ms and a guest under lab load 110-150 ms (the round trip counts both games' frame times); members were then interpolated, 0.2 s + latency late: p90 3.2-3.4 m (lab 20261107-073841 #1, #3); 25 of 60 s in the 0.7.9 final check run 20261107-012856 #2.
2. With the lead on, drive.lead_late's catch-up at 1.33x (+3 m/s) kept the shown target 0.25-0.3 s behind for ~0.75 s after every start known late (a spike, a loss burst): aim p90 2.36 m against the raw lead's 1.20 m (-073841 #2).
3. When no state came for a while (a spike) and the host's member turned round, the lead went on the old way (BUG-0674) and drive.lead_late then stepped the shown time back up to 0.75 s: 8-9 m off for 1.0-1.5 s (lab 20261107-110506 #1, member 2).
Also found while fixing: drive.lead_late's continuation used the shown target's own velocity, so after a frame that stepped back the next state stepped it back again (net_tests).

## Fix
`coop.fix drive.lead_keep` (net/interp.h SnapshotBuffer, tribe.cpp mirror_drive; Tribe member mirrors only):
* the no-lead rule's line at 130 ms (back under 115 ms) instead of 100 / 85: the player network and a moderately loaded guest keep the lead; the mobile network (110-150 ms, 60 ms jitter) does not (kept on there, tribe_walk_mobile went stop-and-go: 1 of 3 and 1 of 3 against 3 of 3, labs 20261107-094647, -095652-2 / -095652);
* the late target catches up at 1 + lag / 0.25 s (at most 2x), going on at the lead's velocity; a state on time (the lead's horizon at most 50 ms over its median) steps nothing back, and when no point of the new path is near where the shown target was going (over 1.5 m: a turn round inside a spike) the shown time is the lead's at once;
* the mirror's drawn step is always held to 2 x its own speed + catch_up (WalkPlace::lead_speed), catch_prop kept.
Off: rel/0.7.9's drive.lead_late and no-lead rule.

## Verification
FAIL (SPORE_FIX_OFF=drive.lead_keep), body_trail_tribe_player: out/lab/20261107-114838-2 0 of 3 (p90 2.24 / 2.00 / 1.81 m, over 2 m 1.06-1.13 s; lane 7), out/lab/20261107-080850-2 0 of 3 (p90 2.85 / 2.62 / 2.71 m); rel/0.7.9 code out/lab/20261107-073841 0 of 3.
PASS (final code), at the same time as the first FAIL run: out/lab/20261107-114838 3 of 3 (p90 1.32 / 1.59 / 1.77 m, over 2 m at most 0.91 s; lane 6, lab load 25-35 on 6 cores). Earlier versions of the fix: -094647-2 3 of 3, -100645 2 of 3, -102038 0 of 3 (the stretches of BUG-0674's turn, fixed after), -110506 2 of 3.
Regression with the fix: tribe_walk_mobile out/lab/20261107-100645-2 3 of 3, -110506 2 of 2; tribe_walk_player -114838 3 of 3, tribe_walk_mobile -115442 2 of 2, body_trail_tribe -115442 2 of 2, tribe_walk -120427-2 2 of 2, -111106 1 of 2 (a held mirror's catch-up frames of 0.91 m, then bounded); body_trail_tribe -111106 2 of 2; net_tests test_lead_keep and test_lead_late (make test).

## History
- 2026-11-01: opened.
- 2026-11-06 (fix/w45-remote-bodies, M1): root cause observed (the 0.2 s delay), fixed by drive.lead.
- 2026-11-07 (orchestrator, 0.7.9 final check on a quiet lab): reopened, the claim does not hold: body_trail_tribe_player 2 of 2 FAIL with the lead (p90 2.14 / 2.81 m, longest over 2 m 1.49 / 2.91 s, out/lab/20261107-012856) and 2 of 2 FAIL with SPORE_FIX_OFF=drive.lead (p90 2.77 / 2.97 m, 2.96 / 4.17 s, out/lab/20261107-012856-2), also FAIL in the final check 20261106-231404 and the rerun 20261107-004042. The lead is better than or equal to 0.7.8 behaviour, so it ships as an improvement (CHANGELOG Changed), the rest goes to 0.7.10 K3.
- 2026-11-07 (fix/w46-k3-bodies, K3): decomposed with the new coop.bodyrec diag rows (the no-lead rule, the slow late catch-up, the step back after a turn); fixed with drive.lead_keep. Status fixed, fixed_in unreleased.
