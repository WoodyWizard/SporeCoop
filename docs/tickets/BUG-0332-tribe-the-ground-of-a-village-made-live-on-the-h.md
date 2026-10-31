---
id: BUG-0332
title: Tribe: the ground of a village made live on the host is up to 5 m higher or lower on the guests that load its save
status: open
kind: desync
stage: Tribe
reported: 2026-10-30
source: lab 20261030-210914 campaign#1 / campaign#3
role: guest
versions: main be725db
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0329
---

## Report
The probe lines of lab 20261030-210914 (campaign#1 lane 2, campaign#3 lane 7): the host's tribe
members and nearby animals and their copies on every guest differ only in their distance from the
planet's centre, by 3.9-5.3 m (host lower in campaign#3, higher in campaign#1), the same on all
guests. In campaign#3 the guests' members stood at 516.05 m in two places, the host's at 510.9-511.0.

## Root cause
Not analysed. Inferred: the host makes the Tribe village live (the Creature -> Tribe change), the
guests load the host's save of it ("Alice entered the Tribe stage: everybody follows", then
"receiving world"); the village ground (a flattened area?) comes out at another height in the
loaded world than in the live one. Not seen on flat ground (creature_cadian fixture: 503.68 m on
both). Next: `coop.ground` on the host before and after it reloads its own Tribe save in such a
world; find the game's village terrain code.

## Fix
Not fixed. Its effect on members and animals is handled by drive.own_height (BUG-0329): they stand
on each game's own ground at the host's place.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
