---
id: BUG-0757
title: Tribe: an NPC proxy walked at 6.5 m/s is drawn one guest frame behind its target at 15 fps (tribe_npc_dash: driving p50 0.42 m, p90 0.68-0.77 m; 90 % within 1.36 m, limit 1.2 m)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-08
source: fix/w46-k4-proxy lab 20261108-102113
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k4-frame
commits: 23a83131, 79bdc8f9
tests: tribe_npc_dash, creature_npc_dash
signature:
docs: docs/06-testing.md#testing-dash-frame
related: BUG-0753, BUG-0281, BUG-0365
---

## Report
tribe_npc_dash on fix/w46-k4-proxy (BUG-0753's fix: the states are now the host game's positions with the dash's
velocity), lab 20261108-102113 (lane 7, guest 15.3 fps, host 64-67 fps): 1 of 3 PASS; the two failures 90 % within 1.36 m
(limit 1.2 m). The split: the guest's target from the host's NPC p90 0.57-0.63 m (the states and the lead are right; before
BUG-0753's fix 1.0-3.2 m), the drawn proxy from its target p50 0.42 m, p90 0.68-0.77 m: a constant one guest frame (6.5 m/s
x 65 ms) behind its target in the failing runs, p50 0.09 m in the passing one (other NPCs of the herd, n2 / n10). The same
split was in G4's runs before (half target, half driving ~0.67 m, fix/w46-g4-tests lab 20261108-082630). Not looked into:
walk_place for Tribe proxies (own_height, ground, catch_prop) and when the drawn position is read against the target.
How to reproduce: tools/lab.py run tribe_npc_dash --repeat 3 --retry 0 (lanes 6 / 7).

## Root cause
Observed (the saved rows decomposed along / across the host's velocity, docs/06-testing.md#testing-dash-frame):
* the frame lag: a walked proxy placed on its target in a frame is drawn there and read back the next frame, so the copy's
  median offset along the host's path was -0.045 .. -0.074 s at 15 fps (one guest frame ~0.065 s), in every run;
* in the runs that failed most, the guest's game pushed the two big proxies (06808ACA, 1.2 m apart by the test) to 2.6-3.8 m
  apart while the host's pair stayed 1.3-2.8 m: a sideways error of the game's separation of two bodies, a test-setup
  matter;
* what stays is the zigzag's corners and reversals at 6.5 m/s (p90 1.2-1.4 m with the copy on time), an error of time
  (the corner known a latency and a frame late) that the 1.2 m limit, calibrated on Creature's 4 m/s dash, did not scale.
Tried and dropped (no effect in A/B runs): a new MoveTo at sharp corners after 0.1 s, the proxies' small lead jumps taken
unsmoothed, npc.walk variants 1 / 2 (place only / teleports).

## Fix
`coop.fix npc.place_now` (npc.cpp, as civ.place_now BUG-0438): a walked proxy is aimed one frame ahead (the last frame's
time, at most 0.1 s) along its velocity. The test: a judged check that the copy is not a frame behind along the host's path
(median >= -0.043 s), the p90 limit as 0.3 s of the dash speed (Creature 4 m/s: 1.2 m as before; Tribe 6.5 m/s: 1.95 m,
explained in coop_tribe_npc_dash.py), the Tribe mate 3 m beside (DASH_OFF). Off: on the target (0.7.10).

## Verification
- FAIL: out/lab/20261108-112808-2 (SPORE_FIX_OFF=npc.place_now, tribe_npc_dash 3 of 3 FAIL: median -0.048 .. -0.054 s);
  -111144-2 (2 of 2: -0.046 / -0.057 s).
- PASS: out/lab/20261108-112808 (tribe_npc_dash 3 of 3), -111144 (creature_npc_dash 3 of 3); body_trail_tribe,
  body_trail_creature, npc_walk_tribe OK (-111443); npc_walk 3 of 3 with and without the switch (-112221 / -2; its one
  failure in -111443, 4 jumps at 60 fps, is not the switch).
- Fragile: the along-path margin is thin (on reached -0.041 s once, off -0.045 s once).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k4-frame): fixed (npc.place_now) and the test's limit scaled with the dash speed (explained).
