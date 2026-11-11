---
id: BUG-0894
title: Velocities from NPC and Tribe snapshots are still written raw into bodies; the NaN guard covers positions and player states only
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0614, BUG-0612, BUG-0292
---

## Report
BUG-0614 says its guard is "a defence for the other writers (NPC, Tribe, Civ snapshots, the raw velocity writes)", but the raw velocity writes from the host's snapshots are not guarded. A NaN or Inf velocity from the host's game (the BUG-0292 class) goes into a guest's proxy and the physics moves it to a NaN position.

Severity: low. Confidence: medium.

## Root cause
* Observed (code): mods/coop/game.cpp:1639 `pose_refused` guards teleport, place, set_position, set_orientation and move_to; mods/coop/coop.cpp:4620 drops bad player states.
* Observed (code): velocity still written to kVelOff unchecked: mods/coop/npc.cpp:1939 (`= s.vel`, an NPC snapshot on the guest); mods/coop/driver.h:348 (walk_place without a goal) and driver.h:418 (air_place, after `place` already refused the pose), reached from the NPC (npc.cpp:1933) and Tribe (tribe.cpp:2844) snapshots.
* Observed (code): mods/coop/net/validate.h:144 `finite3` exists but has no caller.
* Inferred (medium): NPC and Tribe snapshots are host-only and normally finite, so this needs a NaN in the host's game first.

## Fix idea
Check the velocity once (pose_bad-style: write zero or skip) at the driver.h write sites and npc.cpp:1939, under `game.finite_pose`; or drop non-finite NPC and member states at receive with finite3, as coop.cpp:4620 does for player states.

## How to test
Extend net_nan: the host sends an NPC and a tribe member state with a NaN / Inf velocity (a test command on the host). The guest's proxy must keep a finite game position (coop.bodyrec on the guest) and no fault. Must FAIL with SPORE_FIX_OFF=game.finite_pose (the proxy's position becomes non-finite) and PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0614. Severity low, confidence medium.
