---
id: BUG-1557
title: Rivals per-player evolutionary path, Tribe/Civ consequence cards and Space philosophy
status: fixed
kind: feature
stage: All
reported: 2026-11-25
source: Discord (@Хлебушек)
role: both
versions: 0.8.0.1
fixed_in: 0.8.0.2
branch: w0802/rivals-history
commits: 5da1cf6b, 6840b8b6
tests: rivals_history, rivals_history_off, history_rivals_tests
signature:
docs: docs/11-rivals.md
related: BUG-1470, BUG-1471, BUG-1548, BUG-1549, BUG-1550
---

## Report
In competitive (Rivals) mode, players shared the host's Tribe and Civ consequence cards (and therefore ended up with the same or nearly identical evolutionary path / Space philosophy) instead of earning independent Tribe/Civ cards and distinct Space philosophies from their own actions.

## Root cause
`mods/coop/history.cpp:306-307` computed per-player consequence cards for `kCell` (`x.cell_own`), `kCreature` (`creature_card_for`), and `kSpace` (`space_card_for`), but returned `0` for `kTribe` and `kCiv` (and `history::update()` in `history_logic.h` unconditionally copied `host[kTribe]` and `host[kCiv]` from 0.7.17 Together mode where all players shared one tribe and one nation). Furthermore, `creature_rivals`, `tribe_rivals`, and `civ_rivals` did not feed each guest's Tribe/Civ/species-ally outcomes into `history`, `civ_rivals` did not prefer an NPC nation specialty matching the guest's Rivals Tribe card on takeover, and `history::guest_space()` did not wait for Space Rivals guest homeworld emigration before calling `SpaceGame_SetArchetype`.

## Fix
- Added `COOP_FIX("rivals.own_history", "BUG-1557")` in `mods/coop/history.cpp` (default ON; OFF reverts Tribe and Civ cards to host inheritance).
- Extended `history::Record` in `mods/coop/history_logic.h` with per-player Tribe (`tribe_allied`, `tribe_conquered`) and Civ (`civ_military`, `civ_economic`, `civ_religious`, `civ_start_spec`) tallies, pure card helpers (`tribe_card_for`, `civ_card_for`, `civ_spec_for_tribe_card`, and native `cCity` specialty conversions `0=Mil, 1=Rel, 2=Eco`), `own_tribe`/`own_civ` support in `history::update()` with live `kSpace` recomputation when prerequisite cards change, and backwards-compatible store encoding (32-byte legacy v1 when tallies are zero, 43-byte extended v1 otherwise).
- Exposed host tracking APIs in `mods/coop/history.h` / `mods/coop/history.cpp` (`record_tribe_outcome`, `set_civ_start_specialty`, `record_civ_outcome`, `player_card`, `player_has_own_tribe`, `host_sync_now`) and extended `coop.history` (`status`, `tribe`, `civ`, `civ_start`).
- Wired gameplay modules:
  - `mods/coop/creature_rivals.cpp`: credits species-ally social bonus and `dna` in `on_guest_impress`, and runs `history::host_sync_now()` on native Creature -> Tribe transition before Tribe loads.
  - `mods/coop/tribe_rivals.cpp`: records guest NPC tribe ally (`h_ApplyRel`, `SmoothOver`, `setrel`) and conquest (`h_SetConquered`, `setrel`) outcomes to `history::record_tribe_outcome`.
  - `mods/coop/civ_rivals.cpp`: prefers free NPC nations whose capital specialty matches a guest's Rivals Tribe card when Tribe Rivals history is present, records taken-over starting specialty via `history::set_civ_start_specialty`, and records guest city capture/conversion/buy outcomes (`cCity +0x29C`) via `history::record_civ_outcome`.
  - `mods/coop/space_rivals.cpp` + `mods/coop/history.cpp`: added `spacerivals::emigration_in_progress()` so `history::guest_space()` waits for Space Rivals guest Phase 1 homeworld emigration before calling `SpaceGame_SetArchetype`.

## Verification
- Unit tests (`make unit-cpp unit-net unit-lint` under ASan+UBSan): `history_rivals_tests`, `validate_tests` (`test_history`), `fixes_tests`, and `test_lint.py` all PASS.
- Two-game Wine lab ON/OFF twin on lane 2 (`out/lab/20261125-150846`):
  - `rivals_history` (ON): **PASS** (40 s, 0 faults on both games). Verified Space Rivals emigration (`00460035 -> 00420011`), guest Tribe/Civ/Space `cPlayer` card divergence (`60A78928` / `6AEB96A1` / `B40D2187` vs host `0CAC124D` / `0A35D0F5` / `A5656E84`), independent native `cEmpire +0x58` archetype (`16` Ecologist vs host `17` Wanderer), Civ `civ_start_spec` tie-breaking (2 vs 2) and live overtake to Economic (`3360727F` -> Bard `50FEE6BF`, `cEmpire +0x58 = 13`), and full persistence across guest rejoin.
  - `rivals_history_off` (`SPORE_FIX_OFF=rivals.own_history`): **FAIL** (94 s) on the exact Tribe/Civ/Space card and `cEmpire +0x58` divergence assertions (guest remained locked to host cards and archetype `17`).
- Regression check on lane 2 (`out/lab/20261125-151114`): `rivals_tribe` (35 s) and `rivals_civ` (38 s) **PASS** (2/2, 0 faults).
