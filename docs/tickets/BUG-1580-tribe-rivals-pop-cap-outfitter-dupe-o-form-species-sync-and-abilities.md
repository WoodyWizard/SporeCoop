---
id: BUG-1580
title: Tribe Rivals: baby pop cap, outfitter baby dupe, o_Form bool bug, host/guest species and diet sync, persistent tribe pol, alliance totem guard, night-sleep AI, tool sell, abilities, and KO respawn
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-26
source: YouTube stream (Vlaid18 + DERMOGEN, v0.8.1.1) + Discord host logs
role: both
versions: 0.8.1.1
fixed_in: 0.8.2
branch: main
commits: 3bb74b46
tests: tribe_rivals_tests, tribe_tests, rivals_tribe, rivals_tribe_wipe, rivals_tribe_polish, rivals_tribe_abilities_pets
signature: tribe_rivals: auto-formed NPC tribe 00000001 + MSG_TRIBE_ANIM 1178 bytes dropped + 151/6 outfitter baby dupe
docs: docs/re/tribe.md
related: BUG-1548, BUG-1564, BUG-1571, BUG-1573
---

## Report
During the `v0.8.1.1` stream (`Ламон.spo` and `Кельва.spo`):
1. Guest bypassed the 6/9/12 Tribe population cap by entering/exiting the Tribal Outfitter (163 babies -> `151/6` members), overflowing UDP `MSG_TRIBE_ANIM` (1178 bytes dropped) and causing a 17.5 s reload freeze.
2. Zero NPC rival tribes spawned for 13 minutes in `Кельва.spo` despite `total_auto=6` in the host log.
3. Guest spawned as a random herbivore duck ("утка", unable to gather meat, seeing fish as seaweed), while Host saw a duck chieftain surrounded by "Дед" adults, and species swaps on rejoin failed to sync.
4. Reloading after a wipe reassigned Guest to a different (already-destroyed maroon) tribe, converting Guest's real tribe into an autonomous `0/9` NPC tribe.
5. Allying with the rival player granted an NPC totem piece, triggered premature Tribe Stage victory while an enemy NPC tribe was still alive, and fired an unexpected mid-stage `Draft started`.
6. Guest tribesmen ran NPC night-sleep AI inside the main hut, could not sell tool huts, could not consistently attack wild animals/NPC huts, suffered auto-attacks from Host's tribesmen during Peace, had broken `Flying Fish` / `Fire Bombs` and 15x false `OnCooldown` denials (even 6 minutes later), and lacked a chieftain KO respawn indicator.

## Root cause
- `mods/coop/tribe_rivals.cpp:1372`: `handle_guest_baby()` only checked `tribe_food(gt) < 10` without checking the hut-tier population cap, and `TribeStage_BabyBorn` (`0x00CD74A0`) was called from `TribeStage_Enter` (`0x00CD7B21`) when `stage+0x3A3 == 0` after returning from Outfitter.
- `mods/coop/tribe_rivals.cpp:1665`: `tribe::o_Form` returns `bool` in `AL`, not `void*`; stale upper `EAX` bits (`0x00000001`, `0x40626201`) incremented `g_auto_form_count` even when no tribe formed.
- `mods/coop/tribe_rivals.cpp:790-819,881-921,1699`: `swap_tribe_species()` was never called on Guest (after `save_start()` stripped slots back to `orig_slot`), set `species_swapped = true` before `if (!world::cinematic_playing())` (skipping existing chieftain/adults permanently), and never re-swapped when `species::key` changed.
- `mods/coop/tribe_rivals.cpp`: `pvp_npc_rel_for(Ally)` used `NpcRel::Allied (4)` instead of `Friendly (3)`, and `TribeMission_SetTargetTribe` / `TribeToTribeMission_Complete` / `TribeStage_AddGoalProgress` accepted player-owned rival tribes.
- `mods/coop/tribe.cpp`: `Flying Fish` / `Fire Bombs` checked `spatial->IsPlayerOwned()` (`Tribe_IsPlayerOwned`), which returned `false` on Guest's rival tribe, and `chieftain_ability_on_native_cd()` treated stale `0.0f` slots as active cooldowns without checking playing/recharging bits.

## Fix
- `mods/coop/tribe_rivals_logic.h`, `mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_logic.h`, `mods/coop/tribe.cpp`:
  - Enforced hut-tier pop cap (6/9/12, hard cap 12) in `handle_guest_baby()`, blocked non-hut-buy `TribeStage_Enter` baby spawns (`tribe.outfitter_baby`), and chunked list-format `MSG_TRIBE_ANIM` / `MSG_NPC_ANIM` payloads to `<= 1100` bytes (`split_anim_payload`).
  - Fixed `host_try_auto_form_npc()` to check real `all_tribes().size()` growth and require a connected guest.
  - Rewrote `swap_tribe_species_ex()` to separate slot swap from `members_swapped`, retry until post-cinematic adults/chieftain match `want_key`, re-swap on species key changes, run on both Host and Guest (`guest_sync_species()`), and yield `tribe_species.cpp` during Tribe Rivals. Fixed `on_buy_influence()` to return `false` when inactive.
  - Persisted `player_key -> (pol, pos)` immediately in `"tribe.rivals"` store v2 and excluded conquered/dead-hut tribes from assignment.
  - Kept PvP `Ally` at `NpcRel::Friendly (3)` and hooked `TribeMission_SetTargetTribe`, `TribeToTribeMission_Complete`, and `TribeStage_AddGoalProgress` to exclude rival player tribes from NPC missions, totem poles, and victory progress.
  - Suppressed NPC night-sleep (`0x04611FF4`) and Peace auto-attacks on rival player tribes, relayed Guest tool hut selling (`CommunityEditor_Sell` -> `CustomOp::SellTool`), added Guest wild/NPC hut attack fallback, scoped `Tribe_IsPlayerOwned` override for Guest consequence abilities, fixed native ability cooldown detection, and added Guest chieftain KO detection + countdown indicator (`CustomOp::ChiefRespawn`).

## Verification
- no-fail-run: reproduced in live v0.8.1.1 stream & host logs (`sporekit-20261126-211112.log`, 163 babies / `MSG_TRIBE_ANIM` 1178 B drop / `o_Form` 0x00000001 / 15x `OnCooldown`) and covered by new ASan+TSan unit tests (`tests/tribe_rivals_tests.cpp`, `tests/tribe_tests.cpp` including `--nofix` failure check).
- PASS runs: `20261126-155137:rivals_tribe`, `20261126-155137:rivals_tribe_wipe`, `20261126-155137:rivals_tribe_polish`, `20261126-155137:rivals_tribe_abilities_pets`, plus `make test` (all 10 suites).
