---
id: BUG-0904
title: UI: the lobby's 'Settings (N changed)' caption wraps onto two lines and spills out of its button
status: open
kind: bug
stage: UI
reported: 2026-11-10
source: docs/w47-readme-0712 (README shots, lane 2, 2026-11-10)
role: host
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
Seen while making the README pictures (`SPORE_LANE=2 python3 tools/tests/readme_shots.py menu`, 1440x1080,
the host in the galaxy menu with one session setting changed, Friendly fire on): the lobby's top-left button
(`settings`, 40..250 x 66..98 in the window, coop_ui.cpp `D.settings_btn`) reads "Settings (1 changed)", and the
caption is broken into two lines, "Settings (1" and "changed)", the second line below the button's bottom edge
(over the "Hosting on UDP port" line). With no changed setting the caption "Settings" fits. The README picture
`docs/media/ui_lobby.jpg` is therefore taken before the setting is changed. Probably the same in game (F9).

## Root cause
Inferred: the button is 210 px wide; the caption at the window's font does not fit and the game's text field
wraps it (word wrap on). A wider button, a shorter caption ("Settings: 1 changed" may not fit either, "Settings *1"),
or no wrap would fix it.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened.
