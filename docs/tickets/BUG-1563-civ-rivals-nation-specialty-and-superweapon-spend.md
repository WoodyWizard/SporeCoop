---
id: BUG-1563
title: Civ Rivals: guaranteed nation/city specialty conversion to match Tribe card and guest superweapon spending
status: fixed
kind: feature
stage: Civ
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in: 0.8.1
branch: w081/civ-spec-superweapon
commits: 193030fd
tests: rivals_civ_spec_sw, rivals_civ_spec_sw_off
signature: 0x00BF18CA
docs: docs/11-rivals.md
related: BUG-1549, BUG-1555, BUG-1557
---

## Report
1. In Civ Rivals, if all players finished Tribe with the same consequence card (or no free NPC nation on the planet happens to have the matching Military/Economic/Religious specialty), a guest is assigned an NPC nation with a mismatched specialty instead of converting that taken-over nation/capital to match the guest's own Tribe consequence card.
2. When a guest fires a superweapon in Civ Rivals (`civ.cpp` `host_superweapon_request`), `o_FireSW(pc, type, pos)` runs against a guest `cCivilization` with `mIsPlayer == 0`, where:
   - `0x00BF7020` never populates `cCivilization::mSuperweaponTools` (`+0x470`) on NPC civs (`mIsPlayer == 0`), causing an access violation (`0xC0000005` at `0x00BF18CA`, reading `0x0000000C` for type 3) inside `cCity::LaunchSuperweapon` -> `0x00BF18C0`;
   - `host_superweapon_request` did not validate `civ_money(pc) >= cost`, wrongly marked `g_sw_fired = type` even when refused, and left `FireSuperweapon` (`0x00BF8BB0`) on its `mIsPlayer == 0` NPC budget refill branch (`+0x4AC`/`+0x4B0`) instead of cleanly deducting `cost` from `mWealth (+0x98)`.

## Root cause
- `civ_rivals.cpp` preferred matching NPC nations on takeover, but fell back to an unmatching NPC nation without updating its capital city specialty (`cCity +0x540` via `cCity_SetSpecialty`, `+0x29C`, and `Civilization_RefreshUnlocks`) or replicating the converted specialty in `SK_ASSIGN`.
- `civ.cpp` `host_superweapon_request` called `o_FireSW` directly on an NPC-origin guest `cCivilization` (`mIsPlayer == 0`) with uninitialized `+0x470` superweapon tools and without validating/finalizing `mWealth (+0x98)` directly.

## Fix
- Behind `COOP_FIX("rivals.civ_spec_convert", "BUG-1563")`: after assigning an NPC `cCivilization` to a guest, if its capital specialty (`+0x540`) does not match the desired specialty from the player's Tribe consequence card, convert the capital city on host (`SporeCoop::cCity_SetSpecialty`, `+0x29C`, and `SporeCoop::Civilization_RefreshUnlocks`), record the converted starting specialty in `history::set_civ_start_specialty`, replicate `cap_specialty` in `SK_ASSIGN` (backward-compatible trailing bytes) so guests convert their local copy too, update `civ_arrival.cpp` `vehicle_editor()` to honor the guest's converted Civ Rivals capital specialty, and expose `spec_converted` in `coop.civ rivals`.
- Behind `COOP_FIX("rivals.civ_sw_spend", "BUG-1563")`: in `host_superweapon_request` for Civ Rivals guests (`from != 0`), validate `civ_money(pc) >= superweapon_cost(type, foreign)` (rejecting with a notice and incrementing `sw_refused_funds` when short), temporarily lend the host player civ's initialized superweapon tool vector (`+0x470`) via RAII + `game::swap_open` (`civ_rivals::SwToolsBorrow`) across `o_FireSW`, and on successful fire clamp `mWealth (+0x98)` to `max(0, before - cost)` with zeroed NPC budgets (`+0x4AC`/`+0x4B0`), immediate `SK_ASSIGN` sync, and `sw_spent` counter.

## Verification
- Unit tests (`tests/civ_rivals_tests.cpp`): `make unit-cpp unit-net unit-lint` and `python3 tools/tests/test_lint.py` all **PASS** (0 problems).
- Two-game Wine lab ON/OFF twin on lane 3 (`out/lab/20261125-174743`):
  - `rivals_civ_spec_sw` (ON): **PASS** (27 s, 0 faults on host & guest). Converted guest capital 0 -> 2 (Economic) on both host and guest (`spec_converted=1`, `+0x540 == 2`, `+0x29C == 2`, `history` `start=2`); refused short-funds superweapon (`sw_refused_funds=1`, `fired=-1`, wealth 500 -> 500); fired type 3 with sufficient funds and planted NPC budgets (`sw_spent=1`, `fired=3`, `mWealth` 5000 -> 3000, `dom=0.0`, `exp=0.0`, guest synced `player_money=3000`).
  - `rivals_civ_spec_sw_off` (`SPORE_FIX_OFF=rivals.civ_spec_convert,rivals.civ_sw_spend`): **FAIL** (46 s, expected twin failure: `spec_converted=0`, capital stayed Military `0`, `start=1`, `sw_refused_funds=0`, `sw_spent=0`, and reproduced host fault `0xC0000005` at `0x00BF18CA`).
- Regression check on lane 3 (`out/lab/20261125-174743`): `rivals_civ` **PASS** (38 s, 0 faults).
