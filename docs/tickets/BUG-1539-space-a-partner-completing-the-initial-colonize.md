---
id: BUG-1539
title: Space: a partner completing the initial colonize-planet tutorial mission blocks the other players from finishing it without destroying the partner's colony
status: fixed
kind: bug
stage: Space
reported: 2026-11-23
source: discord
role: all
versions: 0.7.16
fixed_in: 0.7.17.1
branch: rel/0.7.17.1
commits: 473beb20, 6bace456, bc4b3d13, 0bfbd3da, cb6ee98b, b9715d6f
tests: space_colonize_mission, space_colonize_mission_off
signature:
docs: docs/re/space.md#space-colonize-tutorial
related: BUG-0181, BUG-0191
---

## Report
Player report in Discord (`#russian`, 2026-11-22, no report zip attached): in Space stage co-op, the initial tutorial mission asks to colonize a target planet; once one player places the colony, the remaining players can no longer complete their own tutorial mission unless they destroy their partner's colony and place a new one.

## Root cause
Observed (objdump of `cMissionColonize::Update` `0x00C4EAD0`, vtable `0x0146DF58`, Cast `0x02BA2A01`):
* The initial colonize-planet tutorial mission has ID `0x398ACA5E` (`+0x78`), with `mColonizeMissionState` at `+0x1F0` (`0` = need colony, `1` = need buildings, `2` = objective done) and target `cPlanet*` at `+0x1E8` (`cPlanet+0x13C` = `cPlanetRecord*`, `+0x184` = `PlanetID`).
* `cMissionColonize::Update` only checks whether the local player's own empire colonized the target (`GetColonyPlanets().size() > 1` for `0x398ACA5E`, `cPlanet_IsColonizedBy` for other missions). Once a co-op partner colonizes that planet, its colony is mirrored onto the target planet under the partner's empire, leaving the remaining players' `cMissionColonize` stuck in state `0` unless they destroy the partner's colony.

## Fix
`space.colonize_mission` (`mods/coop/spacecoop.cpp`, `mods/coop/space_allies.cpp`, `mods/coop/space_ext.h`, `mods/coop/space_logic.h`, `mods/coop/wire/space_core.h`, `data/symbols/overrides_dc04aee5.sym`, `docs/re/space.md#space-colonize-tutorial`, `space` family minor bumped to `1.4`):
* Hook `cMissionColonize_Update` (`0x00C4EAD0`). When an active `cMissionColonize` is in state `0` or `1`, the session is not in Rivals mode, and either an allied partner colonized the target planet (`spaceext::allies_partner_colonized(target_pid)`) or a partner completed `0x398ACA5E` in this `world_epoch()`, invoke the game's own `cMissionColonize_FinishObjective` (`0x00C55650`) to advance `mColonizeMissionState` to `2` and trigger turn-in completion without touching the partner's colony.
* Append optional backwards-compatible trailing `u32 mission_id` to `wire::space_core::Mission` (`K_MISSION`, `space` minor 1.4) so partners who colonize another planet still notify remaining players holding `0x398ACA5E`.

## Verification
* Unit tests: `space_tests` (`test_colonize_mission`) and `wire_tests` (`K_MISSION` backwards compatibility with and without trailing `mission_id`).
* FAIL without fix (`SPORE_FIX_OFF=space.colonize_mission`): `out/lab/20261124-130047` (`space_colonize_mission_off`: guest mission `398ACA5E` remained stuck at `state=3 col_state=0` after host colonized target planet `0046000C`).
* PASS with fix: `out/lab/20261124-130013` (`space_colonize_mission`: guest mission `398ACA5E` advanced `col_state=0 -> 2` with `partner_done=1` after host colonized `0046000C`, leaving partner's colony intact, 0 faults).

## History
- 2026-11-23: opened for 0.7.17.1 from Discord `#russian` report (2026-11-22).
- 2026-11-24: fixed via `space.colonize_mission` on `rel/0.7.17.1`; verified with PASS (`20261124-130013`) and FAIL OFF-twin (`20261124-130047`).
- 2026-11-24: released in 0.7.17.1.
