---
id: BUG-1552
title: Tribe crash guard in modes::rts_anchor, BUG-0359 tripwire, and Cell Rivals gameplay fixes
status: open
kind: bug
stage: Cell
reported: 2026-11-25
source: v0.8.0 independent audit & campaign_rivals
role: both
versions: 0.8.0
fixed_in:
branch: w0801/cell-crash
commits:
tests:
signature:
docs: docs/11-rivals.md
related: BUG-0359, BUG-1538, BUG-1546
---

## Report
1. `campaign_rivals` crashed host on Creature->Tribe entry (`0xC0000005` in `Sim_GameData_Filter+0x6` reading `0x3F028F7C`): `modes::rts_anchor` (`mods/coop/modes.cpp:1649-1656`) fell through from `kTribe` (`mPlayerTribe == nullptr`) into `GetPlayerCivilization` without checking `mode == kCiv`.
2. `game::for_each_object` silently skips corrupted vtable entries (`0x3F028F5C`, BUG-0359 class) without logging when corruption first appears.
3. `cell_rivals.cpp:637-643` steals Creature Rivals draft payouts because it does not check payout stage.
4. Cell Rivals guest reaching 1000 food first strands the host (no transition trigger/vote).
5. Cell Rivals food rollback omits BUG-1538 spent-DNA clamp (`cell_rivals.cpp:508-509`), 3-4p draft uses intersection `common_mask`, poison cloud PvP lacks owner attribution / meat drops (`cellsync_combat.cpp:568-608`), extreme `host_scale / own_scale` divergence empties lagging guest's pool during catch-up (`cellsync_guest.cpp:428`), and `monitor.py:648` amplifies `%.1f` Cell `pos=` rounding by 30,000x.

## Root cause
See `docs/plan-0.8.0.1.md` WP-2.

## Fix
1. `mods/coop/modes.cpp` (`rts_anchor`): strictly gate `mPlayerTribe` (`nm+0x70`) lookup on `game::mode_id() == kTribe` and `GetPlayerCivilization` lookup on `game::mode_id() == kCiv`, preventing any fallthrough from Tribe into `GetPlayerCivilization` -> `Sim_GameData_Filter` when `mPlayerTribe` is null.
2. `mods/coop/game.cpp` (`for_each_object`): added a one-shot per-object diagnostic tripwire (capped at 8 distinct objects per run) logging `obj`, `*(uint32_t*)obj`, `mode_id()`, and `H->frame_index()` whenever `!in_exe(*(void**)obj)` is seen, dating any BUG-0359 (`0x3F028F5C`) vtable corruption without log spam.
3. `mods/coop/cell_rivals.cpp` (`deliver_payout_host`, `rivals_tick`): switched to `rivals::take_pending_draft_payout_for(rivals::Stage::Cell, pid)` and gated Creature-entry delivery on `pending_payout_stage[pid]` being `Stage::Cell` or legacy `Stage::None`, preventing Cell Rivals from stealing Creature Rivals draft payouts.
4. `mods/coop/cell_rivals_logic.h` + `mods/coop/cell_rivals.cpp` (`should_unlock_host_stage_end`, `on_cell_race_end`, `rivals_tick`): after catch-up and draft completely finish for a guest-won Cell race while host is still below 1000 food, unlock the host's local Cell->Creature evolution bar via `set_food_total(1000)` and notify the host; also guard `report_stage_complete` on `!race.stage_finished`.
5. `mods/coop/cell_rivals_logic.h` + `mods/coop/cell_rivals.cpp` (`kSubFoodRollback`): applied the BUG-1538 spent-DNA clamp (`set_spent_dna`) after late physical food rollbacks so Cell DNA never drops below 0.
6. `mods/coop/cell_rivals_logic.h` + `mods/coop/cell_rivals.cpp` (`draft_missing_union_mask`, `build_draft_items_for_pickers`, `resolve_draft_pick`, `on_cell_race_end`, `on_cell_draft_pick`): built 3-4 player Cell drafts from the union of missing parts across reported non-winner pickers (prioritizing universally-missing parts and skipping empty drafts) and resolved each pick against the picker's owned mask to prevent wasted no-op picks.
7. `mods/coop/cell_rivals_logic.h` + `mods/coop/cellsync_host.cpp` + `mods/coop/cellsync_guest.cpp` + `mods/coop/cellsync_combat.cpp` + `mods/coop/cell_rivals.cpp`: encoded poison cloud owner ID (0..3) in unused `MSG_CELL_STATE` flag bits 4..6 (backwards-compatible), strictly gated 3-4p Cell Rivals poison cloud damage on `rivals::at_war(me, owner)` via `allow_poison_cloud_pvp`, and routed lethal war poison kills through `rivals_report_pvp_kill` (`kSubPvpKill`) for kill credit and 3 physical meat chunk drops.
8. `mods/coop/cell_rivals_logic.h` + `mods/coop/cellsync_guest.cpp` + `tools/tests/monitor.py`: switched lagging guests in Cell Rivals to their local pool (refilling refused tiles) when `host_scale / own_scale` diverges beyond `[0.125, 8.0]` (with hysteresis `[0.25, 4.0]` to rejoin host pool), guarded `guest_tick` against over-scaling diverged mirrors, and added `Drawn.scale_quant_slack` (`0.05 * sqrt(3) * (ko / kv)`) in `tools/tests/monitor.py` for `ko > kv` Cell comparisons.

## Verification
- Built all Windows targets cleanly with `make -j$(nproc)` (0 warnings, 0 errors).
- Added ASan+UBSan unit tests in `tests/cell_rivals_tests.cpp` covering stage-tagged draft payout non-theft, guest-wins-first host unlock policy, 3-4p draft filtering and no-op resolution, food rollback spent-DNA clamp, poison cloud owner flags and PvP attribution, and catch-up `wscale` divergence/hysteresis (`cell_rivals_tests`: 146 passed, 0 failed; full `make unit-cpp` green).
- Ran `python3 tools/tests/test_lint.py` (0 problems) and verified `monitor.Drawn.scale_quant_slack` directly.
- Wine/lab verification pending on rel/0.8.0.1 integration.
