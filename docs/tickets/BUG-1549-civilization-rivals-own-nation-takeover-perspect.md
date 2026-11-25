---
id: BUG-1549
title: Civilization Rivals: own nation takeover, perspective swap, per-player NPC diplomacy, war city capture, inviolable capital and draft
status: fixed
kind: feature
stage: Civ
reported: 2026-11-25
source: docs/11-rivals.md 11.3.4
role: both
versions: 0.8.0
fixed_in: 0.8.0
branch: w08/rivals-civ
commits: c44ea321
tests: rivals_civ, rivals_civ_off, civ_rivals_tests
switch: rivals.civ
signature:
docs: docs/re/civ.md#civ-rivals-re, docs/11-rivals.md
related: BUG-0794
---

## Report
Implement Civilization Rivals mode (WP-R5 of v0.8.0, `docs/11-rivals.md` 11.3.4, 11.4-11.9): each guest takes over an existing NPC `cCivilization` chosen by configured base distance (`Near`/`Middle`/`Far`) with its AI disabled, plays from that nation's own perspective and Sporebucks wallet, maintains per-player relations with remaining NPC nations (with Civ influence actions `TurnAgainst`, `Protect`, `SmoothOver`, and buying influence with own Civ money), exchanges money/spice gifts and trade income with allies, fights rival vehicles and captures ordinary rival cities at War while keeping every player's capital inviolable against capture/conversion/buyout/superweapons, and races to own >50% of NPC cities to trigger the catch-up timer, Civ design draft, and forced stage advance readiness.

## Root cause
Previously, Civilization co-op ran only in shared-nation Together mode (`civ_wallet` / `g_city_owner` over one `cCivilization`), with no NPC nation takeover, guest perspective swap, per-player NPC relation table, capital inviolability gates, or stage-end race/draft wiring.

## Fix
Gated behind `COOP_FIX("rivals.civ", "BUG-1549", ...)` (switch `rivals.civ`) using `MSG_RIVALS_FACTION` (`Stage::Civ`) and Wave 2 Rivals registries (`register_faction_handler`, `register_npc_influence_callback`, `register_stage_hooks`):
- **RE (`docs/re/civ.md#civ-rivals-re`, `data/symbols/overrides_dc04aee5.sym`):** Verified GOG 2024 addresses for `GameNounManager_GetPlayerPoliticalID` (`0x00B1F910`), `Civ_IncomeTick` (`0x00BF7310`), `City_SetIsPlayerOwned` (`0x00BD8310`), `City_CanTargetSuperweapon` (`0x00BDA440`), `City_LaunchGlobalSuperweapon` (`0x00BE4300`), `TerrainCursor_Get` (`0x00B30EB0`), `TerrainCursor_SetObjects` (`0x00B2FC70`), and `City_TradeDelivery` (`0x00BE19B0`). Also verified the single-player-civ `+0x89` invariant in `cRelationshipManager::CalculateRelationshipAbsolute` (`0x00D05AF4`): only one `cCivilization` per process may have `+0x89 == 1`, or `(polA, polB)` swaps recurse infinitely (`0xC00000FD`).
- **Portable logic & unit tests:** `mods/coop/civ_rivals_logic.h` + ASan/UBSan suite `tests/civ_rivals_tests.cpp` (base distance selection, PvP/capture/superweapon gates, `NpcRelationTable`, influence buy cost, alliance gifts, >50% NPC cities win evaluator, Civ draft items, wire codecs).
- **Runtime (`mods/coop/civ_rivals.{h,cpp}` + surgical hooks in `civ.cpp`, `civ_host.cpp`, `civ_shots.cpp`, `civ_own_designs.cpp`, `civ_debug.cpp`, `civ_wallet.cpp`, `shared.cpp`, `wsync.cpp`):**
  - R2: Host picks an NPC `cCivilization` matching `rivals::base_distance()`, folds `+0x4AC`/`+0x4B0` budgets into `+0x98` (3000 floor), keeps host `+0x89 = 0`, disables NPC AI via `h_CivAI` (running `Civ_IncomeTick` for passive income) and building/turret/vehicle/budget hooks, applies guest species, and broadcasts `SK_ASSIGN`.
  - R1: Guest swaps `GetPlayerCivilization`, `GameNounManager_GetPlayerPoliticalID`, `City_IsPlayerOwned` / `+0x33C`, `cCivilization+0x89`, and `TerrainCursor` selectable objects to its assigned nation, while keeping `civ::world_anchor()` bound to the unswapped underlying civ pointer and restoring across saves (`savestrip::add_save_hooks`).
  - R4: Host tracks per-player NPC relations, syncs each guest via `SK_NPC_RELS` (pausing `wsync` `civ_rel`), implements `TurnAgainst`/`Protect`/`SmoothOver`, charges buyer's own Civ money in `on_buy_influence`, and awards influence on trade deliveries (`City_TradeDelivery`).
  - War / Alliance / R7: Allied/Neutral damage and orders blocked; War enables vehicle PvP and ordinary rival city capture; player capitals are blocked from capture, capture damage, religious conversion, buyout, targeted superweapons (`City_CanTargetSuperweapon` / `FireSuperweapon`), and global superweapons (`City_LaunchGlobalSuperweapon`); allied money/spice gifts transfer via `SK_GIFT`.
  - Stage end & Draft: Host evaluates non-capital city shares every second (`rivals::set_player_progress`, `report_stage_complete`), starts Civ design draft on catch-up end (`on_stage_race_end`), applies drafted designs to picking guests (`SK_DRAFT_APPLY`), and calls `GameCiv_ShowPlanetWon` for Civ -> Space readiness.

## Verification
- Unit & lint: `make -j$(nproc) && make unit-cpp unit-net unit-lint` -> PASS (0 warnings, ASan+UBSan `civ_rivals_tests` PASS, `re_lint` / `docs_lint` / `glyph_lint` / `tickets` / `ratchet_lint` / `msg_manifest` 0 problems).
- Two-game Wine lab (lane 5, `out/lab/20261125-040209`):
  - `rivals_civ`: PASS (38 s, 0 faults on host and guest, 0 monitor/log-gate findings; all 33 checks verified R2 takeover by distance=Far & AI disable, R1 guest perspective swap & independent own money, R4 buy influence with own money & Protect/TurnAgainst/SmoothOver synced to guest, alliance money & spice gifts, Neutral/Allied PvP block vs War vehicle PvP & ordinary rival city capture, R7 capital capture/conversion/targeted+global superweapon blocks, and >50% NPC cities win -> catch-up -> Civ draft pick applied on guest).
  - `rivals_civ_off` (`SPORE_FIX_OFF=rivals.civ`): FAIL cleanly at R2 takeover (`fix=0 active=0 assigned=0`).

## History
- 2026-11-25: opened.
- 2026-11-25: implemented WP-R5 Civilization Rivals (`rivals.civ`), verified with `civ_rivals_tests` and Wine lab run `20261125-040209` (`rivals_civ` PASS, `rivals_civ_off` FAIL).
