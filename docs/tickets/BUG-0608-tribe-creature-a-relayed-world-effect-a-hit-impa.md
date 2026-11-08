---
id: BUG-0608
title: Tribe / Creature: a relayed world effect (a hit impact, BUG-0603) is played at the sender's absolute position, off the ground where the two games' grounds differ (BUG-0332)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: fix/w45-tribe (T2 of 0.7.9), inferred from BUG-0332 and BUG-0603
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state (0.7.9: fix/w45-village-ground)
commits: 6b49ee3d (0.7.9: bbe9fc59, the BUG-0332 fix)
tests: effect_ground (coop_effect_ground.py); tribe_village_cadian (the two grounds, BUG-0332)
signature:
docs: docs/04-netcode.md#netcode-effect-agl, docs/re/tribe.md#tribe-hit-impacts, docs/re/tribe.md#tribe-village-ground, docs/re/tribe.md#tribe-terrain-stamps
related: BUG-0603, BUG-0332, BUG-0451
---

## Report
Found while fixing BUG-0603 (no player report): a world effect the relay sends (MSG_EFFECT pool 5: a hit impact of
BUG-0603, a world effect of BUG-0451) carries the sender's absolute position. In 2 of 4 four-player campaigns
(campaign_ct, BUG-0332) the guests' ground round the village was 5.1-5.9 m higher than the host's; members and
animals stand on each game's own ground (drive.own_height), so a hit impact on a guest's copy of a victim would be
made 5-6 m under that copy's feet.

## Root cause
Inferred, not seen on screen: effects.cpp make_transform uses the received position as it is; nothing maps it to
the receiver's ground (the NPC states have a height-over-ground trailer, BUG-0550; world effects have none). On the
fixtures the grounds agree (tribe fixture, creature fixture within 0.2 m), so the lab tests do not show it.

Observed (fix/w45-village-ground, BUG-0332): the only cause of the two grounds differing round a village is the
host's live render of the Tribe start's terrain stamps; with the host on the loaded ground (tribe.host_reload) the
host's and the guest's GetHeightAt agree at all 1499 sampled points of the region (out/lab/20261106-045849), so a
relayed effect at the host's absolute position is on the guest's ground too. Same mechanism as BUG-0332, no own code.

## Fix
0.7.11 (fix/w46-k2a-state, K2a): the general fix of the mechanism, for every ground difference the BUG-0332 fix
does not remove (any terrain the two games do not share): MSG_EFFECT pool 5 carries the effect's height over the
sender's ground (an f32 after the transform; older SporeCoop stops reading before it) and the receiver plays the
effect at that height over its own ground, the rule of every remote body (BUG-0425). `coop.fix effects.ground off`:
the sender's absolute position.

0.7.9: the BUG-0332 fix `tribe.host_reload` (one ground for everybody); `coop.fix tribe.host_reload off` brings the
5-14 m differences, and with them this bug, back.

## Verification
0.7.11, coop_effect_ground.py (the guest is given the host's world effect as from a host whose ground lies 5 m
higher, and the real relay of the host's effect):
* FAIL with `SPORE_FIX_OFF=effects.ground`: out/lab/20261107-223012 (the effect +5.00 m over the guest's ground).
* PASS: out/lab/20261107-222542 (-0.32 m), out/lab/20261107-223602.

0.7.9:
The test of the cause (BUG-0332, `tribe_village_cadian`): the game's own ground on both games, 1499 points.
* FAIL with `SPORE_FIX_OFF=tribe.host_reload`: out/lab/20261106-045914 (62 points off, up to 2.36 m).
* PASS: out/lab/20261106-045849 (0 points off).
Not verified on screen: a hit impact on a guest's copy of a victim standing on such a patch (no test places a fight
there); the effect's position is the sender's absolute one, which is on the ground once the grounds agree.

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-village-ground): same mechanism as BUG-0332; fixed by tribe.host_reload.
- 2026-11-07: released in 0.7.9.
- 2026-11-07 (fix/w46-k2a-state, K2a of 0.7.11): the general fix, effects.ground (the height over the receiver's ground).
