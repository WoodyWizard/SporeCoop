---
id: BUG-0363
title: tests: creature_npc_dash fails about 1 in 3 runs on 0.7.3.2 (p90 2.4-6.4 m, limit 1.2 m), with or without creature.puppet_walk
status: open
kind: test
stage: Creature
reported: 2026-11-01
source: found on fix/w29-puppet-move
role: guest
versions: 0.7.3.2
fixed_in:
branch:
commits:
tests: creature_npc_dash
signature:
docs:
related: BUG-0281, BUG-0321, BUG-0361
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

## Fix
Open.

## Verification
Open.

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w30-npc-walk: not the proxy driving (fails as often with npc.walk on and off); the failing samples have the proxy on its target.
