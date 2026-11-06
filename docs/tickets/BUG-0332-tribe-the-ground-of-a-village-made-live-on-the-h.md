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
tests: tribe_village_ground (coop_tribe_village_ground.py, a probe)
signature:
docs: docs/re/tribe.md#tribe-ground-height, docs/re/tribe.md#tribe-village-ground
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
2026-11-03 (fix/w41-tribe-world): not analysed further (time box). The effect on members and animals stays
handled by drive.own_height / tribe.mirror_ground (each game's own ground at the host's place). The detector does not
compare heights along the radius as a finding (worlddiff matches within 3 / 15 m), so its Tribe rows do not show it.

## Fix
Not fixed. Its effect on members and animals is handled by drive.own_height (BUG-0329): they stand
on each game's own ground at the host's place.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
- 2026-11-03: looked at on fix/w41-tribe-world, no new finding.
- 2026-11-05 (fix/w45-tribe, T2 of 0.7.9): not reproduced on the `creature` fixture. New probe test
  tribe_village_ground (coop_tribe_village_ground.py): the players' live Creature -> Tribe change, then the game's own
  ground (`coop.ground`, cPlanetModel::GetHeightAt) on both games at the new village's centre and on rings of 10, 25,
  50 and 100 m (8 directions): host and guest within 0.20 m everywhere, twice 20 s apart (out/lab/20261105-232737,
  PASS). Observed: the guest's ground is the host's there; the live village does not change the ground in a way the
  save loses on this planet. Inferred: the campaign's 4-5 m came from its own planet (made in the campaign's Cell and
  Creature stages, not a fixture), so the probe needs such a world to show it: a campaign run (`coop_campaign.py
  --from cell --to tribe`) with this probe's sampling is the next step. Stays open (no failing run on a fixture).
