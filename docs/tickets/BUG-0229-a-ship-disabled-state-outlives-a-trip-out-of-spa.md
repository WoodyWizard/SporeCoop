---
id: BUG-0229
title: a "ship disabled" state outlives a trip out of Space and destroys the ship on return, also the ship of a game loaded meanwhile
status: fixed
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #163 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-space
commits: 7c9ee00, 6dbb476
tests: space_down_away (coop_space_down_away.py parts a, b)
signature:
docs: docs/re/space.md#space-object-serial
related: BUG-0228, BUG-0147, BUG-0131, BUG-0217
switch: space.down_world
---

## Report
From GitHub issue #163: https://github.com/WoodyWizard/SporeCoop/issues/163 (the full analysis, links and the scenario are there).

The co-op "ship disabled" state (a lethal hit next to a partner only disables the ship for 20 s) keeps its timer running while the player is out of Space. On the first Space frame back, if the 20 s have passed, the mod kills the ship with the game's own death. After a load in between, that ship belongs to the newly loaded game. Severity LOW. The bug is older than release 0.7.3.0, which did not change its reach: `tick`, its early return and `end_down` are the same in 940fc74 and 5801eea. The release only added the BUG-0147 repair step and the BUG-0131 helper filter inside `down_tick`.

## Root cause
Observed on lane 3 (GOG 2024): `tick` returns before `down_tick` outside Space, the deadline is wall-clock time, so (a) after 25 s "away" the first Space frame killed the guest's ship (state 2, left 0.0 s); (b) the host disabled, then a load of the same game from the galaxy menu: 27 s after the disable the loaded game's ship was destroyed (state 2): point 4 of the issue holds, `my_ufo()` is the new ship then.

## Fix
`down_world_tick` at the top of `spacecoop::tick`, before the early return: another Space world (`spaceext::world_epoch()` differs from the one recorded at the disable) ends the state without a death and without K_DOWN, clearing the byte through BUG-0228's function; the same world and not in Space pauses the 20 s (`g_down_until += dt`, step capped at 0.5 s). `down_tick` checks the recorded ship before the native death. Switch `space.down_world` (off = 0.7.3.0).

## Verification
PASS out/lab/20261028-224335 ((a) 19.0 -> 16.9 s left after 25 s away; (b) epoch 1 -> 2, down=0, state 0). FAIL with the switch off out/lab/20261028-225138 ((a) state 2, (b) state 2). A real editor trip (no false epoch change) was not run: no console route into the ship editor from the fixture.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-space (7c9ee00): paused in the same world, ended without a death in another; test space_down_away.
- 2026-10-29: released in 0.7.3.1.
