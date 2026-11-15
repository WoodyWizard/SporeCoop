---
id: BUG-0929
title: Creature: a partner's name tag floats 1.5-2 m over a low, long creature (its height comes from the bounding radius)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257 (BUG-0920's side note)
role: host,guest
versions: 0.7.x
fixed_in: 0.7.13
branch: fix/w48-labels
commits: 6f97c555 79858616 57eba16c
tests: tag_height
signature:
docs: docs/re/creature.md#creature-posed-top-portrait
related: BUG-0920
---

## Report
Side note of BUG-0920 (sheet 027 of the final check out/lab/20261111-044257, host): the tag "Bob" stands over the
background about a body length from the two creatures at the nest, with no creature under it. Looked at again on
fix/w47-one-creature-black: the tag is at the same place of the screen in the key frames `host_creature` of lab
20261111-044257 and 20261111-064331 and still off in `evolved` (host "Bob", guest "Alice"), so it is not the moment
of the puppet's respawn. In play_creature (the guest plays a tall creature) the tag sits just over the head.

## Root cause
Measured 2026-11-12 (fix/w48-labels, lane 4). Not the radius alone: the creature fixture's red species has a
bounding radius of 1.37 m (old tag height 2.08 m), its rest-pose box (spatial vt 0x68) is 1.50 m high (1.73 m with
its scale 1.15), but it stands crouched: its top in the picture is 1.07 m over its position (projected heights,
`coop.find project`). The radius and the box are both the rest pose's size; the highest posed bone (creature +0xB54
+0x17C model, bones at +0x2E4, 700 bytes each, position +0x10) is at 0.91-1.02 (x scale 1.05-1.17 m).

## Fix
Fixed 2026-11-12 (fix/w48-labels), switch `creature.tag_height`: the tag at the highest posed bone x the scale + 0.4 m
(1.55 m for that creature), the old height when the creature has no posed model. Off: radius x 1.3 + 0.3 (0.7.12).

## Verification
`tag_height` (`coop_tag_camera.py --turns 0 --gap 0.7`): with the camera at rest the gap between the tag's lowest
row and the top of the partner's body in the shots (walked up from the partner's own projected point), in metres by
the game's camera, at most 0.7.
* FAIL (`SPORE_FIX_OFF=creature.tag_height`): out/lab/20261112-123947 (1.04 m).
* PASS: out/lab/20261112-123942 (0.43 m), out/lab/20261112-124126 (tag_height of the final check).

## History
- 2026-11-11: opened.
- 2026-11-12: fixed on fix/w48-labels (creature.tag_height), tag_height FAIL 20261112-123947, PASS 20261112-123942.
- 2026-11-12: released in 0.7.13.
