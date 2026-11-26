---
id: BUG-1583
title: Creature Rivals and Shared Library: habitable same-continent nest selection, starter species armor filter, symmetric War/FF PvP hostility, readable UI labels, and non-empty Shared Library creation names
status: fixed
kind: bug
stage: Creature
reported: 2026-11-26
source: YouTube stream (Vlaid18 + DERMOGEN, v0.8.1.1) + Discord host logs
role: both
versions: 0.8.1.1
fixed_in: 0.8.2
branch: main
commits: 3bb74b46
tests: creature_rivals_tests, species_tests, library_tests, rivals_creature
signature: creature_rivals: swapped herd (player 1, 0 members) on empty continent + armored Civ starter + green War nest
docs: docs/re/creature.md
related: BUG-1547, BUG-0873
---

## Report
During the `v0.8.1.1` stream (`Бейлака.spo` and `Лыбар.spo`, reproduced 2/2 times):
1. Guest spawned on an isolated empty continent ~850 m across water with zero wild nests and `0 members` in the assigned herd, softlocking evolution and triggering sea-monster deaths.
2. Guest auto-spawned directly as a Civilization-stage armored creature (`20-26 HP`, Strike lvl 5 vs Host's `10 HP` starting Creature).
3. Even at War with Friendly Fire ON, Host's nest creatures stayed green/friendly and unattackable by Guest; when FF was OFF, Host could attack Guest asymmetrically.
4. Creature Rivals UI displayed raw hex/coordinate strings (`nest (-413, 152, 255) sp=067B0EAA`, `Wild 0x03A9F746`), which players perceived as garbled text.
5. Shared Library imports had empty creation names (`''`, 8x in host logs) and synced creations lacked Sporepedia cards.

## Root cause
- `mods/coop/creature_rivals_logic.h` & `creature_rivals.cpp`: `select_nest_for_guest()` scored distance without checking herd population (`members > 0`), surrounding wild nest density, or water crossings between Host and candidate.
- `mods/coop/species_line.cpp` & `creature_rivals.cpp`: Creature-stage auto-selection did not filter out Tribe/Civ outfit instances or high-tier outfitted creatures at brain level 0.
- `mods/coop/creature_rivals.cpp`: on Guest, native `CreatureFriendly` (`0x00C0C3B0`) checked `PlayerSpecies == host_species` and kept Host's herd green/friendly; `allow_pvp_damage()` hardcoded `friendly_fire = false`.
- `mods/coop/creations_library.cpp`: `dbpf::metadata_names()` only matched offline author ID `-2`, discarding names of online/Maxis `0x030BDEE3` creations, and `sync_feed_lists_task()` cleared `g_tab_dirty` even when 0 `AssetBrowserFeedLists` roots existed yet.

## Fix
- `mods/coop/creature_rivals_logic.h` & `mods/coop/creature_rivals.cpp`:
  - (`rivals.creature_habitable_nest`): sampled 12 great-circle terrain points (`ground_radius` vs `water_radius`) for water crossings, added BFS wild-nest landmass clustering requiring populated (`members > 0`) nests with >= 2 wild neighbors, and added `ensure_herd_populated()` to populate empty assigned herds.
  - (`rivals.creature_starter_guard`, `species.creature_starter_filter` in `species_line_core.h` / `species_line.cpp`): rejected Tribe/Civ outfit instances and high-tier/armored creatures at brain level 0, switching Guest cleanly to a starting Creature-stage option or Host's starting Creature species.
  - (`rivals.creature_pvp_symmetry`): made `allow_creature_damage()` symmetric for `at_war || friendly_fire_setting`, added 1 Hz `sync_rival_hostility()` syncing native `cSpeciesRelationshipManager` and herd flags, and hooked `CreatureCursorKind` (`0x00D30830`) + `CreatureSocialMode` (`0x00C0E800`) to enforce symmetric War/FF targeting.
  - Replaced raw `sp=HEX` / `nest (x,y,z)` / `Wild 0xHEX` UI rows with human-readable species names, distance in meters, brain tier, and `"Wild species #N"` / `"Дикий вид #N"`.
- `mods/coop/creations_library.cpp` & `mods/coop/species_line_core.h` (`creations.library_names`): parsed both offline and online/Maxis `0x030BDEE3` metadata names plus `ThumbnailMetadata` names from `DecodePNG`, added non-empty kind fallbacks (`Creature #N`, etc.), flushed Pollination on existing keys, and kept `g_tab_dirty` retrying until `AssetBrowserFeedLists` roots exist.

## Verification
- no-fail-run: reproduced 2/2 times in live v0.8.1.1 stream & host logs (`sporekit-20261126-190925.log`: `0 members` isolated continent, armored Civ creature, green War nest, 8x empty Shared Library names) and covered by new ASan+TSan unit tests (`tests/creature_rivals_tests.cpp`, `tests/species_tests.cpp`, `tests/library_tests.cpp`).
- PASS runs: `20261126-155538:rivals_creature`, plus `make test` (all 10 suites).
