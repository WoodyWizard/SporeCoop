---
id: BUG-0239
title: a guest's meal sent while the host is in an editor (or has no cell) is never acknowledged, so the guest's food total drops by it later
status: fixed
kind: bug
stage: Cell
reported: 2026-10-28
source: GitHub #173 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-cell
commits: 8d2c882, b8bc761, 666f692
tests: cell_food_away, cell_tests (make test)
switch: pool.food_ack_away
signature:
docs: docs/re/cell.md#cell-ko-grace
related: #173, BUG-0051, BUG-0128, BUG-0153
---

## Report
From GitHub issue #173: https://github.com/WoodyWizard/SporeCoop/issues/173 (the full analysis, links and the scenario are there).

BUG-0051 (#29, commit e7ab004, release 0.7.3.0) lets a guest count its own meals as "on the way" until the host echoes their number in a food total. The host only echoes meals it processed. It ignores the guest's MSG_CELL_EAT / MSG_CELL_FOOD entirely while it is outside the Cell stage (an editor) or has no avatar. Such a meal stays "on the way" on the guest, so the guest's total stays above the host's. When the guest's next meal is acknowledged, the guest drops by the whole missing amount at once, which is the BUG-0051 symptom (a level down and up when a level line lies in between). The net loss is the same as before 0.7.3.0; it only comes later and at an unexpected moment. Severity LOW.

## Root cause
Observed (lane 2, `coop_cell_food_away.py` with `SPORE_FIX_OFF=pool.food_ack_away`): the guest ate 10 points while the host handled Cell messages as in an editor (`coop.pool away 1`); after the host's return the guest stayed at 10 against the host's 0, and its next meal (1 point) dropped it from 10 to 1 at once.

`on_message` returned before the host's MSG_CELL_EAT / MSG_CELL_FOOD handlers outside the Cell stage or without an avatar, so `g_food_ack` never covered that meal number and the guest kept it in `Meals::unacked`.

## Fix
The host reads a guest's MSG_CELL_EAT / MSG_CELL_FOOD before the `!cell` return and records the meal number (`g_food_ack[from]`) without points and without any game call, as for a meal eaten first. One reader per message (`read_eat`, `read_food`) is used by this path and by the in-Cell handlers, so the layouts cannot drift; the in-Cell path is unchanged. Kills and breaks dropped this way are acknowledged too (their numbers carry no points). Switch `pool.food_ack_away`; test hook `coop.pool away 1|0` (host; cleared at session end).

## Verification
* Unit: `tests/cell_tests.cpp` `meals_away()` (an acknowledged dropped meal: the guest is at the host total at once; without the ack, the dip of 10 at the next meal).
* Two-game `tools/tests/coop_cell_food_away.py` (suite `cell_food_away`), lane 2: FAIL with `SPORE_FIX_OFF=pool.food_ack_away` (host 0 / guest 10 after the return, then a dip 10 -> 1); PASS twice with the fix.
* Not run: the real editor route (round 2 of the issue's test plan); the hook takes the same `!cell` branch the editor does.
* Lab: FAIL out/lab/20261028-235143 (`SPORE_FIX_OFF=pool.food_ack_away`); PASS out/lab/20261028-231910 and out/lab/20261028-235414.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-cell (b8bc761): the host acknowledges a meal it cannot count; test cell_food_away FAIL without / PASS with.
- 2026-10-29: released in 0.7.3.1.
