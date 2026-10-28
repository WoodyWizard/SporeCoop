---
id: BUG-0182
title: Space (GOG 2024): zooming in does not land in the first minute after a load
status: open
kind: bug
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* On lane 10 (GOG 2024, `space` fixture), right after `twoinst.start('space')` the ship of the host
  and of the guest flies to the home planet on a click but zooming in (wheel at the ship) does not
  land on it; the same zoom lands 50-80 s later. Also with the menu intro fix disabled (so not the
  intro of docs/re/space.md 7.9, which only lasts ~10 s); `movie=0` and the HUD answers meanwhile.
  Made round 1 of `coop_space_rescue.py` fail ("both on the home planet"); the test retries the first
  landing for 150 s now. Not seen on GOG 2016 lanes (rescue round 1 passed there); cause unknown
  (tutorial state of the fixture, planet LOD loading at ~66 fps?). Reproduce: SPORE_LANE=10, `start
  ('space')`, then `Player.land(home)` at once. (The guest "movie" that made the guest's clicks
  vanish on this fixture, described on feat/space-trade, is fixed: docs/re/space.md 7.9.)
* The extra zoom-ins of those retries leave the two players at different heights over the planet
  (a ship's height is its zoom, docs/re/space.md 7.10). That made the rescue fail on GOG 2024 (the
  3D distance never got under 60 m): fixed, the repair measures along the ground now, and the bot
  zooms to the partner's height. The landing delay itself is still not understood.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-space: not reached (budget spent on the 10 other Space tickets). Runtime note from the wave 3 tests on lane 6 (GOG 2024): right after `start('space')` the page's "Land" button (`coop.space land`, LandOnActivePlanet) landed both ships on the first try about 20-30 s after start() in coop_space_armada_return.py (4 runs), so a test that needs an early landing can use it; the wheel zoom-in delay itself was not investigated.
- 2026-10-28 (fix/w4-census): not reached (time spent on the fuzz test, BUG-0208, BUG-0205 and the census). Nothing new; the wave 3 note (`coop.space land` lands on the first try 20-30 s after start) is still the workaround for tests.
- 2026-10-28 (fix/rel-finale): the space_finale_alone failure "zoomed into the core's system" before 0.7.3.0 is not this bug: it is the galaxy -> core system zoom-in, which needs ~10 wheels of 3 notches while the test stopped at 10 (BUG-0213). Nothing new on the planet landing delay.
