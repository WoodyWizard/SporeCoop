---
id: BUG-1603
title: Cell Rivals: rejoining guest loses accumulated food and unlocked parts when loading host save
status: fixed
kind: bug
stage: Cell
reported: 2026-11-27
source: AI playtest (v0.8.2.3, PLAYTEST-021)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 634d50f2, 3ccb71f1
tests: rivals_cell, cell_rivals_tests, make test
signature: cell_rivals: rejoining guest food drops back to host save baseline after disconnect/rejoin
switch: rivals.cell_rejoin_progress
docs:
related:
---

## Report
When a Cell Rivals guest disconnected and rejoined, loading the host's world save reset the guest's Cell food progress and unlocked parts back to the host's baseline instead of preserving the guest's earned progress. A follow-up race during the 6.5-hour playtest (`PLAYTEST-021`) also showed a rejoining guest could send one `SK_PROGRESS` with its old food before the host save reload completed, consuming a one-shot restore check before food dropped (`498 -> 414`).

## Root cause
1. `cell_rivals.cpp` had no `player_key -> {food, parts_mask}` persistence map or restore wire subkind (`kSubRestoreProgress`).
2. After initial persistence was added, `g_rejoin_checked[from]` was one-shot: if the rejoining guest reported its pre-reload local food before finishing the host save load, the host cleared the check before the drop happened. Legitimate host-authoritative rival-eat rollbacks (`rivals_on_late_food`) also needed to adjust the saved food floor.

## Fix
- Added portable `CellProgressMemory` (`remember`, `apply_rollback`, `lookup`, `write_restore_progress`, `read_restore_progress`) in `mods/coop/cell_rivals_logic.h` with `kSubRestoreProgress = 18`.
- Added `COOP_FIX("rivals.cell_rejoin_progress", "BUG-1603")` in `mods/coop/cell_rivals.cpp`: host records each guest's food and unlocked parts mask, saves on leave, adjusts on legitimate rival-eat rollbacks (`apply_rollback`), and keeps a 45-second post-join restore window (`g_rejoin_window_until[from]` with 1.5 s `g_last_restore_tx[from]` rate limit) so rejoining guests restore their saved progress even when `SK_PROGRESS` races ahead of save load.
- Added unit tests in `tests/cell_rivals_tests.cpp`.

## Verification
- PASS run: live 4-player playtest `out/ai-playtest/20261127-075046` (live restore of `guest3` `540 -> 642` at 13:08:12), `rivals_cell` lab run, and `tests/cell_rivals_tests.cpp` + full `make test`.
- no-fail-run: twin switch `rivals.cell_rejoin_progress` tested with unit tests and live Wine rejoin verification.
