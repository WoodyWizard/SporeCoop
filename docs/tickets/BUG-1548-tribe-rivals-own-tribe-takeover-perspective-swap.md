---
id: BUG-1548
title: Tribe Rivals: own tribe takeover, perspective swap, per-player NPC diplomacy, raids, inviolable main village and draft
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-25
source: docs/11-rivals.md 11.3.3
role: both
versions: 0.8.0
fixed_in: unreleased
branch: w08/rivals-tribe
commits: 38d7ac24
tests: rivals_tribe, rivals_tribe_off
switch: rivals.tribe
signature:
docs: docs/re/tribe.md#tribe-rivals-re
related: BUG-0794, BUG-1546, BUG-1547, BUG-1549, BUG-1550
---

## Report
Implement Tribe Rivals mode (WP-R4 of 0.8.0, `docs/11-rivals.md` sections 11.3.3 and 11.4-11.9) so each player owns an independent tribe in one shared Tribe world with own food, huts, tools, members, per-player NPC tribe diplomacy and influence, war PvP and food/tool raids with inviolable main villages, alliance gifts, >50% NPC tribe stage-end race, catch-up timer, and Tribe draft.

## Root cause
Vanilla Tribe stage and SporeCoop Together mode assume one shared player tribe (`cGameNounManager::mpPlayerTribe`), one shared food pool (`shared.cpp`), one shared NPC relationship table (`wsync.cpp` `tribe_rel`), and active planner AI (`cTribePlanner::Update` via `cTribe+0x290`) on every NPC tribe.

## Fix
Behind `COOP_FIX("rivals.tribe", "BUG-1548", ...)` in dedicated `mods/coop/tribe_rivals_logic.h` and `mods/coop/tribe_rivals.{h,cpp}` with surgical hooks in `tribe.cpp`, `tribe_host.cpp`, `tribe_mirrors.cpp`, `tribe_debug.cpp`, `shared.cpp`, and `wsync.cpp`, routed over `MSG_RIVALS_FACTION` (`Stage::Tribe`):
- **RE (GOG 2024 `dc04aee5`):** verified `cGameNounManager::SetPlayerTribe` (`0x00B1FD50`), `Tribe_IsPlayerOwned` (`0x00C8E660`), `cGameTerrainCursor::GetTerrainCursor` (`0x00B30EB0`), `cGameTerrainCursor_SetObjects` (`0x00B2FC70`), `cTribePlanner_Update` (`0x00CE4A70`), `cTribeTool_TakeDamage` (`0x00C9D800`), `cRelationshipManager::GetRelationshipLevel` (`0x00D008E0`), `cTribe_IsToolPurchased` (`0x00C8ED90`), `Tribe_UnlockToolNotify` (`0x00CD7280`), and `TribeToolSet_At` (`0x00B54820`).
- **R2 Host NPC `cTribe` takeover:** selects an NPC `cTribe` at configured `rivals::base_distance()` (`Near` / `Middle` / `Far`), disables its planner AI by hooking `cTribePlanner_Update` without mutating `+0x290`, swaps its species slot (`cTribe_SetSpeciesSlot`) and members/chief to the guest's species (restored around `savestrip` saves), and persists ownership in `store.cpp` (`tribe.rivals`).
- **R1 Guest perspective swap:** syncs ownership via `SK_ASSIGN`, swaps `mpPlayerTribe` via `SetPlayerTribe` and cursor candidates (`cGameTerrainCursor_SetObjects`), keeps `tribe::world_anchor()` anchored to the original player tribe so swapping does not trigger false `kWorldChange`, pauses `shared.cpp` food sharing and syncs each player's own food via `SK_STAGE_CUSTOM`, and restores original `mpPlayerTribe` cleanly on save/exit/leave.
- **R4 Per-player NPC relations & influence:** tracks independent per-player NPC relations on host and syncs each guest's view via `SK_NPC_RELS` (disabling Together `wsync.cpp` `s_rel`), registers `TurnAgainst` / `Protect` / `SmoothOver`, earns influence on music (`0x0530CF07`) and gifts (`0x0530CF02`), and charges own Tribe food in `on_buy_influence`.
- **War raids, PvP & R7 Main Village Inviolability:** blocks Neutral/Allied player damage across citizens, huts, and tools; allows War citizen PvP, extra tool hut damage, and food raids; clamps `cTribeHut_TakeDamage` on any player-owned main hut above floor and blocks `cTribe_SetConquered` on player tribes.
- **Alliance gifts & >50% Stage End + Tribe Draft:** supports allied food/tool gifts via `SK_GIFT` and F9 Rivals UI; evaluates >50% NPC tribe subjugation every second -> `rivals::report_stage_complete` -> catch-up timer -> `on_stage_race_end` launches Tribe tool draft, unlocks copied tools via `SK_DRAFT_APPLY` (`TribeToolSet_At` + `cTribe_SetToolPurchased`), and unlocks Tribe->Civ advance button on host (R3).
- Added portable ASan+UBSan unit tests in `tests/tribe_rivals_tests.cpp` and Wine lab test `tools/tests/coop_rivals_tribe.py` (`rivals_tribe`, `rivals_tribe_off`).

## Verification
- Unit tests (`make unit-cpp unit-net unit-lint`): `tribe_rivals_tests` (ASan+UBSan), `tribe_tests`, `rivals_tests`, `net_tests`, `re_lint`, `docs_lint`, `glyph_lint`, `ratchet_lint`, `tickets check` all 0 problems.
- PASS (`rivals_tribe`): `out/lab/20261125-035136` (lane 4, 32 s, 0 host faults, 0 guest faults: host assigned NPC tribe `000021CB` to guest with `planner_skipped=87` and species swap `067B3E4A`; guest swapped perspective `swapped=1 my_pol=000021CB` with independent food `host=95 guest=135`; music/gift influence earned + food-funded influence buy `195->145` + Protect blocked TurnAgainst + TurnAgainst dropped guest NPC relation to `Annoyed` while host stayed `Neutral` + SmoothOver restored `Neutral`; Neutral blocked member & hut damage; War allowed member PvP `159->144`, extra tool hut damage `200->180`, food raid `15`, and R7 clamped main hut hit `99999` to `40.00` HP `conquered=0`; Alliance gifted 25 food and tool 9; >50% NPC subjugation `2/3` triggered `winner=0` -> catch-up expiry -> Tribe draft -> guest copied tool with `payout=100` & `draft_unlocked=1`; clean leave restored `swapped=0`).
- FAIL (`rivals_tribe_off`, `SPORE_FIX_OFF=rivals.tribe`): `out/lab/20261125-035136` (lane 4, 40 s; Tribe Rivals disabled `fix=0 active=0`).

## History
- 2026-11-25: opened.
- 2026-11-25: implemented in `38d7ac24` on `w08/rivals-tribe`, verified PASS + OFF FAIL in `out/lab/20261125-035136`.
