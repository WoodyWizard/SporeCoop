---
id: BUG-0990
title: Creature: the host's camera looks into the ground at the end of campaign_short's creature play (seen once)
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in:
branch:
commits:
tests: campaign_short (key frame play-creature)
signature:
docs:
related:
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447, sheet
018_campaign_short_03_play-creature.jpg): at the end of the creature play the host's (Alice's) picture is only
ground texture: the camera looks into the terrain, no own creature, only the edge arrow to Bob. The three
guests' pictures are normal. Alice at (34.8 -495.8 -100.1), Carol 9 m away sees the scene from above as usual.

## Root cause
Not known. Candidates: the camera inside a slope or a rock after the bot's walk (the game's creature camera
collides with terrain), a camera left by an earlier test step of the bot. Seen once; the campaign bot has no
camera command in the creature play (only the wheel in other stages).

## Fix
Not done. Next: a key frame's manifest could carry the camera's eye height over the ground (`coop.cam`) to tell
a camera in the ground from a close-up.

## Verification
no-fail-run: open, seen once

## History
- 2026-11-12: opened.
