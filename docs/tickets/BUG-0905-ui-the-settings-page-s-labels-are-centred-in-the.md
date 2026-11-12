---
id: BUG-0905
title: UI: the Settings page's labels are centred in their column, so the rows start at different x
status: fixed
kind: bug
stage: UI
reported: 2026-11-10
source: docs/w47-readme-0712 (README shots, lane 2, 2026-11-10)
role: 
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in: 0.7.12
branch: docs/w47-readme-0712
commits: bc841b97
tests: ui (tools/tests/coop_ui.py)
signature:
docs: docs/08-ui.md#ui-text-width
related: BUG-0765, BUG-0762
---

## Report
Seen while making the README pictures (`readme_shots.py menu`, lane 2, 1440x1080, page 1/3 of the Settings page in
the galaxy menu, `docs/media/ui_settings.jpg`): the setting labels do not start at one x. "Partner arrows", "Name
tags over players" start about at the group header's indent, "Progress at the single-player pace", "Friendly fire
*", "Joining after the start" further right, by a few to ~10 px; the Cell rows the same. It looks as if each label
is centred in a fixed-width field (or its width is set from the text) instead of left-aligned. Cosmetic.

## Root cause
Observed (runtime, lane 2): the game's labels always center their caption in their box (docs/08-ui.md line on
Labels); the caption alignment (IButton +0x3C, values 0, 1, 2, 4, 8, 16) and the button type (+0x1C, 0..5) change
nothing (tried on a label widened to 430 px with the `coop.ui align` / `coop.ui area` experiment commands). A
left-aligned label is a box fitted to the text's width; coop_ui.cpp text_width took one average glyph width (9.3
px), wrong by up to ~15 px on a label ("Friendly fire" 121 estimated, 110 drawn), so the text started up to ~8 px
off the column.

## Fix
text_width sums the advance of each glyph, measured in game (12 against 4 copies of every ASCII and Cyrillic glyph
in a label, `coop.ui caption`; docs/08-ui.md#ui-text-width). The settings labels now start within 1 px of each other
(415..416 at 1440x1080). Switch `coop.fix core.ui_layout off` (every label is fitted anew when it changes).

## Verification
FAIL: lab 20261110-203554 (`SPORE_FIX_OFF=core.ui_layout,core.ui_give_hint`, lane 2): "the settings labels are left-aligned (text starts at x 411..423, 11 labels)"
PASS: lab 20261110-204029 (lane 2).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on docs/w47-readme-0712 (coordinator follow-up of the README pictures).
- 2026-11-11: released in 0.7.12.
