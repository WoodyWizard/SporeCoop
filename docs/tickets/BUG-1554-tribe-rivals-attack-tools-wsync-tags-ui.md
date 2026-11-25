---
id: BUG-1554
title: Tribe Rivals: guest War attack, guest tool purchases, wsync isolation, tags/relations, Creature payout and NPC Influence UI
status: open
kind: bug
stage: Tribe
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in:
branch: w0801/tribe
commits:
tests: tests/tribe_rivals_tests.cpp
signature:
docs: docs/11-rivals.md
related: BUG-1548, BUG-1547
---

## Report
Independent static audit of `BUG-1548` found critical gameplay blockers in Tribe Rivals:
1. `tribe_host.cpp:706` runs `ff_try_order` in Tribe Rivals, swallowing guest attack orders against host tribe members at War.
2. Guest tool/hut purchases (`worldobj.cpp:576-585`, `tribe.cpp:1642`) still target host `player_tribe()` instead of sender's tribe.
3. `wsync.cpp` clobbers guest hut level/pop cap (`s_tlevel`) and goal bar (`s_goal`) with host values, omits host village food sync, `tribe_host.cpp:712` feeds guest actions into host `wsync::goal_target`, and `wsync.cpp:928` (`s_nest`) lacks a `creature_rivals::active()` guard.
4. Missing rival member/chief tags (`tribe.cpp:5167-5177`), missing player-vs-player War/Ally write into `cRelationshipManager`, co-op `epic_tick` still active in Rivals (`tribe.cpp:4393-4414`), Creature Rivals draft payout never delivered as Tribe food, and no NPC tribe list for F9 Influence UI.

## Root cause
See `docs/plan-0.8.0.1.md` WP-4.

## Fix
Implemented WP-4 on `w0801/tribe`:
1. **Guest War attack routing (`mods/coop/tribe_host.cpp`, `mods/coop/tribe_rivals_logic.h`)**: gated `ff_try_order` and `ff_host_tick` on `!tribe_rivals::active()`, added `tribe_rivals::decide_host_order` + `tribe_of_target_obj` so Tribe Rivals guest hostile orders (`action == 7` attack / `15` raid) against a rival player's citizen/hut/tool pass directly to `DoAction` when `rivals::at_war(from, target_owner)` and are blocked only when not at war.
2. **Guest tool/hut purchases (`mods/coop/worldobj.cpp`, `mods/coop/tribe.h`, `mods/coop/tribe.cpp`, `mods/coop/tribe_rivals.h`, `mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_rivals_logic.h`)**: routed `worldobj.cpp` `tool_buy` through `tribe::buy_player_tool_for(from, type, pos)` using `plan_tool_buy` to target `tribe_rivals::tribe_of_player(from)`, validate the 150 m distance from that player's village, check and deduct food from that player's tribe on new purchases (`spend_player_food` / `sync_player_food`), and build the tool in the sender's village.
3. **`wsync.cpp` isolation (`mods/coop/wsync.cpp`, `mods/coop/tribe_host.cpp`, `mods/coop/tribe_rivals_logic.h`)**: gated `s_tlevel`, `s_goal`, and host `wsync::goal_target` pushes on `!tribe_rivals::active()`, gated `s_nest` on `!creature_rivals::active()`, and updated `r_tfood`/`s_tfood` so host's own village food is synced to guests in Tribe Rivals without clobbering the guest's own swapped tribe food (`should_sync_tribe_food`).
4. **Rival tags, arrows, PvP diplomacy, co-op epic, `party::hit`, and Creature draft payout (`mods/coop/tribe.cpp`, `mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_rivals_logic.h`)**: synced player-vs-player War/Ally/Neutral relations into `cRelationshipManager` (`sync_pvp_native_relations`), walked all player-owned tribes in `tribe::for_each_owned`, `chiefs_tick`, and `restore_chief_scales` (while skipping co-op `epic_tick`, `spread_chiefs`, and `place_chiefs` in Tribe Rivals), classified all player-owned tribes as `party::Side::Player` in `h_CitizenTD`, and delivered `rivals::take_pending_draft_payout_for(rivals::Stage::Creature, pid)` as Tribe starting food for both host and guests (`host_deliver_creature_payouts`).
5. **F9 Influence UI NPC list (`mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_rivals_logic.h`)**: implemented and registered `hooks.on_npc_ui_list` via `build_npc_ui_list` returning live non-player NPC tribes with readable labels (`NPC Tribe #...`), `allied`, `hostile`, `can_turn = true`, `can_protect = allied`, and `can_smooth = hostile`.

## Verification
- Native build (`make -j$(nproc)`): PASS, 0 warnings/0 errors on all modified files.
- Native ASan+UBSan unit tests (`make unit-cpp`, including 11 test suites in `tests/tribe_rivals_tests.cpp` with 5 new suites covering War attack routing, guest tool buy routing/food charging, `wsync` isolation policy, PvP diplomacy + Creature->Tribe payout delivery, and NPC UI list generation): PASS.
- Lint (`python3 tools/tests/test_lint.py`): PASS (`0 problem(s)`).
- Wine/lab live verification pending merge/integration.
