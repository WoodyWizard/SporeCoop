---
id: BUG-1573
title: Tribe Rivals polish: block Neutral food-mat steal, clamp starting food, rebuild held staff, hide F9 Give, and explain hut floor
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-26
source: YouTube 0.8.0 stream audit
role: both
versions: 0.8.0
fixed_in: unreleased
branch: main
commits: 7dddc2d0, 1e808fcb
tests: rivals_tribe_polish
signature:
docs: docs/re/tribe.md
related: BUG-1548, BUG-1554, BUG-1571
---

## Report
Observed five Tribe Rivals & Tribe Together gameplay/UX issues in the 0.8.0 stream audit:
1. Players could steal food from a Neutral/Allied rival player's food mat (action `0x21` / `CitizenAction 8` `RaidFood`) even without being at War.
2. A guest taking over an existing NPC tribe inherited bloated vanilla NPC starting food (100-154 food vs host's 15-25 food).
3. After `swap_tribe_species()` swapped a taken-over tribe's species to the guest's species, the chieftain's held staff/tool stayed attached to the old species' bone index (sticking out of the head or leg).
4. The F9 `Give` button was still visible in Tribe Rivals / Civ Rivals where unit transfer is not valid, and in Tribe Together a guest's `Add Baby` flag (`TribeUI + 0x3A3`) was only unlocked under `wipe_recovery_active()`.
5. Hitting a rival player's main hut down to its 10% inviolability floor gave no explanation to players why the hut stopped taking damage.

## Root cause
- Observed in `mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_rivals_logic.h`, `mods/coop/tribe_host.cpp`, `mods/coop/tribe_species.cpp`, `mods/coop/tribe.cpp`, and `mods/coop/coop.cpp`:
  1. `override_choose_action()` only resolved citizen/hut/tool nouns and did not resolve `kFoodMatNoun` (`0x0629BAFE`, referenced by `cTribe + 0x260` `mpFoodMat`); `tribe_of_target_obj()` lacked `kFoodMatNoun`; and `is_hostile_citizen_action()` only checked actions `7` and `15`, omitting `8` (`RaidFood`).
  2. `host_ensure_assignments()` raised NPC food `< 25` to `25`, but never clamped bloated vanilla NPC food (`100-154`) down to match the host.
  3. `swap_tribe_species()` called `SetSpecies` / `SetSpeciesProfile` without calling `tribe_species.cpp::after_rebuild(c)`.
  4. `coop.cpp::a.give` did not return `0` when `tribe_rivals::active()` or `civ_rivals::active()`, and `tribe.cpp::h_BabyBorn` gated `TribeUI + 0x3A3 = 1` on `tribe_rivals::wipe_recovery_active()`.
  5. `h_HutTD()` returned `0` silently once `clamped <= 0.0f`.

## Fix
Implemented under switch `rivals.tribe_polish` (`coop.fix rivals.tribe_polish off` restores old behavior for testing):
- **Food-mat raid gating (`mods/coop/tribe_rivals_logic.h`, `mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_host.cpp`)**: added `action == 8` to `is_hostile_citizen_action()`, resolved `kFoodMatNoun` (`0x0629BAFE`) via `cTribe + 0x260` in `tribe_of_target_obj()` and `override_choose_action()` (blocks `0x21` on Neutral/Allied/shielded rivals, allows `0x21` at War).
- **Starting food parity (`mods/coop/tribe_rivals_logic.h`, `mods/coop/tribe_rivals.cpp`)**: added `starting_rival_food(npc_food, host_food, fix_on)` and applied it in `host_ensure_assignments()` so a newly assigned guest tribe matches host starting food (floor 15, fallback 25).
- **Held staff bone rebuild (`mods/coop/tribe_species.cpp`, `mods/coop/tribe_internal.h`, `mods/coop/tribe_rivals.cpp`)**: exported `tribe::rebuild_member_hand(void* c)` calling `after_rebuild(c)` (guarded by `game::listed(c, 0x18EB4B7u)`) and invoked it for each swapped member in `swap_tribe_species()`.
- **F9 Give & guest Add Baby (`mods/coop/coop.cpp`, `mods/coop/tribe.cpp`)**: hide F9 `Give` in Tribe Rivals and Civ Rivals; restore `TribeUI + 0x3A3 = 1` in `h_BabyBorn` for all guests (Together and Rivals).
- **Main hut 10% floor explanation (`mods/coop/tribe_rivals.cpp`)**: emit a throttled (30 s) localized notice when a rival main hut hits its 10% floor.

## Verification
- Unit tests (`make unit-cpp`, `tests/tribe_rivals_tests.cpp::test_tribe_polish_food_and_raid`): PASS (starting food clamp ON/OFF + CitizenAction 8 Neutral/Ally/Immune/War matrix).
- Wine lab ON/OFF twin (`out/lab/20261126-131908`):
  - `rivals_tribe_polish` (ON): **PASS** (26 s, 0 faults; verified guest starting food clamped to match host `15`, Neutral `check_foodmat 0` returned `action=-1` with `blocked >= 1`, F9 `Give` hidden on host & guest, War `check_foodmat 0` returned `action=33` (`0x21`), `swap_species` rebuilt cleanly, and `hit_hut` clamped at 10% floor with `conquered=0`).
  - `rivals_tribe_polish_off` (`SPORE_FIX_OFF=rivals.tribe_polish`): **FAIL** (29 s; guest inherited `100` food vs host `15`, Neutral `check_foodmat` returned `action=0 blocked=0`, War returned `action=0`).
- Regressions (`out/lab/20261126-132121`): `rivals_tribe` **PASS** (36 s), `rivals_tribe_wipe` **PASS** (44 s).

## History
- 2026-11-26: opened from 0.8.0 YouTube stream audit.
- 2026-11-26: fixed on `main` (`7dddc2d0`, `1e808fcb`), verified with unit tests, ON/OFF Wine lab twin (`out/lab/20261126-131908`), and Tribe regressions (`out/lab/20261126-132121`).
