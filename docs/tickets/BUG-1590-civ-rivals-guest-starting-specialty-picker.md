---
id: BUG-1590
title: Civ Rivals: guest cannot choose starting nation specialty (forced into Economic / host Tribe card with no picker)
status: fixed
kind: feature
stage: Civ
reported: 2026-11-26
source: Discord player report (v0.8.2.2, SporeCoop-report-20261127-061420.zip)
role: both
versions: 0.8.2.2
fixed_in: 0.8.2.3
branch: main
commits: c9029b7e
tests: rivals_civ_spec_sw, civ_rivals_tests, make test
signature: civ_rivals: guest assigned Economic rival nation without starting specialty choice
switch: rivals.civ_spec_pick
docs: docs/11-rivals.md
related: BUG-1563, BUG-1565, BUG-1587
---

## Report
Players on Discord reported that in Civilization Rivals, a guest cannot choose their starting civilization specialty (Military / Religious / Economic) and gets dropped straight into Economic with no choice (`SporeCoop-report-20261127-061420.zip`: `luftklas took command of a Economic rival nation` both after Tribe -> Civ and on a brand-new Civ planet `Капта.spo`).

## Root cause
1. On entering Civilization Rivals (whether from Tribe -> Civ or on a brand-new planet started directly at Civ), `civ_rivals::host_ensure_assignments` immediately assigned each guest a nation using `history::player_card(pl.id, history::kTribe)` (which defaults to the host's Tribe card on fresh Civ starts, or falls back to an arbitrary NPC nation).
2. Guests had no UI or wire action to choose Military, Religious, or Economic for their starting Civ Rivals nation (`coop_ui.cpp` `refresh_rivals()` only showed prior-stage history cards `Cell/Creature/Tribe`, never Civ specialty).
3. Even when a guest clicked `Tribe: Change` in `Starting Path (Mid-Game)` (`MSG_PLAYER_HISTORY` `K_PICK`), `history.cpp` never notified `civ_rivals`, so the already-assigned capital city specialty stayed unchanged forever.

## Fix
- **Portable logic & wire codec (`mods/coop/civ_rivals_logic.h`)**:
  - Added `kSkSpecPick = 18` inside `MSG_RIVALS_FACTION` (`Stage::Civ`) with `SpecPickMsg`, `write_spec_pick`, `read_spec_pick`, `can_pick_starting_specialty(assigned, owned_cities, target_spec)` (allowed strictly while owning only the starting capital = 1 city), `parse_civ_specialty_arg`, and Russian specialty labels `civ_specialty_name_ru`.
- **Host & guest runtime (`mods/coop/civ_rivals.h`, `mods/coop/civ_rivals.cpp`, `mods/coop/history.cpp`)**:
  - Added `COOP_FIX("rivals.civ_spec_pick", "BUG-1590")`: host validates `cs.size() == 1` and `valid_native_specialty(target_spec)`, applies `apply_city_specialty` (`cCity_SetSpecialty` + `+0x540` + `+0x29C` + `Civilization_RefreshUnlocks`), populates land/sea/air vehicle designs via `ensure_civ_vehicle_designs`, updates `g_nations[pid].cap_specialty`, `g_saved_nations`, and `history::set_civ_start_specialty`, and immediately broadcasts `SK_ASSIGN` so the guest's local capital city, unlocks, vehicle designs, and pending `civ_arrival` vehicle editor all switch to the chosen specialty.
  - Wired `history.cpp` `K_PICK` (`kTribe` change while in `modes::kCiv`) to call `civ_rivals::on_tribe_card_picked`.
  - Added an on-arrival localized toast on the guest telling them their current specialty and pointing to `F9 -> Rivals`, localized `Civ Rivals: Nations & Capitals` with each player's specialty tag, and added `coop.civ rivals spec [pid] <mil|rel|eco>` + `spec_picked` status counter.
- **1-click F9 UI (`mods/coop/coop_ui.cpp`)**:
  - Added a direct 1-click Starting Specialty picker (Military / Religious / Economic) at the very top of Page 1 of `F9 -> Rivals` for Civ Rivals guests while they own only their capital (zero impact on host, lobby, Together mode, or `ui_golden`).

## Verification
- PASS run: `out/lab/20261126-215520` (`rivals_civ_spec_sw`: PASS, 29 s, 0 faults on host & guest; guest switched starting specialty Economic -> Military -> Religious live across host & guest with `spec_picked=2` and `history start=3`), plus unit tests (`tests/civ_rivals_tests.cpp` `test_guest_starting_specialty_pick`) and full `make test` (all 10 suites including ASan/UBSan, TSan, `glyph_lint.py`, `static_guard_lint.py`, `tickets.py check`).
- no-fail-run: feature addition (`rivals.civ_spec_pick`) where OFF mode simply returns `"disabled by rivals.civ_spec_pick"` and hides the guest picker rows; covered by unit tests and ON live Wine run `out/lab/20261126-215520`.
