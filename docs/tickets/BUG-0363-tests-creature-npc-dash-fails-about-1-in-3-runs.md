---
id: BUG-0363
title: tests: creature_npc_dash fails about 1 in 3 runs on 0.7.3.2 (p90 2.4-6.4 m, limit 1.2 m), with or without creature.puppet_walk
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: found on fix/w29-puppet-move
role: guest
versions: 0.7.3.2
fixed_in: unreleased
branch: fix/w46-g4-dash
commits: a6573cb3, ba1ff91c, e5a1796b
tests: creature_npc_dash, tribe_npc_dash, dash_selftest (make test)
signature:
docs: docs/06-testing.md#testing-dash-same-moment
related: BUG-0281, BUG-0321, BUG-0361, BUG-0753
---

## Report
`creature_npc_dash` (DASH_FIXTURE=creature coop_tribe_npc_dash.py, guest at 15 fps) failed in 4 of 9
runs on fix/w29-puppet-move (2026-11-01): p90 2.35 / 4.26 / 3.03 / 6.40 m against the 1.2 m limit
(passing runs about 0.3-1 m). With `SPORE_FIX_OFF=creature.puppet_walk` (the 0.7.3.2 puppet driving;
the branch changes nothing in the NPC proxies) 1 of 3 failed: out/lab/20261101-021716; with it on 1 of
3: out/lab/20261101-021720; also 20261101-021402 (1 of 2) and 20261101-015647. Runs were next to 2-4
other games (lanes 2-4 busy).

How to reproduce: `tools/lab.py run creature_npc_dash --repeat 3 --retry 0`.

## Root cause
Not analysed. Candidates (inferred): the frame rate of the 15 fps guest and the machine load; the
proxy walk with npc.kinematic placement (BUG-0361: teleported every frame, the locomotion and the
teleport mark) in a zigzag dash; the sample timing.

Narrowed (fix/w30-npc-walk, 2026-11-01, lanes 4 and 5 busy with the other lane's games):
* Not the proxy driving: with BUG-0361's `npc.walk` on and off alike it failed in half the runs (on: 4 of
  8 passed, out/lab/20261101-092700 and 20261101-093400; off, `SPORE_FIX_OFF=npc.walk`: 4 of 8 passed,
  20261101-092708 and 20261101-093351; failing p90 1.5-5.2 m).
