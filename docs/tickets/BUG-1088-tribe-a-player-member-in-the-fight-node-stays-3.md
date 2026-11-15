---
id: BUG-1088
title: Tribe: a player member in the fight node stays 3-8.75 m over the ground for 36 s on every game (monitor ground gate fails the full campaign)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-13
source: lab
role: host
versions: 0.7.13 and earlier
fixed_in: 0.7.14
branch: fix/w49-raid2
commits: 22c48f58
tests: tribe_member_air, campaign (lab gate monitor ground)
signature:
docs: docs/re/tribe.md#tribe-member-swim
related: BUG-0945
---

## Report
fix/w49-camp, the full campaign lab 20261113-142203 (lane 4): the lab failed the run on the monitor's ground gate
(besides the Tribe timeout): "the tribe members 6.15 m over the ground (Tribe) on 2 lines from s 1630 (the members of
partner 2, one of them up for 19 s without a break)" on all four games. The host's log names it: member #16 of player
2 (Carol), `bt=05218BB3` (the fight node), hp 90, speed 6.3, 3.14 m up at s 1620.8 (14:49:26), "down again after
35.9 s over 3 m, highest 8.75 m" (14:50:02); Carol's game shows the same member up (agl_hi 4.87, 6.15, 6.50,
agl_held 9.3-29.4 s). This was the Tribe play phase (hunts), not a cinematic. BUG-0945's toss by a blow lasts 1.3-1.5 s;
this one 36 s. Seen once in this branch's runs.
Reproduce: the full campaign (`lab.py run =campaign`), look for "tribe: member #.. is .. m over the ground" with a
long "down again after".

## Root cause
Observed: the member was swimming. Its radius 499.77 is Cadian's water surface (about 499.7,
docs/re/creature.md#creature-deep-water); the mod's heights were over GetHeightAt, the sea bed. Runtime (tribe
fixture, raid probes 20261113-174018, -175142 and -175131 with the host alone after the guest left): members crossing
water are 0.6-3.1 m under the planet's water radius (499.98, game::water_radius: terrain +0x34 + +0x38 * +0x3C as the
terrain kind 0xB83110 reads it), 3-28 m over the sea bed, locomotion mode 3, not grounded, 6-7 m/s, for 10-20 s; the
same with no session. So it is the game's own (also the guests' mirrors float on their own water: Carol's agl_hi
4.87-6.50), not a fault of the mod; the gate read the wrong ground. The campaign_tribe_end swimmers of 20261113-153633
were at radius 498.85 the same way.

## Fix
`monitor.tribe_water` (coop.cpp member_agl_of, used by the trace's agl / agl_hi / agl_held and the "member is .. m
over the ground" lines): over water a member's height is min(height over the terrain, max(0, radius - water radius)).
A swimmer reads 0; a member high over water still counts. Off: `coop.fix monitor.tribe_water off`. `coop.ground`
prints `water <r> over_water <m>`.

## Verification
tribe_member_air part 1 (a member floating on deep water): SPORE_FIX_OFF=monitor.tribe_water FAIL
out/lab/20261113-183402-2 (agl_hi 15.18 on both marks); PASS out/lab/20261113-183656 and out/lab/20261113-183656-2
(agl_hi 0.0, the member 3.1 m under the water radius). The full campaign was not rerun (80 min).

## History
- 2026-11-13: opened.
- 2026-11-13: root cause: a swimming member (the game's own); the gate's height now counts over the water (fix/w49-raid2).
- 2026-11-14: released in 0.7.14.
