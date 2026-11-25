---
id: BUG-1555
title: Civ & Space Rivals: NPC diplomacy wipe, guest SpendMoney, Civ->Space liftoff stranding, city handoff, Space influence price and NPC UI
status: open
kind: bug
stage: Civ
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in:
branch: w0801/civ-space
commits:
tests:
signature:
docs: docs/11-rivals.md
related: BUG-1549, BUG-1550
---

## Report
Independent static audit of `BUG-1549` and `BUG-1550` found critical gameplay blockers:
1. `civ_rivals.cpp:710-738` overwrites `cRelationshipManager` from `g_npc_rels` every 1.0 s without reading back native diplomacy changes.
2. `civ_host.cpp:1086,1107` calls `a_SpendMoney` on guest civs with `mIsPlayer==0`, refilling NPC budgets instead of deducting money.
3. Tribe->Civ and Civ->Space draft payouts are never delivered on stage entry.
4. Natural `Civ -> Space` start defers `relinquish_host_home_now` until liftoff (`space_rivals.cpp:828-831`), but post-liftoff retry hits early return (`857-862`), leaving guest stranded at host's star instead of traveling to its new homeworld.
5. `set_guest_civ_cities` (`space_rivals.cpp:146`) is never called by `civ_rivals` on transition.
6. `on_buy_influence_space` (`space_rivals.cpp:1011-1039`) charges raw 50/75/100 Sporebucks instead of Space-scaled money.
7. `space_rivalry.cpp:511,1119,1292` toggle desyncs from `rivals::config()`, and `sync_from_rivals()` drops sync when `money_ptr()` is null.
8. Civ and Space lack `on_npc_ui_list` providers for F9 Influence UI.

## Root cause
See `docs/plan-0.8.0.1.md` WP-5.

## Fix
Implemented all 8 WP-5 items on branch `w0801/civ-space`:
1. **Bidirectional Civ NPC diplomacy sync (`civ_rivals_logic.h`, `civ_rivals.cpp`)**: added per-`(npc, player)` native tracking (`last_native`, `has_native`, `dirty_native`) and `NpcRelationTable::sync_native_slot` / `confirm_native_push`. On the 1.0 s tick, native `cRelationshipManager` deltas are read back into `g_npc_rels` (awarding influence for real diplomacy improvements and new alliances) instead of overwriting native diplomacy; `rel_set` is only written when Rivals influence/actions mark a slot dirty.
2. **Guest Civ `SpendMoney` deduction (`civ_rivals_logic.h`, `civ_rivals.h`, `civ_rivals.cpp`, `civ_host.cpp`)**: added `deduct_direct_wealth` / `spend_direct_wealth` and `civ_rivals::civ_money` / `civ_rivals::host_spend_civ_money`. In `civ_host.cpp` (`host_buy`, `build_in_city`, and `CMD_BUYCITY`), guest civs (`mIsPlayer == 0` on host) check and deduct `mWealth (+0x98)` directly (clamped >= 0, domestic/expansion budgets zeroed) instead of calling `cCivilization::SpendMoney`.
3. **Tribe->Civ and Civ->Space draft payout delivery (`civ_rivals_logic.h`, `civ_rivals.cpp`, `space_rivals_logic.h`, `space_rivals.cpp`)**:
   - Civ Rivals host tick delivers `take_pending_draft_payout_for(Stage::Tribe, pid)` scaled by `kCivDraftPayoutScale` (10x -> 1000/1500/2000 Sporebucks) to host and guest Civ budgets once each player's nation is assigned.
   - Space Rivals host tick delivers `take_pending_draft_payout_for(Stage::Civ, pid)` scaled by `kSpaceDraftPayoutScale` (100x -> 10k/15k/20k Sporebucks) to host (`AddMoney`) and guests (`OP_GRANT_MONEY`, queued until Space Rivals and `player_empire()` are ready).
4. **Space Rivals natural `Civ -> Space` liftoff state machine & `ensure_city_count` (`space_rivals_logic.h`, `space_rivals.cpp`)**: replaced the ad-hoc `g_relinquish_pending` flag with 3-phase `GuestEmigMachine` (`CreateHome -> RelinquishHost -> TeleportHome`) so deferred post-liftoff retry relinquishes host's homeworld and runs `teleport_in_galaxy` / `travel_to_star` without stranding at host's star. Guarded `ensure_city_count` against live `active_planet_id()`, cleared removed tail slots, and recomputed `cCivData` turret/building totals (`+0x20`/`+0x24`).
5. **Civ->Space city count handoff (`civ_rivals_logic.h`, `civ_rivals.cpp`, `space_rivals.h`, `space_rivals.cpp`)**: `civ_rivals` tracks live Civ city counts for guests `1..3`, clamps to `1..3` (`clamp_space_handoff_cities`), and pushes them into `spacerivals::set_guest_civ_cities(pid, count)` on progress eval, city capture, and stage race end. Updated `coop.space rivals emigrate [cities [pid]]` to accept an optional `pid`.
6. **Space influence buy price scaling (`space_rivals_logic.h`, `space_rivals.cpp`)**: added `influence_buy_space_cost` (`kSpaceMoneyPerInfluenceUnit = 200` -> 10k/20k/30k Sporebucks) and wired it into `on_buy_influence_space` for host and guests (with guest-side clamp >= 0 on `OP_CHARGE_MONEY`).
7. **Space rivalry toggle & early-load sync (`space_rivalry.cpp`)**: `host_apply(A_RIVALS/A_ALLIES)` now updates `rivals::set_stage_mode(Stage::Space, ...)` under a re-entrancy `CALL_GUARD`, and `sync_from_rivals()` records a pending sync when `money_ptr()` is null during early Space load and retries on `tick()` once ready.
8. **F9 Influence NPC UI providers (`civ_rivals_logic.h`, `civ_rivals.cpp`, `space_rivals_logic.h`, `space_rivals.cpp`)**: registered `hooks.on_npc_ui_list` in both Civ (readable color/specialty/city-count labels) and Space (discovered NPC empires from `cRelationshipManager` and `cPlayer` contacts) with `allied`, `hostile`, `can_turn`, `can_protect`, `can_smooth`; removed duplicate `Smooth`-only NPC rows from `civ_rivals.cpp` `hook_extra_ui_rows()`.

## Verification
- Cross-compile (`make -j$(nproc)`): PASS, 0 warnings/0 errors (`-Werror`).
- ASan+UBSan unit tests (`make unit-cpp`): PASS (`civ_rivals_tests`: 8/8 suites passed; `space_rivals_tests`: 102 passed, 0 failed).
- Lint (`python3 tools/tests/test_lint.py`): PASS (0 problems).
- Pending Wine/lab E2E verification (`coop_rivals_civ.py`, `coop_rivals_space.py`, and full campaign run) before marking `status: fixed`.
