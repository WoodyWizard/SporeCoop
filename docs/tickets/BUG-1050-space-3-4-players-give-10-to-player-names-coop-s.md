---
id: BUG-1050
title: Space, 3-4 players: "Give 10 to <player>" names coop's give target, but the spice goes to the first other player of the session (give_spice ignores the recipient; the > picker is hidden in Space)
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SEAM-1
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-space
commits: f95e96fe
tests: space_give_pick
signature:
docs: docs/re/space.md#space-war-npc-hits, docs/known-issues.md#issue-space-give-recipient
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
coop.cpp, spacecoop.cpp, coop_ui.cpp; switch `coop.fix space.give_pick` (off: 0.7.13, the first other player and no
">" in Space):
* give_selected_to(to) passes the recipient to `spacecoop::give_spice(10, to)` (`coop.give <id>` too); give_spice
  refuses a recipient that is not another player of the session; 0xFF keeps the old first-other-player choice.
* The co-op window (owner decision 9: ">" back in 3-4 player Space sessions, next to the Give button): in Space with 2+
  possible recipients the row is cut anew: jump 400..515, "Give 10 to <player>" 520..690, ">" 694..720 (the window's
  inner edge, as Send); elsewhere the old places (jump 400..560, ">" 520..560, Give 570..720). App callback
  `give_pick_space`.
Interpretation: the owner's words "next to '<' under 'Fly to'" did not match a "<" in that row (there is none in
Space); ">" was put right of the Give button it belongs to.

## Verification
`coop_space_give_pick.py` (suite `space_give_pick`, multi / extended, party of 3 on one lane, fixture space_free, ~1
min): Bob opens the co-op window, ">" must be shown; real clicks on ">" until the button names Carol, then on Give;
the spice in each game's cargo (`coop.space spice`, the game's inventory count).
* FAIL (SPORE_FIX_OFF=space.give_pick): out/lab/20261113-014626 (">" hidden; Alice 20 -> 30, Carol 0 -> 0)
* PASS: out/lab/20261113-014859-2 (">" right of the window's edge, first layout), out/lab/20261113-015206, out/lab/20261113-020049 (the final
  layout: Carol 0 -> 10, Alice 20 -> 20, Bob 20 -> 10)
Key frame looked at (01_give_pick_guest.jpg of 20261113-015206): ">" inside the window next to Give, nothing
overlaps; the Give caption wraps on two lines (old, BUG-1062). No tools/tests/golden reference has this row (the
golden steps are 2-player).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space: the spice goes to the named player, ">" in Space; FAIL/PASS in the lab.
- 2026-11-14: released in 0.7.14.
