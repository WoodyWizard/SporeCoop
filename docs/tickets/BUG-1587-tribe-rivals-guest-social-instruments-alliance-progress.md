---
id: BUG-1587
title: Tribe Rivals: guest musical instruments do not work, guest cannot ally NPC tribes, and guest Tribe progress bar stays at 0
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-26
source: Discord player report (v0.8.2.2)
role: guest
versions: 0.8.0, 0.8.1, 0.8.2, 0.8.2.1, 0.8.2.2
fixed_in: 0.8.2.3
branch: main
commits: 6f5494d0
tests: tribe_rivals_tests, rivals_tribe, rivals_tribe_wipe
signature: Guest musical instruments return AbilityBar_Status 0x0E in Tribe Rivals; host_eval_tick skips guest NPC alliances; guest cPlayer+0x10F0 stays 0
switch: tribe.rivals_social, rivals.tribe_alliance_sync
docs: docs/re/tribe.md
related: BUG-1561, BUG-1548, BUG-1580
---

## Report
Player report on Discord (`v0.8.2.2`):
1. In Tribe Rivals, the Guest cannot ally with the Host or other tribes, and musical instruments do not work on the Guest.
2. When allying tribes during the Tribe stage, the Guest receives no stage progression bar fill ("опыт в развитие") and the alliance does not count for the Guest.

## Root cause
1. **Guest musical instruments & social meter UI disabled in Tribe Rivals**: all 7 runtime paths of `BUG-1561` (`host_social_play`, `tribe_social_host_tick`, `on_tribe_social_msg`, `tribe_social_guest_tick`, `tribe_social_ui_apply_task`, `h_BarStatus`, `on_tribe_bar_use`) explicitly returned early when `tribe_rivals::active()`. Because `h_BTTick` skips citizen mirrors on guests, native `AbilityBar_Status` (`0x00D1FB30`) always sees `ai+0x1D8 != 0xB46E8803` and returns `0x0E` (disabled) for all instrument buttons. Additionally, `tribe_social_guest_tick` only triggered `AbilityBar_Rebuild` when all 3 bar slots were `< 0`, missing a 2nd or 3rd instrument rack purchase.
2. **Host alliance polling & sync skipped guest tribes**: `tribe_rivals::host_eval_tick` only checked `g_owned[0].pol` (Host) for native `lvl >= 4` (Allied) relations on NPC tribes, never polling guest tribes `g_owned[1..kMaxPlayers-1].pol`; and `h_ApplyRel` updated `g_npc_rels` without immediately calling `host_sync_npc_rels_to_guests()`.
3. **Guest native Tribe progress bar never updated**: `wsync.cpp` `s_goal` skips syncing Host's `cPlayer+0x10F0`/`+0x10F4` in Tribe Rivals (each player tracks their own progress), but `tribe_rivals.cpp` never wrote the guest's own allied+conquered NPC count to `cPlayer+0x10F0`/`+0x10F4` on the guest.
4. **Guest social-mode click fallback**: `override_choose_action` turned `vanilla_action == 0` on unconquered NPC tribes into Attack/Raid (`0x0A`/`0x0C`) without checking Tribe UI mode (`tui+0x390 == 1`).

## Fix
- `mods/coop/tribe.cpp` (`tribe.rivals_social`):
  - Enabled `BUG-1561` guest social performance sync, instrument `AbilityBar_Status`/`Use`, `host_social_play`, and native social meter UI in Tribe Rivals, with per-player `HostSocialEdge` tracking on the host scanning each guest's own `tribe_rivals::tribe_of_player(pl.id)` and sending `kAnimTribeSocial` directly to that guest.
  - Fixed `tribe_social_guest_tick` `AbilityBar_Rebuild` check to compare distinct owned instrument rack count `(4,5,6)` against populated bar slots (`bar+0, +8, +16`) so buying a 2nd or 3rd instrument rack always populates its HUD button.
  - Bounds-checked `member_instrument_ready` cooldown vector against `c+0xC2C`.
- `mods/coop/tribe_rivals.cpp` (`rivals.tribe_alliance_sync`):
  - Extended `host_eval_tick` to poll all players `0..kMaxPlayers-1` for native NPC Alliances (`lvl >= 4`), record `history::record_tribe_outcome(p, true)` for guests, and immediately sync `SK_NPC_RELS`.
  - Called `host_sync_npc_rels_to_guests()` immediately in `h_ApplyRel` when a player's NPC relation changes, and emitted a rate-limited Diplomacy panel notice if a player completes a social/gift event on another player's tribe while not allied.
  - Updated guest `cPlayer+0x10F0` / `+0x10F4` from `g_guest_npc_rels` (`Allied` + `Conquered` count vs `win_needed`) on `SK_NPC_RELS` receipt.
  - Gated `override_choose_action`'s NPC attack/raid fallback on attack mode (`tui+0x390 == 1`).

## Verification
- PASS runs: `out/lab/20261126-212912` (`rivals_tribe`, `rivals_tribe_wipe`: PASS, 0 faults on host & guest) plus `make test` (all 10 suites including `tribe_rivals_tests` and `static_guard_lint`).
- no-fail-run: interactive guest Tribe Rivals musical-instrument performance and alliance progress bar verified statically against `SporeApp.exe` (`0x00D1FB30`, `0x00DA1290`, `0x00DA3490`) and player Discord report; existing `rivals_tribe` / `rivals_tribe_wipe` lab tests verified green after the fix.