* Observed with `DASH_VERBOSE=1` in failing runs (on and off): the samples 3-7 m off had the proxy on its
  own interpolated target (`coop.npc list` err 0.03-0.9 m, state age 0.1-0.46 s, 6-8 states a second):
  the proxy is where the guest's interpolation puts the host's NPC; the error is between that target and
  the host's path over the test's 0.25 s windows (state timing / the test's sampling at 15 fps), not in
  the driving. Per frame (`coop.npctrack`) the drawn proxies were 0.25 m (p50) / 0.4 m (p90) from their
  targets at 15 fps.
* Not checked: the guest's clock offset and interpolation delay during the failing windows, the test's
  sample timing (three console round trips per sample on a 15 fps guest).

Narrowed (2026-11-08, fix/w46-g4-dash, no lab run; the window measure analysed, the lab data of 0.7.11 re-read):
* Not the test's sampling (inferred from the measure, tools/tests/dash_selftest.py): coop_multi.measure takes the
  distance of the guest's reading from the host's path over the 0.25 s before it and the reading after it. On the
  dash's own zigzag (12 m, 2 m to either side, 6.5 m/s) a copy that runs ON the host's path any amount late is
  forgiven: 0.1 / 0.2 / 0.3 s late gives a window p90 of 0.3 / 0.3 / 0.25 m (same moment 0.65 / 1.30 / 1.95 m), a
  copy ahead by up to 0.15 s 0.8 m, at 41 or 66 fps on the host alike; a proxy following its path through a 0.3 s
  lag 0.48 m. The window can only under-report, so its 1.2-1.4 m failures are copies 1.2-1.4 m OFF the host's path.
* Observed (0.7.11 lab, fix on): tribe_npc_dash 20261108-075604 p90 0.93 / 0.74 / 1.33 m (host 66 / 67 / 41 fps,
  guest 15.3, 114 of 124 fresh states in the failing one); with SPORE_FIX_OFF=npc.trail 20261108-080027 1.21 /
  1.39 m (both FAIL, a host at 55 and 68 fps): the fix's margin is 0-0.6 m, and a failure does not need a slow host.
  creature_npc_dash 20261108-081048: 0.36 / 1.42 / 0.46 m (host 63-67 fps); the failing run has a median of 0.22 m
  and a max of 3.88 m (11 of 126 over 1.5 m): episodes of a proxy metres off the path, not a general lag.
* Observed: at the end of the failing tribe run the proxies were 1.13 and 0.98 m from their own interpolation
  targets (`err=`), walking at 6.5 and 3.6 m/s while the target ran at 6.9 / 7.4 m/s (`own=` / `spd=`): the drawn
  proxy is off its target in the driving (Tribe: npc.walk, the game's locomotion), so "the proxy on its target"
  (2026-11-01, before drive.lead) no longer holds.
* Not known yet: the share of the target (states, drive.lead) and of the driving in the error. The test now records
  both (see Fix).

* G4 (2026-11-08, fix/w46-g4-dash): the window measure (coop_multi.measure) forgives a copy on the host's path at any
  lag (tools/tests/dash_selftest.py: 0.25 s late gives p90 0.06 m in the window, 1.62 m at the same moment), so its
  1.2-1.4 m failures were proxies really 1.2-1.4 m off the host's path, not the test's sampling. Judged at the same moment
  on both games' own positions (coop.bodyrec, clocks within ~20 ms) the cause splits: Tribe 1.4-1.7 m (half the target,
  half the driving); Creature: one NPC id (n3) whose interpolation target is 3.6-12 m off the host's NPC for the whole
  run while the other animal is within 0.5-0.6 m, the target from the states themselves (raw lead), confirmed by the
  guest's own sync-check (n3 10.9 m): a real desync, BUG-0753.

## Fix
The test (fix/w46-g4-dash): the dash is judged on both games' own positions at the same moment (`coop.bodyrec` on
both, one clock: every guest frame of the dash, the drawn proxy against the host's NPC at the same time, no delay
because NPC proxies are shown where the host has them now, drive.lead), 90 % within 1.2 m (unchanged limit). The
window measure is still printed (not judged). `coop.bodyrec start diag` on a guest now also records `q<id>` rows
(npc.cpp for_each_diag: the interpolation target the driving used; a test row, no gameplay change), so the test
prints the error split into target-from-host (states / lead) and proxy-from-target (driving), and for every window
sample the frames its console readings really were and the copy's lag on the host's path.

(G4 commits a6573cb3, ba1ff91c, e5a1796b; u<id> raw-lead rows, rows saved in out/lane<N>/dash/, DASH_FEED=0.)

## Verification
The measure: FAIL with the twin SPORE_FIX_OFF=npc.trail (tribe 1.94 m, out/lab/20261108-083625). With the fix the test
now fails exactly when the game is off: tribe 1.36-1.68 m (out/lab/20261108-082630, 0 of 3: half target, half driving),
creature 0 of 3 there and 2 of 3 in 20261108-091147 / -091630 / -092113 (every failure the n3 desync of BUG-0753, every
n1/n2 run p90 0.5-0.6 m). PASS when the game agrees: 20261108-091147 #1/#2. The test is right; it stays red until BUG-0753 (and the Tribe driving margin) is fixed.

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w30-npc-walk: not the proxy driving (fails as often with npc.walk on and off); the failing samples have the proxy on its target.
- 2026-11-08 (fix/w46-g4-tests): G4: the test measures the game now (same moment, both games' own positions); its failures are the real desync BUG-0753 (open) and the Tribe driving margin. The test part is fixed; the red runs belong to BUG-0753.
