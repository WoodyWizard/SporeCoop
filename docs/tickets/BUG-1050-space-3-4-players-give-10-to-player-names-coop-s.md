---
id: BUG-1050
title: Space, 3-4 players: "Give 10 to <player>" names coop's give target, but the spice goes to the first other player of the session (give_spice ignores the recipient; the > picker is hidden in Space)
status: open
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SEAM-1
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-space-give-recipient
related: BUG-0075, BUG-0234
---

## Report
Audit SEAM-1. Not seen at runtime, found by code reading.
In a 3-4 player session in Space the co-op window's button says "Give 10 to <player>" with coop's give target (a pick made with ">" in Tribe or Civ stays, BUG-0075), but the spice goes to the first other player in the session's order. In Space the ">" picker is hidden, so the player cannot choose and the label can name someone else than the receiver. `coop.give <id>` ignores the id in Space too.

## Root cause
Observed in code (main efd2e0ba):
* coop.cpp:6583-6603 `a.give`: the label's name is `g_sess.name_of(give_target())` (6584-6587), the Space branch ignores the name spacecoop::spice() returns.
* coop.cpp:6437-6448 `give_selected_to(to)`: in Space `spacecoop::give_spice(10)`, `to` unused.
* spacecoop.cpp:1478-1485 `gift_target` = the first player of `players()` that is not me (session order, unsorted; net::give_target sorts and honours the pick, net/give_target.h).
* coop_ui.cpp:1406 hides D.give_next when `space`; its box {520,430,560,460} (1029) lies inside D.jump_btn {400,430,560,460} (1018), which is shown in Space: showing ">" in Space needs a new place.
Confidence: high.

## Fix idea
`spacecoop::give_spice(n, to)` with the recipient from give_target(); spice() takes it too; show ">" in Space at a free spot (move it or the jump button). Switch: `coop.fix space.give_pick off` (first other player, ">" hidden).

## How to test
New `coop_space_give_pick.py` (3 instances, space fixture): guest 2 picks guest 3 (`coop.give next` = the > button), clicks Give (the ui action / `coop.give`); assert guest 3's game inventory spice grew by 10 and the host's did not (the game's inventory count, `coop.space spice`). Main: the host got it (FAIL).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
