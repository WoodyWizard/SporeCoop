---
id: BUG-0929
title: Creature: a partner's name tag floats 1.5-2 m over a low, long creature (its height comes from the bounding radius)
status: open
kind: bug
stage: Creature
reported: 2026-11-11
source: key-frame review of the 0.7.12 final check 20261111-044257 (BUG-0920's side note)
role: host,guest
versions: 0.7.x
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-creature-tag-height
related: BUG-0920
---

## Report
Side note of BUG-0920 (sheet 027 of the final check out/lab/20261111-044257, host): the tag "Bob" stands over the
background about a body length from the two creatures at the nest, with no creature under it. Looked at again on
fix/w47-one-creature-black: the tag is at the same place of the screen in the key frames `host_creature` of lab
20261111-044257 and 20261111-064331 and still off in `evolved` (host "Bob", guest "Alice"), so it is not the moment
of the puppet's respawn. In play_creature (the guest plays a tall creature) the tag sits just over the head.

## Root cause
Inferred from the code, not measured: the tag's height over the creature's origin is `bounding_radius * 1.3 + 0.3`
(mods/coop/modes.cpp creature_tag). The Creature fixture's species is low and long: its radius is about its half
length, so the tag is 1.5-2 m up; with the close, steep camera at the nest that is far from the body on the screen.
In the guest's `evolved` frame of 20261111-064331 the tag "Alice" is also about 85 px to the side of the nearest red
creature: which creature is the host's puppet there was not determined.

## Fix
Open (cosmetic). Idea: the model's height (bounding box) instead of the radius.

## Verification
Open. How to test: the one_creature key frames; the overlay records every tag's screen point with the puppet's position (pfind::note_label in coop.cpp on_overlay).

## History
- 2026-11-11: opened.
