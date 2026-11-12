---
id: BUG-0894
title: Velocities from NPC and Tribe snapshots are still written raw into bodies; the NaN guard covers positions and player states only
status: not-reproduced
kind: bug
stage: All
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch: fix/w48-creature
commits: 5e20924f
tests: npc_nan_vel
signature:
docs: docs/re/creature.md#creature-c2-audit-0713
related: BUG-0614, BUG-0612, BUG-0292
---

## Report
BUG-0614 says its guard is "a defence for the other writers (NPC, Tribe, Civ snapshots, the raw velocity writes)", but the raw velocity writes from the host's snapshots are not guarded. A NaN or Inf velocity from the host's game (the BUG-0292 class) goes into a guest's proxy and the physics moves it to a NaN position.

Severity: low. Confidence: medium.

## Root cause
Observed in the code: the snapshot velocities are written raw (npc.cpp kinematic teleport, driver.h air_place / walk_place variant 1) or turned into SetDesiredSpeed. Not reproduced at runtime: 6 s of NaN velocities in every NPC state the host sent (1155 states), on the ground and with the in-the-air flag (air_place), left all 21 proxies of the guest at finite places during and 5 s after, with the guard off (lab 20261112-073101, -074517) and on (-073116, -074532). The drivers place the proxy from the finite position every frame; a NaN walk target is refused by pose_refused (move_to).

## Fix idea
Check the velocity once (pose_bad-style: write zero or skip) at the driver.h write sites and npc.cpp:1939, under `game.finite_pose`; or drop non-finite NPC and member states at receive with finite3, as coop.cpp:4620 does for player states.

## How to test
Extend net_nan: the host sends an NPC and a tribe member state with a NaN / Inf velocity (a test command on the host). The guest's proxy must keep a finite game position (coop.bodyrec on the guest) and no fault. Must FAIL with SPORE_FIX_OFF=game.finite_pose (the proxy's position becomes non-finite) and PASS with the fix.

## Fix
Kept as hardening: a non-finite velocity is taken as 0 where NPC and Tribe member snapshots are received (game::finite_vel). Switch `coop.fix game.finite_vel off`.

## Verification
no-fail-run: not reproducible (npc_nan_vel passed with the guard off: out/lab/20261112-073101, -074517; with it: -073116, -074532).

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0614. Severity low, confidence medium.
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): not reproduced (no proxy went NaN with the guard off); hardening behind game.finite_vel.
