---
id: BUG-0908
title: UI: the lobby's colour '>' button touches the Stage column, the stage text starts right after it
status: fixed
kind: bug
stage: UI
reported: 2026-11-10
source: docs/w47-readme-0712 (README shots, lane 2, 2026-11-10)
role: both
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in: 0.7.12
branch: docs/w47-readme-0712
commits: bc841b97
tests: ui (tools/tests/coop_ui.py)
signature:
docs: docs/08-ui.md#ui-text-width
related: BUG-0764, BUG-0765
---

## Report
Seen in the README picture of the lobby (`readme_shots.py menu`, lane 2, 1440x1080): the own row's colour ">"
button sits right at the start of the Stage column; the stage text (">Galaxy menu") touches it.

## Root cause
Observed (`coop.uistate widgets`, lane 2): the Stage column's labels started at x 345 of the window, the own row's
colour button spans 318..346: the box of "Galaxy menu" (685..817 on screen) overlapped the button (658..686), the
text drawn right after the ">".

## Fix
The Stage column (header and rows) starts at x 362. Switch `coop.fix core.ui_layout off`: x 345.

## Verification
FAIL: lab 20261110-203554 (`SPORE_FIX_OFF=core.ui_layout,core.ui_give_hint`, lane 2): "'Galaxy menu' [685, 376, 817, 402] overlaps '>' [658, 377, 686, 401]" (menu), the same with "Creature" in game
PASS: lab 20261110-204029 (lane 2).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on docs/w47-readme-0712 (coordinator follow-up of the README pictures).
- 2026-11-11: released in 0.7.12.
