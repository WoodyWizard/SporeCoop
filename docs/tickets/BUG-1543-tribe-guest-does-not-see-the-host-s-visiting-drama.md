---
id: BUG-1543
title: Tribe: guest does not see the host's visiting drama UFO spaceship
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-24
source: Discord bug report 2026-11-24 (SporeCoop-report-20261124-215842)
role: guest
versions: 0.7.16, 0.7.16.1, 0.7.99
fixed_in: unreleased
branch: w08/tribe-fixes
commits: 4809df7d
tests: tribe_ufo_mirror, tribe_ufo_mirror_off
switch: tribe.ufo_mirror
signature:
docs: docs/re/creature.md#creature-ufo-mirror, docs/re/tribe.md#tribe-epic-hostile-and-ufo
related: BUG-0486, BUG-0558
---

## Report
In Tribe stage, when the host gets a visiting UFO spaceship drama event, the guest does not see the ship.

## Root cause
`drama.cpp` blocks the guest's local UFO drama event (`g_mask = 1u << 2`) in both Creature and Tribe stages (BUG-0486),
but the host-to-guest UFO mirror (`BUG-0558`, `player_rules.cpp`) gated sending, receiving, and mirror maintenance on
`game::in_creature_stage()` only, leaving Tribe guests without the host's UFO mirror.

## Fix
Behind `COOP_FIX("tribe.ufo_mirror", "BUG-1543", ...)`:
- Extended `mods/coop/player_rules.cpp` `K_UFO` host send, guest `ufo_mirror_rx`, and mirror maintenance to Tribe stage
  (`modes::kTribe`, `0x1654C02`) in addition to Creature stage (`modes::kCreature`) via `tribe_logic::ufo_mirror_stage`.
- Guarded `ufo_dump()`'s creature puppet listing to Creature stage only (in Tribe, `C.puppet_of` holds `modes::RtsMarker*`).
- Added native unit tests in `tests/tribe_tests.cpp` and Wine lab test `tools/tests/coop_tribe_ufo_mirror.py` with
  OFF-twin `tribe_ufo_mirror_off`.

## Verification
- PASS (`tribe_ufo_mirror`): `out/lab/20261125-025515` (lane 3; host `coop.drama start 2` spawned UFO in Tribe mode
  `01654C02`, guest mirrored UFO within 0.3 s at 0.00 m, tracked 10.06 m flight over 10 samples within 1.58 m max
  error, `rx=55 made=1`, guest own UFO drama `started=0`, 0 host faults, 0 guest faults).
- FAIL (`tribe_ufo_mirror_off`, `SPORE_FIX_OFF=tribe.ufo_mirror`): `out/lab/20261125-025515` (lane 3; host spawned UFO
  and moved 45.07 m while guest received `rx=0` and showed 0 UFOs).

## History
- 2026-11-24: opened from Discord bug report.
- 2026-11-25: fixed in `4809df7d` on `w08/tribe-fixes`, verified PASS + OFF FAIL in `20261125-025515`.
