---
id: BUG-0608
title: Tribe / Creature: a relayed world effect (a hit impact, BUG-0603) is played at the sender's absolute position, off the ground where the two games' grounds differ (BUG-0332)
status: open
kind: bug
stage: Tribe
reported: 2026-11-06
source: fix/w45-tribe (T2 of 0.7.9), inferred from BUG-0332 and BUG-0603
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-hit-impacts, docs/re/tribe.md#tribe-village-ground
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-06: opened.
