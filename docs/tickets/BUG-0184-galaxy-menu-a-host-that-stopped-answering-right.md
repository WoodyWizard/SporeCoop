---
id: BUG-0184
title: Galaxy menu: a host that stopped answering right after "Co-op" during the intro (once)
status: not-reproduced
kind: freeze
stage: Menu
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: host
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w1-core; tested on fix/w46-n1-crashes
commits:
tests: tools/tests/coop_ui.py (suite ui), menu_escape
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* `coop_ui.py` on lane 9 (GOG 2024, branch fix/gog2024-tests, 2026-10-24): once in 12 runs of the
  final code the host's console timed out right after its click on "Co-op" during the menu intro
  (the mod posts Escape to skip the intro, docs/re/space.md 7.9); the game log was overwritten by
  the next test before it was read. The other 11 runs (8 of them in a row) passed. An earlier version
  that ended the intro with EndCinematic in the menu broke the menu twice (an access violation in
  `UTFWin::Window::SetFlag`, a game that stopped reacting): that one is gone. If it comes back, keep
  `out/lab/<run>/ui.host.game.log` and look for what the main thread did after "Co-op clicked
  during the menu intro".

## Root cause
Not reproduced (0.7.11 N1, see Verification). The candidate of 2026-10-30 (the mod's Escape reaching the galaxy menu
outside the intro opens something modal and stops the frames) is disproved: Escape with no intro, at the intro's
start, and clicks on "Co-op" 0 to 7 s into the menu's own intro (the mod's skip, also near the intro's end) all keep
the frames going (30 frames in ~0.45 s right after), the co-op window opens, the intro ends, the game stays in the
menu, no fault. coop_ui.py with the click during the intro passed 3 of 3 more (now 17 of 18 in all).
Reasoning: the one failure (2026-10-24) had no game log; the click path is the same today, and nothing seen in the
menu stops the frames. The earlier EndCinematic way of skipping the intro (which did break the menu twice) is gone
since 2026-10-24; the remaining one-off console timeout cannot be told apart from a lab load hitch of that time
(the lab then ran up to 18 games at once, docs/06-testing.md) without its log.

## Fix
None: not reproduced. Related change on fix/w1-core: BUG-0163 (the menu item pointer is dropped as
soon as the game leaves the menu, and the intro check runs before any call on the item), which
removes one way a click near the menu could touch a freed window; it does not explain a hang
inside the menu.

## Verification
2026-10-28, fix/w1-core, lane 2 (GOG 2024): `coop_ui.py` 3 runs in a row, each with the host's
click on "Co-op" during the menu intro ("menu intro playing: True", "the co-op window opens
(clicked during the menu intro)", "the menu intro was skipped"): 3 of 3 passed, no console timeout.
Together with the 11 of 12 from 2026-10-24 that is 14 of 15.

2026-10-30, fix/w13-net, lane 10: `coop_ui.py` 3 of 3 passed, but the intro was not playing at the host's click.

0.7.11 N1 (fix/w46-n1-crashes, GOG 2024, lane 10):
* menu_escape (coop_menu_escape.py): six starts, "Co-op" clicked 0 / 0.3 / 1 / 3 / 7 s into the menu's own intro and
  Escape at its start, then Escape 3x with no intro: OK twice, out/lab/20261107-235247, out/lab/20261107-235631.
* coop_ui.py by hand (SPORE_LANE=10, the lab runs `ui` only on lanes 1-5): 3 of 3 passed, each with "menu intro
  playing: True" at the host's click, the window opened and the intro skipped (the output in
  out/lab/20261107-235631/coop_ui_by_hand.txt).
* (The first menu_escape version, out/lab/20261107-231415 and 20261107-233528, failed on its own model: an intro
  started by hand does not end on the mod's Escape, and a return to the menu does not start the intro again; the
  frames went on in every round there too.)

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: not reproduced on fix/w1-core (coop_ui.py 3 of 3 with the click during the intro).
- 2026-10-30: fix/w13-net: 3 of 3 passed on lane 10, intro not playing at the click (path not exercised); a candidate written down (Escape outside the intro), not verified. Status kept.
- 2026-11-08 (fix/w46-n1-crashes, 0.7.11 N1): the Escape candidate disproved (menu_escape 2x), coop_ui.py 3 of 3 with the click during the intro; stays not-reproduced, closed with the runs.
