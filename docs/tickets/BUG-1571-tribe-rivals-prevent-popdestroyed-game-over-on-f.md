---
id: BUG-1571
title: Tribe Rivals: prevent PopDestroyed Game Over on full tribe wipe, auto-respawn with baby recovery, and grant 5-minute PvP immunity
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-26
source: User report
role: both
versions: 0.8.1.1
fixed_in: unreleased
branch: main
commits: f3479560
tests: rivals_tribe_wipe
signature:
docs: docs/re/tribe.md#tribe-gameover-list
related: BUG-0172, BUG-0960, BUG-1548
---

## Report
In Tribe Rivals, killing every member (including the chieftain) of another human player's tribe had two severe failure modes:
1. Wiping the host's tribe triggered Spore's native population Game Over (`TRG_PRE_PopDestroyed`), hiding the HUD, pausing the guest via `tribe.gameover_wait`, and reloading the save for everyone.
2. Wiping a guest's tribe (or wiping everyone including the chieftain) left the victim with 0 living members and unable to buy/make babies, softlocking them if food had been raided below 10 or while all members were dead, and allowing the attacker to immediately spawn-camp the village.

## Root cause
- Observed (disassembly of GOG 2024 `SporeApp.exe`, verified in `out/lab/20261126-114148`):
  1. When the last player-tribe member falls (`cTribe_UpCount` `0x00C8F500` `< 2`), `cCreatureCitizen::TakeDamage` (`0x00C26940`) posts message `0x05C80207` (`PopDestroyed`) at `0x00C26B80`. `SporeCoop::TribeGame_GoalListener` (`0x00CD81F0`) handles `0x05C80207` (and `0x058BADDD` for hut loss) at `0x00CD8A2A`, hiding the Tribe HUD (`0x00CC9530(0)`) and calling `PlayCinematic("TRG_PRE_PopDestroyed")` at `0x00CD8B0C`.
  2. `mods/coop/tribe_mirrors.cpp::player_tribe_out()` returned `true` whenever 0 members of `player_tribe()` had health `> 0`, causing `tribe::spawn_member()` to refuse all spawns (`BUG-0172` guard) and `tribe.gameover_wait` (`BUG-0960`) to pause guests.
  3. On guests, `mods/coop/tribe.cpp::h_BabyBorn()` required `!g_guest.mirrors.empty()` to forward `CMD_BABY`, and nothing restored `TribeUI + 0x3A3 = 1` (checked by `CanAddBaby` `0x00CD58D0`).
  4. `host_try_war_food_raid` could drain a rival's food all the way to `0` (`< 10` baby cost) while `cTribePlanner` is disabled for player tribes, preventing baby recovery.
  5. When respawning adults after a chieftain knockout, `cTribe::CreateCitizen` (`0x00C8E730`) only places `age == 0` babies at `tribe_pos()` (leaving `age == 1` adults at zero/random position unless placed), and `BT_CitizenKnockoutEnter` (`0x00DB1A30`) sets the tribe-level chieftain knockout countdown at `cTribe + 0x2C8` (`90 s`). While `cTribe + 0x2C8 > 0`, any newly promoted chieftain is immediately knocked out on the next behaviour tick.

## Fix
Implemented under switch `rivals.tribe_wipe_recovery` (`coop.fix rivals.tribe_wipe_recovery off` restores the old behavior for testing):
- **Pure wipe recovery & immunity state machine (`mods/coop/tribe_rivals_logic.h`)**:
  - `PlayerWipeState`, `step_wipe_recovery()`, `is_player_immune()`, `player_immune_remaining()`, `break_immunity_on_attack()`, `raid_steal_amount()`.
  - Defaults: `15 s` respawn delay (`kDefaultWipeRespawnDelay`), `300 s` (5-minute) PvP shield (`kDefaultWipeImmunityDuration`), `2` respawned adults (chieftain + 1 villager), minimum `15` recovery food (`kWipeRecoveryMinFood`), and raid food floor `10` (`kRaidFoodFloor`).
  - Added `CustomOp::Immunity = 5` in `FoodSyncMsg` (`SK_STAGE_CUSTOM`) to sync shield countdowns and wiping state to guests.
  - Extended `decide_damage(..., bool vic_immune)` and `decide_host_order(..., bool vic_immune)` to block all PvP citizen damage, hut/tool damage, food raids, and hostile orders against shielded players.
- **Native Game Over block & baby recovery (`mods/coop/tribe_rivals.cpp`, `mods/coop/tribe_mirrors.cpp`, `mods/coop/tribe.cpp`, `mods/coop/tribe_host.cpp`)**:
  - Hooked `SporeCoop::TribeGame_GoalListener` (`0x00CD81F0`) in `h_TribeGoalListener` to block `0x05C80207` (`PopDestroyed`) and `0x058BADDD` (`HutDestroyed`) while `wipe_recovery_active()`.
  - `player_tribe_out()` returns `false` when `wipe_recovery_active()` so `spawn_member()` stays unlocked and guests are never paused.
  - `h_BabyBorn()` forwards `CMD_BABY` even with an empty mirror map while `wipe_recovery_active()`, keeps `TribeUI + 0x3A3 = 1` unlocked on both host and guest, and `handle_guest_baby()` deducts 10 food only when `tribe::spawn_member()` succeeds.
  - `host_wipe_recovery_tick()` detects 0 live members, starts the 5-minute PvP shield immediately, and on respawn clears `cTribe + 0x2C8 = 0.0f`, spawns 2 adults placed on the village surface via `village_spawn_spot()`, promotes the first as chieftain, removes the lingering knocked-out chieftain corpse, and tops food up to `>= 15`.
  - Anti-exploit: `note_pvp_aggression()` clears a shielded player's own PvP immunity early if they intentionally attack or raid a rival player.
  - Added live PvP shield countdown row in F9 Rivals UI (`on_extra_ui_rows()`).

## Verification
- Unit tests (`make unit-cpp unit-net unit-lint`, `tests/tribe_rivals_tests.cpp::test_wipe_recovery_and_immunity`, `msg_manifest.py check`, `test_lint.py`): PASS (0 problems).
- Wine lab ON/OFF twin (`out/lab/20261126-114148`):
  - `rivals_tribe_wipe` (ON): **PASS** (41 s, 0 faults on host & guest; verified guest wipe with 0 food -> 5-min shield synced `298 s`, 1x auto-respawn staying alive at `live=2` with `15` food, shield blocking citizen hit + hut hit + food raid `immune_blocked 0->3`, immediate baby creation `members 2->3 food 15->5`, anti-exploit early shield break `immune_broken=1`, host one-by-one wipe blocking native `0x05C80207` `pop_go_blocked=1`, host staying in Tribe with `guest_go_paused=0`, host auto-respawning at `live=2` with 5-min shield blocking guest attack).
  - `rivals_tribe_wipe_off` (`SPORE_FIX_OFF=rivals.tribe_wipe_recovery`): **FAIL** on all target assertions (`immune=0`, `recovered=0`, `food=0`, baby buy failed, `pop_go_blocked=0`, `guest_go_paused=1`).
- Tribe regressions (`out/lab/20261126-114320`): 4/4 **PASS** (`rivals_tribe`, `rivals_tribe_abilities_pets`, `tribe_gameover_wait`, `tribe_hut_defeat_wait`).

## History
- 2026-11-26: opened.
- 2026-11-26: fixed on `main` (`f3479560`), verified with unit tests, ON/OFF lab twin (`out/lab/20261126-114148`), and Tribe regressions (`out/lab/20261126-114320`).
