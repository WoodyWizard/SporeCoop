---
id: BUG-0436
title: Creature: the partner's puppet slides in its standing animation for a whole walk (walk gait 0, animation 02481DE5), host watches guest, about 1 run in 20
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: lab
role: host
versions: 0.7.3.3
fixed_in:
branch: fix/w34-walk-stable
commits:
tests: puppet_walk, puppet_under
signature: coop.track on the host: the guest's puppet has animation 02481DE5 and walk gait 0 in every frame of a 25 s walk while it moves at 1-4.75 m/s (speed state 2-3, game speed 3-4.75 m/s, placed on the path: 98 % of the windows matched)
docs:
related: BUG-0360, BUG-0394, BUG-0361, BUG-0407
---

## Report
puppet_under#2 on fix/w34-walk-stable 5557071 (= main's driving), lab 20261101-154000 (lane 3, cold start, the observer
at 61 fps, 0.3 % of its frames over 0.1 s): "host watches guest: the puppet walks where the observer sees it (walk gait in
0 % >= 80 % of 965 frames on its screen)". The same before: fix/w31-creature-gate lab 20261101-100420 puppet_walk (gait 0 %
of 552 frames, 02481DE5 the whole walk) and feat/w32-monitor-diag lab 20261101-131215 puppet_walk (gait 0 % of 1023
frames); each time the host watching the guest. Found while classifying the walk tests' failures (BUG-0407).

## Root cause
Observed (track `out/lane3/track/puppet_walk_host_sees_guest_*.json` of that run): every one of the 2243 puppet rows has
animation 02481DE5 and gait 0, also while its game speed is 3.3-4.75 m/s and the speed state 2-3; the driving places it on
the path (stand 2 %, matched 98 %, no jump). The guest's own creature walks with gait in 96 % of its moving frames. The
guest reports its species 065CBE70 -> 290225CD, the puppet here is 7C329711: the same in every passing run, so not the
cause. How often: 0 of 20 directions in lab 20261101-143049, 1 of ~20 in 20261101-154000. Not analysed further (the
animation of a puppet made at a cold join that never starts its walk?).

## Fix
not fixed yet.

## Verification
no-fail-run: open

## History
- 2026-11-01: opened.
- 2026-11-01: the same on NPC proxies: npc_walk_slope with `SPORE_FIX_OFF=npc.walk_ground` (cold, lane 2, lab
  20261101-184751): every walked proxy 02481DE5 with gait 0 in all 3407 moving frames (0 fall starts, so the
  ground check measured nothing); the two runs after it (20261101-185011) walked normally and failed on falls
  (21.1 / 27.2 vs 0.0 / 1.5 per 100 s). BUG-0361 recorded a run like it ("every walked proxy had gait 0").
