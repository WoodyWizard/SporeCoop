---
id: BUG-1538
title: Cell: a player's DNA can go negative after the first Cell editor update
status: fixed
kind: bug
stage: Cell
reported: 2026-11-23
source: discord
role: all
versions: 0.7.16
fixed_in: 0.7.17.1
branch: rel/0.7.17.1
commits: 61963e9d, b6ed7a52, 473beb20
tests: cell_dna_nonneg, cell_dna_nonneg_off
signature:
docs: docs/re/cell.md#cell-dna-nonneg
related: BUG-0630, BUG-1249
---

## Report
Player report in Discord (`#russian`, 2026-11-22, no report zip attached): while playing Cell stage in co-op, after the first Cell editor update, the player's DNA counter became negative.

## Root cause
Observed (objdump of `Cell_EditorResult` `0x00E809A0` and code audit of `cellsync.cpp` / `dna_own.h`):
* `Cell_EditorResult` writes `S+0x2C = DNAForFood(S+0x1C) - remaining`, and Spore's Cell UI renders `DNAForFood(S+0x1C) - S+0x2C` directly without clamping to `>= 0`. Selling starting parts legitimately yields `spent < 0`, so only the upper bound `spent <= DNAForFood(food)` must be enforced.
* Three co-op paths allowed `S+0x2C > DNAForFood(S+0x1C)`:
  1. Shared pool (`all.shared_dna shared`): `spend_in` and `spend_tick` added a guest's Cell editor spend onto the host's `S+0x2C` without capping at `DNAForFood(food)`, so two concurrent first Cell editors spending against the same pool drove `DNA < 0` on both games.
  2. Own wallet (`all.shared_dna own`, default): `spent_guest_want` expired `g_own_spent_heard` at `5.0 s` while `wsync` cached `cell_spent` (`host_spent`) for `6.0 s` (`kForget`), allowing a 5-6 s window where `host_spent + g_spend_pending` exceeded `DNAForFood(food)`, and `dna_own::Book::spend_cell` had no upper cap.
  3. Guest food rollback (`MSG_CELL_FOOD` `diff < 0` on a refused late meal) lowered `DNAForFood(S+0x1C)` below `S+0x2C`.

## Fix
`cell.dna_nonneg` (`mods/coop/cell_logic.h`, `mods/coop/dna_own.h`, `mods/coop/dna.cpp`, `mods/coop/cellsync.cpp`, `mods/coop/cellsync_debug.cpp`, `docs/re/cell.md#cell-dna-nonneg`):
* Clamp the upper bound `spent <= DNAForFood(food)` (preserving negative `spent` from selling starting parts) across `spend_in`, `spend_tick`, `dna_own::Book::spend_cell`, `spent_guest_want`, `set_spent_dna`, guest food rollback, and `editor_budget()`.
* Extend `g_own_spent_heard` expiry from `5.0 s` to `7.0 s` (longer than `wsync`'s `6.0 s` `kForget`) so own-wallet guests never fall back to stale `host_spent` during a host editor visit.

## Verification
* Unit tests: `cell_tests` (`dna_nonneg` covering shared overspend, part-sell refunds, 5.5 s own-wallet gap, and food drop).
* FAIL without fix (`SPORE_FIX_OFF=cell.dna_nonneg`): `out/lab/20261124-130047` (`cell_dna_nonneg_off`: host `spent=25, dna=-25`, guest own-wallet `spent=45, dna=-45`).
* PASS with fix: `out/lab/20261124-124925` (`cell_dna_nonneg`: concurrent shared overspend clamped on host and guest with `dna >= 0`, negative `spent = -10` preserved, own-wallet overspend clamped with `dna >= 0`).

## History
- 2026-11-23: opened for 0.7.17.1 from Discord `#russian` report (2026-11-22).
- 2026-11-24: fixed via `cell.dna_nonneg` on `rel/0.7.17.1`; verified with PASS (`20261124-124925`) and FAIL OFF-twin (`20261124-130047`).
- 2026-11-24: released in 0.7.17.1.
