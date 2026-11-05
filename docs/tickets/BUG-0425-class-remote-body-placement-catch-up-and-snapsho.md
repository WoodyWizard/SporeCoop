---
id: BUG-0425
title: class: remote body placement, catch-up and snapshot timing (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in:
branch: fix/w44-height
commits: 4b54c7a7, df579966, 9ce58786
tests: npc_air, own_ground
signature:
docs: docs/re/creature.md#creature-height-rule, docs/re/tribe.md#tribe-animal-height
related:
---

## Report
Done: shared drive::walk_place for puppet, Tribe mirrors, NPC proxies (BUG-0394), Space pin (BUG-0378).
Siblings not covered: WalkPlace::ground never enabled (driver.h:217): NPC proxies and Tribe mirrors not put on this game's ground (BUG-0399, downhill falls inferred); trail/stall only for NPC proxies (tribe.cpp:1026/1043, puppets stall=false; BUG-0305); Civ mirrors no own-ground; Space ship mirrors lack speed cap / extrapolation limit (squadron.cpp defaults; fits BUG-0190, 0388); Civ/Tribe accept a late chunk of an OLDER cycle (civ.cpp:1858, tribe.cpp:835) -> objects recreated (obs); no NaN/Inf guard on received poses outside Cell (game.cpp:823-840) (obs); oversize unreliable messages dropped silently (connection.cpp:162-165) (obs); root motion of abilities (BUG-0414); hops (BUG-0376); Tribe standing mirrors still on drive::step.
General fix: one driver policy for every remote body (ground, trail/stall, root motion, speed cap, extrapolation); one CycleTracker; NaN guard in game::teleport/set_position/place; count/log oversize messages. Outcome check: monitor drawn/ground/proxy rules for every kind at 30 fps + jitter; chunk unit test.
Source: docs/generalisation-plan.md G8.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Height part (2026-11-05, fix/w44-height): see BUG-0550 (NPCs in the air drawn on the ground), BUG-0592 (own creatures
left in the ground by a push), BUG-0402 (a terrain difference between the games, the proxy placed right).

## Fix
Height rule (fix/w44-height): a remote body on the sender's ground goes onto this game's ground (as before: on_own_ground,
own_height, out_of_ground); a body off the sender's ground for a reason the game knows (+0x137 = 0, off GetHeightAt)
keeps the sender's height over the sender's terrain over this game's terrain, not grounded (drive::body_airborne,
at_sender_agl, air_place in driver.h; switches npc.air, drive.sender_agl, the puppet's creature.puppet_air). Own
creatures: creature.own_ground. The other parts of this umbrella (trail/stall, speed caps, cycles, NaN guards) are
not touched.

## Verification
Height part: npc_air FAIL 20261105-112120 / PASS 20261105-112004; own_ground FAIL 20261105-113544 / PASS
20261105-113703. The umbrella stays open (its other parts).

## History
- 2026-11-01: opened.
- 2026-11-05 (fix/w44-height): the height part done (BUG-0550, BUG-0592, BUG-0504); left open for the other parts.
