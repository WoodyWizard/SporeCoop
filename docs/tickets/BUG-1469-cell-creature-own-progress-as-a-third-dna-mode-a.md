---
id: BUG-1469
title: Cell, Creature: own progress as a third DNA mode (all.shared_dna 2: own DNA and evolution bar, the stage moves on when every bar is full; idea from PR #209 by IltonBJSilva)
status: open
kind: feature
stage: Cell, Creature
reported: 2026-11-23
source: plan, PR #209
role: host, guest
versions: 0.7.17
fixed_in:
branch: feat/w69-progress
commits: 02127826, ffb010e2, 37a2dd5c, 4fc580f1
tests: own_progress_cell, own_progress_cell_guest, own_progress_afk, own_progress_levels, own_progress_creature, own_progress_deeds, own_progress_rejoin, own_progress_store
signature:
docs: docs/03-gameplay.md#gameplay-own-progress, docs/01-architecture.md#arch-player-store, docs/re/creature.md#creature-own-progress, docs/re/cell.md#cell-own-progress
related: BUG-1291, BUG-1269, BUG-1085, BUG-1472, BUG-1470
switch: core.own_progress, cell.end_click, cell.level_together, core.progress_restore
---

## Report
Owner decision 2026-11-20: a lobby setting with three values: shared, own wallet (0.7.16's default), own progress (each earns alone, its own DNA and evolution bar; nobody evolves alone). Design, wire (creature 1.3, cell 1.2, MSG 76 PROGRESS), store record `progress`, switches and tests: docs/plan-0.7.17.md item 2. Credit IltonBJSilva (PR #209).

## Root cause
A feature (design: docs/03-gameplay.md#gameplay-own-progress). Until now the party had one income: the shared pool
(all.shared_dna 1) or own wallets fed by the party's bar (0, dna_own.h); a guest's gains were forwarded and paced 1/N,
the Cell food was the host's total. Credit: IltonBJSilva, PR #209 (model O, the "waiting for X 64 %" notice, the
stage-end gate in h_Submit, the social-win credit, the brain level global's name, the RE notes on the Cell end sequence
and the levels apart). Not taken from #209: its side file and identity storage (ours: the per-player store record
`progress`), its second hook on the Cell end sequence (ours: the one hook of BUG-1269, `SporeCoop::CellStage_End`), the
default on (ours: own wallet stays the default, owner question 2).
Observed: `SporeCoop::g_BrainLevel` 0x169E370 read by vt 0xD8 (0xC02AA4) and written only by vt 0xDC (0xC02AC1),
objdump of the GOG 2024 .text (docs/re/creature.md#creature-own-progress).

## Fix
* all.shared_dna gets its third value (settings_core.h: max 2, names own wallet / shared / own progress, the lobby row
  "DNA and progress", the default the one constant kSharedDnaDefault = 0); dna.cpp reads it with get() (own_dna_on:
  own wallet only for value 0 as played).
* own_progress_logic.h (portable, tests/dna_tests.cpp): the mode, the old-peer gate (creature 1.3 and cell 1.2), full /
  percent, who is waited for, the stage-end gate (hold, a second click within 10 s forces), the start decision, the
  level line (food_cap / clip / release), MSG_PROGRESS 76 (creature family: kAsk, kSet, kReport, kTable, kGain), the
  store record `progress` v1 (29 bytes, with WP-R1's debt column), the restore order.
* own_progress.cpp: the host decides at the start (an older peer: refused, own wallet, a notice), kicks an older late
  joiner ("update"), keeps every guest's reported values and the store record, answers a guest's ask after each load of
  its world, sends the table (with the "played" flag) every second, runs its kept Cell click once every bar is full,
  banks / releases clipped meals, the notices. The guest asks, applies (food / spent; bar up through AddEvolutionPoints,
  down written; DNA set; a lower brain level written back), reports.
* Steps aside while active: no MSG_DNA total (coop.cpp), MSG_DNA / MSG_DNA_ADD ignored, no pace, a host gain for a
  guest's deed (Actor scope or the hunt credit) sent to the guest (dna.cpp on_dna), no guest spend sent / taken (dna.cpp,
  cellsync.cpp), a guest's meal not added to the host's food, the host's Cell total only acknowledges, wsync cell_spent
  not applied, each game's own goal card, a guest's social win paid to the guest (social.cpp), the sync-check skips DNA /
  bar / food.
* vote.cpp: the Cell gate before the end sequence (cell.end_click) or at the switch (off), a guest's click told to wait,
  the Creature stage-end request refused until every bar is full (then BUG-1085's vote).
* Switches: core.own_progress (off: value 2 plays as own wallet), cell.end_click, cell.level_together,
  core.progress_restore. `coop.progress [resync]` for the tests.

## Verification
open, waiting for the lab run. Built, `make test` passes (tests/dna_tests.cpp progress_*, validate_tests settings and
manifest rows). pending night run: own_progress_cell, own_progress_cell_guest, own_progress_afk, own_progress_levels,
own_progress_creature, own_progress_deeds, own_progress_rejoin, own_progress_store (PASS) and the twins
own_progress_cell_off, own_progress_white_off, own_progress_levels_off, own_progress_creature_off,
own_progress_deeds_off, own_progress_rejoin_off, own_progress_store_off (each must FAIL); regressions near it:
dna_own*, evo_screens_switch, stage_end_guest, cell_food_ack, dna_notice. Old-peer run against 0.7.16.1 in the final
check (value 2 refused at the start with a notice, a late 0.7.16.1 joiner not let in).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 2 (WP-P1, WP-P2), branch feat/w69-progress.
- 2026-11-23: implemented on feat/w69-progress (02127826, ffb010e2); lab tests and twins written, waiting for the night run.
