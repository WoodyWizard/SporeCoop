---
id: BUG-0425
title: class: remote body placement, catch-up and snapshot timing (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w44-height, fix/w45-remote-bodies
commits: 4b54c7a7, df579966, 9ce58786, de9c4d8d, 261e990c, a71e319b, 440a5c2c, 391562e1, 29330917, 391562e1, 29330917
tests: npc_air, own_ground, body_trail_creature, body_trail_tribe, body_trail_player, body_trail_tribe_player, body_trail_civ, body_trail_civ_loss
signature:
docs: docs/re/creature.md#creature-height-rule, docs/re/tribe.md#tribe-animal-height, docs/re/creature.md#creature-remote-lead, docs/06-testing.md#testing-body-trail
related: BUG-0612, BUG-0609, BUG-0410
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
Trail part (2026-11-06, fix/w45-remote-bodies). Observed with both games' own positions at the same moment
(coop.bodyrec, tools/tests/body_trail.py): every host body on a guest is late by a TIME, not by a place: the distance
over the host's speed is 0.16-0.17 s (p50) on a clean network for Creature NPC proxies and Tribe animals, 0.21-0.22 s
for Tribe members, 0.21-0.24 s over the player network, i.e. the interpolation delay (SnapshotBuffer::delay: 0.15 s
NPCs, 0.2 s Tribe members) plus the one-way latency; 0.6-2 m at walking and running speeds. The guest's interpolated
target is as far from the host's body as the drawn copy (npc_walk tracks of this branch's baseline, lab
20261106-011339: target p50 0.74 m, drawn 0.77 m), so the game's locomotion fighting the placing is NOT the cause of
the common trail: the copies follow a target that is late. Over a network with spikes and loss bursts the error
left with the fix is at the start of a walk (known a latency late; p90 1.9-3 m in the first 0.75 s of a Tribe
member's walk, 0.7 m after 2 s). Send rates (NPC 0.1 s, Tribe 0.12 s, Civ 0.1 s) only add their half interval to the
age, which the lead covers. Civ vehicles were already moved on to the present (VelocityEstimate::at): lag p50 0.04 s.

## Fix
Height rule (fix/w44-height): a remote body on the sender's ground goes onto this game's ground (as before: on_own_ground,
own_height, out_of_ground); a body off the sender's ground for a reason the game knows (+0x137 = 0, off GetHeightAt)
keeps the sender's height over the sender's terrain over this game's terrain, not grounded (drive::body_airborne,
at_sender_agl, air_place in driver.h; switches npc.air, drive.sender_agl, the puppet's creature.puppet_air). Own
creatures: creature.own_ground. The other parts of this umbrella (trail/stall, speed caps, cycles, NaN guards) are
not touched.
Trail part (fix/w45-remote-bodies), one rule in one place, `coop.fix drive.lead` (net::g_lead_fix):
net::SnapshotBuffer with `lead` shows the sender's state NOW: its newest state moved on by its velocity for its age on
the sender's clock plus the one-way latency (net::g_one_way = half the session's round trip, set every frame), at
most kMaxLead 0.6 s; the velocity is the state's own, capped by its path between the two newest states, the path's
for a body without a speed of its own, none after a stop or a jump. Used by the NPC proxies (Creature and Tribe) and
the Tribe member mirrors; the Civ vehicles' VelocityEstimate takes the same g_one_way. drive::walk_place closes the
error of a lead target in ~0.25 s (WalkPlace::catch_prop, same switch) instead of at 3 m/s. The partner's puppet keeps
its 0.1 s delay (its stop slide, BUG-0408, would grow by speed x latency). The umbrella's other parts (cycles, NaN
guards, oversize messages, ship caps) are BUG-0612.

## Verification
Height part: npc_air FAIL 20261105-112120 / PASS 20261105-112004; own_ground FAIL 20261105-113544 / PASS
20261105-113703. The umbrella stays open (its other parts).
Trail part: FAIL (SPORE_FIX_OFF=drive.lead) out/lab/20261106-013022 (body_trail_creature p90 0.78 m, body_trail_tribe animals 1.17 m / members 2.04 m), out/lab/20261106-014253 (body_trail_tribe_player 1.73 / 2.71 m, stretches 1.75 / 3.55 s), out/lab/20261106-020354 (final code: all four FAIL: creature 0.71, tribe 1.20 / 2.07, tribe_player 1.69 / 2.76, player 1.13 m); PASS out/lab/20261106-012156 (creature 0.12 m, tribe 0.37 / 0.64 m, civ 0.93 m), out/lab/20261106-014554 (player 0.33 m, tribe 0.43 / 0.66 m), out/lab/20261106-021152 (final lead code: creature 0.12-0.4, player, tribe_player), out/lab/20261106-035710 (body_trail_tribe, body_trail_creature on the final code).

## History
- 2026-11-01: opened.
- 2026-11-05 (fix/w44-height): the height part done (BUG-0550, BUG-0592, BUG-0504); left open for the other parts.
- 2026-11-06 (fix/w45-remote-bodies, M1): the trail part measured and fixed (drive.lead); the rest split to BUG-0612.
