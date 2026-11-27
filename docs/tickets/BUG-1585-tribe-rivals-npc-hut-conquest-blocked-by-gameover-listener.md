---
id: BUG-1585
title: Tribe Rivals: destroying an NPC tribe hut was swallowed by h_TribeGoalListener (0x058BADDD), blocking conquest, totem progress, next waves, and stage progression
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-26
source: Discord player report on v0.8.2
role: both
versions: 0.8.1.2, 0.8.1.3, 0.8.2
fixed_in: 0.8.2.1
branch: main
commits: aa7bb0f1
tests: tribe_rivals_tests, rivals_tribe_wipe, rivals_tribe, tribe_conquest_guest
signature: h_TribeGoalListener blocked 0x058BADDD (kMsgHutDestroyed) for NPC tribes -> conquered=0, rel=Neutral
docs: docs/re/tribe.md
related: BUG-1571, BUG-1580
---

## Report
Player report on Discord (`v0.8.2`): "Started Tribe, broke the hut, the new stage didn't start."

## Root cause
- In Spore's `TribeGame_GoalListener` (`0x00CD81F0`, verified by disassembly at `0x00CD8727..0x00CD8846`), message `0x058BADDD` (`kMsgHutDestroyed`) fires whenever **any** tribe's main hut reaches `0 HP`, carrying `data + 0x08` = defeated `cTribe*` and `data + 0x18` = attacker `cTribe*`.
- The handler first calls `cTribe_SetConquered(victim, true)` (`+0x556 = 1`, burning VFX, clears bundles) and only enters the `TRG_PRE_HutDestroyed` Game Over path if `victim->IsPlayerOwned()` returns `true` (`0x00CD8768`).
- `BUG-1571` added `h_TribeGoalListener` in `mods/coop/tribe_rivals.cpp` to prevent Game Over in Tribe Rivals, but it unconditionally blocked **every** `0x058BADDD` message without inspecting `data + 0x08`. As a result, breaking a free NPC tribe's hut in Tribe Rivals never called `cTribe_SetConquered`, never updated `g_npc_rels`, never completed `cTribeToTribeMission` / `TribeStage_AddGoalProgress`, never spawned subsequent waves, and prevented advancing out of Tribe stage.

## Fix
- `mods/coop/tribe_rivals_logic.h` & `mods/coop/tribe_rivals.cpp` (`rivals.tribe_npc_hut_conquest`):
  - Added pure decision helper `should_block_tribe_goal_msg(wipe_recovery_on, msg_id, victim_is_player, npc_conquest_fix)`.
  - In `h_TribeGoalListener`, inspected `data + 0x08` on `kMsgHutDestroyed` (`0x058BADDD`) and blocked the message **only** when `victim` is a player-owned tribe (`victim == tribe::player_tribe() || is_player_owned_tribe(victim)`).
  - Passed NPC hut destruction straight through to `o_TribeGoalListener` while preserving `owner_of_tribe(data + 0x18)` as `g_conquer_hint_owner` so `h_SetConquered` attributes the NPC conquest to the exact conquering player (Host or Guest).
- `tests/tribe_rivals_tests.cpp` & `tools/tests/coop_rivals_tribe_wipe.py`: added unit tests for `should_block_tribe_goal_msg` and a live Wine lab Phase 3 test (`coop.tribe rivals hit_npc_hut`) destroying a real free NPC tribe's hut and verifying `conquered=1`, `p0=Conquered`, and unchanged `pop_go_blocked`.

## Verification
- FAIL run (reproduces exact `0.8.2` bug with `SPORE_FIX_OFF=rivals.tribe_npc_hut_conquest`): `20261126-200836:rivals_tribe_wipe` (`hp 750.00 -> 0.00 conquered=0 pop_go_blocked=1->2 rel=Neutral`).
- PASS runs (fix ON): `20261126-200925:rivals_tribe_wipe,rivals_tribe,tribe_conquest_guest` (3/3 PASS, 0 faults), plus `make test` (all 10 suites).
