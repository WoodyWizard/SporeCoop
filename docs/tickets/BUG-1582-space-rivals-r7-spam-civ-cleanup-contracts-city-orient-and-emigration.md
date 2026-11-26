---
id: BUG-1582
title: Space Rivals: R7 DestroyCity spam loop, leftover Civ tanks and cities cleanup, contract world-change reset, upright city orientation, emigration travel/Colony Pack unlock, rival puppet empire ID, and atomic rivalry sync
status: fixed
kind: bug
stage: Space
reported: 2026-11-26
source: YouTube stream (Vlaid18 + DERMOGEN, v0.8.1.1) + Discord host logs
role: both
versions: 0.8.1.1
fixed_in: 0.8.2
branch: main
commits: 3bb74b46
tests: space_rivals_tests, space_tests, rivals_space, space_rivals_rejoin_jump, space_editor_colonies
signature: space_rivals: R7 blocked 67x every 2 s + sideways identity quat cities + contract timer leak across saves
docs: docs/re/space.md
related: BUG-1550, BUG-1567, BUG-1570, BUG-1578
---

## Report
During the `v0.8.1.1` stream (`Эномель.spo` Civ->Space and `Ореарун.spo` fresh Space):
1. Leftover Civilization-stage rival tanks and cities remained active on Host's homeworld in Space Stage, destroying cities and triggering 67x `"Родная планета игрока защищена от уничтожения!"` spam every 2 s (plus a false-positive capture block during the Space intro cutscene).
2. Emigrated/mirrored cities appeared tilted sideways on their sides (`"дома на боку лежат"`).
3. Guest could not leave the star system initially and had Colony Pack locked behind tutorial progression.
4. Rival player ships appeared labeled `"Пираты"`, switching/syncing rivalry transiently severed active Alliances, galaxy-view map clicks leaked outside uncovered galaxy context, and Pirate Armada co-op contracts auto-offered in Space Rivals and leaked failure timers into a new save (`Ореарун.spo`).

## Root cause
- `mods/coop/space_rivals.cpp:554` (`h_DestroyCity`): returned early without restoring protected city HP above 0, causing Spore to re-invoke `DestroyCity` every tick forever; R7 notices also fired during `world::cinematic_playing()`.
- `mods/coop/space_rivals.cpp` (`sanitize_city_pose`, `ensure_city_count`): wrote identity quaternion `(0, 0, 0, 1)` for cities placed at equatorial latitudes (`lat = +-0.35`), tilting cities ~70 degrees sideways on the planet sphere.
- `mods/coop/space_contracts.cpp`: lacked a `kWorldChange` reset hook and did not suppress auto-offers when `space_rivals::active()`.
- `mods/coop/space_rivalry.cpp:63,877,1326`: war puppets unconditionally used `kEnemyPol = 0xFFFFFFFFu` (Pirates), and `sync_from_rivals()` broadcast an intermediate Neutral `K_STATE` before copying real relations.
- `mods/coop/space_map.cpp:1321`: `click_tick` did not clear `g_tags` or ignore clicks when `context() != 2 || g_covered`.

## Fix
- `mods/coop/space_rivals.cpp` & `mods/coop/space_rivals_logic.h`:
  - (`rivals.space_r7_guard`): restored protected homeworld city HP to `r7_city_hp_floor` (20%, min 100 HP) and cleared ruin/capture state in `h_DestroyCity`; suppressed false-positive R7 blocks/notices during `world::cinematic_playing()` and on self/idempotent transfers.
  - (`rivals.space_home_cleanup`): swept leftover foreign Civ-stage `cVehicle` objects and unified leftover rival `cCity` / `cCivData` records on Host's homeworld upon entering Space Rivals.
  - (`rivals.space_city_orient`): computed outward radial surface quaternions (`city_surface_quat`) for all sanitized/emigrated/relinquished cities so cities stand upright on the sphere.
  - (`rivals.space_emig_unlock`): unlocked `mbFirstCommunication` / `mbFlightSchoolFinished` / `mbMission201Finished`, missing starter drives/Flight School tools, and a one-time Colony Pack (`0x733B280C`) upon Space Rivals emigration.
- `mods/coop/space_contracts.cpp` & `mods/coop/space_logic.h` (`space.contracts_rivals_world`): suppressed contract auto-offers during Space Rivals and reset active/pending contracts on `kWorldChange` / world-epoch change.
- `mods/coop/space_rivalry.cpp` (`space.rivalry_puppet_pol`, `space.rivalry_sync_preserve`): assigned war puppets the rival's mirror empire political ID instead of Pirates `0xFFFFFFFFu`, and made `sync_from_rivals()` broadcast atomically without transiently severing alliances.
- `mods/coop/space_map.cpp`: cleared `g_tags` and ignored clicks outside uncovered galaxy view.

## Verification
- no-fail-run: reproduced in live v0.8.1.1 stream & host logs (`sporekit-20261126-190925.log`: 67x R7 blocked spam, leftover Civ tanks, sideways cities, cross-world contract fail) and covered by new ASan+TSan unit tests (`tests/space_rivals_tests.cpp` 193 assertions, `tests/space_tests.cpp`).
- PASS runs: `20261126-155137:rivals_space`, `20261126-155137:space_rivals_rejoin_jump`, `20261126-155137:space_editor_colonies`, plus `make test` (all 10 suites).
