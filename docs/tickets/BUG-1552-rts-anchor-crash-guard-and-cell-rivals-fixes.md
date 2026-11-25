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
TBD.

## Verification
TBD.
