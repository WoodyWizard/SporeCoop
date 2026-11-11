---
id: BUG-0904
title: UI: the lobby's 'Settings (N changed)' caption wraps onto two lines and spills out of its button
status: fixed
kind: bug
stage: UI
reported: 2026-11-10
source: docs/w47-readme-0712 (README shots, lane 2, 2026-11-10)
role: host
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in: unreleased
branch: docs/w47-readme-0712
commits: bc841b97
tests: ui (tools/tests/coop_ui.py)
signature:
docs: docs/08-ui.md#ui-text-width
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
Observed (runtime, lane 2, `coop.uistate widgets`): the caption "Settings (1 changed)" needs 186 px (the glyph
advances measured in game, docs/08-ui.md#ui-text-width); the button is 210 px and a button's caption wraps when
it needs more than its width minus ~30 px (its caption borders).

## Fix
The caption is "Settings (N)" ("Настройки (N)"); the settings page marks each changed setting with " *" and says
so in its note line. Switch `coop.fix core.ui_layout off`: the long caption (with the old label fitting and Stage
column).

## Verification
FAIL: lab 20261110-203554 (`SPORE_FIX_OFF=core.ui_layout,core.ui_give_hint`, lane 2): "button 'Settings (1 changed)' needs 186 px, has 180" in the lobby (menu and F9, host and guest)
PASS: lab 20261110-204029 (lane 2).

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on docs/w47-readme-0712 (coordinator follow-up of the README pictures).
