---
id: BUG-0990
title: Creature: the host's camera looks into the ground at the end of campaign_short's creature play (seen once)
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in:
branch: fix/w49-mirror
commits:
tests: campaign_short (key frame play-creature)
signature:
docs: docs/known-issues.md#issue-camera-in-ground
related:
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447, sheet
018_campaign_short_03_play-creature.jpg): at the end of the creature play the host's (Alice's) picture is only
ground texture: the camera looks into the terrain, no own creature, only the edge arrow to Bob. The three
guests' pictures are normal. Alice at (34.8 -495.8 -100.1), Carol 9 m away sees the scene from above as usual.

## Root cause
Not known. Seen once.

## Fix
None.

## Verification
no-fail-run: not reproduced. A scan of the key frames of every lab run on this machine (321 run folders, 886
Creature stage frames of hosts and guests, by sky in the upper picture and contrast) found this one frame only; the
other flagged frames were the death screen's top view and dark scenes. play_creature --quick of the final check of
fix/w49-mirror looked at too (see History).

## History
- 2026-11-12: opened.
- 2026-11-13: fix/w49-mirror: not reproduced (frame scan); closed.
- 2026-11-13: fix/w49-mirror final check: play_creature --quick (20261113-052132) key frames normal.
