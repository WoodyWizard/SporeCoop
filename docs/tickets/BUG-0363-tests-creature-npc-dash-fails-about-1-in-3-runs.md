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

## Fix
Open.

## Verification
Open.

## History
- 2026-11-01: opened.
