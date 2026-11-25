---
id: BUG-1546
title: Cell Rivals: per-player bar, war-gated PvP bites, catch-up timer and part draft
status: fixed
kind: feature
stage: Cell
reported: 2026-11-25
source: docs/11-rivals.md 11.3.1
role: both
versions: 0.8.0
fixed_in: 0.8.0
branch: w08/rivals-cell
commits: 52aebd81
tests: rivals_cell, rivals_cell_off, cell_rivals_tests
signature:
docs: docs/11-rivals.md
related: BUG-0794, BUG-0899, BUG-1469
---

## Report
Implement WP-R2 Cell stage Rivals mode (`docs/11-rivals.md` 11.3.1, 11.5, 11.6, 11.8): every player keeps its own
evolution bar and DNA in the shared host-authoritative tide pool, Neutral/Allied players cannot hurt each other, War
enables host-authoritative PvP bites and kills that drop physical meat chunks with vanilla respawn (no Symbiosis
knockout/revive, no NPC diplomacy or influence), first player to fill its bar starts the catch-up timer, and timer
expiry starts the Cell part draft with escalating Creature DNA payouts for the winner.

## Root cause
Feature work for SporeCoop 0.8.0 Rivals mode. Before WP-R2, Cell stage pooled food/evolution progress and Cell parts
across the party (`cellsync.cpp`, `wsync.cpp`, `parts.cpp`), Cell PvP was controlled only by `all.friendly_fire`
(`BUG-0899`) rather than per-pair Rivals war state, Symbiosis knockout/revive and proximity regen always ran in Cell,
and no `Stage::Cell` hooks or `MSG_RIVALS_FACTION` (`81`) handlers existed for per-player bar reporting, stage win /
catch-up trigger, rival kill meat drops, or Cell part draft unlocking and payout delivery.

## Fix
Implemented Cell Rivals behind `COOP_FIX("rivals.cell", "BUG-1546")` on `w08/rivals-cell`:
* `mods/coop/cell_rivals_logic.h`: portable logic and bounds-checked `MSG_RIVALS_FACTION` (`Stage::Cell`) codecs for
  bar fraction / completion (`food >= 1000`), 13-slot Cell part catalog and draft list builder, war-gated PvP policy
  and host routing, Symbiosis KO/regen/auto-part-share gates, rival kill meat drop ring placement and world-scale
  conversion, and late-food rollback clamping.
* `mods/coop/cell_rivals.cpp`: registers `rivals::register_faction_handler(Stage::Cell, ...)`,
  `rivals::register_npc_influence_callback(Stage::Cell, ...)` (always rejecting NPC diplomacy/influence in Cell), and
  `rivals::register_stage_hooks(Stage::Cell, ...)` (`on_buy_influence`, `on_stage_race_end` -> `rivals::start_draft`,
  `on_draft_pick` -> `SK_DRAFT_APPLY` native `Simulator::Cell::CollectPart` unlock, `on_extra_ui_rows` for F9 Race
  page); tracks per-player live Cell bar/DNA/parts via `SK_PROGRESS` into `rivals::set_player_progress` and triggers
  `rivals::report_stage_complete` at `food >= 1000`; routes War PvP hits via `SK_PVP_HIT`, drops 3 physical meat chunks
  on rival kill (`kSubPvpKill`), delivers escalating next-stage Creature DNA payouts via
  `rivals::take_pending_draft_payout`, and exposes `coop.pool rivals [status|hit|payout]`.
* `mods/coop/cellsync*.{h,cpp}`, `mods/coop/wsync.cpp`, `mods/coop/parts.cpp`, `mods/coop/dna.cpp`,
  `mods/coop/vote.cpp`, `mods/coop/find.cpp`, `mods/coop/coop.cpp`: unpool Cell food/spent DNA/parts while preserving
  first-come-first-served physical food removal (with late-food rollback on collisions), gate Cell PvP and `h_NoAttack`
  on per-pair War state, disable Symbiosis KO/revive and proximity regen in Cell Rivals, and keep rival cell name tags
  and off-screen edge arrows visible.

## Verification
* Portable ASan+UBSan unit tests (`tests/cell_rivals_tests.cpp`, 74 checks passed), `make -j$(nproc)`,
  `make unit-cpp unit-net unit-lint`, and `tools/tests/test_lint.py`: 0 errors, 0 warnings.
* Real two-game Wine lab run `20261125-034902` (lane 2):
  * `rivals_cell` (ON): **PASS** (53 s, 0 faults on host and guest) - verified Cell Rivals activation, Cell influence
    rejection, independent host/guest Cell bars and DNA, F9 Race page progress rows, rival cell name tag and edge arrow,
    Neutral PvP blocked vs War real spike contact damage (`pvp_out +1, pvp_in +1, dmg +3`), lethal War kill without
    KO/revive (`died=True, downed=False, ko=False`) with 3 physical meat chunks dropped in host pool and vanilla
    respawn, Cell parts not auto-shared, full bar -> catch-up timer -> automatic Cell part draft -> `SK_DRAFT_APPLY`
    native unlock of 2 copied parts on guest (`0011220001010`), and escalating 250 Creature DNA payout claimed.
  * `rivals_cell_off` (`SPORE_FIX_OFF=rivals.cell`): **FAIL** cleanly (37 s, 0 faults: `host=0, guest=0`).

## History
- 2026-11-25: opened.
- 2026-11-25: implemented WP-R2 Cell Rivals on `w08/rivals-cell` (`52aebd81`), verified ON PASS and OFF FAIL in lab run `20261125-034902`.
