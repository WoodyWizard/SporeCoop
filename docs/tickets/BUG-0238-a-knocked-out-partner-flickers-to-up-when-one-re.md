---
id: BUG-0238
title: a knocked-out partner flickers to "up" when one refresh is late, counts as a helper meanwhile, and replays its death effect
status: fixed
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #172 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-cell
commits: 8d2c882
tests: cell_ko_gap, cell_tests (make test)
signature:
docs: docs/re/cell.md#cell-ko-grace
related: #172, BUG-0061, BUG-0131
---

## Report
From GitHub issue #172: https://github.com/WoodyWizard/SporeCoop/issues/172 (the full analysis, links and the scenario are there).

BUG-0061 (#39, commit e7ab004, release 0.7.3.0) ends a remote knockout that its owner has not refreshed for 2 s (`kKoStale`). The owner refreshes every 0.25 s over the reliable channel, so a single late refresh (a retransmit under loss, a hitch on either machine) is enough. The partner is then drawn opaque and counts as "up". That includes the BUG-0131 helper question, so for that moment a knocked-out partner can rescue or revive again. The next refresh is seen as a new knockout and replays `cell_death_oneshot`. Severity: wrong behaviour, minor.

## Root cause
Observed (lane 2, `coop_cell_ko_gap.py` with `SPORE_FIX_OFF=pool.ko_grace`): with MSG_CELL_KO held back on the host for 2.6 s, the host drew the knocked-out guest opaque (`puppet 1 op=1.00`, `ko[1]=0`) from 2.03 s on; the next refresh replayed `cell_death_oneshot` (`fx` 1 -> 2); and a lethal bite on the host 2.5 s into a second gap knocked the host out (`downed=1`) because the guest counted as a helper again.

`cell_logic::ko_stale` ended a remote knockout 2 s after its last refresh (BUG-0061), shorter than the knockout itself (10 s); `on_ko` saw the next refresh as a new knockout.

## Fix
* `ko_stale(now, refreshed, left, fix, grace)`: stale only after `max(2 s, left + 2 s)` (`left` clamped to 25.5 s, NaN as 0). A hung owner is drawn knocked out until its own deadline; it is no helper meanwhile (`partners()` drops states older than 3 s).
* `Ko::expired`: set by `ko_expire`, cleared by every `on_ko`. A KO_OUT after an expiry restores the translucency without the death effect.
* `reset_knockout` sends KO_UP when it ends our running knockout (a stage leave while the session is open), so the partners do not keep it for 12 s. On a session end the session is closed and nothing is sent.
* Switch `pool.ko_grace` (off: the 2 s timeout, the replay, no KO_UP). Test hook `coop.pool kodrop <s>` (host: ignore MSG_CELL_KO for s seconds; cleared at session end). `ko[N]=` lines of `coop.pool` now end with `fx=<death effects played>`.

## Verification
* Unit: `tests/cell_tests.cpp` `ko()` (2.5 s gap with 7 s left: not stale; past the deadline: stale; grace off: stale after 2 s).
* Two-game `tools/tests/coop_cell_ko_gap.py` (suite `cell_ko_gap`), lane 2 GOG 2024: FAIL with `SPORE_FIX_OFF=pool.ko_grace` (7 of 29 samples opaque, fx 1 -> 2, host `downed=1`); PASS with the fix (twice, plus lab).
* The KO_UP of `reset_knockout` (step 3) has no in-game test: it needs a guest leaving the Cell stage while knocked out (a save load) and cannot fail on 0.7.3.0.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-cell (8d2c882): deadline-based expiry, no effect replay, KO_UP on a stage leave; test cell_ko_gap FAIL without / PASS with.
