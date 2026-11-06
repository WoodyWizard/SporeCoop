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
branch:
commits:
tests: tribe_walk_mobile (tools/tests/coop_tribe_walk.py --fps 30, SPORE_NET=mobile)
signature:
docs:
related: BUG-0425, BUG-0386, BUG-0365
---

## Report
Found in M2's runs (fix/w45-remote-bodies-2 on rel/0.7.9 + M1's drive.lead). tribe_walk_mobile (both games at 30 fps, netem mobile: 120 ms, 60 ms jitter, 3 % loss) fails its check 'guest sees the host's members (mirrors): no jump of a member while it walks': out/lab/20261106-064239 #1 (5 jumps: (9, 18.76 s, 0.96 m, 32 ms), (9, 18.79, 0.90), (1, 30.65, 0.95), (1, 30.68, 0.92), (1, 33.27, 0.91); 58 target corrections) and #2 (2 jumps); out/lab/20261106-063850 (4 jumps 0.9-1.25 m and 'uneven 36 %', with a tried lead smoothing since removed). With SPORE_FIX_OFF=drive.lead it passed 2 of 2 (out/lab/20261106-062826); with the lead it passed once (out/lab/20261106-053105).

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
