---
id: BUG-0905
title: UI: the Settings page's labels are centred in their column, so the rows start at different x
status: open
kind: bug
stage: UI
reported: 2026-11-10
source: docs/w47-readme-0712 (README shots, lane 2, 2026-11-10)
role: 
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/08-ui.md#ui-window-steps
related: BUG-0765, BUG-0762
---

## Report
Seen while making the README pictures (`readme_shots.py menu`, lane 2, 1440x1080, page 1/3 of the Settings page in
the galaxy menu, `docs/media/ui_settings.jpg`): the setting labels do not start at one x. "Partner arrows", "Name
tags over players" start about at the group header's indent, "Progress at the single-player pace", "Friendly fire
*", "Joining after the start" further right, by a few to ~10 px; the Cell rows the same. It looks as if each label
is centred in a fixed-width field (or its width is set from the text) instead of left-aligned. Cosmetic.

## Root cause
Inferred: the label widgets of the settings rows (coop_ui.cpp, the settings page) use the centred text style of
the window's other labels; a left-aligned style (or a label of the column's full width with left alignment) is
needed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened.
