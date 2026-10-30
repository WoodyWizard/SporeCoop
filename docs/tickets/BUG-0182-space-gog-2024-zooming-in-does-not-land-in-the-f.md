---
id: BUG-0182
title: Space (GOG 2024): zooming in does not land in the first minute after a load
status: not-reproduced
kind: bug
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w13-space
commits:
tests: space_land_early
signature:
docs: docs/re/space.md#space-seed-profile
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
Not reproduced (2026-10-30, fix/w13-space). The wheel's landing conditions were read from the game
(Ghidra: system update 0x1009C90, `Space_ZoomLandReady` 0x10422F0, `CanLandOnActivePlanet`
0xC383D0) and printed at runtime with `coop.space landcheck`. Right after `start('space')` every
condition held (ship at destination, HP 1500, no comm screen, time running, camera zoom-in flag
set at the minimum zoom 5.0) and both players landed 7-16 s after the load. A landing itself pauses
the game time and sets cSimulatorSpaceGame +0x50 bit 2 for about a second while the planet loads;
a bot that clicks the planet again in that second waits up to 25 s for the ship to arrive
(`Player.land`), which looks like a landing delay (seen in this branch's first probe runs, 40 s).
Inferred, not proven: the 2026-10-23 delay may have been that or code of that day.

## Fix
None needed now. `coop.space landcheck` and `coop_space_land_early.py` stay to catch it.

## Verification
`tools/tests/coop_space_land_early.py` (suite `space_land_early`): both players click the home
planet and zoom in at once after the load; must be on it within 40 s. 3 direct runs on lane 9
(landed at 10-16 s; scratch logs not kept) and lab run 20261030-141123 (lane 8, 2 runs: host 15/14 s,
guest 13/13 s) and lab run 20261030-142729 (lane 9). no-fail-run: not reproduced, no fix to switch off.

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w3-space: not reached (budget spent on the 10 other Space tickets). Runtime note from the wave 3 tests on lane 6 (GOG 2024): right after `start('space')` the page's "Land" button (`coop.space land`, LandOnActivePlanet) landed both ships on the first try about 20-30 s after start() in coop_space_armada_return.py (4 runs), so a test that needs an early landing can use it; the wheel zoom-in delay itself was not investigated.
- 2026-10-28 (fix/w4-census): not reached (time spent on the fuzz test, BUG-0208, BUG-0205 and the census). Nothing new; the wave 3 note (`coop.space land` lands on the first try 20-30 s after start) is still the workaround for tests.
- 2026-10-28 (fix/rel-finale): the space_finale_alone failure "zoomed into the core's system" before 0.7.3.0 is not this bug: it is the galaxy -> core system zoom-in, which needs ~10 wheels of 3 notches while the test stopped at 10 (BUG-0213). Nothing new on the planet landing delay.
- 2026-10-30 (fix/w13-space): landing conditions reversed and printed (`coop.space landcheck`); not reproduced in 5 runs on lanes 8/9 (landed 7-16 s after the load); status not-reproduced.
