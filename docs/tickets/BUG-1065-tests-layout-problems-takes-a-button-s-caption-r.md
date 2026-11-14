---
id: BUG-1065
title: tests: layout_problems takes a button's caption room as width - 30 px; the game's text borders are 23 px a side (width - 46), so wrapped captions pass
status: fixed
kind: test
stage: UI
reported: 2026-11-13
source: fix/w49-space2 (BUG-1062 key frame)
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-ui
commits: 8bc5ebf9, 66f43dd7
tests: ui
signature:
docs: docs/08-ui.md#ui-give-fit, docs/06-testing.md#testing-p2-ui
related:
switch: none (a test change: UI_BUTTON_PAD=30 is the old rule)
---

## Report
Found while fixing BUG-1062 (fix/w49-space2): space_give_pick's layout check (layout_problems) passed while the key
frame showed "Give 10 to Carol" on two lines in a 181 px button (caption_width 147: 181 - 30 = 151 >= 147).

## Root cause
Observed at runtime (lane 3, `coop.ui btn` on the co-op window's give button): the button's text borders (IButton
vt +0x50) are 23 11 23 11, so a caption wraps when its advance sum is over the width minus 46; coop_ui.py's
`BUTTON_PAD = 30` (docs/08-ui.md#ui-text-width, "~30 px") is too lenient: a caption 1..16 px too wide passes.
On the lobby window today every button but the give row has room with 46 ("Report a problem" 7 px to spare);
the settings page and other windows are not checked.

## Fix idea
BUTTON_PAD = 46 in coop_ui.py (the ui suite and every test using layout_problems), after checking the settings page
and the menu lobby with it (docs/08-ui.md#ui-give-fit).

## Fix
tools/tests/coop_ui.py: BUTTON_PAD = 46 (the IButton text borders, 23 px a side; `UI_BUTTON_PAD=30` gives the old
rule), and layout_problems runs on every step of the co-op window: the name step and Host / Join on both games, the
Host step, the Join step, the lobby in the menu, both settings pages in the menu and in game (host and guest), the F9
lobby in game. Every caption of every step fits at 46 (no mod change needed; space_give_pick keeps its give-row check,
which layout_problems now matches). Twin: `UI_LAYOUT_TWIN="report -12"` narrows the lobby's "Report a problem"
(caption 157 px) to 198 px with `coop.uistate nudge <name> <dx> <dy> [dw]` (dw new).

## Verification
* FAIL (UI_LAYOUT_TWIN="report -12"): out/lab/20261113-032734 ("Report a problem" needs 157 px, has 152; the
  screenshot shows it on two lines)
* the old rule on the same planted wrap (UI_LAYOUT_TWIN="report -12" UI_BUTTON_PAD=30): out/lab/20261113-032938 PASSED
  (room 168): the check before this fix missed it
* PASS (every step at 46): out/lab/20261113-032529; ui_golden PASS out/lab/20261113-033057

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-ui (46 px on every co-op window step).
- 2026-11-14: released in 0.7.14.
