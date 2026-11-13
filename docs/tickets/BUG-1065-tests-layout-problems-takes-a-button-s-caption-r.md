---
id: BUG-1065
title: tests: layout_problems takes a button's caption room as width - 30 px; the game's text borders are 23 px a side (width - 46), so wrapped captions pass
status: open
kind: test
stage: UI
reported: 2026-11-13
source: fix/w49-space2 (BUG-1062 key frame)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
